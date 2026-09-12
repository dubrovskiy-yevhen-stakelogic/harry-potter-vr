#include "hpvr/quest_gesture.h"

#include "hpvr/hp1_gesture_c.h"
#include "hpvr/wand_trajectory_c.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace hpvr::quest {
namespace {
constexpr std::size_t kMaximumRawSamples = 16384;
constexpr std::int64_t kMaximumSampleGapNs = 100000000;
constexpr float kMaximumSampleJumpMeters = 0.25F;
constexpr float kGestureExtentMeters = 0.42F;
constexpr float kJitterMeters = 0.004F;
constexpr float kRelaxedAssistMultiplier = 1.75F;
constexpr std::uint32_t kLessonRoundCount = 4;
constexpr float kFeedbackSeconds = 0.80F;
constexpr float kMinimumProjectedSpan = 0.02F;
constexpr std::size_t kMaximumGuideTrailPoints = 384;
constexpr std::size_t kShapeSamples = 64;
using ShapePoint = std::array<double, 2>;
using SampledShape = std::array<ShapePoint, kShapeSamples>;

double PointSegmentDistanceSquared(const ShapePoint& p, const ShapePoint& a,
                                   const ShapePoint& b) {
    const double x = b[0]-a[0], y = b[1]-a[1];
    const double length = x*x+y*y;
    const double t = length>1e-16 ? std::clamp(((p[0]-a[0])*x+(p[1]-a[1])*y)/length,0.0,1.0) : 0;
    const double dx=p[0]-a[0]-t*x, dy=p[1]-a[1]-t*y;
    return dx*dx+dy*dy;
}

double PolylineLength(const SampledShape& points) {
    double length=0;
    for(std::size_t i=1;i<points.size();++i)
        length+=std::hypot(points[i][0]-points[i-1][0],points[i][1]-points[i-1][1]);
    return length;
}

// Coverage tolerates small corners, start/end tails and unequal drawing speed.
// Length and ordered progression prevent a dense scribble from covering a rune
// by chance. No reflection is applied: all candidate scales remain positive.
float CoverageScore(const SampledShape& drawn, const SampledShape& pattern,
                    double radius) {
    const double expected_length=PolylineLength(pattern);
    const double length=PolylineLength(drawn);
    if(length<expected_length*.65 || length>expected_length*1.6) return 0;
    double best_ordered=0;
    for(bool reversed : {false,true}) {
        unsigned covered=0, inside=0;
        double regress=0, last=-1;
        for(std::size_t i=0;i<drawn.size();++i) {
            const auto& p=drawn[reversed?drawn.size()-1-i:i];
            double best=std::numeric_limits<double>::infinity();
            std::size_t nearest=0;
            for(std::size_t j=1;j<pattern.size();++j) {
                const double distance=PointSegmentDistanceSquared(p,pattern[j-1],pattern[j]);
                if(distance<best){best=distance;nearest=j;}
            }
            if(best<=radius*radius) {
                ++inside;
                const double position=double(nearest)/double(pattern.size()-1);
                if(last>=0 && position<last-.05) regress+=last-position;
                last=position;
            }
        }
        if(regress>.55) continue;
        for(const auto& p:pattern) {
            double best=std::numeric_limits<double>::infinity();
            for(std::size_t j=1;j<drawn.size();++j)
                best=std::min(best,PointSegmentDistanceSquared(p,drawn[j-1],drawn[j]));
            if(best<=radius*radius) ++covered;
        }
        // The authored pass marks remain unchanged. A small minority of bad
        // points is tolerated; missing most of a rune is not.
        const double precision=double(inside)/double(drawn.size());
        const double recall=double(covered)/double(pattern.size());
        best_ordered=std::max(best_ordered,std::min(precision,recall));
    }
    return static_cast<float>(best_ordered);
}

float AspectCoverageScore(const SampledShape& drawn,const SampledShape& pattern,
                          float radius,bool fit_scale) {
    ShapePoint pattern_min{1e20,1e20},pattern_max{-1e20,-1e20};
    for(const auto& p:pattern) for(unsigned axis=0;axis<2;++axis) {
        pattern_min[axis]=std::min(pattern_min[axis],p[axis]);
        pattern_max[axis]=std::max(pattern_max[axis],p[axis]);
    }
    float best=0;
    // Only executed when a completed stroke fails the cheap rigid comparison.
    // 72 fixed candidates, each 64 points; no work is added to render frames.
    for(unsigned angle=0;angle<72;++angle) {
        const double radians=double(angle)*6.283185307179586/72.0;
        const double cosine=std::cos(radians),sine=std::sin(radians);
        SampledShape candidate{};
        ShapePoint minimum{1e20,1e20},maximum{-1e20,-1e20};
        for(std::size_t i=0;i<drawn.size();++i) {
            candidate[i]={drawn[i][0]*cosine-drawn[i][1]*sine,
                          drawn[i][0]*sine+drawn[i][1]*cosine};
            for(unsigned axis=0;axis<2;++axis) {
                minimum[axis]=std::min(minimum[axis],candidate[i][axis]);
                maximum[axis]=std::max(maximum[axis],candidate[i][axis]);
            }
        }
        ShapePoint scale{1,1};
        if(fit_scale) {
            for(unsigned axis=0;axis<2;++axis)
                scale[axis]=(pattern_max[axis]-pattern_min[axis])/(maximum[axis]-minimum[axis]);
            const double aspect=scale[0]/scale[1];
            if(!std::isfinite(aspect)||aspect<.35||aspect>2.85)continue;
        }
        for(auto& p:candidate) for(unsigned axis=0;axis<2;++axis)
            p[axis]=(p[axis]-(minimum[axis]+maximum[axis])*.5)*scale[axis]+
                    (pattern_min[axis]+pattern_max[axis])*.5;
        const float coverage=CoverageScore(candidate,pattern,radius);
        double error=std::numeric_limits<double>::infinity();
        for(bool reversed:{false,true}){
            double sum=0;
            for(std::size_t i=0;i<candidate.size();++i){
                const auto& p=pattern[reversed?pattern.size()-1-i:i];
                const double dx=candidate[i][0]-p[0],dy=candidate[i][1]-p[1];
                sum+=dx*dx+dy*dy;
            }
            error=std::min(error,sum);
        }
        const float ordered=static_cast<float>(std::clamp(1.0-std::sqrt(error/double(candidate.size()))/(2.0*radius),0.0,1.0));
        best=std::max(best,coverage*(.5F+.5F*ordered));
        if(best>=.999F)break;
    }
    return best;
}

bool FiniteVector(const std::array<float, 3>& value) {
    return std::all_of(value.begin(), value.end(),
                       [](float component) { return std::isfinite(component); });
}
float Distance(const std::array<float, 3>& left,
               const std::array<float, 3>& right) {
    const float x = left[0] - right[0];
    const float y = left[1] - right[1];
    const float z = left[2] - right[2];
    return std::sqrt(x * x + y * y + z * z);
}
bool Normalize(const std::array<float, 3>& input,
               std::array<float, 3>* output) {
    if (output == nullptr || !FiniteVector(input)) return false;
    const float length = std::sqrt(input[0] * input[0] +
                                   input[1] * input[1] +
                                   input[2] * input[2]);
    if (!std::isfinite(length) || length < 1.0e-6F) return false;
    *output = {input[0] / length, input[1] / length, input[2] / length};
    return true;
}

std::array<float, 3> Cross(const std::array<float, 3>& left,
                           const std::array<float, 3>& right) {
    return {left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0]};
}

