#include "../src/hp1_lightmap_repair.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>

namespace {
using namespace hpvr::wand;
void Check(bool value, const char *text) {
    if (!value)
        throw std::runtime_error(text);
}
struct Fixture {
    Hp1BspTopology topology;
    Hp1BspTriangleMesh mesh;
    Hp1ActorVisualCensus actors;
};
Fixture Synthetic() {
    Fixture f;
    f.topology.status = f.mesh.status = f.actors.status = Hp1ProfileStatus::ok;
    f.topology.points = {{0, 0, 0}};
    f.topology.vectors = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Hp1BspSurface surface;
    surface.normal_vector_index = 2;
    surface.texture_u_vector_index = 0;
    surface.texture_v_vector_index = 1;
    surface.light_map_index = 0;
    f.topology.surfaces.push_back(surface);
    surface.light_map_index = 1;
    f.topology.surfaces.push_back(surface);
    Hp1LightMapIndex lm;
    lm.u_clamp = lm.v_clamp = 2;
    lm.u_scale = lm.v_scale = 1;
    lm.light_actor_index = 0;
    f.topology.light_maps.push_back(lm);
    lm.light_actor_index = 3;
    lm.data_offset = 4;
    f.topology.light_maps.push_back(lm);
    f.topology.light_references = {1, 2, 0, 2, 0};
    f.topology.light_bits.assign(6, 255);
    Hp1BspTriangle triangle;
    f.mesh.triangles.push_back(triangle);
    triangle.surface_index = 1;
    f.mesh.triangles.push_back(triangle);
    Hp1ActorVisual light;
    light.actor_reference = 1;
    light.location_serialized = true;
    light.location_unreal = {0, 0, 10};
    light.light_brightness = 255;
    light.light_radius = 16;
    light.light_saturation = 255;
    Hp1ClassDefaultProperty dark;
    dark.name = "bDarkLight";
    dark.boolean_value = dark.boolean_value_serialized = true;
    light.serialized_properties.push_back(dark);
    f.actors.actors.push_back(light);
    light.actor_reference = 2;
    light.serialized_properties.clear();
    f.actors.actors.push_back(light);
    return f;
}
std::vector<std::uint8_t> Atlas(const Fixture &f, bool signed_lights, bool ambient,
                              bool dark_non_incidence = true, bool abyss_lighting = true) {
    const auto layout = detail::MakeLightmapLayout(f.topology, f.mesh, f.mesh.triangles.size());
    std::vector<std::uint8_t> atlas(std::size_t(layout.size) * layout.size * 4U);
    const auto lights = detail::GatherLightSources(f.actors);
    const detail::DarkZoneAmbient policy(f.topology, f.actors, ambient);
    for (std::size_t i = 0; i < layout.placements.size(); ++i) {
        const auto &p = layout.placements[i];
        if (p.surface < 0)
            continue;
        const auto pixels =
            detail::BakeLightmapTile(f.topology, lights, policy, p, i, signed_lights, dark_non_incidence, abyss_lighting);
        detail::VisitLightmapTile(
            f.topology.light_maps[i], p, pixels, [&](auto x, auto y, const auto &rgba) {
                std::copy(rgba.begin(), rgba.end(),
                          atlas.begin() +
                              static_cast<std::ptrdiff_t>((std::size_t(y) * layout.size + x) * 4U));
            });
    }
    return atlas;
}
void AbyssSynthetic() {
    auto f = Synthetic();
    f.topology.zone_count = 4;
    f.topology.zone_actor_references = {0, 0, 0, 33};
    Hp1ActorVisual actor;
    actor.actor_reference = 33;
    actor.qualified_class_name = "Engine.ZoneInfo";
    Hp1ClassDefaultProperty kill;
    kill.name = "bKillZone";
    kill.boolean_value_serialized = kill.boolean_value = true;
    actor.serialized_properties.push_back(kill);
    f.actors.actors.push_back(actor);
    f.topology.surfaces[0].polygon_flags = 1;
    const auto rectangle = [&](float x0, float y0, float x1, float y1,
                               float height, unsigned other_zone) {
        Hp1BspNode node;
        node.vertex_count = 4;
        node.surface_index = 0;
        node.vertex_pool_index = static_cast<std::int32_t>(f.topology.vertices.size());
        node.zone_indices = {static_cast<std::uint8_t>(other_zone), 3};
        node.plane = {0, 0, 1, height};
        node.front_node_index = node.back_node_index = -1;
        for (const auto& xy : {std::array{x0, y0}, std::array{x1, y0},
                               std::array{x1, y1}, std::array{x0, y1}}) {
            f.topology.vertices.push_back({static_cast<std::int32_t>(f.topology.points.size()), 0});
            f.topology.points.push_back({xy[0], xy[1], height});
        }
        f.topology.nodes.push_back(node);
    };
    // Solid floor encountered before portals must not become the fade height.
    rectangle(-10, -10, 10, 10, 496, 0);
    rectangle(-10, -10, 10, -2, 728, 2);
    rectangle(-10, 2, 10, 10, 728, 2);
    rectangle(-10, -2, -2, 2, 728, 2);
    rectangle(2, -2, 10, 2, 728, 2);
    const Hp1ChallengeAbyssLighting abyss(f.topology, f.actors, true);
    Check(abyss.Enabled() && abyss.PolygonCount() == 4 && abyss.PortalHeight() == 728,
          "portal extraction excludes zone0 solid floors and keeps split polygons");
    Check(abyss.Visibility({-5, 0, 496}) == 0 &&
          abyss.Visibility({-5, 0, 760}) == 0.5F &&
          abyss.Visibility({-5, 0, 792}) == 1 &&
          abyss.Visibility({0, 0, 496}) == 1 &&
          abyss.Visibility({11, 0, 496}) == 1,
          "height attenuation preserves a central safe hole and exterior");
    Check(abyss.AttenuatePacked(0xff204080, {-5, 0, 760}) == 0xff102040,
          "fixture packed light is attenuated once with alpha preserved");
    Check(Hp1ChallengeAbyssLighting(f.topology, f.actors, false).Visibility({-5, 0, 496}) == 1,
          "non-challenge map policy is a byte-preserving no-op");
    // Move a positively lit tile under one portal; no dark lights are needed.
    f.topology.points[0] = {-5, 0, 0};
    f.topology.light_references[0] = 2;
    f.topology.light_references[1] = 0;
    const auto old = Atlas(f, true, true, true, false);
    const auto dark = Atlas(f, true, true);
    const auto layout = detail::MakeLightmapLayout(f.topology, f.mesh, 2);
    const auto offset = (std::size_t(layout.placements[0].y) * layout.size + layout.placements[0].x) * 4;
    Check(old[offset] > 0 && dark[offset] == 0 && dark[offset + 1] == 0 && dark[offset + 2] == 0,
          "abyss extinguishes positive direct lighting, not only ambient");
}
std::uint64_t Integer(std::ifstream &input, unsigned bytes) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) {
        const auto c = input.get();
        Check(c != EOF, "truncated prepared prefix");
        value |= std::uint64_t(c) << (8U * i);
    }
    return value;
}
struct CachedAtlas {
    std::uint32_t width{}, height{}, maps{}, world_vertices{};
    std::vector<std::uint8_t> pixels;
};
CachedAtlas ReadCachedAtlas(const std::filesystem::path &path) {
    // Diagnostic reader only: skip large immutable geometry/texture blocks.
    // The runtime validates the complete cache checksum before calling repair.
    std::ifstream input(path, std::ios::binary);
    Check(bool(input), "cache open failed");
    char magic[8]{};
    input.read(magic, 8);
    Check(std::string_view(magic, 8) == "HPVRSCN1", "cache magic differs");
    Check(Integer(input, 4) == 1 && Integer(input, 4) == 45, "cache schema/revision differs");
    input.seekg(48);
    Check(Integer(input, 4) == 1, "payload schema differs");
    for (int i = 0; i < 3; ++i)
        (void)Integer(input, 4);
    CachedAtlas result;
    result.width = static_cast<std::uint32_t>(Integer(input, 4));
    result.height = static_cast<std::uint32_t>(Integer(input, 4));
    result.world_vertices = static_cast<std::uint32_t>(Integer(input, 4));
    for (int i = 0; i < 4; ++i)
        (void)Integer(input, 4);
    result.maps = static_cast<std::uint32_t>(Integer(input, 4));
    for (int i = 0; i < 2; ++i)
        (void)Integer(input, 4);
    const auto vertices = Integer(input, 8);
    Check(vertices <= 22000000, "vertex prefix limit");
    input.seekg(static_cast<std::streamoff>(vertices * 44), std::ios::cur);
    const auto textures = Integer(input, 8);
    Check(textures <= 256ULL * 256 * 256 * 4, "texture prefix limit");
    input.seekg(static_cast<std::streamoff>(textures), std::ios::cur);
    const auto count = Integer(input, 8);
    Check(result.width <= 4096 && result.height <= 4096 &&
              count == std::uint64_t(result.width) * result.height * 4,
          "atlas prefix limit");
    result.pixels.resize(static_cast<std::size_t>(count));
    input.read(reinterpret_cast<char *>(result.pixels.data()), static_cast<std::streamsize>(count));
    Check(bool(input), "truncated atlas");
    return result;
}
void Owned(const std::filesystem::path &root, const std::filesystem::path &cache_dir) {
    for (const auto map : {0, 1}) {
        const auto path = root / "Maps" / (map ? "Lev_Tut1b.unr" : "Lev_Tut1.unr");
        auto cached = ReadCachedAtlas(cache_dir / (map ? "map-1.hpvc" : "map-0.hpvc"));
        const auto before = cached.pixels;
        const auto start = std::chrono::steady_clock::now();
        const auto result =
            repair_hp1_bsp_dark_lightmaps(path, std::numeric_limits<std::uint32_t>::max(),
                                          cached.maps, cached.width, cached.height, cached.pixels);
        const auto ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        if (result.status != Hp1ProfileStatus::ok)
            throw std::runtime_error(result.error);
        std::cout << "OWNED_ATLAS map=" << map << " width=" << cached.width
                  << " maps=" << cached.maps << " dark_actors=" << result.dark_light_actor_count
                  << " dark_tiles=" << result.dark_light_maps
                  << " ambient_tiles=" << result.ambient_light_maps
                  << " affected=" << result.affected_lightmaps
                  << " changed=" << result.changed_texels << " staged_bytes=" << result.staged_bytes
                  << " ms=" << ms << '\n';
        const auto second =
            repair_hp1_bsp_dark_lightmaps(path, std::numeric_limits<std::uint32_t>::max(),
                                          cached.maps, cached.width, cached.height, cached.pixels);
        Check(second.status == Hp1ProfileStatus::ok && second.changed_texels == 0,
              "owned repair must be idempotent");
        if (!map) {
            Check(before == cached.pixels, "map0 must remain byte identical");
            Fixture first;
            first.topology = load_hp1_bsp_topology(path);
            first.actors = inspect_hp1_actor_visuals(path);
            first.mesh = build_hp1_bsp_triangle_mesh(first.topology, 0.02F);
            Check(Atlas(first, true, false) == before,
                  "first-map fresh shared baker preserves C45 atlas exactly");
            continue;
        }
        Check(result.changed_texels > 0, "map1 should change authored dark lighting");
        Fixture f;
        f.topology = load_hp1_bsp_topology(path);
        f.actors = inspect_hp1_actor_visuals(path);
        f.mesh = build_hp1_bsp_triangle_mesh(f.topology, 0.02F);
        const auto expected = Atlas(f, true, true);
        Check(expected == cached.pixels, "repaired atlas equals full fresh signed bake");
        auto signed_incidence = Atlas(f, true, true, false, false);
        std::size_t radial_changed = 0;
        for (std::size_t i = 0; i < expected.size(); i += 4)
            if (!std::equal(expected.begin() + i, expected.begin() + i + 3,
                            signed_incidence.begin() + i)) ++radial_changed;
        Check(radial_changed > 0, "retail radial dark lights must change wall texels");
        const auto upgrade = repair_hp1_bsp_dark_lightmaps(
            path, std::numeric_limits<std::uint32_t>::max(), cached.maps,
            cached.width, cached.height, signed_incidence);
        Check(upgrade.status == Hp1ProfileStatus::ok && signed_incidence == expected,
              "previous signed-incidence atlas upgrades to radial dark-light bake");
        std::cout << "OWNED_NON_INCIDENCE changed_texels=" << radial_changed << '\n';
        const Hp1ChallengeAbyssLighting abyss(f.topology, f.actors, true);
        std::cout << "OWNED_ABYSS_GEOMETRY polygons=" << abyss.PolygonCount()
                  << " portal=" << abyss.PortalHeight() << " fade_top=" << abyss.FadeTop() << '\n';
        Check(abyss.Enabled() && abyss.PolygonCount() == 12 &&
              abyss.PortalHeight() == 728.0F && abyss.FadeTop() == 792.0F,
              "owned moving-column abyss derives its complete authored portal footprint");
        for (float z : {0.0F, 496.0F, 640.0F, 728.0F})
            Check(abyss.Visibility({-2650, -6500, z}) == 0,
                  "abyss extinguishes all geometry lighting below the original portal");
        Check(abyss.Visibility({-2650, -6500, 760}) == 0.5F &&
              abyss.Visibility({-2650, -6500, 792}) == 1 &&
              abyss.Visibility({-2650, -6500, 880}) == 1 &&
              abyss.Visibility({-2650, -6500, 1008}) == 1,
              "short smooth fade preserves platform bottoms and tops");
        Check(abyss.Visibility({-2300, -4800, 640}) == 1 &&
              abyss.Visibility({-2650, -6200, 640}) == 0 &&
              abyss.Visibility({-2000, -6200, 640}) == 1 &&
              abyss.Visibility({-2711, -7006, 146}) == 1,
              "safe central island, adjacent room and earlier separate pit remain unchanged");
        Check(abyss.AttenuatePacked(0xffddeeffU, {-2650, -6500, 640}) == 0xff000000U &&
              abyss.AttenuatePacked(0xffddeeffU, {-2300, -4800, 640}) == 0xffddeeffU,
              "static fixture lighting uses exactly the same footprint and full-light attenuation");
        auto c50 = Atlas(f, true, true, true, false);
        const auto c50_upgrade = repair_hp1_bsp_dark_lightmaps(
            path, std::numeric_limits<std::uint32_t>::max(), cached.maps,
            cached.width, cached.height, c50);
        Check(c50_upgrade.status == Hp1ProfileStatus::ok && c50 == expected &&
              c50_upgrade.changed_texels > 0, "C50 signed radial cache upgrades to full abyss attenuation");
        std::cout << "OWNED_ABYSS c50_changed_texels=" << c50_upgrade.changed_texels
                  << " polygons=" << abyss.PolygonCount() << " portal=" << abyss.PortalHeight()
                  << " fade_top=" << abyss.FadeTop() << '\n';
        const auto layout = detail::MakeLightmapLayout(f.topology, f.mesh, f.mesh.triangles.size());
        const auto lights = detail::GatherLightSources(f.actors);
        const detail::DarkZoneAmbient ambient_policy(f.topology, f.actors, true);
        std::size_t extinguished = 0, unchanged_above = 0;
        for (std::size_t i = 0; i < layout.placements.size(); ++i) {
            const auto& p = layout.placements[i];
            if (p.surface < 0 || !abyss.Candidate(std::size_t(p.surface))) continue;
            const auto& surface = f.topology.surfaces[std::size_t(p.surface)];
            const auto& lm = f.topology.light_maps[i];
            const auto& base = f.topology.points[std::size_t(surface.base_point_index)];
            const auto& u = f.topology.vectors[std::size_t(surface.texture_u_vector_index)];
            const auto& v = f.topology.vectors[std::size_t(surface.texture_v_vector_index)];
            const auto& n = f.topology.vectors[std::size_t(surface.normal_vector_index)];
            const float length = std::sqrt(detail::dot(n, n));
            const Hp1BspVector normal{n.x / length, n.y / length, n.z / length};
            const auto old_pixels = detail::BakeLightmapTile(f.topology, lights, ambient_policy, p, i, true, true, false);
            const auto new_pixels = detail::BakeLightmapTile(f.topology, lights, ambient_policy, p, i, true);
            for (int y = 0; y < lm.v_clamp; ++y) for (int x = 0; x < lm.u_clamp; ++x) {
                const auto point = detail::solve_plane_coordinates(u, v, n,
                    detail::dot(u, base) + lm.pan.x + float(x) * lm.u_scale,
                    detail::dot(v, base) + lm.pan.y + float(y) * lm.v_scale, detail::dot(n, base));
                if (!point) continue;
                const auto index = std::size_t(y) * std::size_t(lm.u_clamp) + std::size_t(x);
                const float visibility = abyss.Visibility(*point, normal);
                if (visibility == 0) {
                    Check(new_pixels[index] == std::array<std::uint8_t, 4>{0, 0, 0, 255},
                          "every footprint texel below portal is opaque black despite direct lights");
                    extinguished += old_pixels[index][0] != 0 ? 1U : 0U;
                } else if (visibility == 1) {
                    Check(new_pixels[index] == old_pixels[index],
                          "all unaffected footprint/height texels retain exact C50 lighting");
                    ++unchanged_above;
                }
            }
        }
        Check(extinguished > 0 && unchanged_above > 0,
              "owned long-pit walls have both newly extinguished and preserved bright texels");
        std::cout << "OWNED_ABYSS_SAMPLES extinguished=" << extinguished
                  << " unchanged=" << unchanged_above << '\n';
        for (const auto &[ref, source] : lights)
            if (source.dark) {
                std::size_t tiles = 0;
                for (std::size_t i = 0; i < layout.placements.size(); ++i) {
                    if (layout.placements[i].surface < 0)
                        continue;
                    const auto &lm = f.topology.light_maps[i];
                    for (auto j = lm.light_actor_index;
                         j >= 0 &&
                         static_cast<std::size_t>(j) < f.topology.light_references.size() &&
                         f.topology.light_references[static_cast<std::size_t>(j)] != 0;
                         ++j)
                        if (f.topology.light_references[static_cast<std::size_t>(j)] == ref) {
                            ++tiles;
                            break;
                        }
                }
                std::cout << "OWNED_DARK_LIGHT ref=" << ref << " tiles=" << tiles << '\n';
            }
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        AbyssSynthetic();
        {
            auto radial = Synthetic();
            radial.actors.actors[0].location_unreal = {60, 0, 0};
            radial.actors.actors[0].light_effect = 13;
            const auto corrected = Atlas(radial, true, true);
            const auto signed_incidence = Atlas(radial, true, true, false);
            Check(corrected != signed_incidence,
                  "NonIncidence dark light affects a perpendicular wall");
            for (const auto& source : {Atlas(radial, false, false, false),
                                      signed_incidence}) {
                auto upgraded = source;
                const auto repaired = detail::RepairDarkLightmapAtlas(
                    radial.topology, radial.mesh, radial.actors, 2, 2, 512, 512, upgraded);
                Check(repaired.status == Hp1ProfileStatus::ok && upgraded == corrected,
                      "unsigned and signed legacy caches upgrade to radial falloff");
            }
            radial.actors.actors[0].location_unreal = {401, 0, 0};
            Check(Atlas(radial, true, true) == Atlas(radial, true, true, false),
                  "radial dark light never darkens beyond its authored radius");
            radial.actors.actors[0].location_unreal = {60, 0, 0};
            radial.topology.light_bits[0] = radial.topology.light_bits[1] = 0;
            Check(Atlas(radial, true, true) == Atlas(radial, true, true, false),
                  "NonIncidence still respects authored BSP visibility");
        }
        auto f = Synthetic();
        const auto layout = detail::MakeLightmapLayout(f.topology, f.mesh, 2);
        auto old = Atlas(f, false, false);
        const auto expected = Atlas(f, true, true);
        Check(old != expected, "negative contribution changes lightmap");
        auto atlas = old;
        auto result =
            detail::RepairDarkLightmapAtlas(f.topology, f.mesh, f.actors, 2, 2, 512, 512, atlas);
        Check(result.status == Hp1ProfileStatus::ok && result.affected_lightmaps == 1 &&
                  result.changed_texels > 0,
              "one authored dark tile is repaired");
        Check(atlas == expected, "absolute repair equals unclamped signed accumulation, not "
                                 "subtraction from saturated RGB");
        result =
            detail::RepairDarkLightmapAtlas(f.topology, f.mesh, f.actors, 2, 2, 512, 512, atlas);
        Check(result.status == Hp1ProfileStatus::ok && result.changed_texels == 0,
              "repeated repair is exact no-op");
        const auto ambient = detail::encode_linear(0.085F);
        Check(atlas[(std::size_t(layout.placements[0].y) * 512 + layout.placements[0].x) * 4] ==
                  ambient,
              "equal positive and negative light cancel before encoding");
        atlas = old;
        atlas[(std::size_t(layout.placements[0].y) * 512 + layout.placements[0].x) * 4 + 3] = 0;
        const auto corrupt = atlas;
        result =
            detail::RepairDarkLightmapAtlas(f.topology, f.mesh, f.actors, 2, 2, 512, 512, atlas);
        Check(result.status != Hp1ProfileStatus::ok && atlas == corrupt,
              "mismatched cached tile fails without mutation");
        result =
            detail::RepairDarkLightmapAtlas(f.topology, f.mesh, f.actors, 2, 3, 512, 512, atlas);
        Check(result.status != Hp1ProfileStatus::ok && atlas == corrupt,
              "mismatched tile count fails without mutation");
        result =
            detail::RepairDarkLightmapAtlas(f.topology, f.mesh, f.actors, 2, 2, 1024, 512, atlas);
        Check(result.status != Hp1ProfileStatus::ok && atlas == corrupt,
              "mismatched dimensions fail without mutation");
        auto two = Synthetic();
        two.topology.light_maps[1].light_actor_index = 0;
        two.topology.light_maps[1].data_offset = 0;
        auto partial = Atlas(two, false, false);
        partial[(std::size_t(layout.placements[1].y) * 512 + layout.placements[1].x) * 4 + 3] = 0;
        const auto partial_before = partial;
        result = detail::RepairDarkLightmapAtlas(two.topology, two.mesh, two.actors, 2, 2, 512, 512,
                                                 partial);
        Check(result.status != Hp1ProfileStatus::ok && partial == partial_before,
              "late tile failure cannot partially mutate an earlier valid tile");
        f.topology.light_bits[0] = f.topology.light_bits[1] = 0;
        Check(Atlas(f, true, true) == Atlas(f, false, false),
              "occluded dark light contributes nothing");
        f.topology.light_bits.clear();
        result =
            detail::RepairDarkLightmapAtlas(f.topology, f.mesh, f.actors, 2, 2, 512, 512, atlas);
        Check(result.status != Hp1ProfileStatus::ok && atlas == corrupt,
              "truncated visibility fails without mutation");
        if (argc == 3)
            Owned(argv[1], argv[2]);
        std::cout << "DARK_LIGHTMAP_TESTS=PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
