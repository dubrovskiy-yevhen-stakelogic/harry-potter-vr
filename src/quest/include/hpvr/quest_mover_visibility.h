#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace hpvr::quest::mover_visibility {

inline constexpr std::uint32_t kSourceTwoSided = 0x00000100U;

// Evaluate once while the CPU vertex array is available. A draw containing any
// authored two-sided face keeps the conservative no-cull pipeline; mixed draws
// must not lose those faces. Invalid or empty ranges also default to no culling.
// The BSP extractor repairs winding after the Unreal-to-scene axis reflection.
// With BuildViewProjection and the negative-height Vulkan viewport, its visible
// faces use VK_FRONT_FACE_COUNTER_CLOCKWISE and may use VK_CULL_MODE_BACK_BIT.
template <typename Vertex>
[[nodiscard]] bool RequiresTwoSidedRendering(std::span<const Vertex> vertices,
    std::size_t first_vertex, std::size_t vertex_count) noexcept {
    if (vertex_count < 3 || vertex_count % 3 != 0 || first_vertex > vertices.size() ||
        vertex_count > vertices.size() - first_vertex) return true;
    for (const auto& vertex : vertices.subspan(first_vertex, vertex_count)) {
        if ((vertex.polygon_flags & kSourceTwoSided) != 0U) return true;
    }
    return false;
}

template <typename Vertex>
[[nodiscard]] bool MoverNeedsTwoSided(std::span<const Vertex> vertices) noexcept {
    return RequiresTwoSidedRendering(vertices, 0, vertices.size());
}

}  // namespace hpvr::quest::mover_visibility
