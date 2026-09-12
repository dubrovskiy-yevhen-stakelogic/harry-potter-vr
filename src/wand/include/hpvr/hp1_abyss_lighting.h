#pragma once

#include "hpvr/hp1_gesture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace hpvr::wand {

// Visual approximation for Lev_Tut1b's long moving-column abyss. Its authored
// horizontal zone-3 portals define the footprint (including safe islands), not
// a room-sized black plane. Geometry lighting fades across 64UU above the
// portal and is fully extinguished below it. Emissive particles are separate.
// This is a load-time policy, not a reconstruction of native engine fog code.
class Hp1ChallengeAbyssLighting {
public:
    Hp1ChallengeAbyssLighting(const Hp1BspTopology& topology,
                             const Hp1ActorVisualCensus& actors, bool enabled) {
        if (!enabled || topology.status != Hp1ProfileStatus::ok ||
            actors.status != Hp1ProfileStatus::ok || topology.zone_count <= 3 ||
            topology.zone_actor_references.size() != topology.zone_count) return;
        const auto zone = std::ranges::find_if(actors.actors, [&](const auto& actor) {
            return actor.actor_reference == topology.zone_actor_references[3];
        });
        if (zone == actors.actors.end()) return;
        bool kill = false;
        for (const auto& property : zone->serialized_properties)
            if (property.name == "bKillZone" && property.boolean_value_serialized)
                kill = property.boolean_value;
        if (!kill) return;

        for (const auto& node : topology.nodes) {
            if (node.zone_indices[0] == 0 || node.zone_indices[1] == 0 ||
                node.zone_indices[0] == node.zone_indices[1] ||
                (node.zone_indices[0] != 3 && node.zone_indices[1] != 3) ||
                node.vertex_count < 3 || std::abs(node.plane[2]) < 0.999F ||
                std::abs(node.plane[0]) > 0.001F || std::abs(node.plane[1]) > 0.001F)
                continue;
            if (node.surface_index < 0 || std::size_t(node.surface_index) >= topology.surfaces.size() ||
                (topology.surfaces[std::size_t(node.surface_index)].polygon_flags & 1U) == 0) continue;
            auto polygon = Points(topology, node);
            if (polygon.size() != node.vertex_count) continue;
            const float height = polygon.front().z;
            if (!std::isfinite(height) || std::ranges::any_of(polygon, [&](const auto& p) {
                    return !std::isfinite(p.x) || !std::isfinite(p.y) ||
                           !std::isfinite(p.z) || std::abs(p.z - height) > 0.25F;
                })) continue;
            if (polygons_.empty()) portal_height_ = height;
            // Disjoint-height portals need another volume; do not broaden this
            // narrowly scoped policy to unrelated floors.
            if (std::abs(height - portal_height_) > 0.25F) continue;
            Polygon footprint;
            for (const auto& p : polygon) {
                footprint.points.push_back({p.x, p.y});
                footprint.minimum[0] = std::min(footprint.minimum[0], p.x);
                footprint.minimum[1] = std::min(footprint.minimum[1], p.y);
                footprint.maximum[0] = std::max(footprint.maximum[0], p.x);
                footprint.maximum[1] = std::max(footprint.maximum[1], p.y);
            }
            polygons_.push_back(std::move(footprint));
        }
        if (polygons_.empty()) return;
        candidates_.assign(topology.surfaces.size(), false);
        for (const auto& node : topology.nodes) {
            if (node.surface_index < 0 || std::size_t(node.surface_index) >= candidates_.size())
                continue;
            const auto points = Points(topology, node);
            if (points.size() < 3) continue;
            std::array<float, 3> lo{INFINITY, INFINITY, INFINITY};
            std::array<float, 3> hi{-INFINITY, -INFINITY, -INFINITY};
            for (const auto& p : points) {
                lo = {std::min(lo[0], p.x), std::min(lo[1], p.y), std::min(lo[2], p.z)};
                hi = {std::max(hi[0], p.x), std::max(hi[1], p.y), std::max(hi[2], p.z)};
            }
            if (lo[2] >= FadeTop()) continue;
            for (const auto& polygon : polygons_)
                if (lo[0] <= polygon.maximum[0] + 0.5F && hi[0] >= polygon.minimum[0] - 0.5F &&
                    lo[1] <= polygon.maximum[1] + 0.5F && hi[1] >= polygon.minimum[1] - 0.5F) {
                    candidates_[std::size_t(node.surface_index)] = true;
                    break;
                }
        }
    }

