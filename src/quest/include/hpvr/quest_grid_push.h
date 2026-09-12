#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>

namespace hpvr::quest::grid_push {
using Position = std::array<float, 3>;
using PendingHits = std::map<std::int32_t, Position>;

// Original GridMover chooses its cardinal movement from Other.Location. For a
// spell that is the projectile's impact, not the player's head/body position.
// These origins are transient: consume before a mover's busy-state early exit,
// then clear remaining entries after the immediate event-reduction batch.
inline void RememberImpact(PendingHits& pending, std::int32_t actor, const Position& impact) {
    if (actor > 0 && std::ranges::all_of(impact, [](float v) { return std::isfinite(v); }))
        pending.insert_or_assign(actor, impact);
}

inline Position ConsumeOrigin(PendingHits& pending, std::int32_t actor,
                              const Position& physical_pusher) {
    const auto found = pending.find(actor);
    if (found == pending.end()) return physical_pusher;
    const auto origin = found->second;
    pending.erase(found);
    return origin;
}
} // namespace hpvr::quest::grid_push
