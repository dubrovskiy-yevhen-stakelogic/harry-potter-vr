#include "hpvr/hp1_gesture.h"
#include "hpvr/hp1_package_linker.h"
#include "hpvr/quest_lighting.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <vector>

namespace {
constexpr float kMetersPerUnrealUnit = 0.02F;
constexpr float kTau = 6.28318530717958647692F;

std::array<float, 3> RotateYaw(const std::array<float, 3>& value,
                               const float yaw) {
    const float cosine = std::cos(yaw);
    const float sine = std::sin(yaw);
    return {cosine * value[0] + sine * value[2], value[1],
            -sine * value[0] + cosine * value[2]};
}

std::array<float, 3> HsvLightColor(const std::uint8_t hue,
                                   const std::uint8_t saturation) {
    const float h = static_cast<float>(hue) * 6.0F / 255.0F;
    const float s = 1.0F - static_cast<float>(saturation) / 255.0F;
    const float chroma = s;
    const float x = chroma *
        (1.0F - std::abs(std::fmod(h, 2.0F) - 1.0F));
    std::array<float, 3> rgb{};
    if (h < 1.0F) rgb = {chroma, x, 0.0F};
    else if (h < 2.0F) rgb = {x, chroma, 0.0F};
    else if (h < 3.0F) rgb = {0.0F, chroma, x};
    else if (h < 4.0F) rgb = {0.0F, x, chroma};
    else if (h < 5.0F) rgb = {x, 0.0F, chroma};
    else rgb = {chroma, 0.0F, x};
    const float match = 1.0F - chroma;
    for (float& component : rgb) component += match;
    return rgb;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: hpvr_quest_world_lighting_probe "
                     "<data-root> <map>\n";
        return EXIT_FAILURE;
    }
    const std::filesystem::path root(argv[1]);
    const std::filesystem::path map(argv[2]);
    const auto starts = hpvr::wand::inspect_hp1_player_starts(map);
    const auto actors = hpvr::wand::inspect_hp1_actor_visuals(map);
    const auto scene = hpvr::wand::build_hp1_textured_bsp_scene(
        root, map, kMetersPerUnrealUnit,
        std::numeric_limits<std::uint32_t>::max());
    if (starts.status != hpvr::wand::Hp1ProfileStatus::ok ||
        starts.player_starts.empty() ||
        actors.status != hpvr::wand::Hp1ProfileStatus::ok ||
        scene.status != hpvr::wand::Hp1ProfileStatus::ok ||
        scene.vertices.empty()) {
        std::cerr << "lighting_probe_status=load_error\n";
        return EXIT_FAILURE;
    }
    const auto& start = starts.player_starts.front();
    const std::array<float, 3> start_position{
        start.location_unreal.y * kMetersPerUnrealUnit,
        start.location_unreal.z * kMetersPerUnrealUnit,
        -start.location_unreal.x * kMetersPerUnrealUnit};
    const float yaw = static_cast<float>(start.rotation_units[1]) * kTau /
                      65536.0F;
    std::vector<hpvr::quest::AuthoredLight> lights;
    for (const auto& actor : actors.actors) {
        if (!actor.location_serialized ||
            actor.qualified_class_name != "Engine.Light" ||
            actor.light_brightness == 0 || actor.light_radius == 0) continue;
        const std::array<float, 3> position{
            actor.location_unreal.y * kMetersPerUnrealUnit,
            actor.location_unreal.z * kMetersPerUnrealUnit,
            -actor.location_unreal.x * kMetersPerUnrealUnit};
        lights.push_back({
            RotateYaw({position[0] - start_position[0],
                       position[1] - start_position[1],
                       position[2] - start_position[2]}, yaw),
            HsvLightColor(actor.light_hue, actor.light_saturation),
            static_cast<float>(actor.light_radius) * 25.0F *
                kMetersPerUnrealUnit,
            static_cast<float>(actor.light_brightness) / 255.0F});
    }
    float minimum = std::numeric_limits<float>::infinity();
    float maximum = 0.0F;
    double sum = 0.0;
    std::size_t shadows = 0;
    std::size_t highlights = 0;
    for (const auto& vertex : scene.vertices) {
        const auto local = RotateYaw(
            {vertex.position_m.x - start_position[0],
             vertex.position_m.y - start_position[1],
             vertex.position_m.z - start_position[2]}, yaw);
        const float luminance = hpvr::quest::PackedLightingLuminance(
            hpvr::quest::PackAuthoredLighting(local, lights));
        minimum = std::min(minimum, luminance);
        maximum = std::max(maximum, luminance);
        sum += luminance;
        if (luminance < 0.32F) ++shadows;
        if (luminance > 0.78F) ++highlights;
    }
    const float average = static_cast<float>(
        sum / static_cast<double>(scene.vertices.size()));
    const bool contrast = minimum < 0.25F && maximum > 0.60F &&
                          maximum - minimum > 0.40F && shadows > 0 &&
                          highlights > 0;
    std::cout << "lighting_probe_status="
              << (contrast ? "ok" : "flat")
              << " vertices=" << scene.vertices.size()
              << " lights=" << lights.size()
              << " min=" << minimum
              << " average=" << average
              << " max=" << maximum
              << " shadows=" << shadows
              << " highlights=" << highlights << '\n';
    return contrast ? EXIT_SUCCESS : EXIT_FAILURE;
}