std::array<float, 3> GuidePoint(const hpvr_wand_lesson_plane& plane,
                                const hpvr_wand_vec2 point) {
    const float right_offset = (point.x - 0.5F) * plane.width_m;
    const float up_offset = (0.5F - point.y) * plane.height_m;
    return {
        plane.origin_x_m + plane.right_x * right_offset +
            plane.up_x * up_offset,
        plane.origin_y_m + plane.right_y * right_offset +
            plane.up_y * up_offset,
        plane.origin_z_m + plane.right_z * right_offset +
            plane.up_z * up_offset};
}

bool BuildAimFacingPlane(const std::array<float, 3>& tip,
                         const std::array<float, 3>& forward,
                         const hpvr_wand_vec2 anchor,
                         hpvr_wand_lesson_plane* output) {
    if (output == nullptr) return false;
    const std::array<float, 3> world_up{0.0F, 1.0F, 0.0F};
    const std::array<float, 3> fallback_up{0.0F, 0.0F, 1.0F};
    std::array<float, 3> right{};
    if (!Normalize(Cross(forward, world_up), &right) &&
        !Normalize(Cross(forward, fallback_up), &right)) return false;
    std::array<float, 3> up{};
    if (!Normalize(Cross(right, forward), &up)) return false;
    const float anchor_x = (anchor.x - 0.5F) * kGestureExtentMeters;
    const float anchor_y = (anchor.y - 0.5F) * kGestureExtentMeters;
    *output = {
        tip[0] - right[0] * anchor_x + up[0] * anchor_y,
        tip[1] - right[1] * anchor_x + up[1] * anchor_y,
        tip[2] - right[2] * anchor_x + up[2] * anchor_y,
        right[0], right[1], right[2], up[0], up[1], up[2],
        kGestureExtentMeters, kGestureExtentMeters};
    return true;
}

