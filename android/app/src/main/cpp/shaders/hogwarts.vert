#version 450

layout(push_constant) uniform EyeTransform {
    mat4 view_projection;
    vec4 dark_position_radius[2];
    vec4 dark_color_strength[2];
} eye;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uint in_texture_layer;
layout(location = 3) in uint in_polygon_flags;
layout(location = 4) in uint in_packed_light;
layout(location = 5) in vec2 in_lightmap_uv;
layout(location = 6) in uint in_has_lightmap;

layout(location = 0) out vec2 out_uv;
layout(location = 1) flat out uint out_texture_layer;
layout(location = 2) flat out uint out_polygon_flags;
layout(location = 3) out vec3 out_light;
layout(location = 4) out vec2 out_lightmap_uv;
layout(location = 5) flat out uint out_has_lightmap;
layout(location = 6) out vec3 out_world_position;
layout(location = 7) flat out float out_actor_opacity;

void main() {
    gl_Position = eye.view_projection * vec4(in_position, 1.0);
    if ((in_polygon_flags & 0x08000000u)!=0u) {
        gl_Position=eye.view_projection*vec4(in_position+eye.dark_position_radius[0].xyz,1.0);
        gl_Position.z=gl_Position.w*0.99999;
    }
    out_uv = in_uv;
    out_texture_layer = in_texture_layer;
    out_polygon_flags = in_polygon_flags;
    out_actor_opacity = eye.dark_position_radius[1].w < 0.0 ? clamp(-eye.dark_position_radius[1].w, 0.0, 1.0) : -1.0;
    out_lightmap_uv = in_lightmap_uv;
    out_has_lightmap = in_has_lightmap;
    out_world_position = in_position; // Reflective BSP is already world-space.
    out_light = vec3(
        float(in_packed_light & 0xffu),
        float((in_packed_light >> 8u) & 0xffu),
        float((in_packed_light >> 16u) & 0xffu)) * (1.25 / 255.0);
    if ((in_polygon_flags & 0x40000000u)!=0u) out_light /= 1.25;
    else if (in_has_lightmap == 0u) {
        // Authored subtractive lights follow world positions on moving brushes.
        // Their centers are already in this brush's rigid local coordinate frame.
        // Non-movers/UI receive zero radii; BSP darkness is baked into its atlas.
        for (int i = 0; i < 2; ++i) {
            vec4 source = eye.dark_position_radius[i];
            vec4 color = eye.dark_color_strength[i];
            if (source.w <= 0.0) continue;
            float ratio = distance(in_position, source.xyz) / source.w;
            if (ratio >= 1.0) continue;
            float falloff;
            if (color.w < 0.0) {
                falloff = clamp((1.0 + 2.0 * ratio * ratio * ratio - 3.0 * ratio * ratio) /
                                max(ratio, 0.0001), 0.0, 1.0) * 1.55;
            } else {
                float linear = 1.0 - ratio;
                falloff = linear * linear * 1.85;
            }
            out_light -= color.rgb * (abs(color.w) * falloff);
        }
        out_light = max(out_light, vec3(0.0));
    }
}
