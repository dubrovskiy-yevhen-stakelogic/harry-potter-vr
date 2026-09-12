#pragma once

#include "hpvr/quest_maps.h"

namespace hpvr::quest {

enum class SpellInputPath { Basic, Gesture, Voice };

constexpr bool MapAllowsSpellInput(unsigned map_id, SpellInputPath path) {
    if (map_id == kFlipendoChallengeMapId) return true;
    return map_id == kIntroductionMapId && path != SpellInputPath::Voice;
}

constexpr bool BasicSpellGameplayAllowed(unsigned map_id, unsigned quest_stage,
                                         bool challenge_complete) {
    if (!MapAllowsSpellInput(map_id, SpellInputPath::Basic)) return false;
    if (map_id == kFlipendoChallengeMapId) return !challenge_complete;
    return quest_stage < 20 || quest_stage == 23;
}

} // namespace hpvr::quest
