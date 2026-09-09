#include "hpvr/hp1_gesture.h"
#include "hpvr/hp1_package_linker.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace {
std::string fold(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return result;
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: hpvr_hp1_world_probe <data-root> <map>\n";
        return EXIT_FAILURE;
    }
    const std::filesystem::path root(argv[1]);
    const std::filesystem::path map_path(argv[2]);
    const auto actors = hpvr::wand::inspect_hp1_actor_visuals(map_path);
    const auto linked = hpvr::wand::link_hp1_package_graph(root, map_path);
    if (actors.status != hpvr::wand::Hp1ProfileStatus::ok ||
        linked.status != hpvr::wand::Hp1PackageLinkStatus::ok) {
        std::cerr << "world_probe_status=error actor_error=" << actors.error
                  << " link_error=" << linked.error << '\n';
        return EXIT_FAILURE;
    }
    std::set<std::string> classes;
    std::map<std::string, std::size_t> tags;
    std::size_t events = 0;
    std::size_t edges = 0;
    std::size_t lights = 0;
    std::size_t triggers = 0;
    std::size_t ambient_actors = 0;
    std::size_t decoded_ambient = 0;
    for (const auto& actor : actors.actors) {
        classes.insert(fold(actor.qualified_class_name));
        if (actor.tag_serialized && fold(actor.tag) != "none") ++tags[fold(actor.tag)];
        if (actor.event_serialized && fold(actor.event) != "none") ++events;
        if (fold(actor.qualified_class_name) == "engine.light" &&
            actor.location_serialized && actor.light_brightness > 0 &&
            actor.light_radius > 0) ++lights;
        if (actor.event_serialized && actor.collision_radius > 0.0F) ++triggers;
        if (!actor.ambient_sound_serialized ||
            actor.ambient_sound_reference >= 0) continue;
        ++ambient_actors;
        const auto resolved = std::ranges::find_if(
            linked.imports, [&actor, &map_path](const auto& import) {
                return fold(import.source_package) == fold(map_path.stem().string()) &&
                       import.source_reference == actor.ambient_sound_reference &&
                       import.target_kind == hpvr::wand::Hp1ImportTargetKind::export_object;
            });
        if (resolved == linked.imports.end()) continue;
        const auto package = std::ranges::find_if(
            linked.graph.packages, [&resolved](const auto& item) {
                return fold(item.package_name) == fold(resolved->target_package);
            });
        if (package == linked.graph.packages.end()) continue;
        const auto pcm = hpvr::wand::load_hp1_pcm_sound(
            package->path, resolved->target_reference);
        if (pcm.status == hpvr::wand::Hp1ProfileStatus::ok) {
            ++decoded_ambient;
            std::cout << "ambient actor=" << actor.object_name
                      << " package=" << package->package_name
                      << " sound=" << pcm.object_name
                      << " rate=" << pcm.sample_rate
                      << " samples=" << pcm.samples.size() << '\n';
        }
    }
    const auto fallback_census = hpvr::wand::inspect_hp1_sound_assets(
        root / "Sounds/Ambient.uax");
    bool fallback_pcm = false;
    for (const auto& sound : fallback_census.sounds) {
        if (fold(sound.object_name) != "s_fire_loop") continue;
        const auto pcm = hpvr::wand::load_hp1_pcm_sound(
            root / "Sounds/Ambient.uax", sound.sound_reference);
        fallback_pcm = pcm.status == hpvr::wand::Hp1ProfileStatus::ok;
        std::cout << "ambient_fallback sound=" << sound.object_name
                  << " status=" << (fallback_pcm ? "ok" : "error")
                  << " rate=" << pcm.sample_rate
                  << " samples=" << pcm.samples.size() << '\n';
    }
    for (const auto& actor : actors.actors) {
        if (!actor.event_serialized) continue;
        const auto found = tags.find(fold(actor.event));
        if (found != tags.end()) edges += found->second;
    }
    std::cout << "world_probe_status=ok actors=" << actors.actors.size()
              << " classes=" << classes.size()
              << " tags=" << tags.size()
              << " events=" << events
              << " event_edges=" << edges
              << " triggers=" << triggers
              << " lights=" << lights
              << " ambient_actors=" << ambient_actors
              << " decoded_stream_wrappers=" << decoded_ambient
              << " fallback_pcm=" << fallback_pcm << '\n';
    return ambient_actors > 0 && fallback_pcm ? EXIT_SUCCESS : EXIT_FAILURE;
}
