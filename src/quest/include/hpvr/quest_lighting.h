#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace hpvr::quest {

struct AuthoredLight {
    std::array<float, 3> position{};
    std::array<float, 3> color{1.0F, 1.0F, 1.0F};
    float radius = 1.0F;
    float intensity = 1.0F;
    std::uint32_t effect = 0;
};

// Two strongest authored dark lights, evaluated in the mover's local space.
// Together with its existing MVP this fits the Vulkan minimum 128-byte push limit.
struct AuthoredDarkLightPush {
    std::array<std::array<float, 4>, 2> position_radius{};
    std::array<std::array<float, 4>, 2> color_strength{};
};
static_assert(sizeof(AuthoredDarkLightPush) == 64);

[[nodiscard]] AuthoredDarkLightPush BuildAuthoredDarkLightPush(
    std::span<const AuthoredLight> lights,
    const std::array<float, 3>& world_reference,
    const std::array<float, 3>& local_origin,
    const std::array<std::array<float, 3>, 3>& local_axes);

// CPU equivalent of the vertex shader, for authored-light/transform regressions.
[[nodiscard]] std::array<float, 3> EvaluateAuthoredDarkLightPush(
    const std::array<float, 3>& local_position,
    const AuthoredDarkLightPush& lights) noexcept;

[[nodiscard]] std::uint32_t PackAuthoredLighting(
    const std::array<float, 3>& position,
    std::span<const AuthoredLight> lights);

[[nodiscard]] float PackedLightingLuminance(
    std::uint32_t packed_light) noexcept;

}  // namespace hpvr::quest
