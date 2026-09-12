#pragma once
#include "hpvr/quest_reflection_math.h"
#include <array>
#include <cstddef>
#include <span>

namespace hpvr::quest {
constexpr std::size_t kAbyssFogRectangles = 12;
struct AbyssFogVolume {
    std::array<std::array<float, 4>, kAbyssFogRectangles> rectangles{};
    std::array<float, 4> bounds{};
    float top = 0, density = 0;
    unsigned count = 0;
};
// Coordinates are source Unreal axes expressed in meters (Z is vertical).
inline AbyssFogVolume MakeAbyssFogVolume(std::span<const std::array<float, 4>> rectangles,
                                       float top, float density) {
    AbyssFogVolume volume;
    if (rectangles.empty() || rectangles.size() > kAbyssFogRectangles ||
        !std::isfinite(top) || !std::isfinite(density) || density <= 0) return volume;
    volume.bounds = rectangles.front();
    for (std::size_t i = 0; i < rectangles.size(); ++i) {
        const auto& r = rectangles[i];
        if (std::ranges::any_of(r, [](float v) { return !std::isfinite(v); }) ||
            r[0] >= r[2] || r[1] >= r[3]) return {};
        for (std::size_t j = 0; j < i; ++j) {
            const auto& q = rectangles[j];
            if (std::min(r[2], q[2]) > std::max(r[0], q[0]) + 0.0001F &&
                std::min(r[3], q[3]) > std::max(r[1], q[1]) + 0.0001F) return {};
        }
        volume.rectangles[i] = r;
        volume.bounds = {std::min(volume.bounds[0], r[0]), std::min(volume.bounds[1], r[1]),
                         std::max(volume.bounds[2], r[2]), std::max(volume.bounds[3], r[3])};
    }
    volume.top = top; volume.density = density; volume.count = static_cast<unsigned>(rectangles.size());
    return volume;
}
inline bool AbyssClipAxis(float origin, float delta, float minimum, float maximum,
                          float& enter, float& leave) {
    // Half-open stationary axes avoid counting a coplanar shared rectangle seam twice.
    if (std::abs(delta) < 0.000001F) return origin >= minimum && origin < maximum;
    const float a = (minimum - origin) / delta, b = (maximum - origin) / delta;
    enter = std::max(enter, std::min(a, b)); leave = std::min(leave, std::max(a, b));
    return leave > enter;
}
inline float AbyssFogTransmittance(const AbyssFogVolume& volume,
                                  const std::array<float, 3>& eye,
                                  const std::array<float, 3>& surface) {
    if (!volume.count || volume.count > kAbyssFogRectangles || volume.density <= 0 ||
        std::ranges::any_of(eye, [](float v) { return !std::isfinite(v); }) ||
        std::ranges::any_of(surface, [](float v) { return !std::isfinite(v); }) ||
        (eye[2] >= volume.top && surface[2] >= volume.top)) return 1;
    std::array<float, 3> delta{};
    float length = 0;
    for (std::size_t i = 0; i < 3; ++i) { delta[i] = surface[i] - eye[i]; length += delta[i] * delta[i]; }
    length = std::sqrt(length);
    if (!std::isfinite(length) || length < 0.000001F) return 1;
    float global_enter = 0, global_leave = 1;
    if (!AbyssClipAxis(eye[0], delta[0], volume.bounds[0], volume.bounds[2], global_enter, global_leave) ||
        !AbyssClipAxis(eye[1], delta[1], volume.bounds[1], volume.bounds[3], global_enter, global_leave)) return 1;
    if (std::abs(delta[2]) < 0.000001F) { if (eye[2] >= volume.top) return 1; }
    else {
        const float boundary = (volume.top - eye[2]) / delta[2];
        if (delta[2] > 0) global_leave = std::min(global_leave, boundary);
        else global_enter = std::max(global_enter, boundary);
    }
    if (global_leave <= global_enter) return 1;
    float optical_depth = 0;
    for (unsigned i = 0; i < volume.count; ++i) {
        float enter = global_enter, leave = global_leave;
        const auto& r = volume.rectangles[i];
        if (!AbyssClipAxis(eye[0], delta[0], r[0], r[2], enter, leave) ||
            !AbyssClipAxis(eye[1], delta[1], r[1], r[3], enter, leave)) continue;
        const float d0 = std::max(0.0F, volume.top - eye[2] - delta[2] * enter);
        const float d1 = std::max(0.0F, volume.top - eye[2] - delta[2] * leave);
        // Exact integral of linearly increasing height density, not a ray march.
        optical_depth += length * (leave - enter) * (d0 + d1) * 0.5F * volume.density;
        if (optical_depth >= 12.0F) return 0;
    }
    return std::exp(-optical_depth);
}
struct alignas(16) AbyssFogUniform {
    Matrix4 inverse_clip_to_source{};
    std::array<float, 4> eye{}, viewport{}, params{}, bounds{};
    std::array<std::array<float, 4>, kAbyssFogRectangles> rectangles{};
};
static_assert(sizeof(AbyssFogUniform) == 320);
static_assert(offsetof(AbyssFogUniform, eye) == 64 && offsetof(AbyssFogUniform, rectangles) == 128);
inline AbyssFogUniform MakeAbyssFogUniform(const AbyssFogVolume& volume,
    const Matrix4& scene_to_source, const Matrix4& view_projection, unsigned width, unsigned height) {
    AbyssFogUniform out;
    Matrix4 inverse;
    if (!volume.count || !width || !height || !InvertReflectionMatrix(view_projection, inverse)) return out;
    out.inverse_clip_to_source = MultiplyMatrices(scene_to_source, inverse);
    if (std::abs(out.inverse_clip_to_source[11]) < 0.000001F) return {};
    for (unsigned i = 0; i < 3; ++i) {
        out.eye[i] = out.inverse_clip_to_source[8 + i] / out.inverse_clip_to_source[11];
        if (!std::isfinite(out.eye[i])) return {};
    }
    out.viewport = {1.0F / float(width), 1.0F / float(height), 0, 0};
    out.params = {volume.top, volume.density, float(volume.count), 1};
    out.bounds = volume.bounds; out.rectangles = volume.rectangles;
    return out;
}
} // namespace hpvr::quest