bool SampleShape(std::span<const std::array<float, 2>> input,
                 SampledShape* output, double* energy) {
    if (input.size() < 3 || input.size() > kMaximumRawSamples) return false;
    double length = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        for (const float value : input[i])
            if (!std::isfinite(value) || std::abs(value) > 10000.0F) return false;
        if (i != 0) length += std::hypot(double(input[i][0]) - input[i - 1][0],
                                        double(input[i][1]) - input[i - 1][1]);
    }
    if (length < kMinimumProjectedSpan) return false;
    std::size_t segment = 1;
    double traversed = 0;
    for (std::size_t i = 0; i < output->size(); ++i) {
        const double distance = length * double(i) / double(output->size() - 1);
        for (;;) {
            const auto& a = input[segment - 1];
            const auto& b = input[segment];
            const double edge = std::hypot(double(b[0]) - a[0], double(b[1]) - a[1]);
            if (segment + 1 == input.size() || traversed + edge >= distance) {
                const double t = edge > 1.0e-12 ? std::clamp((distance - traversed) / edge, 0.0, 1.0) : 0;
                (*output)[i] = {a[0] + (double(b[0]) - a[0]) * t,
                                a[1] + (double(b[1]) - a[1]) * t};
                break;
            }
            traversed += edge;
            ++segment;
        }
    }
    ShapePoint center{};
    for (const auto& point : *output) {
        center[0] += point[0] / double(output->size());
        center[1] += point[1] / double(output->size());
    }
    double xx = 0, yy = 0, xy = 0;
    for (auto& point : *output) {
        point[0] -= center[0]; point[1] -= center[1];
        xx += point[0] * point[0]; yy += point[1] * point[1];
        xy += point[0] * point[1];
    }
    *energy = xx + yy;
    // A line remains a line after rotation/scaling; never promote it to a rune.
    return *energy > 1.0e-6 && xx * yy - xy * xy > *energy * *energy * 1.0e-5;
}

struct CurlStructure {
    double turn = 0;
    double turning_distance = 0;
    double inner_endpoint = 1;
    double outer_endpoint = 0;
    double length_per_radius = 0;
    double radius = 0;
};

bool NormalizeCurlAspect(SampledShape* points) {
    double xx=0,yy=0,xy=0;
    for(const auto& p:*points){xx+=p[0]*p[0];yy+=p[1]*p[1];xy+=p[0]*p[1];}
    const double angle=.5*std::atan2(2*xy,xx-yy),cosine=std::cos(angle),sine=std::sin(angle);
    const double variance_x=xx*cosine*cosine+2*xy*cosine*sine+yy*sine*sine;
    const double variance_y=xx*sine*sine-2*xy*cosine*sine+yy*cosine*cosine;
    if(variance_y<variance_x*.06||variance_x<1e-10)return false;
    const double scale_x=std::sqrt(variance_x/points->size()),scale_y=std::sqrt(variance_y/points->size());
    for(auto& p:*points){
        const double x=p[0]*cosine+p[1]*sine,y=-p[0]*sine+p[1]*cosine;
        p={x/scale_x,y/scale_y};
    }
    return true;
}

CurlStructure DescribeCurl(const SampledShape& points) {
    CurlStructure result;
    for(const auto& point:points)
        result.radius=std::max(result.radius,std::hypot(point[0],point[1]));
    if(result.radius<1e-8)return result;
    const double first=std::hypot(points.front()[0],points.front()[1])/result.radius;
    const double last=std::hypot(points.back()[0],points.back()[1])/result.radius;
    result.inner_endpoint=std::min(first,last);
    result.outer_endpoint=std::max(first,last);
    result.length_per_radius=PolylineLength(points)/result.radius;
    ShapePoint previous{};
    bool have_previous=false;
    // Four-sample chords suppress small wrist tremor without imposing a
    // drawing speed. Arc-length sampling above keeps this angle invariant.
    for(std::size_t i=2;i+2<points.size();++i){
        const ShapePoint direction{points[i+2][0]-points[i-2][0],
                                   points[i+2][1]-points[i-2][1]};
        if(std::hypot(direction[0],direction[1])<result.radius*.002)continue;
        if(have_previous){
            const double angle=std::atan2(previous[0]*direction[1]-previous[1]*direction[0],
                                          previous[0]*direction[0]+previous[1]*direction[1]);
            result.turn+=angle;result.turning_distance+=std::abs(angle);
        }
        previous=direction;have_previous=true;
    }
    result.turn=std::abs(result.turn);
    return result;
}

