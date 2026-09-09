#ifndef HPVR_REFLECTION_HIT_POLICY_H
#define HPVR_REFLECTION_HIT_POLICY_H
// Shared by GLSL and the host regression. Distances are camera-space meters.
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
namespace hpvr::quest {
using std::abs;
using std::clamp;
#endif

float ReflectionHitTolerance(float ray_depth_span) {
    return clamp(0.035f + abs(ray_depth_span), 0.035f, 0.09f);
}

bool ReflectionHitBracketValid(float front_gap, float behind_gap, float ray_depth_span) {
    float tolerance = ReflectionHitTolerance(ray_depth_span);
    // A silhouette jump is not an intersection. Both ends must converge to the
    // surface; checking only the behind end stretches small floating objects.
    return front_gap < 0.0f && behind_gap >= 0.0f &&
           front_gap >= -tolerance && behind_gap <= tolerance;
}

bool ReflectionSameDepthLayer(float hit_depth, float sample_depth) {
    // Color is half resolution, while Quest depth is full resolution. The
    // chosen color texel must actually belong to the hit, not its background.
    float tolerance = clamp(0.035f + hit_depth * 0.002f, 0.035f, 0.08f);
    return sample_depth > 0.0f && abs(sample_depth - hit_depth) <= tolerance;
}

#ifdef __cplusplus
}
#endif
#endif
