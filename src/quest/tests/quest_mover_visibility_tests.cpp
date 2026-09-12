#include "hpvr/quest_mover_visibility.h"
#include "hpvr/quest_view.h"
#include "../../../android/app/src/main/cpp/quest_challenge_movers.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
namespace visibility = hpvr::quest::mover_visibility;
namespace movers = hpvr::quest::movers;
using Vector = std::array<float, 3>;
using Triangle = std::array<Vector, 3>;
struct Vertex { std::uint32_t polygon_flags = 0; };

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
Vector Cross(const Vector& a, const Vector& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]};
}
float Dot(const Vector& a, const Vector& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vector Unit(Vector value) {
    const float length = std::sqrt(Dot(value, value));
    Require(length > 0, "non-degenerate normal");
    for (auto& component : value) component /= length;
    return value;
}

// Vulkan's rasterization area is -1/2 the ordinary framebuffer cross product.
// The current viewport maps NDC y to .5 - y/2 (negative viewport height).
double VulkanArea(const Triangle& triangle, const hpvr::quest::ViewPose& eye) {
    hpvr::quest::Matrix4 projection{};
    Require(hpvr::quest::BuildViewProjection(eye, {-1.F, 1.F, -1.F, 1.F}, 0,
        .001F, 1000.F, &projection), "valid projection");
    std::array<std::array<double, 2>, 3> framebuffer{};
    for (unsigned vertex = 0; vertex < 3; ++vertex) {
        std::array<double, 4> clip{};
        for (unsigned row = 0; row < 4; ++row) {
            clip[row] = projection[12 + row];
            for (unsigned column = 0; column < 3; ++column)
                clip[row] += projection[column * 4 + row] * triangle[vertex][column];
        }
        Require(clip[3] > 0, "triangle remains in front of eye");
        framebuffer[vertex] = {.5 + .5 * clip[0] / clip[3],
            .5 - .5 * clip[1] / clip[3]};
    }
    return -.5 * ((framebuffer[1][0] - framebuffer[0][0]) *
        (framebuffer[2][1] - framebuffer[0][1]) -
        (framebuffer[1][1] - framebuffer[0][1]) *
        (framebuffer[2][0] - framebuffer[0][0]));
}

void TestConservativeDrawSelection() {
    std::vector<Vertex> vertices(12);
    const auto policy = [&](std::size_t first, std::size_t count) {
        return visibility::RequiresTwoSidedRendering(std::span<const Vertex>(vertices), first, count);
    };
    Require(!policy(0, 12), "one-sided mover uses back-face culling");
    Require(!visibility::MoverNeedsTwoSided(std::span<const Vertex>(vertices)),
        "validated single-span API uses the same policy");
    vertices[0].polygon_flags = 2U; // Masking is independent of sidedness.
    Require(!policy(0, 12), "masked gate remains one-sided");
    vertices[7].polygon_flags = visibility::kSourceTwoSided | 2U;
    Require(policy(0, 12), "any two-sided vertex preserves a mixed draw");
    Require(!policy(0, 6), "another draw does not inherit a neighbour's flags");
    Require(policy(6, 6), "two-sided face is honoured away from the first triangle");
    for (auto& vertex : vertices) vertex.polygon_flags = visibility::kSourceTwoSided;
    Require(policy(0, 12), "fully two-sided mover remains visible from both sides");
    Require(policy(0, 0) && policy(0, 2) && policy(1, 4), "incomplete triangles use no culling");
    Require(policy(13, 3) && policy(9, 6), "out-of-range draws use no culling");
    Require(policy(std::numeric_limits<std::size_t>::max(), 3), "first offset cannot overflow");
    Require(policy(0, std::numeric_limits<std::size_t>::max()), "count cannot overflow");
    Require(visibility::RequiresTwoSidedRendering(std::span<const Vertex>{}, 0, 3),
        "empty vertex data uses no culling");
    Require(visibility::MoverNeedsTwoSided(std::span<const Vertex>{}),
        "empty validated span uses no culling");
}

void TestRepairedSourceWindingAndStereoProjection() {
    // A tiny source-space patch isolates the facing convention from clipping;
    // dimensions do not affect winding. This uses the extractor's axis mapping
    // and its documented winding repair, then the real mover and eye transforms.
    Triangle source{{{0, 0, 0}, {.01F, 0, 0}, {0, .01F, 0}}};
    Triangle converted{};
    for (unsigned i = 0; i < 3; ++i) converted[i] = movers::FromUnreal(source[i]);
    const Vector expected_normal = movers::FromUnreal({0, 0, 1});
    Require(Dot(Cross(movers::Subtract(converted[1], converted[0]),
        movers::Subtract(converted[2], converted[0])), expected_normal) < 0,
        "Unreal-to-scene mapping reverses winding before repair");
    std::swap(converted[1], converted[2]);

    unsigned checked = 0;
    for (const Vector rotation : {Vector{}, Vector{0, 81920, 0},
        Vector{0, 49152, 0}, Vector{8192, 5000, -4096}}) {
        Triangle triangle{}; Vector center{};
        for (unsigned i = 0; i < 3; ++i) {
            triangle[i] = movers::Add({-12, 4, -7}, movers::RotateBrushLocal(converted[i], rotation, .3F));
            for (unsigned axis = 0; axis < 3; ++axis) center[axis] += triangle[i][axis] / 3;
        }
        const auto normal = Unit(Cross(movers::Subtract(triangle[1], triangle[0]),
            movers::Subtract(triangle[2], triangle[0])));
        const auto tangent = Unit(movers::Subtract(triangle[1], triangle[0]));
        for (float distance : {.025F, .1F, .5F, 2.F}) {
            for (float oblique : {0.F, .7F, 1.7F}) {
                for (float side : {-1.F, 1.F}) {
                    hpvr::quest::ViewPose head;
                    for (unsigned axis = 0; axis < 3; ++axis)
                        head.position[axis] = center[axis] + normal[axis] * distance * side +
                            tangent[axis] * distance * oblique;
                    Require(hpvr::quest::BuildLookOrientation(head.position, center, &head.orientation),
                        "valid head orientation");
                    hpvr::quest::Matrix4 eye_to_world{};
                    Require(hpvr::quest::BuildRigidTransform(head, &eye_to_world), "valid stereo basis");
                    for (float eye_offset : {-.032F, .032F}) {
                        auto eye = head;
                        for (unsigned axis = 0; axis < 3; ++axis)
                            eye.position[axis] += eye_to_world[axis] * eye_offset;
                        const float facing = Dot(normal, movers::Subtract(eye.position, center));
                        Require(std::abs(facing) > .001F, "eye does not lie on the surface");
                        Require((VulkanArea(triangle, eye) > 0) == (facing > 0),
                            "CCW front face follows the authored normal for both eyes");
                        ++checked;
                    }
                }
            }
        }
    }
    Require(checked == 192, "all distance, rotation, oblique and stereo cases were checked");
}
}  // namespace

int main() {
    try {
        TestConservativeDrawSelection();
        TestRepairedSourceWindingAndStereoProjection();
        std::cout << "quest_mover_visibility_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "quest_mover_visibility_tests: " << error.what() << '\n';
        return 1;
    }
}
