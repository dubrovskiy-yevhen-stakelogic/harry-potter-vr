#include "hpvr/quest_lighting.h"

#include <algorithm>
#include <cmath>

namespace hpvr::quest {

std::uint32_t PackAuthoredLighting(
    const std::array<float, 3>& position,
    const std::span<const AuthoredLight> lights) {
    std::array<float, 3> result{0.16F, 0.17F, 0.20F};
    std::array<float, 3> strongest_color{1.0F, 1.0F, 1.0F};
    std::array<float, 3> second_color{1.0F, 1.0F, 1.0F};
    float strongest = 0.0F;
    float second = 0.0F;
    for (const auto& light : lights) {
        if (!std::isfinite(light.radius) || light.radius <= 0.0F ||
            !std::isfinite(light.intensity) || light.intensity <= 0.0F) {
            continue;
        }
        const float dx = position[0] - light.position[0];
        const float dy = position[1] - light.position[1];
        const float dz = position[2] - light.position[2];
        const float distance_squared = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(distance_squared) ||
            distance_squared >= light.radius * light.radius) continue;
        const float distance = std::sqrt(distance_squared);
        const float linear = 1.0F - distance / light.radius;
        const float contribution =
            linear * linear * light.intensity * 1.85F;
        if (contribution > strongest) {
            second = strongest;
            second_color = strongest_color;
            strongest = contribution;
            strongest_color = light.color;
        } else if (contribution > second) {
            second = contribution;
            second_color = light.color;
        }
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result[axis] += strongest_color[axis] * strongest +
                        second_color[axis] * second * 0.24F;
    }
    const auto channel = [&result](const std::size_t axis) {
        return static_cast<std::uint32_t>(std::lround(
            std::clamp(result[axis], 0.0F, 1.25F) * (255.0F / 1.25F)));
    };
    return channel(0) | (channel(1) << 8U) | (channel(2) << 16U) |
           0xFF000000U;
}

float PackedLightingLuminance(const std::uint32_t packed_light) noexcept {
    const float red =
        static_cast<float>(packed_light & 0xFFU) * 1.25F / 255.0F;
    const float green = static_cast<float>(
        (packed_light >> 8U) & 0xFFU) * 1.25F / 255.0F;
    const float blue = static_cast<float>(
        (packed_light >> 16U) & 0xFFU) * 1.25F / 255.0F;
    return red * 0.2126F + green * 0.7152F + blue * 0.0722F;
}

}  // namespace hpvr::quest
