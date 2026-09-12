#include "../src/hp1_zone_ambient.h"

#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
using namespace hpvr::wand;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

Hp1ActorVisualCensus Actors() {
    Hp1ActorVisualCensus actors;
    actors.status = Hp1ProfileStatus::ok;
    Hp1ActorVisual zone;
    zone.actor_reference = 42;
    zone.qualified_class_name = "Engine.ZoneInfo";
    Hp1ClassDefaultProperty kill;
    kill.name = "bKillZone";
    kill.boolean_value = kill.boolean_value_serialized = true;
    zone.serialized_properties.push_back(kill);
    actors.actors.push_back(zone);
    return actors;
}

Hp1BspTopology Topology() {
    Hp1BspTopology topology;
    topology.status = Hp1ProfileStatus::ok;
    topology.zone_count = 3;
    topology.zone_actor_references = {0, 0, 42};
    topology.surfaces.resize(2);
    Hp1BspNode root;
    root.plane = {1, 0, 0, 0};
    root.front_node_index = root.back_node_index = -1;
    root.zone_indices = {2, 1};
    root.vertex_count = 3;
    topology.nodes.push_back(root);
    // One shared lightmap has safe and dark polygon fragments.
    auto fragment = root;
    fragment.zone_indices = {0, 2};
    topology.nodes.push_back(fragment);
    fragment.surface_index = 1;
    fragment.zone_indices = {0, 1};
    topology.nodes.push_back(fragment);
    return topology;
}
}

int main(int argc, char** argv) {
    try {
        auto topology = Topology();
        auto actors = Actors();
        detail::DarkZoneAmbient policy(topology, actors, true);
        Check(policy.Candidate(0) && !policy.Candidate(1), "only surfaces touching authored dark zones are candidates");
        Check(policy.Suppress(0, {-10, 0, 0}, {0, 0, 1}), "zero-ambient kill-zone texel suppresses fallback");
        Check(!policy.Suppress(0, {10, 0, 0}, {0, 0, 1}), "safe texel of SAME shared lightmap remains unchanged");
        Check(!policy.Suppress(1, {-10, 0, 0}, {0, 0, 1}), "unrelated surface stays unchanged");
        Check(!policy.Suppress(0, {-10, 0, 0}, {std::numeric_limits<float>::quiet_NaN(), 0, 1}), "nonfinite sample fails closed");
        Check(!detail::DarkZoneAmbient(topology, actors, false).Candidate(0), "map-zero and mover gating disables policy");
        Hp1ClassDefaultProperty brightness;
        brightness.name = "AmbientBrightness";
        brightness.kind = 1;
        brightness.value = {20};
        actors.actors[0].serialized_properties.push_back(brightness);
        Check(!detail::DarkZoneAmbient(topology, actors, true).Candidate(0), "authored nonzero ambient is preserved");
        actors.actors[0].serialized_properties.back().value = {0};
        Check(detail::DarkZoneAmbient(topology, actors, true).Candidate(0), "explicit zero ambient is respected");
        actors.actors[0].serialized_properties.back().value.clear();
        Check(!detail::DarkZoneAmbient(topology, actors, true).Candidate(0), "malformed ambient fails closed");
        actors = Actors();
        actors.actors[0].serialized_properties[0].boolean_value = false;
        Check(!detail::DarkZoneAmbient(topology, actors, true).Candidate(0), "nonlethal zone is unchanged");
        actors = Actors();
        topology.nodes[0].back_node_index = 0;
        Check(!detail::DarkZoneAmbient(topology, actors, true).Suppress(0, {-10, 0, 0}, {0, 0, 1}), "cyclic BSP traversal is bounded");
        if (argc > 1) {
            Hp1PackageReadScope reads;
            const std::filesystem::path root = argv[1];
            for (const auto map : {"Lev_Tut1.unr", "Lev_Tut1b.unr"}) {
                const auto path = root / "Maps" / map;
                const auto owned = load_hp1_bsp_topology(path);
                const auto census = inspect_hp1_actor_visuals(path);
                Check(owned.status == Hp1ProfileStatus::ok && census.status == Hp1ProfileStatus::ok, "owned topology and actors load");
                const detail::DarkZoneAmbient masks(owned, census, true);
                std::size_t candidates = 0, dark_samples = 0, safe_shared_samples = 0;
                for (std::size_t surface = 0; surface < owned.surfaces.size(); ++surface)
                    candidates += masks.Candidate(surface);
                for (const auto& node : owned.nodes) {
                    if (node.vertex_count < 3 || !masks.Candidate(static_cast<std::size_t>(node.surface_index))) continue;
                    Hp1BspVector center{};
                    for (unsigned i = 0; i < node.vertex_count; ++i) {
                        const auto point = owned.points[static_cast<std::size_t>(owned.vertices[static_cast<std::size_t>(node.vertex_pool_index) + i].point_index)];
                        center.x += point.x / node.vertex_count;
                        center.y += point.y / node.vertex_count;
                        center.z += point.z / node.vertex_count;
                    }
                    auto normal = owned.vectors[static_cast<std::size_t>(owned.surfaces[static_cast<std::size_t>(node.surface_index)].normal_vector_index)];
                    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                    Check(length > 0, "owned lightmap surface normal is valid");
                    normal.x /= length; normal.y /= length; normal.z /= length;
                    const bool dark = masks.Suppress(static_cast<std::size_t>(node.surface_index), center, normal);
                    const auto zone_actor = owned.zone_actor_references[node.zone_indices[1]];
                    Check(dark == (zone_actor == 2195 || zone_actor == 2347), "visible-side point classification matches authored node zone");
                    dark_samples += dark;
                    safe_shared_samples += !dark;
                }
                if (std::string_view(map) == "Lev_Tut1.unr")
                    Check(candidates == 0 && dark_samples == 0, "accepted first map has zero changed samples even without map-name gate");
                else
                    Check(candidates == 66 && dark_samples == 82 && safe_shared_samples > 0, "only authored abyss fragments change, including shared lightmaps");
                std::cout << "OWNED_DARK_ZONE map=" << map << " candidates=" << candidates
                          << " dark=" << dark_samples << " preserved_shared=" << safe_shared_samples << '\n';
            }
        }
        std::cout << "DARK_ZONE_AMBIENT_TESTS=PASS\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
