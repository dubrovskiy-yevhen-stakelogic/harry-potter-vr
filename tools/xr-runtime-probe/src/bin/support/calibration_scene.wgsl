struct EyeUniform {
    view_projection: mat4x4<f32>,
    eye_position: vec4<f32>,
    frame_data: vec4<f32>,
};

@group(0) @binding(0)
var<uniform> eye: EyeUniform;
@group(0) @binding(1)
var map_texture: texture_2d_array<f32>;
@group(0) @binding(2)
var map_sampler: sampler;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec3<f32>,
    @location(2) animated: f32,
    @location(3) texture_uv: vec2<f32>,
    @location(4) texture_layer: f32,
};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) animated: f32,
    @location(2) texture_uv: vec2<f32>,
    @location(3) texture_layer: f32,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var world = input.position;
    if input.animated > 0.5 {
        let center = vec2<f32>(0.0, -3.82);
        let local = world.xz - center;
        let angle = eye.frame_data.x * 0.7;
        let sine = sin(angle);
        let cosine = cos(angle);
        world.x = center.x + local.x * cosine - local.y * sine;
        world.z = center.y + local.x * sine + local.y * cosine;
        world.y += 0.12 * sin(eye.frame_data.x * 1.6);
    }

    var output: VertexOutput;
    output.clip_position = eye.view_projection * vec4<f32>(world, 1.0);
    output.color = input.color;
    output.animated = input.animated;
    output.texture_uv = input.texture_uv;
    output.texture_layer = input.texture_layer;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    if input.texture_layer >= 0.0 {
        let repeated_uv = fract(input.texture_uv);
        return textureSample(
            map_texture,
            map_sampler,
            repeated_uv,
            i32(round(input.texture_layer))
        );
    }
    var brightness = 1.0;
    if input.animated > 0.5 {
        brightness = 0.88 + 0.12 * sin(eye.frame_data.x * 2.4);
    }
    return vec4<f32>(input.color * brightness, 1.0);
}
