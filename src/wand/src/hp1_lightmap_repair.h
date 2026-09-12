#pragma once
#include "hpvr/hp1_package_linker.h"
#include "hp1_lightmap_baker.h"

namespace hpvr::wand::detail {
inline Hp1LightmapRepair
RepairDarkLightmapAtlas(const Hp1BspTopology &topology, const Hp1BspTriangleMesh &mesh,
                        const Hp1ActorVisualCensus &actors, std::size_t selected_count,
                        std::size_t expected_decoded_lightmaps, std::uint32_t width,
                        std::uint32_t height, std::vector<std::uint8_t> &atlas) {
    Hp1LightmapRepair result;
    try {
        if (topology.status != Hp1ProfileStatus::ok || mesh.status != Hp1ProfileStatus::ok ||
            actors.status != Hp1ProfileStatus::ok || !selected_count)
            throw std::runtime_error("lightmap repair source is invalid");
        const auto layout = MakeLightmapLayout(topology, mesh, selected_count);
        if (width != layout.size || height != layout.size ||
            atlas.size() != std::size_t(width) * height * 4U)
            throw std::runtime_error("cached lightmap atlas layout does not match source");
        const auto count = static_cast<std::size_t>(std::ranges::count_if(
            layout.placements, [](const auto &item) { return item.surface >= 0; }));
        if (count != expected_decoded_lightmaps)
            throw std::runtime_error("cached lightmap tile count does not match source");
        const auto lights = GatherLightSources(actors);
        for (const auto &[reference, source] : lights) {
            (void)reference;
            result.dark_light_actor_count += source.dark ? 1U : 0U;
        }
        const DarkZoneAmbient original_ambient(topology, actors, false);
        const DarkZoneAmbient corrected_ambient(topology, actors, true);
        struct Patch {
            std::size_t index{};
            LightmapPixels pixels;
        };
        std::vector<Patch> patches;
        for (std::size_t index = 0; index < layout.placements.size(); ++index) {
            const auto &placement = layout.placements[index];
            if (placement.surface < 0)
                continue;
            const auto &lm = topology.light_maps[index];
            bool dark = false;
            if (lm.light_actor_index >= 0) {
                auto end = static_cast<std::size_t>(lm.light_actor_index);
                for (;
                     end < topology.light_references.size() && topology.light_references[end] != 0;
                     ++end) {
                    const auto found = lights.find(topology.light_references[end]);
                    dark = dark || (found != lights.end() && found->second.dark);
                }
                if (end >= topology.light_references.size())
                    throw std::runtime_error("lightmap repair reference span is invalid");
            }
            const bool ambient =
                corrected_ambient.Candidate(static_cast<std::size_t>(placement.surface));
            if (!dark && !ambient)
                continue;
            ++result.affected_lightmaps;
            result.dark_light_maps += dark ? 1U : 0U;
            result.ambient_light_maps += ambient ? 1U : 0U;
            auto corrected =
                BakeLightmapTile(topology, lights, corrected_ambient, placement, index, true);
            const auto legacy =
                BakeLightmapTile(topology, lights, original_ambient, placement, index, false, false, false);
            // Accept one quantization unit across host/ARM libm implementations;
            // alpha, rectangle coordinates and all texels still must agree.
            const auto matches = [&](const LightmapPixels &pixels) {
                bool equal = true;
                VisitLightmapTile(lm, placement, pixels, [&](auto x, auto y, const auto &expected) {
                    const auto offset = (std::size_t(y) * width + x) * 4U;
                    for (std::size_t c = 0; c < 4; ++c)
                        equal = equal && std::abs(int(atlas[offset + c]) - int(expected[c])) <=
                                             (c == 3 ? 0 : 1);
                });
                return equal;
            };
            if (!matches(legacy) && !matches(corrected)) {
                // A fresh bake from the interim zero-ambient implementation
                // already fixed ambient but still added bDarkLight positively.
                const auto interim =
                    BakeLightmapTile(topology, lights, corrected_ambient, placement, index, false, false, false);
                const auto signed_incidence =
                    BakeLightmapTile(topology, lights, corrected_ambient, placement, index, true, false, false);
                const auto signed_radial =
                    BakeLightmapTile(topology, lights, corrected_ambient, placement, index, true, true, false);
                if (!matches(interim) && !matches(signed_incidence) && !matches(signed_radial))
                    throw std::runtime_error("cached lightmap tile differs from source bake: " +
                                             std::to_string(index));
            }
            VisitLightmapTile(lm, placement, corrected, [&](auto x, auto y, const auto &pixel) {
                const auto offset = (std::size_t(y) * width + x) * 4U;
                if (!std::equal(pixel.begin(), pixel.end(),
                                atlas.begin() + static_cast<std::ptrdiff_t>(offset)))
                    ++result.changed_texels;
            });
            result.staged_bytes += corrected.size() * sizeof(corrected[0]);
            patches.push_back({index, std::move(corrected)});
        }
        // All allocations, source sampling and cache checks precede mutation.
        for (const auto &patch : patches)
            VisitLightmapTile(topology.light_maps[patch.index], layout.placements[patch.index],
                              patch.pixels, [&](auto x, auto y, const auto &pixel) {
                                  const auto offset = (std::size_t(y) * width + x) * 4U;
                                  std::copy(pixel.begin(), pixel.end(),
                                            atlas.begin() + static_cast<std::ptrdiff_t>(offset));
                              });
        result.status = Hp1ProfileStatus::ok;
    } catch (const std::exception &exception) {
        result.error = exception.what();
        result.changed_texels = 0;
    }
    return result;
}
} // namespace hpvr::wand::detail
