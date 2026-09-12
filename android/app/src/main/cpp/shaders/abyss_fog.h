// Bounded analytic integration through disjoint authored portal rectangles.
// No depth samples, ray marching, extra render targets or additional passes.
layout(set = 0, binding = 5) uniform AbyssFogData {
    mat4 inverse_clip_to_source;
    vec4 eye;
    vec4 viewport;
    vec4 params; // top, linear-height density, rectangle count, enabled
    vec4 bounds;
    vec4 rectangles[12];
} abyss;

bool abyssClipRectangle(vec2 origin, vec2 inverse_delta, bvec2 parallel, vec4 r,
                        float global_enter, float global_leave,
                        out float enter, out float leave) {
    // Parallel axes remain half-open, including exact shared portal seams.
    // Reciprocals and the parallel mask are computed once, not for each portal.
    bvec2 outside = mix(greaterThanEqual(origin, r.zw), bvec2(true), lessThan(origin, r.xy));
    if (any(mix(bvec2(false), outside, parallel))) return false;
    vec2 a = (r.xy - origin) * inverse_delta;
    vec2 b = (r.zw - origin) * inverse_delta;
    vec2 near_t = mix(min(a, b), vec2(global_enter), parallel);
    vec2 far_t = mix(max(a, b), vec2(global_leave), parallel);
    enter = max(global_enter, max(near_t.x, near_t.y));
    leave = min(global_leave, min(far_t.x, far_t.y));
    return leave > enter;
}
float abyssFogVisibility() {
    if (abyss.params.w == 0.0) return 1.0;
    vec4 source = abyss.inverse_clip_to_source * vec4(
        gl_FragCoord.x * abyss.viewport.x * 2.0 - 1.0,
        1.0 - gl_FragCoord.y * abyss.viewport.y * 2.0, gl_FragCoord.z, 1.0);
    if (abs(source.w) < 0.000001) return 1.0;
    vec3 surface = source.xyz / source.w;
    vec3 eye = abyss.eye.xyz;
    if (eye.z >= abyss.params.x && surface.z >= abyss.params.x) return 1.0;
    vec3 delta = surface - eye;
    bvec2 parallel = lessThan(abs(delta.xy), vec2(0.000001));
    vec2 inverse_delta = vec2(1.0) / mix(delta.xy, vec2(1.0), parallel);
    float global_enter, global_leave;
    if (!abyssClipRectangle(eye.xy, inverse_delta, parallel, abyss.bounds,
                            0.0, 1.0, global_enter, global_leave)) return 1.0;
    if (abs(delta.z) < 0.000001) { if (eye.z >= abyss.params.x) return 1.0; }
    else {
        float boundary = (abyss.params.x - eye.z) / delta.z;
        if (delta.z > 0.0) global_leave = min(global_leave, boundary);
        else global_enter = max(global_enter, boundary);
    }
    if (global_leave <= global_enter) return 1.0;
    float distance_m = length(delta);
    if (distance_m < 0.000001) return 1.0;
    float depth_scale = distance_m * 0.5 * abyss.params.y;
    float optical_depth = 0.0;
    int count = clamp(int(abyss.params.z), 0, 12);
    for (int i = 0; i < count; ++i) {
        float enter, leave;
        if (!abyssClipRectangle(eye.xy, inverse_delta, parallel, abyss.rectangles[i],
                                global_enter, global_leave, enter, leave)) continue;
        float d0 = max(0.0, abyss.params.x - eye.z - delta.z * enter);
        float d1 = max(0.0, abyss.params.x - eye.z - delta.z * leave);
        optical_depth += (leave - enter) * (d0 + d1) * depth_scale;
        if (optical_depth >= 12.0) return 0.0;
    }
    return exp(-optical_depth);
}
