#pragma once

#include "hpvr/hp1_gesture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace hpvr::quest::zones {

class Query {
public:
    bool Load(const wand::Hp1BspTopology& topology, const wand::Hp1ActorVisualCensus& census) {
        if (topology.status != wand::Hp1ProfileStatus::ok || census.status != wand::Hp1ProfileStatus::ok ||
            topology.nodes.empty() || topology.zone_count == 0 || topology.zone_count > 64 ||
            topology.zone_actor_references.size() != topology.zone_count) return false;
        Query query;
        for (std::size_t zone = 0; zone < topology.zone_count; ++zone) {
            const auto reference = topology.zone_actor_references[zone];
            const auto actor = std::ranges::find_if(census.actors,
                [reference](const auto& item) { return item.actor_reference == reference; });
            if (actor == census.actors.end()) continue;
            for (const auto& property : actor->serialized_properties)
                if (EqualName(property.name, "bKillZone") && property.boolean_value_serialized && property.boolean_value)
                    query.lethal_[zone] = true;
        }
        query.zone_count_ = static_cast<std::uint8_t>(topology.zone_count);
        query.nodes_.reserve(topology.nodes.size());
        for (const auto& node : topology.nodes) {
            if (node.front_node_index < -1 || node.back_node_index < -1 ||
                node.front_node_index >= static_cast<std::int64_t>(topology.nodes.size()) ||
                node.back_node_index >= static_cast<std::int64_t>(topology.nodes.size()) ||
                node.zone_indices[0] >= topology.zone_count || node.zone_indices[1] >= topology.zone_count ||
                std::ranges::any_of(node.plane, [](float value) { return !std::isfinite(value); })) return false;
            query.nodes_.push_back({node.plane, node.front_node_index, node.back_node_index, node.zone_indices});
        }
        *this = std::move(query);
        return true;
    }

    // Input is the original package's Unreal XYZ, not converted scene metres.
    [[nodiscard]] std::optional<std::uint8_t> ZoneAt(const std::array<float, 3>& point_unreal) const {
        if (nodes_.empty() || std::ranges::any_of(point_unreal, [](float v) { return !std::isfinite(v); }))
            return std::nullopt;
        std::int32_t index = 0;
        for (std::size_t steps = 0; steps <= nodes_.size() && steps < 4096; ++steps) {
            if (index < 0 || static_cast<std::size_t>(index) >= nodes_.size()) return std::nullopt;
            const auto& node = nodes_[static_cast<std::size_t>(index)];
            const double side = double(point_unreal[0]) * node.plane[0] +
                double(point_unreal[1]) * node.plane[1] + double(point_unreal[2]) * node.plane[2] - node.plane[3];
            const bool front = side >= 0;
            const auto next = front ? node.front : node.back;
            if (next == -1) {
                const auto zone = node.zones[front ? 1U : 0U];
                return zone < zone_count_ ? std::optional<std::uint8_t>(zone) : std::nullopt;
            }
            index = next;
        }
        return std::nullopt;
    }
    [[nodiscard]] bool IsLethal(const std::array<float, 3>& point_unreal) const {
        const auto zone = ZoneAt(point_unreal);
        return zone.has_value() && lethal_[*zone];
    }
    [[nodiscard]] std::size_t lethal_count() const {
        return static_cast<std::size_t>(std::ranges::count(lethal_, true));
    }

private:
    struct Node {
        std::array<float, 4> plane{};
        std::int32_t front = -1, back = -1;
        std::array<std::uint8_t, 2> zones{};
    };
    static bool EqualName(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const char c = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] + ('a' - 'A')) : a[i];
            const char d = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] + ('a' - 'A')) : b[i];
            if (c != d) return false;
        }
        return true;
    }
    std::vector<Node> nodes_;
    std::array<bool, 64> lethal_{};
    std::uint8_t zone_count_ = 0;
};

}  // namespace hpvr::quest::zones
