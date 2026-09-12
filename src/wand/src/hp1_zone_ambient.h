#pragma once

#include "hpvr/hp1_gesture.h"
#include "hpvr/hp1_abyss_lighting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace hpvr::wand::detail {

// The accepted tutorial lighting remains unchanged. Lev_Tut1b has two authored
// kill zones with zero AmbientBrightness: their abyss bottoms must not acquire
// our general-purpose warm ambient fallback. A surface/lightmap may straddle a
// safe zone, so classify its texels on the visible side rather than the entire
// surface. This policy only affects CPU lightmap preparation.
class DarkZoneAmbient {
public:
    DarkZoneAmbient(const Hp1BspTopology& topology,
                    const Hp1ActorVisualCensus& actors, bool enabled)
        : topology_(topology), abyss_(topology, actors, enabled) {
        if (!enabled || topology.status != Hp1ProfileStatus::ok ||
            actors.status != Hp1ProfileStatus::ok || topology.nodes.empty() ||
            topology.zone_count == 0 || topology.zone_count > dark_.size() ||
            topology.zone_actor_references.size() != topology.zone_count) return;
        for (std::size_t zone = 0; zone < topology.zone_count; ++zone) {
            const auto reference = topology.zone_actor_references[zone];
            const auto actor = std::ranges::find_if(actors.actors,
                [reference](const auto& item) { return item.actor_reference == reference; });
            if (actor == actors.actors.end() ||
                !EqualName(actor->qualified_class_name, "Engine.ZoneInfo")) continue;
            bool kill = false;
            bool zero_ambient = true;
            for (const auto& property : actor->serialized_properties) {
                if (EqualName(property.name, "bKillZone") && property.boolean_value_serialized)
                    kill = property.boolean_value;
                if (EqualName(property.name, "AmbientBrightness"))
                    zero_ambient = property.kind == 1 && property.value.size() == 1 && property.value[0] == 0;
            }
            dark_[zone] = kill && zero_ambient;
        }
        if (std::ranges::none_of(dark_, [](bool value) { return value; })) return;
        candidates_.assign(topology.surfaces.size(), false);
        for (const auto& node : topology.nodes) {
            if (node.vertex_count < 3 || node.surface_index < 0 ||
                static_cast<std::size_t>(node.surface_index) >= candidates_.size()) continue;
            const auto zone = node.zone_indices[1];
            if (zone < topology.zone_count && dark_[zone])
                candidates_[static_cast<std::size_t>(node.surface_index)] = true;
        }
    }

    [[nodiscard]] bool Candidate(std::size_t surface) const noexcept {
        return (surface < candidates_.size() && candidates_[surface]) || abyss_.Candidate(surface);
    }

    [[nodiscard]] float FullLightVisibility(std::size_t surface, Hp1BspVector position,
                                            Hp1BspVector normal) const noexcept {
        return abyss_.Candidate(surface) ? abyss_.Visibility(position, normal) : 1.0F;
    }

    [[nodiscard]] bool Suppress(std::size_t surface, Hp1BspVector position,
                                Hp1BspVector visible_normal) const noexcept {
        // Keep the historical ambient-only eligibility unchanged so old cache
        // validation is independent of the new full-light volume candidates.
        if (surface >= candidates_.size() || !candidates_[surface]) return false;
        const std::array<float, 3> point{
            position.x + visible_normal.x * 0.25F,
            position.y + visible_normal.y * 0.25F,
            position.z + visible_normal.z * 0.25F};
        if (std::ranges::any_of(point, [](float value) { return !std::isfinite(value); })) return false;
        std::int32_t index = 0;
        for (std::size_t step = 0; step <= topology_.nodes.size() && step < 4096; ++step) {
            if (index < 0 || static_cast<std::size_t>(index) >= topology_.nodes.size()) return false;
            const auto& node = topology_.nodes[static_cast<std::size_t>(index)];
            const double side = double(point[0]) * node.plane[0] + double(point[1]) * node.plane[1] +
                                double(point[2]) * node.plane[2] - node.plane[3];
            if (!std::isfinite(side)) return false;
            const bool front = side >= 0;
            const auto next = front ? node.front_node_index : node.back_node_index;
            if (next == -1) {
                const auto zone = node.zone_indices[front ? 1U : 0U];
                return zone < topology_.zone_count && dark_[zone];
            }
            index = next;
        }
        return false;
    }

private:
    static bool EqualName(std::string_view left, std::string_view right) noexcept {
        if (left.size() != right.size()) return false;
        for (std::size_t i = 0; i < left.size(); ++i) {
            const auto fold = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; };
            if (fold(left[i]) != fold(right[i])) return false;
        }
        return true;
    }
    const Hp1BspTopology& topology_;
    Hp1ChallengeAbyssLighting abyss_;
    std::array<bool, 64> dark_{};
    std::vector<bool> candidates_;
};

}  // namespace hpvr::wand::detail
