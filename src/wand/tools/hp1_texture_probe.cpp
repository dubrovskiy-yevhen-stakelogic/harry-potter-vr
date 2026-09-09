#include "hpvr/hp1_gesture.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
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
    if (argc != 3) {
        std::cerr
            << "usage: hpvr_hp1_texture_probe <texture-package> <export-ref>\n";
        return EXIT_FAILURE;
    }
    std::int32_t reference{};
    try {
        const auto parsed = std::stoll(argv[2]);
        if (parsed < 1 ||
            parsed > std::numeric_limits<std::int32_t>::max()) {
            throw std::out_of_range("export reference");
        }
        reference = static_cast<std::int32_t>(parsed);
    } catch (const std::exception&) {
        std::cerr << "invalid positive export reference\n";
        return EXIT_FAILURE;
    }

    const auto texture = hpvr::wand::load_hp1_p8_texture(
        std::filesystem::path(argv[1]), reference);
    if (texture.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "texture_status=" << static_cast<int>(texture.status)
                  << " error=" << texture.error << '\n';
        return EXIT_FAILURE;
    }
    std::uint8_t alpha_min = 255;
    std::uint8_t alpha_max = 0;
    std::size_t alpha_zero_count = 0;
    for (std::size_t index = 3; index < texture.rgba8.size(); index += 4) {
        const auto alpha = texture.rgba8[index];
        alpha_min = std::min(alpha_min, alpha);
        alpha_max = std::max(alpha_max, alpha);
        alpha_zero_count += alpha == 0 ? 1U : 0U;
    }
    const auto& top = texture.mips.front();
    std::cout << "texture_status=ok"
              << " package=" << argv[1]
              << " version=" << texture.package_version
              << " texture_ref=" << texture.texture_reference
              << " object=" << texture.object_name
              << " palette_ref=" << texture.palette_reference
              << " format=" << static_cast<unsigned>(texture.format)
              << " format_serialized=" << texture.format_serialized
              << " compressed_mips="
              << texture.compressed_mips_serialized
              << " mips=" << texture.mips.size()
              << " top=" << top.width << 'x' << top.height
              << " rgba_bytes=" << texture.rgba8.size()
              << " rgba_fnv1a64=" << std::hex << fnv1a64(texture.rgba8)
              << std::dec
              << " alpha_min=" << static_cast<unsigned>(alpha_min)
              << " alpha_max=" << static_cast<unsigned>(alpha_max)
              << " alpha_zero=" << alpha_zero_count << '\n';
    for (std::size_t index = 0; index < texture.mips.size(); ++index) {
        const auto& mip = texture.mips[index];
        std::cout << "mip=" << index
                  << " size=" << mip.width << 'x' << mip.height
                  << " bits=" << static_cast<unsigned>(mip.width_bits)
                  << ',' << static_cast<unsigned>(mip.height_bits)
                  << " indexed_bytes=" << mip.indexed_bytes << '\n';
    }
    return EXIT_SUCCESS;
}
