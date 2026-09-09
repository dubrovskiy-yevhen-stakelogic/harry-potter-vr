#version 450

layout(push_constant) uniform WandTransform {
    mat4 model_view_projection;
    vec4 color_multiplier;
} wand;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 out_color;

void main() {
    gl_Position = wand.model_view_projection * vec4(in_position, 1.0);
    out_color = vec4(in_color.rgb * wand.color_multiplier.rgb,
                     in_color.a * wand.color_multiplier.a);
}
