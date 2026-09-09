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
};

[[nodiscard]] std::uint32_t PackAuthoredLighting(
    const std::array<float, 3>& position,
    std::span<const AuthoredLight> lights);

[[nodiscard]] float PackedLightingLuminance(
    std::uint32_t packed_light) noexcept;

}  // namespace hpvr::quest
