#version 450

layout(set = 0, binding = 0) uniform sampler2DArray map_texture;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_tint;
layout(location = 2) flat in uint in_texture_layer;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 source = texture(
        map_texture, vec3(in_uv, float(in_texture_layer))).rgb;
    vec3 emissive = max(source - vec3(0.012), vec3(0.0));
    out_color = vec4(emissive * in_tint.rgb * in_tint.a, 1.0);
}
