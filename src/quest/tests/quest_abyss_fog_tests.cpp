#include "hpvr/quest_abyss_fog.h"
#include "hpvr/hp1_abyss_lighting.h"
#include <iostream>
#include <cstdint>
#include <limits>
#include <stdexcept>
using namespace hpvr::quest;
void Check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }

namespace {
// Scalar mirror of abyss_fog.h's multiply-based slab clipping. The original
// divide-based CPU integral remains unchanged as an independent reference.
float FastVisibility(const AbyssFogVolume& volume, const std::array<float, 3>& eye,
                     const std::array<float, 3>& surface) {
    if (!volume.count || (eye[2] >= volume.top && surface[2] >= volume.top)) return 1;
    std::array<float, 3> delta{};
    for (unsigned axis = 0; axis < 3; ++axis) delta[axis] = surface[axis] - eye[axis];
    std::array<bool, 2> parallel{};
    std::array<float, 2> inverse{};
    for (unsigned axis = 0; axis < 2; ++axis) {
        parallel[axis] = std::abs(delta[axis]) < .000001F;
        inverse[axis] = 1 / (parallel[axis] ? 1 : delta[axis]);
    }
    const auto clip = [&](const std::array<float, 4>& r, float global_enter, float global_leave,
                          float& enter, float& leave) {
        for (unsigned axis = 0; axis < 2; ++axis)
            if (parallel[axis] && (eye[axis] < r[axis] || eye[axis] >= r[axis + 2])) return false;
        std::array<float, 2> near_t{}, far_t{};
        for (unsigned axis = 0; axis < 2; ++axis) {
            const float a = (r[axis] - eye[axis]) * inverse[axis];
            const float b = (r[axis + 2] - eye[axis]) * inverse[axis];
            near_t[axis] = parallel[axis] ? global_enter : std::min(a, b);
            far_t[axis] = parallel[axis] ? global_leave : std::max(a, b);
        }
        enter = std::max(global_enter, std::max(near_t[0], near_t[1]));
        leave = std::min(global_leave, std::min(far_t[0], far_t[1]));
        return leave > enter;
    };
    float global_enter = 0, global_leave = 1;
    if (!clip(volume.bounds, 0, 1, global_enter, global_leave)) return 1;
    if (std::abs(delta[2]) < .000001F) { if (eye[2] >= volume.top) return 1; }
    else {
        const float boundary = (volume.top - eye[2]) / delta[2];
        if (delta[2] > 0) global_leave = std::min(global_leave, boundary);
        else global_enter = std::max(global_enter, boundary);
    }
    if (global_leave <= global_enter) return 1;
    const float distance = std::sqrt(delta[0]*delta[0] + delta[1]*delta[1] + delta[2]*delta[2]);
    if (distance < .000001F) return 1;
    const float depth_scale = distance * .5F * volume.density;
    float optical_depth = 0;
    for (unsigned i = 0; i < volume.count; ++i) {
        float enter = 0, leave = 0;
        if (!clip(volume.rectangles[i], global_enter, global_leave, enter, leave)) continue;
        const float d0 = std::max(0.F, volume.top - eye[2] - delta[2] * enter);
        const float d1 = std::max(0.F, volume.top - eye[2] - delta[2] * leave);
        optical_depth += (leave - enter) * (d0 + d1) * depth_scale;
        if (optical_depth >= 12) return 0;
    }
    return std::exp(-optical_depth);
}

void CheckFastVisibility(const AbyssFogVolume& volume, const char* name) {
    unsigned rays = 0;
    float maximum_error = 0;
    const auto compare = [&](const std::array<float, 3>& eye, const std::array<float, 3>& surface) {
        const float reference = AbyssFogTransmittance(volume, eye, surface);
        const float optimized = FastVisibility(volume, eye, surface);
        const float error = std::abs(reference - optimized);
        maximum_error = std::max(maximum_error, error);
        ++rays;
        Check(std::isfinite(optimized) && optimized >= 0 && optimized <= 1,
              "optimized fog transmittance finite and bounded");
        if (error > .00003F) {
            std::cerr << "FOG_DIFFERENTIAL_MISMATCH volume=" << name << " reference=" << reference
                      << " optimized=" << optimized << " eye=" << eye[0] << ',' << eye[1] << ',' << eye[2]
                      << " surface=" << surface[0] << ',' << surface[1] << ',' << surface[2] << '\n';
            throw std::runtime_error("optimized fog differs from reference beyond 3e-5 absolute visibility");
        }
    };
    std::uint32_t seed = 0x715A13E5U;
    const auto unit = [&]() { seed = seed * 1664525U + 1013904223U; return float(seed >> 8) / 16777216.F; };
    const auto point = [&]() {
        return std::array{
            volume.bounds[0] + (unit()*2 - .5F) * (volume.bounds[2] - volume.bounds[0]),
            volume.bounds[1] + (unit()*2 - .5F) * (volume.bounds[3] - volume.bounds[1]),
            volume.top + (unit()*2 - 1) * 20};
    };
    for (unsigned i = 0; i < 200000; ++i) {
        auto eye = point(), surface = point();
        switch (i % 10) {
        case 0: surface[0] = eye[0]; break;
        case 1: surface[1] = eye[1]; break;
        case 2: surface[2] = eye[2]; break;
        case 3: surface[0] = eye[0]; surface[1] = eye[1]; break;
        case 4: surface = eye; break;
        case 5: surface[2] = volume.top; break;
        case 6: surface[2] = std::nextafter(volume.top, -std::numeric_limits<float>::infinity()); break;
        case 7: surface[0] = eye[0] + .0000005F; break;
        case 8: surface[1] = eye[1] - .0000015F; break;
        default: break;
        }
        compare(eye, surface);
    }
    // Exact and one-ULP portal edges, parallel/near-parallel rays, above/below
    // rim, camera inside, and both eye offsets. Shared seams stay half-open.
    for (unsigned i = 0; i < volume.count; ++i) for (unsigned axis = 0; axis < 2; ++axis) {
        const auto& r = volume.rectangles[i];
        for (unsigned edge : {axis, axis + 2}) for (float side : {-1.F, 0.F, 1.F}) {
            const float coordinate = side == 0 ? r[edge] : std::nextafter(r[edge], side > 0 ?
                std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity());
            for (float height : {-10.F, -.01F, 0.F, .01F, 2.F})
                for (float drift : {-.0000015F, -.0000005F, 0.F, .0000005F, .0000015F}) {
                    std::array eye{(r[0]+r[2])*.5F, (r[1]+r[3])*.5F, volume.top+height};
                    eye[axis] = coordinate;
                    auto surface = eye;
                    surface[axis] += drift;
                    surface[1-axis] = r[3-axis] + 4;
                    surface[2] = volume.top - 5;
                    compare(eye, surface);
                    compare(surface, eye);
                    eye[0] -= .032F; compare(eye, surface);
                    eye[0] += .064F; compare(eye, surface);
                }
        }
    }
    // The original exact-zero cutoff is retained; its discontinuity at depth
    // 12 is only 6.15e-6 visibility, also covered by the absolute error bound.
    const auto& r = volume.rectangles[0];
    const float distance = (r[3] - r[1]) * .5F;
    for (float depth : {0.F, .00001F, .1F, 1.F, 8.F, 11.99999F, 12.F, 12.00001F, 15.F}) {
        const float height = volume.top - depth / (distance * volume.density);
        compare({(r[0]+r[2])*.5F, r[1]+distance*.5F, height},
                {(r[0]+r[2])*.5F, r[1]+distance*1.5F, height});
    }
    std::cout << "FOG_DIFFERENTIAL volume=" << name << " rays=" << rays
              << " max_absolute_error=" << maximum_error << " limit=3e-5\n";
}
} // namespace

