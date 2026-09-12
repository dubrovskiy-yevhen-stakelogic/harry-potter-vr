#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace hpvr::quest {

// A bounded, startup-built approximation of Les_SpellShape's steady-state
// particles. The pattern and Sparkle_3 image come from the owned game at runtime.
inline constexpr std::size_t kTargetMarkerFrames = 8;
inline constexpr std::size_t kTargetMarkerParticles = 128;
inline constexpr std::size_t kTargetMarkerVerticesPerFrame =
    kTargetMarkerParticles * 6;
inline constexpr std::uint32_t kTargetMarkerAtlasSize = 256;

struct TargetMarkerVertex {
    float position[3];
    float texture_uv[2];
};
static_assert(sizeof(TargetMarkerVertex) == 5 * sizeof(float));
static_assert(offsetof(TargetMarkerVertex, texture_uv) == 3 * sizeof(float));

struct TargetMarkerBatch {
    std::vector<TargetMarkerVertex> vertices;

    [[nodiscard]] bool valid() const noexcept {
        return vertices.size() ==
            kTargetMarkerFrames * kTargetMarkerVerticesPerFrame;
    }
    [[nodiscard]] std::uint32_t first_vertex(double seconds) const noexcept {
        if (!std::isfinite(seconds) || seconds < 0.0) return 0;
        constexpr double period = static_cast<double>(kTargetMarkerFrames) / 16.0;
        const auto frame = std::min(kTargetMarkerFrames - 1,
            static_cast<std::size_t>(std::fmod(seconds, period) * 16.0));
        return static_cast<std::uint32_t>(frame * kTargetMarkerVerticesPerFrame);
    }
};

