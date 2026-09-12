#version 450

layout(push_constant) uniform ParticleTransform {
    mat4 model_view_projection;
    vec4 tint;
    uint texture_layer;
    layout(offset = 96) vec4 uv_rect;
} particle;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_tint;
layout(location = 2) flat out uint out_texture_layer;

void main() {
    gl_Position = particle.model_view_projection * vec4(in_position, 1.0);
    out_uv = particle.uv_rect.xy + in_uv * particle.uv_rect.zw;
    out_tint = particle.tint * in_color;
    out_texture_layer = particle.texture_layer;
}
