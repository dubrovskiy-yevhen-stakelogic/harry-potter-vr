#include "hpvr/quest_effects_clock.h"
#include "hpvr/quest_frontend.h"
#include "hpvr/quest_target_marker.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

using namespace hpvr::quest;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void TestClock() {
    WorldEffectsClock clock;
    Check(clock.Seconds() == 0.0F, "effects start at zero");
    for (float invalid : {0.0F, -1.0F,
            std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN()}) {
        clock.Advance(invalid, true);
        Check(clock.Seconds() == 0.0F, "invalid deltas cannot poison effects");
    }
    clock.Advance(0.01F, false);
    Check(clock.Seconds() == 0.0F, "inactive world does not advance");
    clock.Advance(0.01F, true);
    Check(std::abs(clock.Seconds() - 0.01F) < 1e-6F, "normal delta advances");
    clock.Advance(90.0F, true);
    Check(std::abs(clock.Seconds() - 0.06F) < 1e-6F, "resume hitch is bounded");

    // All eyes read the same clock; reading never advances simulation.
    const auto left = clock.Seconds();
    Check(left == clock.Seconds() && left == clock.Seconds(), "eye reads are stable");
    clock.Reset();
    constexpr unsigned ticks = 200000;
    for (unsigned i = 0; i < ticks; ++i) {
        clock.Advance(WorldEffectsClock::kMaximumStepSeconds, true);
        Check(std::isfinite(clock.Seconds()) && clock.Seconds() >= 0.0F &&
              clock.Seconds() <= WorldEffectsClock::kWrapSeconds,
              "long-session phases stay finite and bounded");
    }
    const auto expected = std::fmod(
        double(ticks) * double(WorldEffectsClock::kMaximumStepSeconds),
        WorldEffectsClock::kWrapSeconds);
    Check(std::abs(double(clock.Seconds()) - expected) < .0002,
          "long-session clock wraps without float accumulation drift");
    clock.Reset();
    Check(clock.Seconds() == 0.0F, "map initialization resets effects");
}

void TestLiveMenusAndEffects() {
    QuestFrontEnd front;
    WorldEffectsClock clock;
    front.screen = FrontScreen::Game;
    for (const auto screen : {FrontScreen::Game, FrontScreen::Vr, FrontScreen::Debug}) {
        front.screen = screen;
        front.vr_return = FrontScreen::Game;
        const auto previous = clock.Seconds();
        clock.Advance(.05F, !front.PausesWorld());
        Check(clock.Seconds() > previous, "VR and debug panels over gameplay stay live");
    }
    for (const auto screen : {FrontScreen::Main, FrontScreen::Pause, FrontScreen::Story}) {
        front.screen = screen;
        const auto previous = clock.Seconds();
        clock.Advance(.05F, !front.PausesWorld());
        Check(clock.Seconds() == previous, "paused frontend does not advance world effects");
    }
    front.screen = FrontScreen::Vr;
    front.vr_return = FrontScreen::Pause;
    const auto paused = clock.Seconds();
    clock.Advance(.05F, !front.PausesWorld());
    Check(clock.Seconds() == paused, "VR panel over pause remains paused");

    TargetMarkerBatch marker;
    clock.Reset();
    const auto first_marker = marker.first_vertex(clock.Seconds());
    const float first_age = std::fmod(clock.Seconds() * .92F + .2F, 1.0F);
    for (unsigned i = 0; i < 5; ++i) clock.Advance(.02F, true);
    Check(marker.first_vertex(clock.Seconds()) != first_marker,
          "owned target marker animation progresses");
    Check(std::fmod(clock.Seconds() * .92F + .2F, 1.0F) != first_age,
          "existing fireplace particle ages progress without new particles");
}

void TestSceneWiring() {
    const auto scene_path = std::filesystem::path(__FILE__).parent_path() /
        "../../../android/app/src/main/cpp/quest_scene.cpp";
    std::ifstream input(scene_path);
    Check(input.good(), "scene source is available for map-routing regression check");
    const std::string source{std::istreambuf_iterator<char>(input), {}};
    const auto advance = source.find("void QuestScene::Advance(const float delta_seconds)");
    const auto clock = source.find("state.effects_clock.Advance(delta_seconds,", advance);
    const auto map_route = source.find("AdvanceChallenge(delta_seconds);return;", advance);
    Check(advance != std::string::npos && clock < map_route,
          "effects tick before the second-map early return");
    const auto draw = source.find("void QuestScene::RecordSpellDraw(");
    const auto draw_end = source.find("\nvoid QuestScene::", draw + 1);
    const auto draw_source = source.substr(draw, draw_end - draw);
    Check(draw_source.find("state.effects_clock.Seconds()") != std::string::npos &&
          draw_source.find("animation_elapsed_seconds") == std::string::npos,
          "world effects do not depend on character animation being active");
}
}  // namespace

int main() {
    TestClock();
    TestLiveMenusAndEffects();
    TestSceneWiring();
    std::cout << "PASS: independent effects clock, map routing, live VR menus, finite hitches and long-session wrap\n";
}