namespace target_marker_detail {
inline float Fraction(float value) noexcept { return value - std::floor(value); }
inline float Random(std::uint32_t seed) noexcept {
    seed ^= seed >> 16;
    seed *= 0x7feb352dU;
    seed ^= seed >> 15;
    seed *= 0x846ca68bU;
    seed ^= seed >> 16;
    return static_cast<float>(seed & 0x00ffffffU) / 16777216.0F;
}
using Vec3 = std::array<float, 3>;
inline float Dot(const Vec3& a, const Vec3& b) noexcept {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
inline Vec3 Cross(const Vec3& a, const Vec3& b) noexcept {
    return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
inline bool Normalize(Vec3* value) noexcept {
    const float length = std::sqrt(Dot(*value, *value));
    if (!std::isfinite(length) || length < 0.00001F) return false;
    for (auto& component : *value) component /= length;
    return true;
}
}  // namespace target_marker_detail

// The local layout matches kParticleQuad / BuildRibbonModel: local X runs
// bottom(0) to top(1); local Y is centered and points opposite camera-right.
// Pass model start=center-up*size/2, end=center+up*size/2, width=size.
[[nodiscard]] inline TargetMarkerBatch BuildTargetMarkerBatch(
    std::span<const std::array<float, 2>> pattern) {
    TargetMarkerBatch result;
    if (pattern.size() < 2 || pattern.size() > 4096) return result;
    std::array<float, 2> minimum = pattern.front(), maximum = minimum;
    for (const auto& point : pattern) {
        for (std::size_t axis = 0; axis < 2; ++axis) {
            if (!std::isfinite(point[axis])) return result;
            minimum[axis] = std::min(minimum[axis], point[axis]);
            maximum[axis] = std::max(maximum[axis], point[axis]);
        }
    }
    const float extent = std::max(maximum[0]-minimum[0], maximum[1]-minimum[1]);
    if (!std::isfinite(extent) || extent < 0.00001F) return result;
    std::vector<std::array<float, 2>> points;
    std::vector<float> lengths;
    points.reserve(pattern.size());
    lengths.reserve(pattern.size());
    for (const auto& source : pattern) {
        const std::array<float, 2> point{
            (source[0]-minimum[0]-(maximum[0]-minimum[0])*0.5F)/extent,
            (source[1]-minimum[1]-(maximum[1]-minimum[1])*0.5F)/extent};
        if (!points.empty()) {
            const auto& previous = points.back();
            const float segment = std::hypot(point[0]-previous[0], point[1]-previous[1]);
            if (segment < 0.000001F) continue;
            lengths.push_back(lengths.back() + segment);
        } else lengths.push_back(0.0F);
        points.push_back(point);
    }
    if (points.size() < 2 || lengths.back() < 0.00001F) return result;
    result.vertices.reserve(kTargetMarkerFrames * kTargetMarkerVerticesPerFrame);
    constexpr float tau = 6.28318530718F;
    constexpr std::array<std::array<float, 2>, 6> corners{{
        {-0.5F,-0.5F},{0.5F,-0.5F},{0.5F,0.5F},
        {-0.5F,-0.5F},{0.5F,0.5F},{-0.5F,0.5F}}};
    constexpr std::array<std::array<float, 2>, 6> quad_uv{{
        {0,1},{0,0},{1,0},{0,1},{1,0},{1,1}}};
    for (std::size_t frame = 0; frame < kTargetMarkerFrames; ++frame) {
        for (std::size_t particle = 0; particle < kTargetMarkerParticles; ++particle) {
            const auto seed = static_cast<std::uint32_t>(particle + 1);
            const float age = target_marker_detail::Fraction(
                target_marker_detail::Random(seed*7U) + static_cast<float>(frame)/
                    static_cast<float>(kTargetMarkerFrames));
            const float distance = lengths.back() * (static_cast<float>(particle)+0.5F)/
                static_cast<float>(kTargetMarkerParticles);
            const auto upper = std::lower_bound(lengths.begin()+1, lengths.end(), distance);
            const auto segment = static_cast<std::size_t>(upper-lengths.begin());
            const float fraction = (distance-lengths[segment-1])/
                (lengths[segment]-lengths[segment-1]);
            const float angle = target_marker_detail::Random(seed*13U)*tau;
            const float drift = age*age*0.027F;
            const float x = points[segment-1][0] +
                (points[segment][0]-points[segment-1][0])*fraction + std::cos(angle)*drift;
            const float y = points[segment-1][1] +
                (points[segment][1]-points[segment-1][1])*fraction + std::sin(angle)*drift;
            const float size = (0.047F+target_marker_detail::Random(seed*23U)*0.065F)*
                (1.0F-age*0.22F);
            const float spin = angle + age*(target_marker_detail::Random(seed*31U)-0.5F)*2.0F;
            const float sine = std::sin(spin), cosine = std::cos(spin);
            const auto tile = std::min(15U, static_cast<unsigned>(age*16.0F));
            for (std::size_t corner = 0; corner < 6; ++corner) {
                const float dx = (corners[corner][0]*cosine-corners[corner][1]*sine)*size;
                const float dy = (corners[corner][0]*sine+corners[corner][1]*cosine)*size;
                result.vertices.push_back({{0.5F+y+dx,-x+dy,0.0F},
                    {(static_cast<float>(tile%4U)*64.0F+2.5F+quad_uv[corner][0]*59.0F)/256.0F,
                     (static_cast<float>(tile/4U)*64.0F+2.5F+quad_uv[corner][1]*59.0F)/256.0F}});
            }
        }
    }
    return result;
}

// Pack 16 white-to-violet lifetime variants of the owned Sparkle_3 into one layer.
// Particle age is selected with UVs, keeping the existing 20-byte vertex format
// and one draw per eye. Black gutters prevent adjacent lifetime tiles bleeding.
[[nodiscard]] inline std::vector<std::uint8_t> BuildTargetMarkerAtlas(
    std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height) {
    if (!width || !height || width > 4096 || height > 4096 ||
        rgba.size() != static_cast<std::size_t>(width)*height*4U) return {};
    std::vector<std::uint8_t> result(kTargetMarkerAtlasSize*kTargetMarkerAtlasSize*4U, 0);
    for (unsigned tile = 0; tile < 16; ++tile) {
        const float age = static_cast<float>(tile)/15.0F;
        const float color_phase = age*age;
        const float fade = std::pow(1.0F-age, 0.8F);
        const std::array<float, 3> tint{
            (1.0F-color_phase*(1.0F-44.0F/255.0F))*fade,
            (1.0F-color_phase*(1.0F-34.0F/255.0F))*fade,
            (1.0F-color_phase*(1.0F-221.0F/255.0F))*fade};
        for (unsigned y = 0; y < 60; ++y) for (unsigned x = 0; x < 60; ++x) {
            const auto source = (static_cast<std::size_t>(y*height/60U)*width+x*width/60U)*4U;
            const auto dest = ((tile/4U*64U+y+2U)*256U+tile%4U*64U+x+2U)*4U;
            for (unsigned channel = 0; channel < 3; ++channel) {
                result[dest+channel] = static_cast<std::uint8_t>(std::clamp(
                    std::lround(static_cast<float>(rgba[source+channel])*tint[channel]),0L,255L));
            }
            result[dest+3U] = 255;
        }
    }
    return result;
}

struct TargetMarkerPlacement {
    bool valid = false;
    std::array<float, 3> center{};
    std::array<float, 3> normal{};
    std::array<float, 3> up{};
    float size = 0.0F;
};

// Place the complete billboard on the viewer-facing support plane, not at a
// ray hit inside the model. Every billboard point remains outside the AABB,
// including oblique views and authored CentreOffset values.
[[nodiscard]] inline TargetMarkerPlacement PlaceTargetMarker(
    const std::array<float, 3>& minimum, const std::array<float, 3>& maximum,
    const std::array<float, 3>& viewer,
    const std::array<float, 3>& center_offset = {}, float size_modifier = 1.0F,
    float margin = 0.035F) noexcept {
    using namespace target_marker_detail;
    TargetMarkerPlacement result;
    if (!std::isfinite(size_modifier) || size_modifier <= 0.0F ||
        !std::isfinite(margin) || margin < 0.0F) return result;
    Vec3 center{}, half{};
    float extent = 0.0F;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) ||
            !std::isfinite(viewer[axis]) || !std::isfinite(center_offset[axis]) ||
            maximum[axis] < minimum[axis]) return result;
        half[axis] = (maximum[axis]-minimum[axis])*0.5F;
        if (!std::isfinite(half[axis]) || half[axis] > 10000.0F) return result;
        extent = std::max(extent, half[axis]);
        center[axis] = minimum[axis]+half[axis];
        result.normal[axis] = viewer[axis]-center[axis];
    }
    if (extent < 0.00001F || !Normalize(&result.normal)) return result;
    Vec3 right = Cross({0,1,0}, result.normal);
    if (!Normalize(&right)) {
        right = Cross({0,0,1}, result.normal);
        if (!Normalize(&right)) return result;
    }
    result.up = Cross(result.normal, right);
    float support = 0.0F, projected_width = 0.0F;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        support += std::abs(result.normal[axis])*half[axis];
        projected_width += 2.0F*std::abs(right[axis])*half[axis];
    }
    const float shift = std::max(0.0F, support+margin-Dot(center_offset,result.normal));
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.center[axis] = center[axis]+center_offset[axis]+result.normal[axis]*shift;
        if (!std::isfinite(result.center[axis])) return TargetMarkerPlacement{};
    }
    result.size = std::clamp(projected_width*size_modifier, 0.55F, 2.4F);
    result.valid = true;
    return result;
}

}  // namespace hpvr::quest