bool IsSingleCurl(const CurlStructure& curl) {
    // The recognizable structure is an open coil: one endpoint lies inside
    // the outer turn. A circle has two outer endpoints; random squiggles turn
    // back and forth or travel too far; a line never winds around the center.
    return curl.turn>=3.8&&curl.turn<=10.5&&
        curl.turning_distance<=curl.turn+2.3&&
        curl.inner_endpoint<=.60&&curl.outer_endpoint>=.65&&
        curl.outer_endpoint-curl.inner_endpoint>=.28&&
        curl.length_per_radius>=3.2&&curl.length_per_radius<=8.8;
}
}  // namespace

GestureShapeMatch CompareGestureShape(
    std::span<const std::array<float, 2>> drawn,
    std::span<const std::array<float, 2>> pattern,
    float accuracy_radius, bool fit_scale) {
    if (!std::isfinite(accuracy_radius) || accuracy_radius <= 0.0F ||
        accuracy_radius > 1.0F) return {};
    SampledShape a{}, b{};
    double energy_a = 0, energy_b = 0;
    if (!SampleShape(drawn, &a, &energy_a) || !SampleShape(pattern, &b, &energy_b)) return {};
    const double scale = fit_scale ? std::sqrt(energy_b / energy_a) : 1.0;
    double best_error = std::numeric_limits<double>::infinity();
    for (const bool reverse : {false, true}) {
        double dot = 0, cross = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const auto& target = b[reverse ? b.size() - 1 - i : i];
            dot += a[i][0] * target[0] + a[i][1] * target[1];
            cross += a[i][0] * target[1] - a[i][1] * target[0];
        }
        // Closed-form rigid alignment covers all angles, including 180 degrees,
        // with two fixed 64-point comparisons instead of an angular search.
        const double magnitude = std::hypot(dot, cross);
        const double cosine = magnitude > 1.0e-12 ? dot / magnitude : 1.0;
        const double sine = magnitude > 1.0e-12 ? cross / magnitude : 0.0;
        double error = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const auto& target = b[reverse ? b.size() - 1 - i : i];
            const double x = scale * (a[i][0] * cosine - a[i][1] * sine) - target[0];
            const double y = scale * (a[i][0] * sine + a[i][1] * cosine) - target[1];
            error += x * x + y * y;
        }
        best_error = std::min(best_error, error);
    }
    const double rms = std::sqrt(best_error / double(a.size()));
    const float rigid=static_cast<float>(std::clamp(1.0-rms/(2.0*accuracy_radius),0.0,1.0));
    return {true, rigid>=.999F ? rigid : std::max(rigid,AspectCoverageScore(a,b,accuracy_radius,fit_scale))};
}

GestureShapeMatch CompareGameplayGestureShape(
    std::span<const std::array<float,2>> drawn,
    std::span<const std::array<float,2>> pattern) {
    SampledShape stroke{},reference{};
    double stroke_energy=0,reference_energy=0;
    if(!SampleShape(drawn,&stroke,&stroke_energy)||
       !SampleShape(pattern,&reference,&reference_energy))return {};
    const double physical_radius=DescribeCurl(stroke).radius;
    if(!NormalizeCurlAspect(&stroke)||!NormalizeCurlAspect(&reference))return {true,0};
    const auto expected=DescribeCurl(reference),actual=DescribeCurl(stroke);
    if(!IsSingleCurl(expected))return {};
    if(physical_radius<.06||!IsSingleCurl(actual))return {true,0};
    // Outside a lesson, recognizing the coil is the task. Do not punish an
    // otherwise correct fast stroke for its curl size or exact endpoint.
    return {true,1.0F};
}

struct QuestGesture::State {
    std::vector<hpvr_wand_vec2> template_points;
    std::vector<std::array<float, 2>> shape_template;
    hpvr_hp1_spell_profile_report profile{};
    std::vector<hpvr_wand_tracked_tip_sample> raw_samples;
    std::vector<hpvr_wand_vec2> feedback_points;
    hpvr_wand_lesson_plane plane{};
    std::array<float, 3> locked_origin{};
    std::array<float, 3> locked_direction{0.0F, 0.0F, -1.0F};
    FlipendoEvent pending_event{};
    GestureVisualState visual = GestureVisualState::Idle;
    float feedback_seconds = 0.0F;
    float last_score = 0.0F;
    bool active = false;
    bool trigger_held = false;
    bool blocked_until_release = true;
    bool event_pending = false;
    std::uint64_t next_serial = 1;
    std::uint32_t attempts = 0;
    std::uint32_t accepted = 0;
    std::uint32_t rejected = 0;
    std::uint32_t lesson_round = 0;
    bool relaxed = false;
    bool gameplay = false;
    GestureDiagnostics diagnostics{};

