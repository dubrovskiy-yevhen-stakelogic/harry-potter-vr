#pragma once
#include "hp1_zone_ambient.h"
#include "hpvr/hp1_authored_environment.h"
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace hpvr::wand::detail {
// Shared CPU-only lightmap preparation. Atlas packing and positive-light
// arithmetic intentionally retain the existing prepared-cache byte layout.
constexpr std::uint32_t kLightmapGutter{1};

[[nodiscard]] inline float dot(Hp1BspVector a, Hp1BspVector b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] inline Hp1BspVector cross(Hp1BspVector a, Hp1BspVector b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] inline std::optional<Hp1BspVector>
solve_plane_coordinates(Hp1BspVector u, Hp1BspVector v, Hp1BspVector n, float u_value,
                        float v_value, float n_value) {
    const auto vx_n = cross(v, n);
    const auto nx_u = cross(n, u);
    const auto ux_v = cross(u, v);
    const float determinant = dot(u, vx_n);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-10F) {
        return std::nullopt;
    }
    const float inverse = 1.0F / determinant;
    return Hp1BspVector{(u_value * vx_n.x + v_value * nx_u.x + n_value * ux_v.x) * inverse,
                        (u_value * vx_n.y + v_value * nx_u.y + n_value * ux_v.y) * inverse,
                        (u_value * vx_n.z + v_value * nx_u.z + n_value * ux_v.z) * inverse};
}

[[nodiscard]] inline std::array<float, 3> hsb_light_color(std::uint8_t hue,
                                                          std::uint8_t saturation) {
    const float h = static_cast<float>(hue) * 6.0F / 255.0F;
    const float s = 1.0F - static_cast<float>(saturation) / 255.0F;
    const float x = s * (1.0F - std::abs(std::fmod(h, 2.0F) - 1.0F));
    std::array<float, 3> rgb{};
    if (h < 1.0F)
        rgb = {s, x, 0.0F};
    else if (h < 2.0F)
        rgb = {x, s, 0.0F};
    else if (h < 3.0F)
        rgb = {0.0F, s, x};
    else if (h < 4.0F)
        rgb = {0.0F, x, s};
    else if (h < 5.0F)
        rgb = {x, 0.0F, s};
    else
        rgb = {s, 0.0F, x};
    for (float &channel : rgb)
        channel += 1.0F - s;
    return rgb;
}

[[nodiscard]] inline std::uint8_t encode_linear(float value) noexcept {
    value = std::clamp(value, 0.0F, 1.0F);
    const float srgb =
        value <= 0.0031308F ? value * 12.92F : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
    return static_cast<std::uint8_t>(std::lround(srgb * 255.0F));
}

inline bool IsDarkLight(const Hp1ActorVisual &actor) noexcept {
    for (const auto &property : actor.serialized_properties) {
        constexpr std::string_view name = "bDarkLight";
        if (property.name.size() != name.size())
            continue;
        bool same = true;
        for (std::size_t i = 0; i < name.size(); ++i) {
            const auto fold = [](char c) {
                return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c;
            };
            same = same && fold(property.name[i]) == fold(name[i]);
        }
        if (same && property.boolean_value_serialized)
            return property.boolean_value;
    }
    return false;
}
struct LightSource {
    const Hp1ActorVisual *actor{};
    bool dark{};
};
using LightSources = std::map<std::int32_t, LightSource>;
inline LightSources GatherLightSources(const Hp1ActorVisualCensus &actors) {
    LightSources result;
    for (const auto &actor : actors.actors)
        if (actor.location_serialized && actor.light_brightness > 0 && actor.light_radius > 0)
            result.emplace(actor.actor_reference, LightSource{&actor, IsDarkLight(actor)});
    return result;
}
struct LightmapPlacement {
    std::uint32_t x{};
    std::uint32_t y{};
    std::int32_t surface{-1};
};

