#pragma once

#include "hpvr/quest_grid_motion.h"

#include <cstdint>
#include <vector>

namespace hpvr::quest::charms_block {

using Vec = grid_motion::Vec;
using Bounds = grid_motion::Bounds;
constexpr std::size_t kBoxTriangleCount = 12;

struct Motion {
    grid_motion::State falling;
    float held_seconds = 0;
    bool was_held = false;
};
struct StepResult {
    Vec offset{};
    bool blocked = false, released = false, landed = false;
};

inline bool Finite(const Vec& v) {
    return std::ranges::all_of(v, [](float value) { return std::isfinite(value); });
}
inline bool Valid(const Bounds& box) {
    if (!Finite(box.minimum) || !Finite(box.maximum)) return false;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (box.maximum[axis] <= box.minimum[axis]) return false;
    return true;
}

// Collision ranges identify the block itself. Nearby floor and wall triangles
// must never be excluded merely because they fit inside the same bounds.
template<class Triangle>
void AppendBox(std::vector<Triangle>& triangles, const Bounds& box) {
    std::array<Vec, 8> points;
    for (unsigned index = 0; index < points.size(); ++index)
        for (unsigned axis = 0; axis < 3; ++axis)
            points[index][axis] = (index & (1U << axis)) ? box.maximum[axis] : box.minimum[axis];
    constexpr std::array<std::array<unsigned, 3>, kBoxTriangleCount> faces{{
        {0,2,3}, {0,3,1}, {4,5,7}, {4,7,6},
        {0,4,6}, {0,6,2}, {1,3,7}, {1,7,5},
        {0,1,5}, {0,5,4}, {2,6,7}, {2,7,3}
    }};
    for (const auto& face : faces) {
        Triangle triangle{};
        for (unsigned index = 0; index < 3; ++index) triangle.vertices[index] = points[face[index]];
        triangle.minimum = triangle.maximum = triangle.vertices[0];
        for (const auto& point : triangle.vertices)
            for (unsigned axis = 0; axis < 3; ++axis) {
                triangle.minimum[axis] = std::min(triangle.minimum[axis], point[axis]);
                triangle.maximum[axis] = std::max(triangle.maximum[axis], point[axis]);
            }
        const auto normal = grid_motion::Cross(grid_motion::Sub(triangle.vertices[1], triangle.vertices[0]),
                                               grid_motion::Sub(triangle.vertices[2], triangle.vertices[0]));
        triangle.normal = grid_motion::Scale(normal, 1.0F / std::sqrt(grid_motion::Dot(normal, normal)));
        triangles.push_back(triangle);
    }
}

template<class Triangles>
Vec Move(const Triangles& triangles, const Bounds& box, const Vec& desired,
         std::size_t own_first, std::size_t own_count, bool* blocked = nullptr, float frame_yaw = 0) {
    const auto hit = grid_motion::Sweep(triangles, box, desired, own_first, own_count,false,frame_yaw);
    Vec offset = grid_motion::Scale(desired, hit.found ? hit.fraction : 1.0F);
    if (blocked) *blocked = hit.found && hit.fraction < 1;
    if (!hit.found || hit.fraction >= 1) return offset;
    // A wall can stop one component without locking every wand movement.
    for (const unsigned axis : {0U, 2U, 1U}) {
        Vec remainder{};
        remainder[axis] = desired[axis] - offset[axis];
        if (std::abs(remainder[axis]) < .00001F) continue;
        const auto slide = grid_motion::Sweep(triangles, grid_motion::Translate(box, offset), remainder, own_first, own_count,false,frame_yaw);
        offset = grid_motion::Add(offset, grid_motion::Scale(remainder, slide.found ? slide.fraction : 1.0F));
    }
    return offset;
}

template<class Triangles>
StepResult Advance(Motion& state, const Bounds& box, const Vec& desired_center,
                   bool held, float seconds, float maximum_hold_seconds,
                   const Triangles& triangles, std::size_t static_count,
                   std::size_t own_first, std::size_t own_count, float frame_yaw = 0) {
    StepResult result;
    if (!Valid(box) || !Finite(desired_center) || !std::isfinite(seconds) || seconds <= 0 ||
        !std::isfinite(maximum_hold_seconds) || maximum_hold_seconds <= 0) return result;
    seconds = std::min(seconds, .05F);
    if (held) {
        if (!state.was_held) state.held_seconds = 0;
        state.held_seconds += seconds;
        if (state.held_seconds >= maximum_hold_seconds) {
            held = false;
            result.released = true;
        }
    }
    if (held != state.was_held) state.falling = {};
    state.was_held = held;
    if (held) {
        const auto center = grid_motion::Scale(grid_motion::Add(box.minimum, box.maximum), .5F);
        auto movement = grid_motion::Sub(desired_center, center);
        const float length = std::sqrt(grid_motion::Dot(movement, movement));
        if (length > .00001F) movement = grid_motion::Scale(movement, std::min(1.0F, seconds * 4.0F / length));
        result.offset = Move(triangles, box, movement, own_first, own_count, &result.blocked,frame_yaw);
        return result;
    }
    // No floor within a short ray is still a fall, not a reason to freeze.
    const auto fall = grid_motion::Advance(state.falling, box, {}, seconds, triangles,
                                           static_count, own_first, own_count,0,frame_yaw);
    result.offset = fall.offset;
    result.landed = fall.landed;
    return result;
}

inline bool TouchesPlate(const Bounds& box, const Vec& position, float radius, float height) {
    if (!Valid(box) || !Finite(position) || !std::isfinite(radius) || !std::isfinite(height) || radius < 0 || height < 0)
        return false;
    const auto center = grid_motion::Scale(grid_motion::Add(box.minimum, box.maximum), .5F);
    const auto half = grid_motion::Scale(grid_motion::Sub(box.maximum, box.minimum), .5F);
    return std::hypot(center[0] - position[0], center[2] - position[2]) <= radius + std::max(half[0], half[2]) &&
        std::abs(center[1] - position[1]) <= height + half[1];
}

template<class Triangles>
bool SettleOnPlate(const Triangles& triangles, const Bounds& box, const Vec& plate,
                   std::size_t own_first, std::size_t own_count, Vec* result, float frame_yaw = 0) {
    if (!result || !Valid(box) || !Finite(plate)) return false;
    const auto center = grid_motion::Scale(grid_motion::Add(box.minimum, box.maximum), .5F);
    const Vec horizontal{plate[0] - center[0], 0, plate[2] - center[2]};
    const auto hit = grid_motion::Sweep(triangles, box, horizontal, own_first, own_count,false,frame_yaw);
    if (hit.found && hit.fraction < .999F) return false;
    *result = horizontal;
    const Vec vertical{0, plate[1] - center[1], 0};
    const auto floor = grid_motion::Sweep(triangles, grid_motion::Translate(box, horizontal), vertical, own_first, own_count,false,frame_yaw);
    *result = grid_motion::Add(*result, grid_motion::Scale(vertical, floor.found ? floor.fraction : 1.0F));
    return true;
}

inline bool NeedsReset(const Vec& offset, float meters_per_unit) {
    return Finite(offset) && std::isfinite(meters_per_unit) && meters_per_unit > 0 && offset[1] < -100.0F * meters_per_unit;
}

} // namespace hpvr::quest::charms_block
