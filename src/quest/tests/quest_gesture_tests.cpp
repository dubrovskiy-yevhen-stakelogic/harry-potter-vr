#include "hpvr/quest_gesture.h"
#include "hpvr/hp1_gesture_c.h"

#include <cstdlib>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <string_view>
#include <sstream>
#include <vector>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "quest gesture test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

using Point = std::array<float, 2>;
void ReplayHeadsetTraces(const std::vector<Point>& pattern,float radius) {
    const char* path=std::getenv("HPVR_TEST_GESTURE_TRACE_LOG");
    if(path==nullptr||path[0]=='\0')return;
    std::ifstream stream(path);Expect(stream.good(),"headset numeric trace file opens");
    const char* expected=std::getenv("HPVR_TEST_GESTURE_EXPECT_GAMEPLAY_MATCHES");
    const bool require_matches=expected!=nullptr&&std::string_view(expected)=="1";
    unsigned count=0,lesson_passes=0,gameplay_passes=0;
    std::string line;
    while(std::getline(stream,line)){
        const auto marker=line.find("points=");if(marker==std::string::npos)continue;
        std::vector<Point> points;std::istringstream input(line.substr(marker+7));std::string field;
        while(std::getline(input,field,';')){std::replace(field.begin(),field.end(),',',' ');std::istringstream pair(field);Point p{};if(pair>>p[0]>>p[1])points.push_back(p);}
        if(points.size()<3)continue;
        const auto score=hpvr::quest::CompareGestureShape(points,pattern,radius,true);
        ++count;lesson_passes+=score.valid&&score.score>=.5F;
        const auto gameplay=hpvr::quest::CompareGameplayGestureShape(points,pattern);
        gameplay_passes+=gameplay.valid&&gameplay.score>=.5F;
        if(require_matches)Expect(gameplay.valid&&gameplay.score>=.5F,
            "a completed headset coil rejected by lesson tracing must pass gameplay structure");
    }
    Expect(count>0,"headset replay contains numeric paths");
    std::cout<<"headset replay: "<<count<<" strokes, lesson passes="<<lesson_passes
             <<", gameplay passes="<<gameplay_passes<<'\n';
}
std::vector<Point> Transform(const std::vector<Point>& points, float angle,
                             float scale = 1.0F, bool reverse = false) {
    std::vector<Point> result;
    for (const auto& point : points) result.push_back({
        2.0F + scale * (point[0] * std::cos(angle) - point[1] * std::sin(angle)),
        -3.0F + scale * (point[0] * std::sin(angle) + point[1] * std::cos(angle))});
    if (reverse) std::reverse(result.begin(), result.end());
    return result;
}

std::vector<Point> MakeCoil(float turns,float inner_radius=.08F) {
    std::vector<Point> points;
    for(unsigned i=0;i<128;++i){
        const float t=float(i)/127,angle=t*turns*2*std::numbers::pi_v<float>;
        const float radius=inner_radius+(.5F-inner_radius)*t;
        points.push_back({radius*std::cos(angle),radius*std::sin(angle)});
    }
    return points;
}

void TestGameplayStructure(const std::vector<Point>& reference) {
    unsigned cases=0;
    const auto started=std::chrono::steady_clock::now();
    for(float turns:{1.F,1.15F,1.3F,1.5F})
        for(float aspect:{.55F,1.F,1.8F})
            for(unsigned degrees=0;degrees<360;degrees+=15)
                for(bool backwards:{false,true})for(bool mirrored:{false,true}){
                    auto points=MakeCoil(turns);
                    for(auto& p:points)p[0]*=aspect*(mirrored?-1.F:1.F);
                    const auto drawn=Transform(points,float(degrees)*std::numbers::pi_v<float>/180,.4F,backwards);
                    const auto result=hpvr::quest::CompareGameplayGestureShape(drawn,reference);
                    if(!result.valid||result.score<.5F)std::cerr<<"gameplay failure turns="<<turns<<" aspect="<<aspect<<" angle="<<degrees<<'\n';
                    Expect(result.valid&&result.score>=.5F,
                        "a broad single curl passes gameplay at any angle direction or handedness");
                    ++cases;
                }
    std::vector<Point> line,circle,ellipse,figure_eight,zigzag,arc;
    for(unsigned i=0;i<128;++i){
        const float t=float(i)/127,angle=t*2*std::numbers::pi_v<float>;
        line.push_back({t,t});
        circle.push_back({.5F*std::cos(angle),.5F*std::sin(angle)});
        ellipse.push_back({.2F*std::cos(angle),.5F*std::sin(angle)});
        figure_eight.push_back({.5F*std::cos(angle),.5F*std::sin(2*angle)});
        zigzag.push_back({t,(i%16<8)?.4F:-.4F});
        arc.push_back({.5F*std::cos(angle*.65F),.5F*std::sin(angle*.65F)});
    }
    auto repeated=MakeCoil(3.F),tiny=Transform(MakeCoil(1.3F),0,.01F);
    for(const auto* wrong:{&line,&circle,&ellipse,&figure_eight,&zigzag,&arc,&repeated,&tiny})
        for(unsigned degrees=0;degrees<360;degrees+=15){
            const auto drawn=Transform(*wrong,float(degrees)*std::numbers::pi_v<float>/180);
            const auto result=hpvr::quest::CompareGameplayGestureShape(drawn,reference);
            Expect(!result.valid||result.score<.5F,
                "gameplay assistance rejects lines circles arcs scribbles repeated loops and tiny jitter");
        }
    std::uint32_t seed=0x9765211;
    for(unsigned trial=0;trial<512;++trial){
        std::vector<Point> wrong;
        for(unsigned i=0;i<16;++i){
            seed=seed*1664525U+1013904223U;const float x=float(seed>>16)/65535;
            seed=seed*1664525U+1013904223U;wrong.push_back({x,float(seed>>16)/65535});
        }
        const auto result=hpvr::quest::CompareGameplayGestureShape(wrong,reference);
        Expect(!result.valid||result.score<.5F,"unrelated random strokes cannot pass gameplay structure");
    }
    auto invalid=MakeCoil(1.3F);invalid[2][0]=std::numeric_limits<float>::quiet_NaN();
    Expect(!hpvr::quest::CompareGameplayGestureShape(invalid,reference).valid&&
           !hpvr::quest::CompareGameplayGestureShape({},reference).valid,
           "gameplay recognition still rejects nonfinite and absent tracking");
    invalid.assign(16385,{0,0});
    Expect(!hpvr::quest::CompareGameplayGestureShape(invalid,reference).valid,
           "gameplay recognition keeps the bounded input limit");
    std::cout<<"gameplay structure: "<<cases<<" coil variations plus 704 negatives in "
        <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<" ms host\n";
}

