#include "hpvr/quest_gesture.h"
#include "hpvr/hp1_gesture_c.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "quest gesture test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}  // namespace

int main() {
    hpvr::quest::QuestGesture gesture;
    Expect(!gesture.IsLoaded(), "fresh gesture must not claim a profile");
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
                               const float extent_m) {
            candidate.Reset();
            auto point = arm;
            Expect(candidate.Observe(point), "exact trace neutral must arm");
            point.cast_held = true;
            point.predicted_display_time_ns += period;
            Expect(candidate.Observe(point), "exact trace press must start");
            for (std::size_t index = 1; index < template_points.size(); ++index) {
                for (int part = 1; part <= 8; ++part) {
                    const float blend = static_cast<float>(part) / 8.0F;
                    const auto& previous = template_points[index - 1];
                    const auto& next = template_points[index];
                    const float x = previous.x + (next.x - previous.x) * blend;
                    const float y = previous.y + (next.y - previous.y) * blend;
                    point.tip = {arm.tip[0], arm.tip[1] + (anchor.y - y) * extent_m,
                                 arm.tip[2] + (x - anchor.x) * extent_m};
                    point.predicted_display_time_ns += period;
                    point.cast_held = index + 1 != template_points.size() || part != 8;
                    Expect(candidate.Observe(point), "exact trace sample must score safely");
                }
            }
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