    [[nodiscard]] bool Enabled() const noexcept { return !polygons_.empty(); }
    [[nodiscard]] float PortalHeight() const noexcept { return portal_height_; }
    [[nodiscard]] float FadeTop() const noexcept { return portal_height_ + 64.0F; }
    [[nodiscard]] std::size_t PolygonCount() const noexcept { return polygons_.size(); }
    [[nodiscard]] std::vector<std::array<float, 4>> RectangularFootprints() const {
        std::vector<std::array<float, 4>> result;
        for (const auto& polygon : polygons_) {
            auto points = polygon.points;
            std::array<float, 2> minimum{INFINITY, INFINITY}, maximum{-INFINITY, -INFINITY};
            for (auto& point : points) for (unsigned axis = 0; axis < 2; ++axis) {
                // BSP splitting introduces sub-hundredth-UU roundoff at the
                // authored integer grid. Normalize only that tiny error so
                // adjacent rectangles retain identical, non-overlapping seams.
                const float grid = std::round(point[axis]);
                if (std::abs(point[axis] - grid) <= 0.01F) point[axis] = grid;
                minimum[axis] = std::min(minimum[axis], point[axis]);
                maximum[axis] = std::max(maximum[axis], point[axis]);
            }
            double area = 0;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const auto& a = points[i];
                const auto& b = points[(i + 1) % points.size()];
                area += double(a[0]) * b[1] - double(b[0]) * a[1];
            }
            const double rectangle = double(maximum[0] - minimum[0]) *
                                     (maximum[1] - minimum[1]);
            // Never fill a non-rectangular portal's missing corners. The twelve
            // original BSP pieces are disjoint rectangles with collinear points.
            if (rectangle <= 0 || std::abs(std::abs(area) * 0.5 - rectangle) > 0.01) return {};
            result.push_back({minimum[0], minimum[1], maximum[0], maximum[1]});
        }
        return result;
    }
    [[nodiscard]] bool Candidate(std::size_t surface) const noexcept {
        return surface < candidates_.size() && candidates_[surface];
    }
    [[nodiscard]] float Visibility(Hp1BspVector point,
                                   Hp1BspVector visible_normal = {}) const noexcept {
        if (!Enabled() || !std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z) || point.z >= FadeTop()) return 1.0F;
        // Move wall samples a fraction of one UU into their visible space.
        const std::array<float, 2> xy{point.x + visible_normal.x * 0.25F,
                                      point.y + visible_normal.y * 0.25F};
        bool inside = false;
        for (const auto& polygon : polygons_) {
            if (xy[0] < polygon.minimum[0] || xy[0] > polygon.maximum[0] ||
                xy[1] < polygon.minimum[1] || xy[1] > polygon.maximum[1]) continue;
            bool positive = false, negative = false;
            for (std::size_t i = 0; i < polygon.points.size(); ++i) {
                const auto& a = polygon.points[i];
                const auto& b = polygon.points[(i + 1) % polygon.points.size()];
                const float side = (b[0] - a[0]) * (xy[1] - a[1]) -
                                   (b[1] - a[1]) * (xy[0] - a[0]);
                positive |= side > 0.01F;
                negative |= side < -0.01F;
            }
            if (!(positive && negative)) { inside = true; break; }
        }
        if (!inside) return 1.0F;
        const float t = std::clamp((point.z - portal_height_) / 64.0F, 0.0F, 1.0F);
        return t * t * (3.0F - 2.0F * t);
    }
    [[nodiscard]] std::uint32_t AttenuatePacked(std::uint32_t lighting,
                                               Hp1BspVector point) const noexcept {
        const float visibility = Visibility(point);
        if (visibility >= 1.0F) return lighting;
        std::uint32_t result = lighting & 0xff000000U;
        for (unsigned shift : {0U, 8U, 16U})
            result |= std::uint32_t(std::lround(float((lighting >> shift) & 255U) * visibility)) << shift;
        return result;
    }

private:
    struct Polygon {
        std::vector<std::array<float, 2>> points;
        std::array<float, 2> minimum{INFINITY, INFINITY};
        std::array<float, 2> maximum{-INFINITY, -INFINITY};
    };
    static std::vector<Hp1BspVector> Points(const Hp1BspTopology& topology,
                                          const Hp1BspNode& node) {
        if (node.vertex_pool_index < 0 || std::size_t(node.vertex_pool_index) > topology.vertices.size() ||
            node.vertex_count > topology.vertices.size() - std::size_t(node.vertex_pool_index)) return {};
        std::vector<Hp1BspVector> result;
        result.reserve(node.vertex_count);
        for (std::size_t i = 0; i < node.vertex_count; ++i) {
            const auto index = topology.vertices[std::size_t(node.vertex_pool_index) + i].point_index;
            if (index < 0 || std::size_t(index) >= topology.points.size()) return {};
            result.push_back(topology.points[std::size_t(index)]);
        }
        return result;
    }
    float portal_height_{};
    std::vector<Polygon> polygons_;
    std::vector<bool> candidates_;
};

} // namespace hpvr::wand