void TestShapeMatcher() {
    // An invented asymmetric rune, not coordinates copied from game assets.
    const std::vector<Point> pattern{{.10F,.90F},{.28F,.35F},{.65F,.10F},
        {.43F,.58F},{.80F,.35F},{.64F,.88F}};
    std::vector<Point> dense{pattern.front()};
    for (std::size_t i = 1; i < pattern.size(); ++i) {
        const auto parts = i * 7 + 3;
        for (std::size_t part = 1; part <= parts; ++part) {
            const float t = static_cast<float>(part) / static_cast<float>(parts);
            dense.push_back({pattern[i-1][0] + (pattern[i][0]-pattern[i-1][0])*t,
                             pattern[i-1][1] + (pattern[i][1]-pattern[i-1][1])*t});
        }
    }
    std::vector<Point> line, circle, scribble, spiral;
    std::uint32_t random = 0x1d8a9223;
    for (int i = 0; i <= 128; ++i) {
        const float t = static_cast<float>(i) / 128.0F;
        line.push_back({t, t});
        circle.push_back({.5F + .4F*std::cos(2*std::numbers::pi_v<float>*t),
                          .5F + .4F*std::sin(2*std::numbers::pi_v<float>*t)});
        spiral.push_back({.5F + .4F*t*std::cos(8*std::numbers::pi_v<float>*t),
                          .5F + .4F*t*std::sin(8*std::numbers::pi_v<float>*t)});
        random = random * 1664525U + 1013904223U;
        const float x = static_cast<float>(random >> 16) / 65535.0F;
        random = random * 1664525U + 1013904223U;
        scribble.push_back({x,static_cast<float>(random >> 16)/65535.0F});
    }
    const auto started = std::chrono::steady_clock::now();
    unsigned rotation_checks = 0;
    for (int degrees = 0; degrees < 360; degrees += 5) {
        const float angle = static_cast<float>(degrees)*std::numbers::pi_v<float>/180.0F;
        for (bool relaxed : {false, true}) for (bool reverse : {false, true}) {
            const auto transformed = Transform(dense,angle,relaxed ? .25F : 1.0F,reverse);
            const auto score = hpvr::quest::CompareGestureShape(transformed,pattern,.04F,relaxed);
            Expect(score.valid && score.score > .999F,
                   "all rotations and stroke directions preserve the resampled shape");
            ++rotation_checks;
        }
        for (const auto* negative : {&line,&circle,&spiral,&scribble}) {
            const auto transformed = Transform(*negative,angle);
            const auto score = hpvr::quest::CompareGestureShape(transformed,pattern,.07F,true);
            Expect(!score.valid || score.score < .5F,
                   "rotation/scale assistance must reject lines circles spirals and scribbles");
        }
    }
    const auto small = Transform(pattern,1.234F,.25F);
    Expect(hpvr::quest::CompareGestureShape(small,pattern,.04F,false).score < .5F,
           "original difficulty retains physical drawing size after rotation alignment");
    Expect(hpvr::quest::CompareGestureShape(small,pattern,.07F,true).score > .999F,
           "relaxed difficulty keeps uniform scale assistance");
    auto bent = pattern;
    bent[2][0] += .16F;
    const auto strict = hpvr::quest::CompareGestureShape(bent,pattern,.04F,false);
    const auto relaxed = hpvr::quest::CompareGestureShape(bent,pattern,.07F,true);
    Expect(strict.valid && relaxed.valid && relaxed.score > strict.score + .10F,
           "wider relaxed radius must remain observably more forgiving");
    auto invalid = pattern; invalid[2][0] = std::numeric_limits<float>::quiet_NaN();
    Expect(!hpvr::quest::CompareGestureShape(invalid,pattern,.04F,true).valid,
           "nonfinite points fail closed");
    invalid[2][0] = std::numeric_limits<float>::infinity();
    Expect(!hpvr::quest::CompareGestureShape(invalid,pattern,.04F,true).valid,
           "infinite points fail closed");
    invalid.assign(16385,Point{0,0});
    Expect(!hpvr::quest::CompareGestureShape(invalid,pattern,.04F,true).valid,
           "oversized sample arrays are rejected before traversal");
    Expect(!hpvr::quest::CompareGestureShape({},pattern,.04F,true).valid &&
           !hpvr::quest::CompareGestureShape(pattern,pattern,-1.0F,true).valid &&
           !hpvr::quest::CompareGestureShape(line,pattern,.04F,true).valid,
           "empty strokes invalid accuracy and degenerate geometry fail closed");
    std::cout << "rotation matcher: " << rotation_checks << " positive angle/direction cases, "
              << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()
              << " ms host verification\n";
}