    void Diagnose(const char* reason,float required_score) {
        const auto serial=diagnostics.serial+1;
        diagnostics={};diagnostics.serial=serial;diagnostics.attempt=attempts;
        diagnostics.reason=reason;diagnostics.sample_count=static_cast<std::uint32_t>(raw_samples.size());
        diagnostics.score=last_score;diagnostics.threshold=required_score;
        if(raw_samples.empty())return;
        diagnostics.duration_seconds=static_cast<float>(double(raw_samples.back().predicted_display_time_ns-
            raw_samples.front().predicted_display_time_ns)*1e-9);
        std::array<float,2> minimum{1e20F,1e20F},maximum{-1e20F,-1e20F},previous{};
        float min_depth=1e20F,max_depth=-1e20F;
        for(std::size_t i=0;i<raw_samples.size();++i) {
            const auto& p=raw_samples[i];
            const float x=p.position_x_m-plane.origin_x_m,y=p.position_y_m-plane.origin_y_m,z=p.position_z_m-plane.origin_z_m;
            const std::array<float,2> point{.5F+(x*plane.right_x+y*plane.right_y+z*plane.right_z)/plane.width_m,
                .5F-(x*plane.up_x+y*plane.up_y+z*plane.up_z)/plane.height_m};
            for(unsigned axis=0;axis<2;++axis){minimum[axis]=std::min(minimum[axis],point[axis]);maximum[axis]=std::max(maximum[axis],point[axis]);}
            const float depth=x*locked_direction[0]+y*locked_direction[1]+z*locked_direction[2];
            min_depth=std::min(min_depth,depth);max_depth=std::max(max_depth,depth);
            if(i)diagnostics.path_length+=std::hypot(point[0]-previous[0],point[1]-previous[1]);
            previous=point;
            const auto wanted=static_cast<std::size_t>(diagnostics.projected_point_count)*(raw_samples.size()-1)/
                std::max<std::size_t>(1,std::min<std::size_t>(32,raw_samples.size())-1);
            if(i==wanted&&diagnostics.projected_point_count<32)
                diagnostics.projected_points[diagnostics.projected_point_count++]=point;
        }
        diagnostics.projected_extent={maximum[0]-minimum[0],maximum[1]-minimum[1]};
        diagnostics.depth_span_meters=max_depth-min_depth;
    }
};

QuestGesture::QuestGesture() : state_(std::make_unique<State>()) {
    state_->raw_samples.reserve(1024);
}
QuestGesture::~QuestGesture() = default;

bool QuestGesture::LoadFlipendoProfile(const std::filesystem::path& data_root) {
    State& state = *state_;
    const std::string base = (data_root / "system" / "HPBase.u").string();
    const std::string lesson = (data_root / "Maps" / "Lev_Tut1.unr").string();
    std::vector<hpvr_wand_vec2> points(HPVR_HP1_GESTURE_MAX_TEMPLATE_POINTS);
    std::vector<std::int32_t> segments(HPVR_HP1_GESTURE_MAX_SEGMENTS);
    hpvr_hp1_spell_profile_report report{};
    const std::uint32_t status = hpvr_hp1_load_spell_profile_utf8(
        base.c_str(), lesson.c_str(), "FlipPattern", "spellFlip",
        points.data(), static_cast<std::uint32_t>(points.size()),
        segments.data(), static_cast<std::uint32_t>(segments.size()), &report);
    if (status != HPVR_HP1_PROFILE_OK || report.status != status ||
        report.abi_version != HPVR_HP1_GESTURE_ABI_VERSION ||
        report.template_point_count < 2 ||
        report.template_point_count > points.size() ||
        report.pass_mark_count == 0 ||
        report.pass_mark_count > HPVR_HP1_PASS_MARK_COUNT ||
        !std::isfinite(report.accuracy_radius) ||
        report.accuracy_radius <= 0.0F ||
        !std::isfinite(report.draw_time_seconds) ||
        report.draw_time_seconds <= 0.0F) return false;
    for (std::uint32_t index = 0; index < report.pass_mark_count; ++index) {
        if (!std::isfinite(report.pass_marks[index]) ||
            report.pass_marks[index] < 0.0F || report.pass_marks[index] > 1.0F)
            return false;
    }
    points.resize(report.template_point_count);
    if (std::any_of(points.begin(), points.end(), [](const auto& point) {
            return !std::isfinite(point.x) || !std::isfinite(point.y);
        })) return false;
    state.template_points = std::move(points);
    state.shape_template.clear();
    state.shape_template.reserve(state.template_points.size());
    for (const auto& point : state.template_points) state.shape_template.push_back({point.x, point.y});
    state.profile = report;
    Reset();
    return true;
}

