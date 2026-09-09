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

bool FitProjectedShapeToTemplate(
    hpvr_wand_vec2* points,
    const std::uint32_t point_count,
    const std::vector<hpvr_wand_vec2>& template_points) {
    if (points == nullptr || point_count < 2 || template_points.size() < 2)
        return false;
    float drawn_min_x = std::numeric_limits<float>::infinity();
    float drawn_max_x = -std::numeric_limits<float>::infinity();
    float drawn_min_y = std::numeric_limits<float>::infinity();
    float drawn_max_y = -std::numeric_limits<float>::infinity();
    float template_min_x = std::numeric_limits<float>::infinity();
    float template_max_x = -std::numeric_limits<float>::infinity();
    float template_min_y = std::numeric_limits<float>::infinity();
    float template_max_y = -std::numeric_limits<float>::infinity();
    for (std::uint32_t index = 0; index < point_count; ++index) {
        drawn_min_x = std::min(drawn_min_x, points[index].x);
        drawn_max_x = std::max(drawn_max_x, points[index].x);
        drawn_min_y = std::min(drawn_min_y, points[index].y);
        drawn_max_y = std::max(drawn_max_y, points[index].y);
    }
    for (const auto point : template_points) {
        template_min_x = std::min(template_min_x, point.x);
        template_max_x = std::max(template_max_x, point.x);
        template_min_y = std::min(template_min_y, point.y);
        template_max_y = std::max(template_max_y, point.y);
    }
    const float drawn_span_x = drawn_max_x - drawn_min_x;
    const float drawn_span_y = drawn_max_y - drawn_min_y;
    const float template_span_x = template_max_x - template_min_x;
    const float template_span_y = template_max_y - template_min_y;
    if (!std::isfinite(drawn_span_x) || !std::isfinite(drawn_span_y) ||
        !std::isfinite(template_span_x) || !std::isfinite(template_span_y) ||
        drawn_span_x < kMinimumProjectedSpan ||
        drawn_span_y < kMinimumProjectedSpan || template_span_x <= 0.0F ||
        template_span_y <= 0.0F) return false;
    for (std::uint32_t index = 0; index < point_count; ++index) {
        points[index].x = template_min_x +
            (points[index].x - drawn_min_x) * template_span_x / drawn_span_x;
        points[index].y = template_min_y +
            (points[index].y - drawn_min_y) * template_span_y / drawn_span_y;
    }
    return true;
}
}  // namespace

struct QuestGesture::State {
    std::vector<hpvr_wand_vec2> template_points;
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
    state.profile = report;
    Reset();
    return true;
}

void QuestGesture::SetLessonDifficulty(const bool relaxed) {
    if (state_->relaxed == relaxed) return;
    state_->relaxed = relaxed;
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
            state.raw_samples.clear();
            state.active = false;
            state.blocked_until_release = true;
            state.visual = GestureVisualState::Canceled;
            state.feedback_seconds = kFeedbackSeconds;
            return true;
        }
    }
    if (state.raw_samples.size() >= kMaximumRawSamples) {
        state.raw_samples.clear();
        state.active = false;
        state.blocked_until_release = true;
        state.visual = GestureVisualState::Canceled;
        state.feedback_seconds = kFeedbackSeconds;
        return true;
    }
    bool timed_out = false;
    if (!state.relaxed && !state.raw_samples.empty()) {
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
        state.raw_samples.clear();
        state.visual = GestureVisualState::Rejected;
        state.feedback_seconds = kFeedbackSeconds;
        ++state.rejected;
        return true;
    }
    const std::int64_t period =
        hpvr_hp1_lesson_resampling_period_ns(state.profile.draw_time_seconds);
    if (period <= 0) return false;
    const hpvr_wand_projection_options options{
        HPVR_WAND_AUTHORED_POINT_CAPACITY, kJitterMeters, period};
    std::array<hpvr_wand_vec2, HPVR_WAND_AUTHORED_POINT_CAPACITY> projected{};
    hpvr_wand_projection_report projection{};
    const std::uint32_t projection_status = hpvr_wand_project_trajectory(
        state.raw_samples.data(),
        static_cast<std::uint32_t>(state.raw_samples.size()), &state.plane,
        &options, projected.data(),
        static_cast<std::uint32_t>(projected.size()), &projection);
    state.raw_samples.clear();
    if (projection_status != HPVR_WAND_PROJECTION_OK ||
        projection.status != projection_status ||
        projection.output_point_count < 2 ||
        projection.output_point_count > projected.size()) {
        state.feedback_points.clear();
        state.visual = GestureVisualState::Rejected;
        state.feedback_seconds = kFeedbackSeconds;
        ++state.rejected;
        return true;
    }
    if (state.relaxed && !FitProjectedShapeToTemplate(projected.data(),
                                     projection.output_point_count,
                                     state.template_points)) {
        state.feedback_points.clear();
        state.visual = GestureVisualState::Rejected;
        state.feedback_seconds = kFeedbackSeconds;
        ++state.rejected;
        return true;
    }
    state.feedback_points.assign(
        projected.begin(),
        projected.begin() +
            static_cast<std::ptrdiff_t>(projection.output_point_count));
    hpvr_hp1_gesture_score_report score{};
    const float accuracy = effective_accuracy();
    const std::uint32_t score_status = hpvr_hp1_compare_gesture(
        projected.data(), projection.output_point_count,
        state.template_points.data(),
        static_cast<std::uint32_t>(state.template_points.size()),
        accuracy, &score);
    if (score_status != HPVR_HP1_SCORE_OK ||
        score.status != score_status || !std::isfinite(score.score)) return false;
    state.last_score = score.score;
    state.feedback_seconds = kFeedbackSeconds;
    const float required_score = threshold();
    if (score.score >= required_score) {
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
    const std::uint32_t index = state_->relaxed ? 0 :
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
    return IsLoaded() && !state_->relaxed ? state_->profile.draw_time_seconds : 0.0F;
}
std::uint32_t QuestGesture::attempt_count() const { return state_->attempts; }
std::uint32_t QuestGesture::accepted_count() const { return state_->accepted; }
std::uint32_t QuestGesture::rejected_count() const { return state_->rejected; }

}  // namespace hpvr::quest
