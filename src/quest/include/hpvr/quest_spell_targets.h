#pragma once

#include "hpvr/quest_gesture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hpvr::quest {

struct SpellTargetDescriptor {
    std::int32_t actor_reference = 0;
    std::array<float, 3> bounds_min{};
    std::array<float, 3> bounds_max{};
    bool enabled = true;
};

struct SpellTargetResult {
    std::uint64_t serial = 0;
    std::size_t target_index = 0;
    std::int32_t actor_reference = 0;
    float distance_m = 0.0F;
    bool hit = false;
};

class QuestSpellTargets final {
public:
    [[nodiscard]] bool SetTargets(
        const std::vector<SpellTargetDescriptor>& targets);
    void Reset();
    void Advance(float delta_seconds);
    [[nodiscard]] bool Consume(const FlipendoEvent& event,
                               SpellTargetResult* output);
    [[nodiscard]] bool ApplyHit(
        const SpellTargetResult& result,
        const std::array<float, 3>& direction);
    [[nodiscard]] bool SetSceneOffset(
        std::size_t target_index,
        const std::array<float, 3>& offset);

    [[nodiscard]] std::array<float, 3> ReactionOffset(
        std::size_t target_index) const;
    [[nodiscard]] std::size_t TargetCount() const;
    [[nodiscard]] std::uint32_t HitCount() const;
    [[nodiscard]] std::uint32_t MissCount() const;

private:
    struct Reaction {
        float elapsed_seconds = 0.0F;
        std::array<float, 3> direction{0.0F, 0.0F, -1.0F};
        bool active = false;
    };

    std::vector<SpellTargetDescriptor> targets_;
    std::vector<Reaction> reactions_;
    std::vector<std::array<float, 3>> scene_offsets_;
    std::uint64_t last_serial_ = 0;
    std::uint64_t last_applied_serial_ = 0;
    std::uint32_t hit_count_ = 0;
    std::uint32_t miss_count_ = 0;
};

}  // namespace hpvr::quest
