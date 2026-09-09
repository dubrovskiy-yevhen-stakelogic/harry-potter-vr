#include "hpvr/quest_spell_targets.h"

#include <cassert>
#include <cmath>
#include <vector>

using hpvr::quest::FlipendoEvent;
using hpvr::quest::QuestSpellTargets;
using hpvr::quest::SpellTargetDescriptor;
using hpvr::quest::SpellTargetResult;

namespace {

FlipendoEvent AcceptedEvent(const std::uint64_t serial) {
    FlipendoEvent event{};
    event.serial = serial;
    event.locked_direction = {0.0F, 0.0F, -1.0F};
    event.score = 0.8F;
    event.threshold = 0.5F;
    return event;
}

void SelectsNearestTargetUsingPressLockedRay() {
    QuestSpellTargets targets;
    assert(targets.SetTargets({
        {10, {-0.5F, -1.0F, -5.0F}, {0.5F, 1.0F, -4.0F}},
        {20, {-0.5F, -1.0F, -9.0F}, {0.5F, 1.0F, -8.0F}},
    }));
    SpellTargetResult result{};
    const auto event = AcceptedEvent(1);
    assert(targets.Consume(event, &result));
    assert(result.hit);
    assert(result.actor_reference == 10);
    assert(std::abs(result.distance_m - 4.0F) < 1.0e-5F);
    assert(targets.HitCount() == 0);
}

void RejectsDuplicateAndCountsMiss() {
    QuestSpellTargets targets;
    assert(targets.SetTargets({
        {10, {2.0F, -1.0F, -5.0F}, {3.0F, 1.0F, -4.0F}},
    }));
    SpellTargetResult result{};
    const auto event = AcceptedEvent(7);
    assert(targets.Consume(event, &result));
    assert(!result.hit);
    assert(targets.MissCount() == 1);
    assert(!targets.Consume(event, &result));
    assert(targets.MissCount() == 1);
}

void ReactionMovesAndReturnsHome() {
    QuestSpellTargets targets;
    assert(targets.SetTargets({
        {10, {-0.5F, -1.0F, -5.0F}, {0.5F, 1.0F, -4.0F}},
    }));
    SpellTargetResult result{};
    assert(targets.Consume(AcceptedEvent(1), &result));
    assert(result.hit);
    assert(targets.ReactionOffset(0) ==
           (std::array<float, 3>{0.0F, 0.0F, 0.0F}));
    assert(targets.ApplyHit(result, {0.0F, 0.0F, -1.0F}));
    assert(!targets.ApplyHit(result, {0.0F, 0.0F, -1.0F}));
    assert(targets.HitCount() == 1);
    targets.Advance(0.60F);
    const auto middle = targets.ReactionOffset(0);
    assert(middle[1] > 0.20F);
    assert(middle[2] < -0.60F);
    targets.Advance(0.60F);
    const auto end = targets.ReactionOffset(0);
    assert(std::abs(end[0]) < 1.0e-6F);
    assert(std::abs(end[1]) < 1.0e-6F);
    assert(std::abs(end[2]) < 1.0e-6F);
}

void SceneOffsetMovesTargetAndHitboxTogether() {
    QuestSpellTargets targets;
    assert(targets.SetTargets({
        {10, {2.0F, -1.0F, -5.0F}, {3.0F, 1.0F, -4.0F}},
    }));

    SpellTargetResult result{};
    assert(targets.Consume(AcceptedEvent(1), &result));
    assert(!result.hit);
    assert(targets.SetSceneOffset(0, {-2.5F, 0.0F, 0.0F}));
    assert(!targets.SetSceneOffset(1, {0.0F, 0.0F, 0.0F}));
    assert(targets.Consume(AcceptedEvent(2), &result));
    assert(result.hit);
    assert(result.actor_reference == 10);
    assert(std::abs(result.distance_m - 4.0F) < 1.0e-5F);
}

}  // namespace

int main() {
    {
        QuestSpellTargets targets;
        assert(targets.SetTargets({
            {10, {-0.5F,-1.0F,-3.0F}, {0.5F,1.0F,-2.0F}, false},
            {20, {-0.5F,-1.0F,-5.0F}, {0.5F,1.0F,-4.0F}, true},
        }));
        SpellTargetResult result{};
        assert(targets.Consume(AcceptedEvent(1), &result));
        assert(result.hit && result.actor_reference==20);
    }
    SelectsNearestTargetUsingPressLockedRay();
    RejectsDuplicateAndCountsMiss();
    ReactionMovesAndReturnsHome();
    SceneOffsetMovesTargetAndHitboxTogether();
    return 0;
}
