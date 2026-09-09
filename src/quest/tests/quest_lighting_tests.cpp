#include "hpvr/quest_lighting.h"

#include <array>
#include <cassert>
#include <vector>

using hpvr::quest::AuthoredLight;
using hpvr::quest::PackAuthoredLighting;
using hpvr::quest::PackedLightingLuminance;

namespace {

void ProducesVisibleFalloff() {
    const std::vector<AuthoredLight> lights{
        AuthoredLight{{0.0F, 0.0F, 0.0F},
                      {1.0F, 0.35F, 0.12F}, 10.0F, 1.0F},
    };
    const float near_light = PackedLightingLuminance(
        PackAuthoredLighting({0.0F, 0.0F, 0.0F}, lights));
    const float middle_light = PackedLightingLuminance(
        PackAuthoredLighting({5.0F, 0.0F, 0.0F}, lights));
    const float outside_light = PackedLightingLuminance(
        PackAuthoredLighting({11.0F, 0.0F, 0.0F}, lights));
    assert(near_light > middle_light + 0.20F);
    assert(middle_light > outside_light + 0.04F);
    assert(outside_light < 0.20F);
}

void PreservesAuthoredColor() {
    const std::vector<AuthoredLight> lights{
        AuthoredLight{{0.0F, 0.0F, 0.0F},
                      {0.05F, 0.10F, 1.0F}, 8.0F, 1.0F},
    };
    const std::uint32_t packed =
        PackAuthoredLighting({0.0F, 0.0F, 0.0F}, lights);
    const auto red = packed & 0xFFU;
    const auto blue = (packed >> 16U) & 0xFFU;
    assert(blue > red * 2U);
}

void DenseLightsDoNotFlattenEverything() {
    std::vector<AuthoredLight> lights;
    for (int index = 0; index < 128; ++index) {
        lights.push_back({{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F},
                          10.0F, 0.20F});
    }
    const float center = PackedLightingLuminance(
        PackAuthoredLighting({0.0F, 0.0F, 0.0F}, lights));
    const float edge = PackedLightingLuminance(
        PackAuthoredLighting({9.0F, 0.0F, 0.0F}, lights));
    assert(center < 0.75F);
    assert(center > edge + 0.15F);
}

}  // namespace

int main() {
    ProducesVisibleFalloff();
    PreservesAuthoredColor();
    DenseLightsDoNotFlattenEverything();
    return 0;
}