void TestNaturalStrokeVariants(const std::vector<Point>& pattern,float radius) {
    std::vector<Point> dense;
    for(std::size_t i=1;i<pattern.size();++i)for(unsigned part=0;part<8;++part){
        const float t=static_cast<float>(part)/8;
        dense.push_back({pattern[i-1][0]+(pattern[i][0]-pattern[i-1][0])*t,
                         pattern[i-1][1]+(pattern[i][1]-pattern[i-1][1])*t});
    }
    dense.push_back(pattern.back());
    unsigned cases=0;
    for(float aspect:{.4F,.55F,.75F,1.F,1.4F,2.F,2.5F})
        for(unsigned degrees=0;degrees<360;degrees+=30)for(bool reverse:{false,true}) {
            std::vector<Point> distorted=dense;
            for(auto& p:distorted)p[0]*=aspect;
            auto input=Transform(distorted,float(degrees)*std::numbers::pi_v<float>/180,.75F,reverse);
            const auto exact=hpvr::quest::CompareGestureShape(input,pattern,radius,true);
            if(!exact.valid||exact.score<.5F)
                std::cerr<<"aspect failure "<<aspect<<" angle "<<degrees<<" reverse "<<reverse<<" score "<<exact.score<<'\n';
            Expect(exact.valid&&exact.score>=.5F,"bounded aspect distortion keeps a recognizable rune at any angle");
            ++cases;
        }
    // Tiny wrist tremor is not an intentional second stroke. This deliberately
    // is not the exact resampled template used by the C48 rotation tests.
    auto noisy=dense;
    for(std::size_t i=1;i+1<noisy.size();++i){
        noisy[i][0]+=.008F*std::sin(float(i)*1.7F);
        noisy[i][1]+=.008F*std::cos(float(i)*2.3F);
    }
    for(unsigned degrees=0;degrees<360;degrees+=30){
        const auto input=Transform(noisy,float(degrees)*std::numbers::pi_v<float>/180);
        const auto match=hpvr::quest::CompareGestureShape(input,pattern,radius,true);
        Expect(match.valid&&match.score>=.5F,"millimeter wrist tremor does not destroy a complete rune");
    }
    auto tails=dense;
    tails.insert(tails.begin(),{dense.front()[0]-.025F,dense.front()[1]+.025F});
    tails.push_back({dense.back()[0]+.035F,dense.back()[1]-.035F});
    Expect(hpvr::quest::CompareGestureShape(tails,pattern,radius,true).score>=.5F,
           "short lead-in and overshoot retain the rune");
    // A wrong stroke must not become valid merely because aspect fit is allowed.
    std::vector<Point> circle,ellipse,line,scribble;
    std::uint32_t seed=0x944589;
    for(unsigned i=0;i<128;++i){
        const float angle=float(i)*2*std::numbers::pi_v<float>/127;
        circle.push_back({.5F+.4F*std::cos(angle),.5F+.4F*std::sin(angle)});
        ellipse.push_back({.5F+.16F*std::cos(angle),.5F+.4F*std::sin(angle)});
        line.push_back({float(i)/127,float(i)/127+.001F*std::sin(angle*3)});
        seed=seed*1664525U+1013904223U;const float x=float(seed>>16)/65535;
        seed=seed*1664525U+1013904223U;scribble.push_back({x,float(seed>>16)/65535});
    }
    for(const auto* negative:{&circle,&ellipse,&line,&scribble})
        for(unsigned degrees=0;degrees<360;degrees+=30){
            const auto input=Transform(*negative,float(degrees)*std::numbers::pi_v<float>/180);
            const auto match=hpvr::quest::CompareGestureShape(input,pattern,radius,true);
            Expect(!match.valid||match.score<.5F,"aspect assistance still rejects unrelated strokes");
        }
    // Independent random paths, not just one conveniently chosen scribble.
    for(unsigned trial=0;trial<64;++trial){
        std::vector<Point> wrong;
        for(unsigned i=0;i<16;++i){
            seed=seed*1664525U+1013904223U;const float x=float(seed>>16)/65535;
            seed=seed*1664525U+1013904223U;wrong.push_back({x,float(seed>>16)/65535});
        }
        const auto match=hpvr::quest::CompareGestureShape(wrong,pattern,radius,true);
        Expect(!match.valid||match.score<.5F,"random multi-turn strokes cannot pass through coverage alone");
    }
    std::cout<<"natural stroke variants: "<<cases<<" aspect/rotation/direction cases plus jitter/tails/negatives\n";
}
}  // namespace

