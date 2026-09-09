#version 450

layout(push_constant) uniform EyeTransform {
    mat4 view_projection;
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

void main() {
    gl_Position = eye.view_projection * vec4(in_position, 1.0);
    out_uv = in_uv;
    out_texture_layer = in_texture_layer;
    out_polygon_flags = in_polygon_flags;
    out_lightmap_uv = in_lightmap_uv;
    out_has_lightmap = in_has_lightmap;
    out_world_position = in_position; // Reflective BSP is already world-space.
    out_light = vec3(
        float(in_packed_light & 0xffu),
        float((in_packed_light >> 8u) & 0xffu),
        float((in_packed_light >> 16u) & 0xffu)) * (1.25 / 255.0);
    if ((in_polygon_flags & 0x40000000u)!=0u) out_light /= 1.25;
}