void QuestGesture::SetLessonDifficulty(const bool relaxed) {
    if (state_->relaxed == relaxed) return;
    state_->relaxed = relaxed;
    Reset();
}

void QuestGesture::SetGameplayMode(const bool gameplay) {
    if(state_->gameplay==gameplay)return;
    state_->gameplay=gameplay;
    Reset();
}

void QuestGesture::SetLessonRound(const std::uint32_t zero_based_round) {
    const std::uint32_t round = std::min(zero_based_round, kLessonRoundCount - 1);
    if (state_->lesson_round == round) return;
    state_->lesson_round = round;
    Reset();
}

void QuestGesture::Reset() {
    State& state = *state_;
    if(state.active)state.Diagnose("EXTERNAL_RESET",threshold());
    state.raw_samples.clear();
    state.feedback_points.clear();
    state.active = false;
    state.trigger_held = false;
    state.blocked_until_release = true;
    state.event_pending = false;
    state.visual = GestureVisualState::Idle;
    state.feedback_seconds = 0.0F;
}

void QuestGesture::Advance(float delta_seconds) {
    State& state = *state_;
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F ||
        state.active || state.feedback_seconds <= 0.0F) return;
    state.feedback_seconds =
        std::max(0.0F, state.feedback_seconds - std::min(delta_seconds, 0.05F));
    if (state.feedback_seconds == 0.0F) {
        state.visual = GestureVisualState::Idle;
        state.feedback_points.clear();
    }
}

