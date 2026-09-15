#include "hpvr/quest_basic_cast.h"
#include "hpvr/quest_cast_permissions.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace hpvr::quest;

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void TestMapPermissions() {
    for (const auto path : {SpellInputPath::Basic, SpellInputPath::Gesture,
                            SpellInputPath::Voice}) {
        Check(MapAllowsSpellInput(kFlipendoChallengeMapId, path),
              "Flipendo challenge keeps every casting input path");
        Check(!MapAllowsSpellInput(kBroomstickTrainingMapId, path),
              "broom lesson blocks basic, gesture and voice casting");
        Check(MapAllowsSpellInput(kCharmsTrainingMapId,path),"charms permits walking spell paths");
        for (const unsigned map : {4U, 999U, std::numeric_limits<unsigned>::max()}) {
            Check(!MapAllowsSpellInput(map, path), "unknown map cannot inherit wand input");
        }
    }
    Check(MapAllowsSpellInput(kIntroductionMapId, SpellInputPath::Basic),
          "introduction keeps spellnone aiming");
    Check(MapAllowsSpellInput(kIntroductionMapId, SpellInputPath::Gesture),
          "introduction keeps lesson gestures");
    Check(!MapAllowsSpellInput(kIntroductionMapId, SpellInputPath::Voice),
          "introduction voice support is unchanged");
}

void TestGameplayParity() {
    for (unsigned stage = 0; stage <= 64; ++stage) {
        for (const bool complete : {false, true}) {
            Check(BasicSpellGameplayAllowed(0, stage, complete) == (stage < 20 || stage == 23),
                  "introduction basic input retains its existing stage gates");
            Check(BasicSpellGameplayAllowed(1, stage, complete) == !complete,
                  "challenge basic input retains its completion gate");
            Check(!BasicSpellGameplayAllowed(2, stage, complete),
                  "broom stage cannot activate the non-challenge fallback");
        }
    }
}

void TestTriggerSequences() {
    const std::array<float, 3> tip{0, 1, 0}, hit{0, 1, -3};
    for (const unsigned map : {0U, 1U, 2U}) {
        BasicCast cast;
        const bool active = BasicSpellGameplayAllowed(map, 0, false);
        Check(!cast.Observe(active, false, tip, hit, .01F), "release only arms input");
        Check(!cast.Observe(active, true, tip, hit, .01F), "held input does not fire");
        Check(cast.Observe(active, false, tip, hit, .01F) == (map != 2),
              "only walking levels emit a basic spell on release");
        if (map == 2) {
            Check(!cast.charging && !cast.flying && cast.require_release,
                  "broom input never starts charge or flight");
        }
    }

    BasicCast stale;
    stale.charging = true;
    stale.flying = true;
    stale.require_release = false;
    const bool active = BasicSpellGameplayAllowed(2, 23, false);
    Check(!stale.Observe(active, false, tip, hit, .01F),
          "queued trigger release cannot emit a spell in the broom lesson");
    Check(!stale.charging && !stale.flying && stale.require_release,
          "disabling input cancels stale charge and projectile state");
    for (unsigned frame = 0; frame < 120; ++frame) {
        Check(!stale.Observe(active, frame % 3 != 0, tip, hit, .01F),
              "repeated broom trigger presses remain disabled");
    }
}
} // namespace

int main() {
    try {
        TestMapPermissions();
        TestGameplayParity();
        TestTriggerSequences();
        std::cout << "quest cast permissions tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
