#include "hpvr/quest_lighting.h"

#include <algorithm>
#include <cmath>

namespace hpvr::quest {

namespace {
float RadialFalloff(const float ratio, const bool non_incidence) noexcept {
    if (!(ratio < 1.0F)) return 0.0F;
    if (non_incidence) {
        return std::clamp((1.0F + 2.0F * ratio * ratio * ratio - 3.0F * ratio * ratio) /
                          std::max(ratio, 1.0e-4F), 0.0F, 1.0F) * 1.55F;
    }
    const float linear = 1.0F - ratio;
    return linear * linear * 1.85F;
}
}

AuthoredDarkLightPush BuildAuthoredDarkLightPush(
    const std::span<const AuthoredLight> lights,
    const std::array<float, 3>& world_reference,
    const std::array<float, 3>& local_origin,
    const std::array<std::array<float, 3>, 3>& local_axes) {
    AuthoredDarkLightPush result;
    std::array<const AuthoredLight*, 2> selected{};
    std::array<float, 2> priorities{};
    for (const auto& light : lights) {
        if (!std::isfinite(light.intensity) || !(light.intensity < 0.0F) ||
            !std::isfinite(light.radius) || !(light.radius > 0.0F)) continue;
        float squared = 0.0F;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto delta = light.position[axis] - world_reference[axis];
            squared += delta * delta;
        }
        if (!std::isfinite(squared)) continue;
        // Do not drop a light just because the brush center is outside its
        // sphere: tall pit columns can still intersect it at their lower end.
        const float priority = -light.intensity * light.radius /
                               (light.radius + std::sqrt(squared));
        const std::size_t slot = priority > priorities[0] ? 0U : 1U;
        if (priority <= priorities[slot]) continue;
        if (slot == 0) {
            priorities[1] = priorities[0];
            selected[1] = selected[0];
        }
        priorities[slot] = priority;
        selected[slot] = &light;
    }
    for (std::size_t i = 0; i < selected.size(); ++i) {
        if (!selected[i]) continue;
        const auto& light = *selected[i];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            for (std::size_t component = 0; component < 3; ++component)
                result.position_radius[i][axis] +=
                    (light.position[component] - local_origin[component]) * local_axes[axis][component];
            result.color_strength[i][axis] = light.color[axis];
        }
        result.position_radius[i][3] = light.radius;
        // A negative strength selects NonIncidence; magnitude is brightness.
        result.color_strength[i][3] = light.effect == 13 ? light.intensity : -light.intensity;
    }
    return result;
}

std::array<float, 3> EvaluateAuthoredDarkLightPush(
    const std::array<float, 3>& local_position,
    const AuthoredDarkLightPush& lights) noexcept {
    std::array<float, 3> result{};
    for (std::size_t i = 0; i < lights.position_radius.size(); ++i) {
        const auto& source = lights.position_radius[i];
        const auto& color = lights.color_strength[i];
        if (!(source[3] > 0.0F)) continue;
        float squared = 0.0F;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const float delta = local_position[axis] - source[axis];
            squared += delta * delta;
        }
        const float amount = RadialFalloff(std::sqrt(squared) / source[3], color[3] < 0.0F) *
                             std::abs(color[3]);
        for (std::size_t axis = 0; axis < 3; ++axis) result[axis] += color[axis] * amount;
    }
    return result;
}

std::uint32_t PackAuthoredLighting(
    const std::array<float, 3>& position,
    const std::span<const AuthoredLight> lights) {
    std::array<float, 3> result{0.16F, 0.17F, 0.20F};
    std::array<float, 3> strongest_color{1.0F, 1.0F, 1.0F};
    std::array<float, 3> second_color{1.0F, 1.0F, 1.0F};
    float strongest = 0.0F;
    float second = 0.0F;
    std::array<float, 3> subtractive{};
    for (const auto& light : lights) {
        if (!std::isfinite(light.radius) || light.radius <= 0.0F ||
            !std::isfinite(light.intensity) || light.intensity == 0.0F) {
            continue;
        }
        const float dx = position[0] - light.position[0];
        const float dy = position[1] - light.position[1];
        const float dz = position[2] - light.position[2];
        const float distance_squared = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(distance_squared) ||
            distance_squared >= light.radius * light.radius) continue;
        const float distance = std::sqrt(distance_squared);
        if (light.intensity < 0.0F) {
            const float amount = -light.intensity *
                RadialFalloff(distance / light.radius, light.effect == 13);
            for (std::size_t axis = 0; axis < 3; ++axis)
                subtractive[axis] += light.color[axis] * amount;
            continue;
        }
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
                        second_color[axis] * second * 0.24F - subtractive[axis];
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