bool QuestGesture::Observe(const GestureSample& sample) {
    State& state = *state_;
    if (!IsLoaded()) return false;
    if (!sample.tracked || !FiniteVector(sample.tip) ||
        !FiniteVector(sample.aim_direction) ||
        sample.predicted_display_time_ns <= 0) {
        if (state.active) {
            state.Diagnose("TRACKING_LOST",threshold());
            state.raw_samples.clear();
            state.active = false;
            state.visual = GestureVisualState::Canceled;
            state.feedback_seconds = kFeedbackSeconds;
        }
        state.blocked_until_release = true;
        state.trigger_held = sample.cast_held;
        return true;
    }
    if (state.blocked_until_release) {
        state.trigger_held = sample.cast_held;
        if (!sample.cast_held) {
            state.blocked_until_release = false;
            if (state.feedback_seconds <= 0.0F)
                state.visual = GestureVisualState::Idle;
        }
        return true;
    }
    const bool pressed = sample.cast_held && !state.trigger_held;
    const bool released = !sample.cast_held && state.trigger_held;
    state.trigger_held = sample.cast_held;
    if (pressed) {
        std::array<float, 3> direction{};
        if (!Normalize(sample.aim_direction, &direction)) {
            state.blocked_until_release = true;
            return true;
        }
        state.raw_samples.clear();
        state.feedback_points.clear();
        state.locked_origin = sample.tip;
        state.locked_direction = direction;
        const hpvr_wand_vec2 anchor = state.template_points.front();
        if (!BuildAimFacingPlane(sample.tip, direction, anchor, &state.plane)) {
            state.blocked_until_release = true;
            return true;
        }
        state.active = true;
        state.visual = GestureVisualState::Recording;
        state.feedback_seconds = 0.0F;
        state.last_score = 0.0F;
        ++state.attempts;
    }
    if (!state.active) return true;
    hpvr_wand_tracked_tip_sample point{
        sample.predicted_display_time_ns, sample.tip[0], sample.tip[1],
        sample.tip[2], 1, {0, 0, 0}};
    if (!state.raw_samples.empty()) {
        const auto& previous = state.raw_samples.back();
        const std::array<float, 3> previous_tip{
            previous.position_x_m, previous.position_y_m, previous.position_z_m};
        if (point.predicted_display_time_ns <= previous.predicted_display_time_ns ||
            point.predicted_display_time_ns - previous.predicted_display_time_ns >
                kMaximumSampleGapNs ||
            Distance(sample.tip, previous_tip) > kMaximumSampleJumpMeters) {
            state.Diagnose(Distance(sample.tip,previous_tip)>kMaximumSampleJumpMeters ? "POSITION_JUMP" : "TIMING_GAP",threshold());
            state.raw_samples.clear();
            state.active = false;
            state.blocked_until_release = true;
            state.visual = GestureVisualState::Canceled;
            state.feedback_seconds = kFeedbackSeconds;
            return true;
        }
    }
    if (state.raw_samples.size() >= kMaximumRawSamples) {
        state.Diagnose("CAPACITY",threshold());
        state.raw_samples.clear();
        state.active = false;
        state.blocked_until_release = true;
        state.visual = GestureVisualState::Canceled;
        state.feedback_seconds = kFeedbackSeconds;
        return true;
    }
    bool timed_out = false;
    if (!state.gameplay && !state.relaxed && !state.raw_samples.empty()) {
        const std::int64_t duration_ns = static_cast<std::int64_t>(
            static_cast<double>(state.profile.draw_time_seconds) * 1.0e9);
        const std::int64_t start = state.raw_samples.front().predicted_display_time_ns;
        const std::int64_t elapsed = point.predicted_display_time_ns - start;
        if (elapsed >= duration_ns) {
            timed_out = true;
            // Score only motion before the deadline, even if the display frame
            // straddles it. Holding the trigger must not begin another attempt.
            const auto& previous = state.raw_samples.back();
            const std::int64_t previous_elapsed =
                previous.predicted_display_time_ns - start;
            const float blend = std::clamp(static_cast<float>(duration_ns - previous_elapsed) /
                static_cast<float>(elapsed - previous_elapsed), 0.0F, 1.0F);
            point.position_x_m = previous.position_x_m +
                (point.position_x_m - previous.position_x_m) * blend;
            point.position_y_m = previous.position_y_m +
                (point.position_y_m - previous.position_y_m) * blend;
            point.position_z_m = previous.position_z_m +
                (point.position_z_m - previous.position_z_m) * blend;
            point.predicted_display_time_ns = start + duration_ns;
            state.blocked_until_release = sample.cast_held;
        }
    }
    state.raw_samples.push_back(point);
    if (!released && !timed_out) return true;

    state.active = false;
    if (state.raw_samples.size() < 2) {
        state.Diagnose("TOO_SHORT",threshold());
        state.raw_samples.clear();
        state.visual = GestureVisualState::Rejected;
        state.feedback_seconds = kFeedbackSeconds;
        ++state.rejected;
        return true;
    }
    // Project the observed geometry, not a 12-second PC mouse sampling clock.
    // The shape matcher resamples by distance, so a quick complete rune retains
    // its corners and receives the same score as a slow one.
    std::vector<std::array<float, 2>> projected;
    projected.reserve(state.raw_samples.size());
    const auto& plane = state.plane;
    for (const auto& captured : state.raw_samples) {
        const float x = captured.position_x_m - plane.origin_x_m;
        const float y = captured.position_y_m - plane.origin_y_m;
        const float z = captured.position_z_m - plane.origin_z_m;
        projected.push_back({
            0.5F + (x * plane.right_x + y * plane.right_y + z * plane.right_z) / plane.width_m,
            0.5F - (x * plane.up_x + y * plane.up_y + z * plane.up_z) / plane.height_m});
    }
    // Feedback stays at the real wand path. Recognition's alignment must never
    // rotate, resize, or snap the displayed trail back onto the guide.
    state.feedback_points.clear();
    const auto stride = std::max<std::size_t>(1,
        (projected.size() + kMaximumGuideTrailPoints - 1) / kMaximumGuideTrailPoints);
    for (std::size_t i = 0; i < projected.size(); i += stride)
        state.feedback_points.push_back({projected[i][0], projected[i][1]});
    if ((projected.size() - 1) % stride != 0)
        state.feedback_points.push_back({projected.back()[0], projected.back()[1]});
    // Restore the previous 4 mm deadband before distance resampling. Holding
    // the wand still must not add meters of accumulated tracking jitter to a
    // short completed rune. Keep the final point and real visual trail intact.
    std::vector<std::array<float,2>> filtered;
    filtered.reserve(projected.size());
    for(std::size_t i=0;i<projected.size();++i)
        if(filtered.empty() || i+1==projected.size() ||
           std::hypot(projected[i][0]-filtered.back()[0],projected[i][1]-filtered.back()[1])>=kJitterMeters/kGestureExtentMeters)
            filtered.push_back(projected[i]);
    const auto score = state.gameplay ?
        CompareGameplayGestureShape(filtered,state.shape_template) :
        CompareGestureShape(filtered,state.shape_template,effective_accuracy(),state.relaxed);
    state.last_score = score.score;
    state.feedback_seconds = kFeedbackSeconds;
    const float required_score = threshold();
    state.Diagnose(score.valid&&score.score>=required_score ? "MATCHED" : "SHAPE_REJECTED",required_score);
    state.raw_samples.clear();
    if (score.valid && score.score >= required_score) {
        state.visual = GestureVisualState::Accepted;
        ++state.accepted;
        state.pending_event = {state.next_serial++,
                               sample.predicted_display_time_ns,
                               sample.tip, state.locked_origin,
                               state.locked_direction, score.score,
                               required_score};
        state.event_pending = true;
    } else {
        state.visual = GestureVisualState::Rejected;
        ++state.rejected;
    }
    return true;
}