struct LightmapLayout {
    std::uint32_t size{};
    std::vector<LightmapPlacement> placements;
};
inline LightmapLayout MakeLightmapLayout(const Hp1BspTopology &topology,
                                         const Hp1BspTriangleMesh &mesh,
                                         std::size_t selected_count) {
    LightmapLayout layout;
    if (selected_count > mesh.triangles.size())
        throw std::runtime_error("lightmap triangle span is invalid");
    auto &placements = layout.placements;
    placements.resize(topology.light_maps.size());
    for (std::size_t index = 0; index < selected_count; ++index) {
        const auto &triangle = mesh.triangles[index];
        if (triangle.surface_index >= topology.surfaces.size())
            throw std::runtime_error("invalid lightmap surface");
        const auto &surface = topology.surfaces[triangle.surface_index];
        if (surface.light_map_index >= 0) {
            if (static_cast<std::size_t>(surface.light_map_index) >= placements.size())
                throw std::runtime_error("invalid surface lightmap index");
            placements[static_cast<std::size_t>(surface.light_map_index)].surface =
                static_cast<std::int32_t>(triangle.surface_index);
        }
    }
    for (const std::uint32_t atlas_size : {512U, 1024U, 2048U, 4096U}) {
        std::uint32_t cursor_x = kLightmapGutter;
        std::uint32_t cursor_y = kLightmapGutter;
        std::uint32_t row_height = 0;
        bool fits = true;
        for (std::size_t index = 0; index < placements.size(); ++index) {
            if (placements[index].surface < 0)
                continue;
            const auto &light_map = topology.light_maps[index];
            if (light_map.u_clamp <= 0 || light_map.v_clamp <= 0 || light_map.u_clamp > 4096 ||
                light_map.v_clamp > 4096)
                throw std::runtime_error("invalid lightmap dimensions");
            const auto width = static_cast<std::uint32_t>(light_map.u_clamp) + kLightmapGutter * 2U;
            const auto height =
                static_cast<std::uint32_t>(light_map.v_clamp) + kLightmapGutter * 2U;
            if (width + 1U > atlas_size || height + 1U > atlas_size) {
                fits = false;
                break;
            }
            if (cursor_x + width > atlas_size) {
                cursor_x = kLightmapGutter;
                cursor_y += row_height;
                row_height = 0;
            }
            if (cursor_y + height > atlas_size) {
                fits = false;
                break;
            }
            placements[index].x = cursor_x + kLightmapGutter;
            placements[index].y = cursor_y + kLightmapGutter;
            cursor_x += width;
            row_height = std::max(row_height, height);
        }
        if (fits) {
            layout.size = atlas_size;
            break;
        }
    }
    if (layout.size == 0) {
        throw std::runtime_error("BSP light maps do not fit the 4096 atlas cap");
    }

    return layout;
}
using LightmapPixels = std::vector<std::array<std::uint8_t, 4>>;
inline LightmapPixels BakeLightmapTile(const Hp1BspTopology &topology,
                                       const LightSources &light_actors,
                                       const DarkZoneAmbient &dark_zone_ambient,
                                       const LightmapPlacement &placement, std::size_t map_index,
                                       bool signed_lights, bool dark_non_incidence = true,
                                       bool abyss_lighting = true,
                                       const Hp1AuthoredZoneAmbient* authored_ambient = nullptr) {
    if (map_index >= topology.light_maps.size() || placement.surface < 0 ||
        static_cast<std::size_t>(placement.surface) >= topology.surfaces.size())
        throw std::runtime_error("invalid lightmap tile");
    const auto &light_map = topology.light_maps[map_index];
    const auto &surface = topology.surfaces[static_cast<std::size_t>(placement.surface)];
    const auto valid = [](std::int32_t i, std::size_t size) {
        return i >= 0 && static_cast<std::size_t>(i) < size;
    };
    if (!valid(surface.base_point_index, topology.points.size()) ||
        !valid(surface.texture_u_vector_index, topology.vectors.size()) ||
        !valid(surface.texture_v_vector_index, topology.vectors.size()) ||
        !valid(surface.normal_vector_index, topology.vectors.size()) || light_map.u_clamp <= 0 ||
        light_map.v_clamp <= 0 || light_map.u_clamp > 4096 || light_map.v_clamp > 4096 ||
        !(light_map.u_scale > 0) || !(light_map.v_scale > 0) || !std::isfinite(light_map.u_scale) ||
        !std::isfinite(light_map.v_scale) || light_map.data_offset < 0 ||
        light_map.light_actor_index < -1)
        throw std::runtime_error("invalid lightmap sampling data");
    const auto &base = topology.points[static_cast<std::size_t>(surface.base_point_index)];
    const auto &texture_u =
        topology.vectors[static_cast<std::size_t>(surface.texture_u_vector_index)];
    const auto &texture_v =
        topology.vectors[static_cast<std::size_t>(surface.texture_v_vector_index)];
    const auto &normal = topology.vectors[static_cast<std::size_t>(surface.normal_vector_index)];
    const float normal_length = std::sqrt(std::max(dot(normal, normal), 1.0e-12F));
    const Hp1BspVector unit_normal{normal.x / normal_length, normal.y / normal_length,
                                   normal.z / normal_length};
    const float base_u = dot(texture_u, base) + light_map.pan.x - 0.5F * light_map.u_scale;
    const float base_v = dot(texture_v, base) + light_map.pan.y - 0.5F * light_map.v_scale;
    const float plane = dot(normal, base);
    const std::size_t width = static_cast<std::size_t>(light_map.u_clamp);
    const std::size_t height = static_cast<std::size_t>(light_map.v_clamp);
    const std::size_t pitch = (width + 7U) / 8U;
    const std::size_t bytes_per_light = pitch * height;
    if (light_map.light_actor_index >= 0) {
        auto end = static_cast<std::size_t>(light_map.light_actor_index);
        const auto begin = end;
        while (end < topology.light_references.size() && topology.light_references[end] != 0)
            ++end;
        const auto offset = static_cast<std::size_t>(light_map.data_offset);
        if (end >= topology.light_references.size() || offset > topology.light_bits.size() ||
            end - begin > (topology.light_bits.size() - offset) / bytes_per_light)
            throw std::runtime_error("invalid lightmap visibility span");
    }
    std::vector<std::array<std::uint8_t, 4>> pixels(width * height);

    const auto visibility = [&](std::size_t light_ordinal, int x, int y) {
        static constexpr int kernel[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
        int sum = 0;
        for (int ky = -1; ky <= 1; ++ky) {
            for (int kx = -1; kx <= 1; ++kx) {
                const int sx = std::clamp(x + kx, 0, light_map.u_clamp - 1);
                const int sy = std::clamp(y + ky, 0, light_map.v_clamp - 1);
                const auto byte = static_cast<std::size_t>(light_map.data_offset) +
                                  light_ordinal * bytes_per_light +
                                  static_cast<std::size_t>(sy) * pitch +
                                  static_cast<std::size_t>(sx >> 3);
                if ((topology.light_bits[byte] & (1U << static_cast<unsigned>(sx & 7))) != 0) {
                    sum += kernel[ky + 1][kx + 1];
                }
            }
        }
        return static_cast<float>(sum) / 16.0F;
    };
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            std::array<float, 3> lighting{0.085F, 0.080F, 0.070F};
            const auto world = solve_plane_coordinates(
                texture_u, texture_v, normal,
                base_u + (static_cast<float>(x) + 0.5F) * light_map.u_scale,
                base_v + (static_cast<float>(y) + 0.5F) * light_map.v_scale, plane);
            if (world && authored_ambient) {
                const auto ambient = authored_ambient->Sample(*world, unit_normal);
                if (ambient) lighting = *ambient;
            }
            if (world.has_value() &&
                dark_zone_ambient.Suppress(static_cast<std::size_t>(placement.surface), *world,
                                           unit_normal)) {
                lighting = {0.0F, 0.0F, 0.0F};
            }
            if (world.has_value() && light_map.light_actor_index >= 0) {
                std::size_t ordinal = 0;
                for (std::size_t reference_index =
                         static_cast<std::size_t>(light_map.light_actor_index);
                     reference_index < topology.light_references.size() &&
                     topology.light_references[reference_index] != 0;
                     ++reference_index, ++ordinal) {
                    const auto found =
                        light_actors.find(topology.light_references[reference_index]);
                    if (found == light_actors.end())
                        continue;
                    const auto &actor = *found->second.actor;
                    const float dx = actor.location_unreal.x - world->x;
                    const float dy = actor.location_unreal.y - world->y;
                    const float dz = actor.location_unreal.z - world->z;
                    const float distance_squared = dx * dx + dy * dy + dz * dz;
                    const float radius = static_cast<float>(actor.light_radius) * 25.0F;
                    if (!(distance_squared < radius * radius))
                        continue;
                    const float distance = std::sqrt(std::max(distance_squared, 1.0e-8F));
                    const float ratio = distance / radius;
                    const float smooth =
                        std::min((1.0F + 2.0F * ratio * ratio * ratio - 3.0F * ratio * ratio) /
                                     std::max(ratio, 1.0e-4F),
                                 1.0F);
                    // LE_NonIncidence (13) is radial: the authored pit lights
                    // must darken walls as well as the horizontal bottom.
                    // Keep the previous path available to validate old caches.
                    const bool radial_dark = dark_non_incidence && found->second.dark &&
                                             actor.light_effect == 13;
                    const float angle = radial_dark ? 1.0F : std::abs(
                        (dx * unit_normal.x + dy * unit_normal.y + dz * unit_normal.z) / distance);
                    const float amount =
                        visibility(ordinal, static_cast<int>(x), static_cast<int>(y)) *
                        std::max(smooth, 0.0F) * angle *
                        (static_cast<float>(actor.light_brightness) / 255.0F) * 1.55F;
                    const auto color = hsb_light_color(actor.light_hue, actor.light_saturation);
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        lighting[channel] += color[channel] * amount *
                                             ((signed_lights && found->second.dark) ? -1.0F : 1.0F);
                    }
                }
            }
            if (abyss_lighting && world.has_value()) {
                const float abyss_visibility = dark_zone_ambient.FullLightVisibility(
                    static_cast<std::size_t>(placement.surface), *world, unit_normal);
                for (float& channel : lighting) channel *= abyss_visibility;
            }
            pixels[y * width + x] = {encode_linear(lighting[0]), encode_linear(lighting[1]),
                                     encode_linear(lighting[2]), 255};
        }
    }

    return pixels;
}
template <class Visit>
void VisitLightmapTile(const Hp1LightMapIndex &light_map, const LightmapPlacement &placement,
                       const LightmapPixels &pixels, Visit visit) {
    for (int y = -1; y <= light_map.v_clamp; ++y)
        for (int x = -1; x <= light_map.u_clamp; ++x) {
            const auto sx = static_cast<std::size_t>(std::clamp(x, 0, light_map.u_clamp - 1));
            const auto sy = static_cast<std::size_t>(std::clamp(y, 0, light_map.v_clamp - 1));
            visit(static_cast<std::uint32_t>(static_cast<int>(placement.x) + x),
                  static_cast<std::uint32_t>(static_cast<int>(placement.y) + y),
                  pixels[sy * static_cast<std::size_t>(light_map.u_clamp) + sx]);
        }
}
} // namespace hpvr::wand::detail
