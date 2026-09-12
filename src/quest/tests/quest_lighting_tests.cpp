#include "hpvr/quest_lighting.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using hpvr::quest::AuthoredLight;
using hpvr::quest::PackAuthoredLighting;
using hpvr::quest::PackedLightingLuminance;

namespace {

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

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
    Check(near_light > middle_light + 0.20F, "positive near/middle falloff");
    Check(middle_light > outside_light + 0.04F, "positive middle/outer falloff");
    Check(outside_light < 0.20F, "outside light remains ambient");
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
    Check(blue > red * 2U, "authored blue hue is preserved");
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
    Check(center < 0.75F, "dense positive lights stay bounded");
    Check(center > edge + 0.15F, "dense positive lights retain contrast");
}

void SignedLightsAndMoverTransform() {
    using hpvr::quest::BuildAuthoredDarkLightPush;
    using hpvr::quest::EvaluateAuthoredDarkLightPush;
    const std::array<std::array<float, 3>, 3> identity{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    const std::array<AuthoredLight, 1> lights{{{{0, 0, 0}, {1, 1, 1}, 9, -1, 13}}};
    const auto push = BuildAuthoredDarkLightPush(lights, {0, 0, 0}, {0, 0, 0}, identity);
    Check(PackAuthoredLighting({0, 0, 0}, lights) == 0xFF000000U,
          "dark light subtracts instead of becoming the brightest positive source");
    Check(EvaluateAuthoredDarkLightPush({0, 0, 0}, push)[0] > 1.5F,
          "dark light fills its center");
    Check(EvaluateAuthoredDarkLightPush({0, 9, 0}, push)[0] == 0,
          "dark light ends at the authored radius");
    const float wall = EvaluateAuthoredDarkLightPush({6, 0, 0}, push)[0];
    Check(std::abs(wall - EvaluateAuthoredDarkLightPush({0, 6, 0}, push)[0]) < 1e-6F,
          "NonIncidence is independent of wall orientation");
    const std::array<std::array<float, 3>, 3> rotation{{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}};
    const auto moved = BuildAuthoredDarkLightPush(lights, {0, 0, 0}, {3, 2, 1}, rotation);
    // world (6,0,0) expressed in this translated/rotated brush frame.
    Check(std::abs(wall - EvaluateAuthoredDarkLightPush({-2, -3, -1}, moved)[0]) < 1e-6F,
          "moving and rotating a brush does not move the world's dark-light sphere");
    const auto absent = BuildAuthoredDarkLightPush({}, {}, {}, identity);
    Check(EvaluateAuthoredDarkLightPush({}, absent) == std::array<float, 3>{},
          "zero push data leaves UI and non-movers unchanged");
    std::vector<AuthoredLight> invalid{{{}, {1, 1, 1}, 1, 1, 13},
        {{}, {1, 1, 1}, 0, -1, 13},
        {{}, {1, 1, 1}, 9, std::numeric_limits<float>::quiet_NaN(), 13}};
    Check(BuildAuthoredDarkLightPush(invalid, {}, {}, identity).position_radius == absent.position_radius,
          "positive and invalid sources cannot become shader dark lights");
}

void RetailPitFalloff() {
    using hpvr::quest::BuildAuthoredDarkLightPush;
    using hpvr::quest::EvaluateAuthoredDarkLightPush;
    // Owned Lev_Tut1b Light262/237: UE units, radii18*25, brightness255,
    // effect13. Evaluate exact authored coordinates rather than a fog plane.
    const std::array<AuthoredLight, 2> lights{{
        {{-2687.27F, -7150.41F, 146.553F}, {1, 1, 1}, 450, -1, 13},
        {{-2689.95F, -6950.78F, 146.553F}, {1, 1, 1}, 450, -1, 13}}};
    const std::array<std::array<float, 3>, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    const auto push = BuildAuthoredDarkLightPush(lights, {-2689, -7050, 350}, {}, axes);
    const auto sample = [&](float z) {
        return EvaluateAuthoredDarkLightPush({-2689, -7050, z}, push)[0];
    };
    const float above = sample(650), rim = sample(560), middle = sample(450), bottom = sample(146.553F);
    Check(above == 0 && rim > 0 && middle > rim + .3F && bottom > middle + 1.0F,
          "retail dark lights fill pit height with smooth radial falloff");
    Check(bottom > 2.9F, "two authored dark lights make the lower abyss black");
    std::cout << "RETAIL_PIT_DARKNESS above=" << above << " rim=" << rim
              << " middle=" << middle << " bottom=" << bottom << '\n';
}

}  // namespace

int main() {
    try {
        ProducesVisibleFalloff();
        PreservesAuthoredColor();
        DenseLightsDoNotFlattenEverything();
        SignedLightsAndMoverTransform();
        RetailPitFalloff();
        std::cout << "AUTHORED_LIGHTING_TESTS=PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