bool QuestGesture::ConsumeEvent(FlipendoEvent* output) {
    State& state = *state_;
    if (output == nullptr || !state.event_pending) return false;
    *output = state.pending_event;
    state.event_pending = false;
    return true;
}

bool QuestGesture::BuildGuide(GestureGuide* const output) const {
    if (output == nullptr) return false;
    output->template_points.clear();
    output->trail_points.clear();
    output->trail_state = state_->visual;
    output->visible = false;
    if (!IsLoaded()) return true;

    const State& state = *state_;
    if (!state.active && state.visual == GestureVisualState::Idle) return true;
    hpvr_wand_lesson_plane plane{};
    std::array<float, 3> normal{};
    plane = state.plane;
    normal = state.locked_direction;
    output->plane_normal = normal;
    output->template_points.reserve(state.template_points.size());
    for (const auto point : state.template_points) {
        output->template_points.push_back(GuidePoint(plane, point));
    }

    if (state.active && !state.raw_samples.empty()) {
        const std::size_t count = state.raw_samples.size();
        const std::size_t stride = std::max<std::size_t>(
            1, (count + kMaximumGuideTrailPoints - 1) /
                   kMaximumGuideTrailPoints);
        output->trail_points.reserve(
            std::min(count, kMaximumGuideTrailPoints) + 1);
        for (std::size_t index = 0; index < count; index += stride) {
            const auto& point = state.raw_samples[index];
            output->trail_points.push_back(
                {point.position_x_m, point.position_y_m, point.position_z_m});
        }
        if ((count - 1) % stride != 0) {
            const auto& point = state.raw_samples.back();
            output->trail_points.push_back(
                {point.position_x_m, point.position_y_m, point.position_z_m});
        }
    } else {
        output->trail_points.reserve(state.feedback_points.size());
        for (const auto point : state.feedback_points) {
            output->trail_points.push_back(GuidePoint(plane, point));
        }
    }
    output->visible = output->template_points.size() >= 2;
    return true;
}
bool QuestGesture::IsLoaded() const { return !state_->template_points.empty(); }
GestureVisualState QuestGesture::visual_state() const { return state_->visual; }
float QuestGesture::last_score() const { return state_->last_score; }
float QuestGesture::threshold() const {
    if (!IsLoaded()) return 0.0F;
    const std::uint32_t index = state_->relaxed || state_->gameplay ? 0 :
        std::min(state_->lesson_round, state_->profile.pass_mark_count - 1);
    return state_->profile.pass_marks[index];
}
float QuestGesture::authored_accuracy() const {
    return IsLoaded() ? state_->profile.accuracy_radius : 0.0F;
}
float QuestGesture::effective_accuracy() const {
    return authored_accuracy() * (state_->relaxed ? kRelaxedAssistMultiplier : 1.0F);
}
bool QuestGesture::relaxed_difficulty() const { return state_->relaxed; }
std::uint32_t QuestGesture::lesson_round() const { return state_->lesson_round; }
float QuestGesture::time_limit_seconds() const {
    return IsLoaded() && !state_->relaxed && !state_->gameplay ? state_->profile.draw_time_seconds : 0.0F;
}
std::uint32_t QuestGesture::attempt_count() const { return state_->attempts; }
std::uint32_t QuestGesture::accepted_count() const { return state_->accepted; }
std::uint32_t QuestGesture::rejected_count() const { return state_->rejected; }
GestureDiagnostics QuestGesture::diagnostics() const { return state_->diagnostics; }

}  // namespace hpvr::quest