int main() {
    for(const auto& w:std::vector<std::vector<Point>>{
        {{0,0},{.25F,1},{.5F,.2F},{.75F,1},{1,0}},
        {{0,0},{.18F,.8F},{.55F,.1F},{.8F,1},{1,.1F}},
        {{0,0},{.1F,.35F},{.2F,.8F},{.3F,.95F},{.4F,.5F},{.45F,.6F},{.54F,.18F},{.68F,.7F},{.8F,1},{.92F,.55F},{1,.1F}}}) {
        Expect(hpvr::quest::CompareGameplayWingardiumShape(w).score==1,"unequal complete gameplay W accepted");
        auto reversed=w;std::reverse(reversed.begin(),reversed.end());
        Expect(hpvr::quest::CompareGameplayWingardiumShape(reversed).score==1,"reverse W accepted");
    }
    for(const auto& wrong:std::vector<std::vector<Point>>{
        {{0,0},{.5F,1},{1,0}},{{0,0},{1,0}},
        {{0,0},{.25F,1},{.5F,0}},{{0,1},{.25F,0},{.5F,1},{.75F,0},{1,1}}})
        Expect(hpvr::quest::CompareGameplayWingardiumShape(wrong).score==0,"V line partial W and M rejected");
    TestShapeMatcher();
    TestGameplayStructure(MakeCoil(1.3F));
    TestNaturalStrokeVariants({{.10F,.90F},{.28F,.35F},{.65F,.10F},{.43F,.58F},{.80F,.35F},{.64F,.88F}},.0525F);
    hpvr::quest::QuestGesture gesture;
    Expect(!gesture.IsLoaded(), "fresh gesture must not claim a profile");
    Expect(!gesture.SelectSpell(hpvr::quest::GestureSpell::Alohomora),
           "unloaded profiles cannot be selected");
    hpvr::quest::GestureSample sample{};
    Expect(!gesture.Observe(sample),
           "gesture without a loaded profile must fail closed");
    Expect(gesture.visual_state() == hpvr::quest::GestureVisualState::Idle,
           "fresh visual state must be idle");
    Expect(!gesture.relaxed_difficulty() && gesture.lesson_round() == 0,
           "original first-round policy must be the default");
    gesture.SetLessonRound(999);
    Expect(gesture.lesson_round() == 3,
           "invalid lesson round must clamp to the fourth authored tier");
    gesture.SetLessonRound(0);
    Expect(gesture.time_limit_seconds() == 0,
           "unloaded gesture must not invent a deadline");

    const char* data_root = std::getenv("HPVR_TEST_HP1_DATA_ROOT");
    if (data_root != nullptr && data_root[0] != '\0') {
        Expect(gesture.LoadFlipendoProfile(std::filesystem::path(data_root)),
               "owned Flipendo profile must load");
        Expect(gesture.IsLoaded(), "loaded profile must report ready");
        Expect(gesture.threshold() == 0.5F,
               "first tutorial pass mark must remain authored");
        Expect(gesture.effective_accuracy() == gesture.authored_accuracy(),
               "original mode must use the authored accuracy without test assist");
        Expect(gesture.time_limit_seconds() == 12.0F,
               "original drawing deadline must come from the owned lesson override");

        hpvr::quest::GestureSample neutral{};
        neutral.tracked = true;
        neutral.predicted_display_time_ns = 1'000'000'000;
        neutral.tip = {0.0F, 0.0F, 0.0F};
        neutral.aim_direction = {0.0F, 0.0F, -1.0F};
        Expect(gesture.Observe(neutral),
               "neutral tracked frame must arm a fresh attempt");

        hpvr::quest::GestureSample pressed = neutral;
        pressed.cast_held = true;
        pressed.predicted_display_time_ns += 10'000'000;
        Expect(gesture.Observe(pressed),
               "trigger press must start capture without dispatching");

        hpvr::quest::GestureSample released = pressed;
        released.cast_held = false;
        released.predicted_display_time_ns += 20'000'000;
        released.tip = {0.01F, 0.0F, 0.0F};
        Expect(gesture.Observe(released),
               "trigger release must finalize the attempt");
        Expect(gesture.attempt_count() == 1,
               "trigger release must finalize exactly one attempt");
        Expect(gesture.accepted_count() + gesture.rejected_count() == 1,
               "finalized attempt must have one terminal score result");

        std::vector<hpvr_wand_vec2> template_points(
            HPVR_HP1_GESTURE_MAX_TEMPLATE_POINTS);
        std::vector<std::int32_t> segments(HPVR_HP1_GESTURE_MAX_SEGMENTS);
        hpvr_hp1_spell_profile_report profile{};
        const std::filesystem::path root(data_root);
        if (std::filesystem::exists(root / "Maps/Lev_Tut3.unr")) {
            using hpvr::quest::GestureSpell;
            hpvr::quest::QuestGesture multi;
            Expect(multi.LoadProfiles(root, true), "all three owned spell profiles load together");
            Expect(multi.selected_spell() == GestureSpell::Flipendo, "profile loading selects Flipendo");
            const std::array<const char*, 2> names{"spellAloho", "SPELLLEV"};
            const std::array<const char*, 2> patterns{"AlohoPattern", "LevPattern"};
            std::array<std::vector<Point>, 2> owned_shapes;
            for (unsigned spell_index = 0; spell_index < 2; ++spell_index) {
                const auto spell = spell_index == 0 ? GestureSpell::Alohomora : GestureSpell::Wingardium;
                std::vector<hpvr_wand_vec2> points(HPVR_HP1_GESTURE_MAX_TEMPLATE_POINTS);
                std::vector<std::int32_t> profile_segments(HPVR_HP1_GESTURE_MAX_SEGMENTS);
                hpvr_hp1_spell_profile_report charm_profile{};
                Expect(hpvr_hp1_load_spell_profile_utf8((root / "system/HPBase.u").string().c_str(),
                    (root / "Maps/Lev_Tut3.unr").string().c_str(), patterns[spell_index], names[spell_index],
                    points.data(), static_cast<std::uint32_t>(points.size()), profile_segments.data(),
                    static_cast<std::uint32_t>(profile_segments.size()), &charm_profile) == HPVR_HP1_PROFILE_OK,
                    "owned new spell template loads");
                points.resize(charm_profile.template_point_count);
                for (const auto& point : points) owned_shapes[spell_index].push_back({point.x, point.y});
                if(spell_index==0){
                    // A hand-drawn keyhole: oval bow, narrow neck, broad base.
                    const std::vector<Point> keyhole{{.5F,0},{.18F,.03F},{0,.22F},{.05F,.42F},
                        {.35F,.56F},{.12F,1},{.86F,.93F},{.64F,.54F},{.93F,.39F},
                        {1,.18F},{.77F,.02F},{.5F,0}};
                    for(unsigned seam=0;seam+1<keyhole.size();++seam){
                        std::vector<Point> stroke;
                        for(unsigned i=0;i<keyhole.size();++i)stroke.push_back(keyhole[(i+seam)%(keyhole.size()-1)]);
                        for(bool reverse:{false,true})for(float angle:{0.F,.3F,1.57F}){
                            auto variant=Transform(stroke,angle,.65F,reverse);
                            Expect(hpvr::quest::CompareGameplayAlohomoraShape(variant,owned_shapes[0]).score==1,
                                "rough keyhole accepts different starting points and directions");
                        }
                    }
                    auto rough=owned_shapes[0];
                    for(std::size_t i=0;i<rough.size();++i){rough[i][0]=rough[i][0]*.65F+.045F*std::sin(float(i)*.8F);rough[i][1]+=.055F*std::cos(float(i)*1.1F);}
                    Expect(hpvr::quest::CompareGameplayAlohomoraShape(rough,owned_shapes[0]).score==1,"rough unequal Alohomora accepted during gameplay");
                    std::vector<Point> circle,line,zigzag;
                    for(unsigned i=0;i<64;++i){const float t=float(i)/63;
                        circle.push_back({.5F+.45F*std::cos(t*2*std::numbers::pi_v<float>),.5F+.45F*std::sin(t*2*std::numbers::pi_v<float>)});
                        line.push_back({t,t});zigzag.push_back({t,(i%2)?1.0F:0.0F});
                    }
                    for(const auto& wrong:{circle,line,zigzag}){
                        Expect(hpvr::quest::CompareGameplayAlohomoraShape(wrong,owned_shapes[0]).score==0,"gameplay Alohomora rejects circle line and scribble");
                        const auto result=hpvr::quest::CompareGestureShape(wrong,owned_shapes[0],charm_profile.accuracy_radius*1.75F,false);
                        Expect(!result.valid||result.score<charm_profile.pass_marks[0],"Alohomora scoring rejects circles, lines and scribbles");
                    }
                    const std::vector<Point> triangle{{0,1},{.5F,0},{1,1},{0,1}};
                    const std::vector<Point> square{{0,0},{1,0},{1,1},{0,1},{0,0}};
                    const std::vector<Point> w{{0,0},{.25F,1},{.5F,.15F},{.75F,1},{1,0}};
                    for(const auto& wrong:{triangle,square,w})
                        Expect(hpvr::quest::CompareGameplayAlohomoraShape(wrong,owned_shapes[0]).score==0,
                            "Alohomora rejects unrelated simple closed shapes and W");
                    for(const auto count:{4U,6U}){
                        const std::vector<Point> incomplete(keyhole.begin(),keyhole.begin()+count);
                        Expect(hpvr::quest::CompareGameplayAlohomoraShape(incomplete,owned_shapes[0]).score==0,
                            "an unfinished half-keyhole does not cast Alohomora");
                    }
                }
                if(spell_index==0)if(const char* replay=std::getenv("HPVR_TEST_ALOHOMORA_TRACE_LOG")){
                    std::ifstream input(replay);Expect(input.good(),"Alohomora numeric recording opens");
                    std::string line;
                    while(std::getline(input,line)){
                        const auto marker=line.find("points=");if(marker==std::string::npos)continue;
                        std::vector<Point> path;std::istringstream pairs(line.substr(marker+7));std::string field;
                        while(std::getline(pairs,field,';')){std::replace(field.begin(),field.end(),',',' ');
                            std::istringstream pair(field);Point p{};if(pair>>p[0]>>p[1])path.push_back(p);}
                        if(path.size()<3)continue;
                        std::cout<<"Alohomora headset replay";
                        for(float radius:{.03F,.0525F,.08F,.1F}){
                            const auto score=hpvr::quest::CompareGestureShape(path,owned_shapes[0],radius,true);
                            std::cout<<" radius="<<radius<<" score="<<score.score;
                        }
                        std::cout<<'\n';
                        const auto revised=hpvr::quest::CompareGestureShape(path,owned_shapes[0],charm_profile.accuracy_radius*1.75F,false);
                        const bool complete=line.find("seconds=0.")==std::string::npos;
                        Expect((revised.valid&&revised.score>=charm_profile.pass_marks[0])==complete,
                            "complete headset Alohomora passes first VR lesson tier; a short click does not");
                    }
                }
                Expect(multi.SelectSpell(spell) && multi.selected_spell() == spell,
                       "preloaded spell selection succeeds");
                multi.SetGameplayMode(true);
                for (unsigned angle = 0; angle < 360; angle += 45) {
                    // W keeps its upright identity: a half turn is an M, not W.
                    if(spell_index==1&&angle>45&&angle<315)continue;
                    multi.Reset();
                    hpvr::quest::GestureSample stroke{};
                    stroke.tracked = true; stroke.predicted_display_time_ns = 1'000'000'000;
                    Expect(multi.Observe(stroke), "neutral frame arms a new spell");
                    const auto shape = Transform(owned_shapes[spell_index], angle * std::numbers::pi_v<float> / 180, .55F);
                    for (std::size_t i = 0; i < shape.size(); ++i) {
                        stroke.cast_held = i + 1 != shape.size();
                        stroke.predicted_display_time_ns += 10'000'000;
                        stroke.tip = {(shape[i][0] - shape[0][0]) * .42F,
                                      -(shape[i][1] - shape[0][1]) * .42F, 0};
                        Expect(multi.Observe(stroke), "new spell accepts bounded tracked samples");
                    }
                    hpvr::quest::FlipendoEvent charm_event;
                    std::cout<<"GAMEPLAY_GESTURE spell="<<spell_index<<" angle="<<angle<<" score="<<multi.last_score()<<'\n';
                    Expect(multi.ConsumeEvent(&charm_event) && charm_event.spell == spell,
                           "rotated scaled new spell emits its own spell identity");
                }
                multi.SetGameplayMode(false);
                for (bool relaxed : {false,true}) for (unsigned round=0;round<4;++round) {
                    multi.SetLessonDifficulty(relaxed);multi.SetLessonRound(round);multi.Reset();
                    hpvr::quest::GestureSample stroke{};
                    stroke.tracked=true;stroke.tip={0,1,-1};stroke.aim_direction={0,0,-1};
                    stroke.predicted_display_time_ns=1'000'000'000;
                    Expect(multi.Observe(stroke),"lesson neutral frame arms");
                    stroke.cast_held=true;stroke.predicted_display_time_ns+=10'000'000;
                    Expect(multi.Observe(stroke),"lesson guide starts at wand");
                    hpvr::quest::GestureGuide guide;
                    Expect(multi.BuildGuide(&guide)&&guide.visible,"lesson guide is available");
                    for(std::size_t i=1;i<guide.template_points.size();++i) {
                        for(unsigned part=1;part<=4;++part) {
                            const float t=part/4.0F;
                            for(unsigned axis=0;axis<3;++axis)stroke.tip[axis]=
                                guide.template_points[i-1][axis]*(1-t)+guide.template_points[i][axis]*t;
                            stroke.predicted_display_time_ns+=10'000'000;
                            Expect(multi.Observe(stroke),"visible lesson guide is traceable");
                        }
                    }
                    stroke.cast_held=false;stroke.predicted_display_time_ns+=10'000'000;
                    Expect(multi.Observe(stroke),"lesson release submits the drawing");
                    hpvr::quest::FlipendoEvent lesson_event;
                    std::cout<<"charms guide spell="<<spell_index<<" relaxed="<<relaxed<<" round="<<round<<" score="<<multi.last_score()<<'\n';
                    Expect(multi.ConsumeEvent(&lesson_event)&&lesson_event.spell==spell,
                        "tracing the rendered guide passes every lesson round");
                }
                multi.SetLessonDifficulty(false);
                multi.SetLessonRound(3);
                Expect(multi.threshold() == charm_profile.pass_marks[3] && multi.time_limit_seconds() == 12,
                       "new spell retains authored lesson timing and fourth mark");
            }
            multi.SetGameplayMode(true);
            for (unsigned spell_index = 0; spell_index < 2; ++spell_index) {
                Expect(multi.SelectSpell(GestureSpell::Flipendo), "reset target identity before acquisition");
                multi.Reset();
                hpvr::quest::GestureSample stroke{};
                stroke.tracked=true;stroke.predicted_display_time_ns=1'000'000'000;
                Expect(multi.Observe(stroke), "aiming before rune acquisition observes a neutral gesture input");
                const auto spell=spell_index==0?GestureSpell::Alohomora:GestureSpell::Wingardium;
                Expect(multi.SelectSpell(spell), "new rune selects its profile on the acquisition frame");
                const auto shape=Transform(owned_shapes[spell_index],.71F,.55F);
                const auto attempts=multi.attempt_count();
                for(std::size_t i=0;i<shape.size();++i){
                    stroke.cast_held=i+1!=shape.size();stroke.predicted_display_time_ns+=10'000'000;
                    stroke.tip={(shape[i][0]-shape[0][0])*.42F,-(shape[i][1]-shape[0][1])*.42F,0};
                    Expect(multi.Observe(stroke), "first newly acquired typed stroke captures without an extra neutral frame");
                }
                hpvr::quest::FlipendoEvent charm_event;
                Expect(multi.attempt_count()==attempts+1&&multi.ConsumeEvent(&charm_event)&&charm_event.spell==spell,
                       "hold aim draw release succeeds on the first attempt for either new spell");
                stroke.cast_held=true;stroke.predicted_display_time_ns+=10'000'000;
                Expect(multi.Observe(stroke), "start another active typed stroke");
                Expect(multi.SelectSpell(GestureSpell::Flipendo), "external mid-stroke profile change cancels the stroke");
                const auto canceled_attempts=multi.attempt_count();
                stroke.predicted_display_time_ns+=10'000'000;
                Expect(multi.Observe(stroke)&&multi.attempt_count()==canceled_attempts&&!multi.ConsumeEvent(&charm_event),
                       "mid-stroke switching cannot cast or start again while the trigger remains held");
            }
            Expect(!multi.SelectSpell(static_cast<GestureSpell>(99)) && multi.IsLoaded(),
                   "invalid selection preserves current loaded profile");
            Expect(!multi.LoadProfiles(root / "missing-package-root", true) && multi.IsLoaded(),
                   "failed reload preserves complete previous profile set");
            Expect(multi.SelectSpell(GestureSpell::Flipendo), "switching back remains available");
            Expect(multi.LoadFlipendoProfile(root) && !multi.SelectSpell(GestureSpell::Alohomora),
                   "legacy Flipendo-only loader does not retain unrequested profiles");
            std::cout << "owned multi-spell profiles: rotated gameplay traces passed\n";
        }
        const std::uint32_t profile_status = hpvr_hp1_load_spell_profile_utf8(
            (root / "system" / "HPBase.u").string().c_str(),
            (root / "Maps" / "Lev_Tut1.unr").string().c_str(),
            "FlipPattern", "spellFlip", template_points.data(),
            static_cast<std::uint32_t>(template_points.size()),
            segments.data(), static_cast<std::uint32_t>(segments.size()),
            &profile);
        Expect(profile_status == HPVR_HP1_PROFILE_OK,
               "test must load the authored template through the public ABI");
        template_points.resize(profile.template_point_count);

        hpvr::quest::QuestGesture rotated;
        Expect(rotated.LoadFlipendoProfile(root),
               "rotated free-space recognizer must load the same profile");
        rotated.SetLessonDifficulty(true);
        rotated.SetLessonRound(3);
        Expect(rotated.effective_accuracy() == rotated.authored_accuracy() * 1.75F &&
                   rotated.threshold() == profile.pass_marks[0] &&
                   rotated.time_limit_seconds() == 0,
               "relaxed mode must preserve the previous demo assist at every tier");
        hpvr::quest::GestureSample arm{};
        arm.tracked = true;
        arm.predicted_display_time_ns = 2'000'000'000;
        arm.tip = {1.0F, 1.0F, 1.0F};
        arm.aim_direction = {1.0F, 0.0F, 0.0F};
        Expect(rotated.Observe(arm), "neutral rotated frame must arm capture");
        hpvr::quest::GestureGuide idle_guide{};
        Expect(rotated.BuildGuide(&idle_guide),
               "tracked neutral wand must evaluate guide visibility");
        Expect(!idle_guide.visible && idle_guide.template_points.empty(),
               "idle guide must remain hidden before trigger press");
        Expect(idle_guide.trail_points.empty(),
               "idle guide must not invent a trail");
        const auto anchor = template_points.front();
        const std::int64_t period =
            hpvr_hp1_lesson_resampling_period_ns(profile.draw_time_seconds);
        Expect(period > 0, "authored profile must produce a sampling period");
        constexpr float compressed_extent_m = 0.42F * 0.25F;
        bool observed_live_trail = false;
        for (std::size_t index = 0; index < template_points.size(); ++index) {
            hpvr::quest::GestureSample point = arm;
            point.predicted_display_time_ns +=
                period * static_cast<std::int64_t>(index + 1);
            point.cast_held = index + 1 != template_points.size();
            point.tip = {
                arm.tip[0],
                arm.tip[1] + (anchor.y - template_points[index].y) *
                                 compressed_extent_m,
                arm.tip[2] + (template_points[index].x - anchor.x) *
                                 compressed_extent_m};
            Expect(rotated.Observe(point),
                   "compressed rotated template sample must be accepted");
            if (index >= 1 && index + 1 < template_points.size()) {
                hpvr::quest::GestureGuide live_guide{};
                Expect(rotated.BuildGuide(&live_guide),
                       "recording frame must build a live guide");
                if (live_guide.trail_points.size() >= 2) {
                    Expect(live_guide.trail_points.back() == point.tip,
                           "live trail endpoint must equal the real wand tip");
                }
                observed_live_trail = observed_live_trail ||
                    (live_guide.visible &&
                     live_guide.trail_state ==
                         hpvr::quest::GestureVisualState::Recording &&
                     live_guide.trail_points.size() >= 2);
            }
        }
        Expect(observed_live_trail,
               "recording guide must expose the live wand trail");
        Expect(rotated.attempt_count() == 1,
               "rotated template must remain one attempt");
        Expect(rotated.accepted_count() == 1,
               "aim-facing scale-normalized template must pass");
        hpvr::quest::GestureGuide accepted_guide{};
        Expect(rotated.BuildGuide(&accepted_guide),
               "accepted frame must retain the result guide");
        Expect(accepted_guide.visible &&
                   accepted_guide.trail_state ==
                       hpvr::quest::GestureVisualState::Accepted &&
                   accepted_guide.trail_points.size() >= 2,
               "accepted guide must retain a visible green trail");
        hpvr::quest::FlipendoEvent event{};
        Expect(rotated.ConsumeEvent(&event),
               "accepted normalized gesture must emit one event");
        Expect(event.locked_direction[0] == 1.0F &&
                   event.locked_direction[1] == 0.0F &&
                   event.locked_direction[2] == 0.0F,
               "event must preserve the rotated press-time aim");

        // Trace the actual displayed guide at its physical size, sampling each
        // authored segment densely enough for the shipped coverage scorer. No
        // proprietary coordinates are baked into these regression tests.
        const auto trace = [&](hpvr::quest::QuestGesture& candidate,
                               const float extent_m, const float angle = 0.0F,
                               const bool backwards = false, const bool fast = false) {
            candidate.Reset();
            auto point = arm;
            Expect(candidate.Observe(point), "exact trace neutral must arm");
            point.cast_held = true;
            point.predicted_display_time_ns += period;
            Expect(candidate.Observe(point), "exact trace press must start");
            const auto first_point = backwards ? template_points.back() : anchor;
            const int partitions = fast ? 2 : 8;
            for (std::size_t index = 1; index < template_points.size(); ++index) {
                for (int part = 1; part <= partitions; ++part) {
                    const float blend = static_cast<float>(part) / static_cast<float>(partitions);
                    const auto& previous = template_points[backwards ? template_points.size()-index : index-1];
                    const auto& next = template_points[backwards ? template_points.size()-1-index : index];
                    const float x = previous.x + (next.x - previous.x) * blend;
                    const float y = previous.y + (next.y - previous.y) * blend;
                    const float dx = x-first_point.x, dy = y-first_point.y;
                    const float rotated_x = dx*std::cos(angle)-dy*std::sin(angle);
                    const float rotated_y = dx*std::sin(angle)+dy*std::cos(angle);
                    point.tip = {arm.tip[0], arm.tip[1]-rotated_y*extent_m,
                                 arm.tip[2]+rotated_x*extent_m};
                    point.predicted_display_time_ns += fast ? period/2 : period;
                    point.cast_held = index + 1 != template_points.size() || part != partitions;
                    Expect(candidate.Observe(point), "exact trace sample must score safely");
                }
            }
            return point.tip;
        };
        hpvr::quest::QuestGesture original;
        Expect(original.LoadFlipendoProfile(root), "original recognizer must load");
        for (std::uint32_t round = 0; round < 4; ++round) {
            original.SetLessonRound(round);
            Expect(original.threshold() == profile.pass_marks[round],
                   "every lesson round must use its own authored pass mark");
            trace(original, 0.42F);
            Expect(original.visual_state() == hpvr::quest::GestureVisualState::Accepted,
                   "tracing the visible physical guide must pass all four original tiers");
            Expect(original.ConsumeEvent(&event) &&
                       event.threshold == profile.pass_marks[round],
                   "original result event must carry the active tier mark");
        }
        trace(original, 0.42F * 0.25F);
        Expect(original.visual_state() == hpvr::quest::GestureVisualState::Rejected &&
                   !original.ConsumeEvent(&event),
               "original mode must not stretch a tiny drawing to fit the guide");
        original.SetLessonDifficulty(true);
        trace(original, 0.42F * 0.25F);
        Expect(original.visual_state() == hpvr::quest::GestureVisualState::Accepted,
               "the difficulty option must restore the same compressed-trace assist");
        const auto completed=original.diagnostics();
        Expect(completed.serial>0 && completed.attempt==original.attempt_count() &&
                   std::string_view(completed.reason)=="MATCHED" && completed.sample_count>32 &&
                   completed.projected_point_count==32 && completed.duration_seconds>0 &&
                   completed.projected_extent[0]>0 && completed.projected_extent[1]>0 &&
                   std::isfinite(completed.depth_span_meters) && completed.path_length>0 &&
                   completed.score>=completed.threshold,
               "completed diagnostics retain bounded projected shape and actual score");
        original.Reset();
        Expect(original.diagnostics().serial==completed.serial,
               "idle reset never repeats an old diagnostic event");

        hpvr::quest::QuestGesture cancellation;
        Expect(cancellation.LoadFlipendoProfile(root),"diagnostic recognizer loads");
        cancellation.SetLessonDifficulty(true);
        auto canceled_sample=arm;
        Expect(cancellation.Observe(canceled_sample),"neutral input arms diagnostics");
        canceled_sample.cast_held=true;canceled_sample.predicted_display_time_ns+=period;
        Expect(cancellation.Observe(canceled_sample),"diagnostic attempt begins");
        canceled_sample.predicted_display_time_ns+=100'000'001;
        Expect(cancellation.Observe(canceled_sample),"gap cancellation is safe");
        Expect(std::string_view(cancellation.diagnostics().reason)=="TIMING_GAP" &&
                   cancellation.diagnostics().serial==1 && cancellation.rejected_count()==0,
               "unscored gap cancellation is distinguishable from shape rejection");
        canceled_sample.cast_held=false;canceled_sample.predicted_display_time_ns+=period;
        Expect(cancellation.Observe(canceled_sample),"release rearms after gap");
        canceled_sample.cast_held=true;canceled_sample.predicted_display_time_ns+=period;
        Expect(cancellation.Observe(canceled_sample),"new attempt after cancellation starts");
        cancellation.Reset();
        Expect(cancellation.diagnostics().serial==2 &&
                   std::string_view(cancellation.diagnostics().reason)=="EXTERNAL_RESET",
               "external target/menu reset reports one cancellation rather than disappearing");

        unsigned owned_rotation_checks = 0;
        for (bool relaxed_mode : {false,true}) {
            original.SetLessonDifficulty(relaxed_mode);
            for (int degrees = 0; degrees < 360; degrees += 15) for (bool backwards : {false,true}) {
                const float angle = static_cast<float>(degrees)*std::numbers::pi_v<float>/180.0F;
                const auto last_tip = trace(original,relaxed_mode ? .105F : .42F,angle,backwards,true);
                Expect(original.visual_state() == hpvr::quest::GestureVisualState::Accepted &&
                           original.ConsumeEvent(&event),
                       "fast owned rune passes arbitrary in-plane angles and either direction");
                hpvr::quest::GestureGuide feedback;
                Expect(original.BuildGuide(&feedback) && !feedback.trail_points.empty(),
                       "rotated stroke retains visual feedback");
                const auto& end = feedback.trail_points.back();
                Expect(std::abs(end[0]-last_tip[0])<1.0e-5F && std::abs(end[1]-last_tip[1])<1.0e-5F &&
                           std::abs(end[2]-last_tip[2])<1.0e-5F,
                       "recognition alignment does not rotate or rescale the real visible trail");
                ++owned_rotation_checks;
            }
        }
        std::cout << "owned Flipendo: " << owned_rotation_checks << " fast rotation/direction cases\n";
        std::vector<Point> owned_pattern, owned_line, owned_circle, owned_scribble;
        for (const auto& vertex : template_points) owned_pattern.push_back({vertex.x,vertex.y});
        TestGameplayStructure(owned_pattern);
        ReplayHeadsetTraces(owned_pattern,profile.accuracy_radius*1.75F);
        hpvr::quest::QuestGesture gameplay;
        Expect(gameplay.LoadFlipendoProfile(root),"gameplay recognizer loads the owned spell identity");
        gameplay.SetLessonDifficulty(false);
        gameplay.SetLessonRound(3);
        gameplay.SetGameplayMode(true);
        Expect(!gameplay.relaxed_difficulty()&&gameplay.threshold()==profile.pass_marks[0]&&
                   gameplay.time_limit_seconds()==0,
               "gameplay policy is independent of the selected lesson difficulty and round");
        auto gameplay_point=arm;
        gameplay_point.aim_direction={0,0,-1};
        Expect(gameplay.Observe(gameplay_point),"fresh gameplay context arms on release");
        gameplay_point.cast_held=true;gameplay_point.predicted_display_time_ns+=10'000'000;
        Expect(gameplay.Observe(gameplay_point),"gameplay press locks target and begins capture");
        const auto coil=Transform(MakeCoil(1.1F),1.123F,.4F,true);
        for(std::size_t i=1;i<coil.size();++i){
            gameplay_point.tip={arm.tip[0]+(coil[i][0]-coil.front()[0])*.42F,
                arm.tip[1]-(coil[i][1]-coil.front()[1])*.42F,arm.tip[2]};
            gameplay_point.predicted_display_time_ns+=8'000'000;
            gameplay_point.cast_held=i+1<coil.size();
            Expect(gameplay.Observe(gameplay_point),"fast natural gameplay stroke captures without canceling");
        }
        Expect(gameplay.ConsumeEvent(&event)&&event.locked_origin==arm.tip&&
                   event.locked_direction==std::array<float,3>{0,0,-1}&&
                   !gameplay.ConsumeEvent(&event),
               "a fast gameplay coil casts exactly once at the press-time target");
        gameplay.SetGameplayMode(false);
        Expect(!gameplay.relaxed_difficulty()&&gameplay.lesson_round()==3&&
                   gameplay.effective_accuracy()==profile.accuracy_radius&&
                   gameplay.threshold()==profile.pass_marks[3]&&
                   gameplay.time_limit_seconds()==profile.draw_time_seconds,
               "returning to class restores exact strict lesson size accuracy tier and deadline");
        gameplay.SetLessonDifficulty(true);gameplay.SetGameplayMode(true);gameplay.Reset();gameplay.SetGameplayMode(false);
        Expect(gameplay.relaxed_difficulty()&&gameplay.threshold()==profile.pass_marks[0]&&
                   gameplay.effective_accuracy()==profile.accuracy_radius*1.75F&&
                   gameplay.time_limit_seconds()==0,
               "gameplay transitions also preserve the separately selected relaxed lesson policy");
        TestNaturalStrokeVariants(owned_pattern,profile.accuracy_radius*1.75F);
        std::uint32_t seed = 0x5177219;
        for (int i = 0; i <= 128; ++i) {
            const float t = static_cast<float>(i)/128.0F;
            owned_line.push_back({t,t});
            owned_circle.push_back({.5F+.4F*std::cos(t*2*std::numbers::pi_v<float>),
                                    .5F+.4F*std::sin(t*2*std::numbers::pi_v<float>)});
            seed=seed*1664525U+1013904223U;
            const float x=static_cast<float>(seed>>16)/65535.0F;
            seed=seed*1664525U+1013904223U;
            owned_scribble.push_back({x,static_cast<float>(seed>>16)/65535.0F});
        }
        for (const auto* negative : {&owned_line,&owned_circle,&owned_scribble}) {
            for (int degrees=0; degrees<360; degrees+=15) {
                const auto input=Transform(*negative,static_cast<float>(degrees)*std::numbers::pi_v<float>/180.0F);
                const auto result=hpvr::quest::CompareGestureShape(input,owned_pattern,profile.accuracy_radius*1.75F,true);
                Expect(!result.valid || result.score<profile.pass_marks[0],
                       "even relaxed owned Flipendo rejects rotated line circle and scribble shapes");
            }
        }

        hpvr::quest::QuestGesture deadline;
        Expect(deadline.LoadFlipendoProfile(root), "deadline recognizer must load");
        auto point = arm;
        Expect(deadline.Observe(point), "deadline neutral must arm");
        point.cast_held = true;
        point.predicted_display_time_ns += 10'000'000;
        Expect(deadline.Observe(point), "deadline press must start");
        for (int index = 0; index < 119; ++index) {
            point.predicted_display_time_ns += 100'000'000;
            Expect(deadline.Observe(point), "held deadline sample must remain valid");
        }
        Expect(deadline.visual_state() == hpvr::quest::GestureVisualState::Recording,
               "original attempt must remain open before the authored 12-second limit");
        point.predicted_display_time_ns += 100'000'000;
        Expect(deadline.Observe(point), "deadline frame must finalize without release");
        Expect(deadline.visual_state() == hpvr::quest::GestureVisualState::Rejected &&
                   deadline.rejected_count() == 1,
               "the authored deadline must judge an incomplete held stroke");
        point.predicted_display_time_ns += 100'000'000;
        Expect(deadline.Observe(point) && deadline.attempt_count() == 1,
               "holding after timeout must not immediately restart drawing");
        point.cast_held = false;
        point.predicted_display_time_ns += 100'000'000;
        Expect(deadline.Observe(point), "release after timeout must rearm");
        point.cast_held = true;
        point.predicted_display_time_ns += 100'000'000;
        Expect(deadline.Observe(point) && deadline.attempt_count() == 2,
               "new press after timeout must allow a new attempt");
        deadline.SetLessonRound(1);
        Expect(deadline.visual_state() == hpvr::quest::GestureVisualState::Idle &&
                   !deadline.ConsumeEvent(&event),
               "changing the round must cancel a stroke rather than rescore it mid-draw");
        deadline.SetLessonDifficulty(true);
        deadline.Reset();
        Expect(deadline.relaxed_difficulty() && deadline.lesson_round() == 1,
               "normal resets must preserve the chosen difficulty and lesson tier");
    }
    std::cout << "quest gesture tests passed\n";
    return EXIT_SUCCESS;
}
