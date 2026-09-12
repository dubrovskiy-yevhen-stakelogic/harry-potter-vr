#include "hpvr/quest_target_marker.h"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
unsigned checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
using namespace hpvr::quest;
using Vec3 = std::array<float, 3>;
void CheckPlacement(const Vec3& eye, const Vec3& offset = {}) {
    const Vec3 minimum{-1,-2,-0.5F}, maximum{1,2,0.5F};
    const auto placement = PlaceTargetMarker(minimum,maximum,eye,offset);
    Check(placement.valid, "valid target placement");
    Check(std::abs(target_marker_detail::Dot(placement.normal,placement.up)) < 0.00001F,
          "billboard up remains tangent to support plane");
    Check(placement.size >= 0.55F && placement.size <= 2.4F, "bounded marker scale");
    for (unsigned corner = 0; corner < 8; ++corner) {
        Vec3 delta{};
        for (unsigned axis = 0; axis < 3; ++axis)
            delta[axis] = placement.center[axis] - ((corner & (1U<<axis)) ? maximum[axis] : minimum[axis]);
        Check(target_marker_detail::Dot(delta,placement.normal) >= 0.0349F,
              "entire target is behind marker plane");
    }
}
}

int main() {
    try {
        using namespace hpvr::quest;
        // Synthetic shape only; no proprietary pattern or texture in tests.
        constexpr std::array<std::array<float, 2>, 6> pattern{{
            {0,0},{0,0},{0,0.5F},{0.5F,1},{1,0.5F},{0.5F,0.5F}}};
        const auto batch = BuildTargetMarkerBatch(pattern);
        Check(batch.valid(), "startup geometry built");
        Check(batch.vertices.size()*sizeof(TargetMarkerVertex) == 122880, "bounded mesh memory");
        const auto again = BuildTargetMarkerBatch(pattern);
        Check(std::memcmp(batch.vertices.data(),again.vertices.data(),
                         batch.vertices.size()*sizeof(TargetMarkerVertex)) == 0,
              "deterministic geometry");
        Check(std::memcmp(batch.vertices.data(),batch.vertices.data()+kTargetMarkerVerticesPerFrame,
                         kTargetMarkerVerticesPerFrame*sizeof(TargetMarkerVertex)) != 0,
              "frames animate particles");
        for (const auto& vertex : batch.vertices) {
            Check(std::isfinite(vertex.position[0]) && std::isfinite(vertex.position[1]) &&
                  vertex.position[2] == 0, "finite coplanar particles");
            Check(vertex.position[0] >= -0.1F && vertex.position[0] <= 1.1F &&
                  std::abs(vertex.position[1]) <= 0.61F, "normalized glyph bounds");
            Check(vertex.texture_uv[0] > 0 && vertex.texture_uv[0] < 1 &&
                  vertex.texture_uv[1] > 0 && vertex.texture_uv[1] < 1, "atlas UV bounds");
        }
        Check(batch.first_vertex(0) == 0 && batch.first_vertex(0.0625) == 768,
              "single draw frame offset");
        Check(batch.first_vertex(0.5) == 0 && batch.first_vertex(1234.5) == 0,
              "animation loop stable");
        Check(batch.first_vertex(-1) == 0 && batch.first_vertex(
              std::numeric_limits<double>::infinity()) == 0, "invalid animation time safe");
        Check(BuildTargetMarkerBatch({}).vertices.empty(), "empty shape rejected");
        constexpr std::array<std::array<float, 2>, 2> degenerate{{{0,0},{0,0}}};
        Check(BuildTargetMarkerBatch(degenerate).vertices.empty(), "zero length shape rejected");
        auto malformed = pattern;
        malformed[0][0] = std::numeric_limits<float>::quiet_NaN();
        Check(BuildTargetMarkerBatch(malformed).vertices.empty(), "nonfinite shape rejected");

        constexpr std::array<std::uint8_t, 16> sparkle{
            0,0,0,255, 255,255,255,255, 255,255,255,255, 0,0,0,255};
        const auto atlas = BuildTargetMarkerAtlas(sparkle,2,2);
        Check(atlas.size() == 262144, "bounded runtime atlas memory");
        Check(atlas == BuildTargetMarkerAtlas(sparkle,2,2), "deterministic atlas");
        Check(atlas[0] == 0 && atlas[1] == 0 && atlas[2] == 0, "black gutter");
        const auto white = (2U*256U+61U)*4U;
        Check(atlas[white] == 255 && atlas[white+1] == 255 && atlas[white+2] == 255,
              "new particles have bright white core");
        const auto violet = ((2U*64U+2U)*256U+61U)*4U;
        Check(atlas[violet+2] > atlas[violet] && atlas[violet+2] > atlas[violet+1] &&
              atlas[violet+2] < 255, "older particles have original blue-violet faded color");
        const auto dead = ((3U*64U+2U)*256U+3U*64U+61U)*4U;
        Check(atlas[dead] == 0 && atlas[dead+1] == 0 && atlas[dead+2] == 0,
              "end of lifetime fades to zero additive emission");
        Check(BuildTargetMarkerAtlas(sparkle,0,0).empty() &&
              BuildTargetMarkerAtlas(sparkle,3,3).empty(), "invalid texture rejected");

        CheckPlacement({0,0,5});
        CheckPlacement({0,0,-5});
        CheckPlacement({5,2,5});
        CheckPlacement({-5,3,-5});
        CheckPlacement({0,10,0});
        CheckPlacement({0,-10,0});
        CheckPlacement({5,2,5},{0,0.4F,-0.2F});
        CheckPlacement({5,2,5},{3,0,3});
        Check(!PlaceTargetMarker({0,0,0},{0,0,0},{0,0,3}).valid,
              "point bounds rejected");
        Check(!PlaceTargetMarker({1,0,0},{0,1,1},{0,0,3}).valid,
              "inverted bounds rejected");
        Check(!PlaceTargetMarker({-1,-1,-1},{1,1,1},{0,0,0}).valid,
              "undefined camera direction rejected");
        Check(!PlaceTargetMarker({-1,-1,-1},{1,1,1},{0,0,3},{},-1).valid,
              "invalid size modifier rejected");
        std::cout << "TARGET_MARKER=PASS checks=" << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TARGET_MARKER=FAIL " << error.what() << '\n';
        return 1;
    }
}