int main(int argc, char** argv) {
 try {
    const std::array rectangles{std::array{0.F, 0.F, 4.F, 10.F}, std::array{6.F, 0.F, 10.F, 10.F}};
    const auto volume = MakeAbyssFogVolume(rectangles, 10, .55F);
    CheckFastVisibility(volume, "holes");
    std::array<std::array<float, 4>, 12> grid{};
    for (unsigned i = 0; i < grid.size(); ++i) {
        const float x = float(i % 4)*4, y = float(i / 4)*4;
        grid[i] = {x,y,x+3,y+3};
    }
    CheckFastVisibility(MakeAbyssFogVolume(grid, 20.16F, .55F), "twelve-portals");
    Check(volume.count == 2, "disjoint portal rectangles");
    const std::array overlapping{std::array{0.F, 0.F, 4.F, 10.F}, std::array{3.F, 0.F, 10.F, 10.F}};
    Check(MakeAbyssFogVolume(overlapping, 10, .55F).count == 0, "overlapping volume cannot double density");
    Check(MakeAbyssFogVolume(rectangles, NAN, .55F).count == 0, "invalid volume rejected");
    Check(AbyssFogTransmittance(volume, {NAN, 0, 12}, {2, 9, 7}) == 1, "invalid ray finite");
    Check(AbyssFogTransmittance(volume, {2, 0, 12}, {2, 9, 10}) == 1, "rim and higher geometry unaffected");
    Check(AbyssFogTransmittance(volume, {2, 0, 12}, {2, 9, 7}) < .02F, "hanging base darkens from fog path");
    Check(AbyssFogTransmittance(volume, {2, 0, 12}, {2, 11, 3}) < .01F, "wall outside footprint still behind fog");
    Check(AbyssFogTransmittance(volume, {5, 1, 12}, {5, 8, 2}) == 1, "safe island empty ray preserved");
    Check(AbyssFogTransmittance(volume, {-2, 1, 12}, {-2, 8, 2}) == 1, "outside ray preserved");
    Check(AbyssFogTransmittance({}, {2, 1, 12}, {2, 8, 2}) == 1, "main map disabled");
    Check(AbyssFogTransmittance(volume, {2, 1, 4}, {2, 1, 4}) == 1, "zero ray finite");
    Check(AbyssFogTransmittance(volume, {2, 1, 4}, {2, 8, 4}) < .0001F, "below-rim horizontal ray finite");
    Check(std::abs(AbyssFogTransmittance(volume, {2, 1, 4}, {2, 8, 12}) -
                   AbyssFogTransmittance(volume, {2, 8, 12}, {2, 1, 4})) < .00001F,
          "path integral symmetric with camera above or below fog");
    const std::array split{std::array{0.F, 0.F, 2.F, 10.F}, std::array{2.F, 0.F, 4.F, 10.F}};
    const std::array<std::array<float, 4>, 1> merged{{{0.F, 0.F, 4.F, 10.F}}};
    CheckFastVisibility(MakeAbyssFogVolume(split, 10, .55F), "shared-seam");
    Check(std::abs(AbyssFogTransmittance(MakeAbyssFogVolume(split, 10, .55F), {2, 0, 12}, {2, 9, 7}) -
                   AbyssFogTransmittance(MakeAbyssFogVolume(merged, 10, .55F), {2, 0, 12}, {2, 9, 7})) < .00001F,
          "shared BSP seam not counted twice");
    const auto multiply = [](const Matrix4& matrix, const std::array<float, 4>& point) {
        std::array<float, 4> result{};
        for (unsigned row = 0; row < 4; ++row) for (unsigned column = 0; column < 4; ++column)
            result[row] += matrix[column * 4 + row] * point[column];
        return result;
    };
    const float c = std::cos(.7F), s = std::sin(.7F);
    const Matrix4 source_transform{-s,c,0,0, 0,0,1,0, -c,-s,0,0, -40,20,1,1};
    for (unsigned eye_index = 0; eye_index < 2; ++eye_index) {
        ViewPose pose;
        pose.position = {float(eye_index) * .064F - .032F, 1.8F, 3.F};
        pose.orientation = {0, std::sin(.3F), 0, std::cos(.3F)};
        Matrix4 vp;
        Check(BuildViewProjection(pose, {-.7F,.8F,-.8F,.75F}, 0, .05F, 150, &vp), "stereo projection");
        const auto uniform = MakeAbyssFogUniform(volume, source_transform, vp, 1832, 1920);
        Check(uniform.params[3] == 1, "per-eye source-space fog uniform enabled");
        const auto expected_eye = multiply(source_transform, {pose.position[0],pose.position[1],pose.position[2],1});
        for (unsigned axis = 0; axis < 3; ++axis)
            Check(std::abs(uniform.eye[axis] - expected_eye[axis]) < .001F, "inverse-VP camera position respects each eye");
        for (float height : {-4.F, 0.F, 2.F}) {
            // Includes transformed actor/mover positions: reconstruction uses
            // final fragment depth, independent of the draw's model matrix.
            const std::array<float, 4> point{.4F,height,-4.F,1};
            const auto clip = multiply(vp, point);
            const float frag_x = (clip[0] / clip[3] * .5F + .5F) * 1832;
            const float frag_y = (.5F - clip[1] / clip[3] * .5F) * 1920;
            const auto reconstructed = multiply(uniform.inverse_clip_to_source,
                {frag_x * uniform.viewport[0] * 2 - 1, 1 - frag_y * uniform.viewport[1] * 2, clip[2] / clip[3], 1});
            const auto expected = multiply(source_transform, point);
            for (unsigned axis = 0; axis < 3; ++axis)
                Check(std::abs(reconstructed[axis] / reconstructed[3] - expected[axis]) < .003F,
                      "Vulkan zero-to-one depth and flipped viewport recover source geometry");
        }
    }
    if (argc == 2) {
        const auto path = std::filesystem::path(argv[1]) / "Maps/Lev_Tut1b.unr";
        const auto topology = hpvr::wand::load_hp1_bsp_topology(path);
        const auto census = hpvr::wand::inspect_hp1_actor_visuals(path);
        hpvr::wand::Hp1ChallengeAbyssLighting authored(topology, census, true);
        auto pieces = authored.RectangularFootprints();
        for (auto& r : pieces) for (auto& v : r) v *= .02F;
        const auto owned = MakeAbyssFogVolume(pieces, 1008*.02F, .55F);
        std::cout << "OWNED_FOG_FOOTPRINTS polygons=" << authored.PolygonCount() << " rectangles=" << pieces.size()
                  << " validated=" << owned.count << '\n';
        Check(owned.count == 12, "all owned footprint rectangles and holes retained");
        CheckFastVisibility(owned, "owned-map");
        const std::array eye{-53.76F, -134.F, 22.F};
        const float base = AbyssFogTransmittance(owned, eye, {-53.76F, -126.F, 880*.02F});
        const float floor = AbyssFogTransmittance(owned, eye, {-53.76F, -126.F, 496*.02F});
        Check(base < .05F && floor < .0001F, "owned hanging base and floor inside coherent fog volume");
        Check(AbyssFogTransmittance(owned, eye, {-53.76F, -126.F, 1008*.02F}) == 1,
              "owned moving-platform top unchanged");
        std::cout << "OWNED_ABYSS_FOG rectangles=" << owned.count << " base=" << base << " floor=" << floor << '\n';
    }
    std::cout << "ABYSS_FOG_TESTS=PASS\n";
 } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
