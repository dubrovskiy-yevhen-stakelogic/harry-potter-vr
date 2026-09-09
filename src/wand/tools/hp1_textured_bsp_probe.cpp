#include "hpvr/hp1_package_linker.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

namespace {

[[nodiscard]] std::uint64_t fnv1a64(
    const std::vector<std::uint8_t>& bytes) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5 && argc != 6) {
        std::cerr << "usage: hpvr_hp1_textured_bsp_probe "
                     "<data-root> <map-package> <meters-per-unit> "
                     "<max-triangles>\n";
        return EXIT_FAILURE;
    }
    try {
        const auto scale = std::stof(argv[3]);
        const auto parsed_limit = std::stoull(argv[4]);
        if (parsed_limit > std::numeric_limits<std::uint32_t>::max()) {
            throw std::out_of_range("triangle limit exceeds uint32");
        }
        const auto scene = hpvr::wand::build_hp1_textured_bsp_scene(
            std::filesystem::path(argv[1]),
            std::filesystem::path(argv[2]),
            scale,
            static_cast<std::uint32_t>(parsed_limit),
            argc == 6 ? std::stoi(argv[5]) : 0);
        if (scene.status != hpvr::wand::Hp1ProfileStatus::ok) {
            std::cerr << "textured_bsp_status="
                      << static_cast<int>(scene.status)
                      << " error=" << scene.error << '\n';
            return EXIT_FAILURE;
        }
        std::uint8_t lightmap_min = 255;
        std::uint8_t lightmap_max = 0;
        std::uint64_t lightmap_sum = 0;
        std::size_t lightmap_samples = 0;
        for (std::size_t offset = 0;
             offset + 3 < scene.lightmap_rgba8.size(); offset += 4) {
            if (scene.lightmap_rgba8[offset + 3] == 0) continue;
            const auto luminance = static_cast<std::uint8_t>(
                (static_cast<std::uint32_t>(scene.lightmap_rgba8[offset]) *
                     54U +
                 static_cast<std::uint32_t>(scene.lightmap_rgba8[offset + 1]) *
                     183U +
                 static_cast<std::uint32_t>(scene.lightmap_rgba8[offset + 2]) *
                     19U) /
                256U);
            lightmap_min = std::min(lightmap_min, luminance);
            lightmap_max = std::max(lightmap_max, luminance);
            lightmap_sum += luminance;
            ++lightmap_samples;
        }
        std::cout << "textured_bsp_status=ok"
                  << " available_triangles=" << scene.available_triangle_count
                  << " selected_triangles=" << scene.selected_triangle_count
                  << " omitted_triangles=" << scene.omitted_triangle_count
                  << " vertices=" << scene.vertices.size()
                  << " layers=" << scene.texture_layer_count
                  << " layer_size=" << scene.texture_layer_width << 'x'
                  << scene.texture_layer_height
                  << " rgba_bytes=" << scene.texture_rgba8.size()
                  << " rgba_fnv1a64=" << std::hex
                  << fnv1a64(scene.texture_rgba8) << std::dec
                  << " lightmap_atlas=" << scene.lightmap_width << 'x'
                  << scene.lightmap_height
                  << " lightmap_bytes=" << scene.lightmap_rgba8.size()
                  << " lightmap_fnv1a64=" << std::hex
                  << fnv1a64(scene.lightmap_rgba8) << std::dec
                  << " decoded_lightmaps=" << scene.decoded_lightmap_count
                  << " lightmap_texels=" << scene.lightmap_texel_count
                  << " lightmap_lights=" << scene.lightmap_light_count
                  << " lightmap_luminance="
                  << static_cast<unsigned>(lightmap_min) << ':'
                  << (lightmap_samples == 0
                          ? 0
                          : lightmap_sum / lightmap_samples)
                  << ':' << static_cast<unsigned>(lightmap_max)
                  << " lightmap_samples=" << lightmap_samples
                  << " decoded_textures=" << scene.decoded_texture_count
                  << " fallback_materials=" << scene.fallback_material_count
                  << " fallback_triangles=" << scene.fallback_triangle_count
                  << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "argument_error=" << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
