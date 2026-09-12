#version 450
#extension GL_GOOGLE_include_directive : require
#include "reflection_hit_policy.h"
#include "abyss_fog.h"

layout(set = 0, binding = 0) uniform sampler2DArray map_texture;
layout(set = 0, binding = 1) uniform sampler2D lightmap_texture;
layout(set = 0, binding = 2) uniform sampler2D history_color;
layout(set = 0, binding = 3) uniform sampler2D history_depth;
layout(set = 0, binding = 4) uniform ReflectionData {
    mat4 projection;
    mat4 inverse_projection;
    vec4 eye;
    vec4 params; // strength, active UV width/height, maximum ray distance
    vec4 depth_uv; // independently sized depth history (no depth blit on Quest)
} history;

layout(location = 0) in vec2 in_uv;
layout(location = 1) flat in uint in_texture_layer;
layout(location = 2) flat in uint in_polygon_flags;
layout(location = 3) in vec3 in_light;
layout(location = 4) in vec2 in_lightmap_uv;
layout(location = 5) flat in uint in_has_lightmap;
layout(location = 6) in vec3 in_world_position;

layout(location = 0) out vec4 out_color;

const uint PF_MASKED = 0x00000002u;

bool sceneDepthAt(vec2 uv,out float scene_depth){
    if(any(lessThan(uv,vec2(.01)))||any(greaterThan(uv,vec2(.99))))return false;
    float depth=textureLod(history_depth,uv*history.depth_uv.xy,0.0).r;
    if(depth>=.99999)return false;
    // Recover only homogeneous W; no inverse mat4 or normalize per sample.
    vec4 inverse_w=vec4(history.inverse_projection[0][3],history.inverse_projection[1][3],
                        history.inverse_projection[2][3],history.inverse_projection[3][3]);
    float reciprocal_w=dot(inverse_w,vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,depth,1.0));
    if(reciprocal_w<=.00001)return false;
    scene_depth=1.0/reciprocal_w;
    return true;
}
bool sceneAt(vec4 clip,out vec2 uv,out float gap){
    if(clip.w<=0.05)return false;
    uv=vec2(clip.x/clip.w*.5+.5,.5-clip.y/clip.w*.5);
    float scene_depth;
    if(!sceneDepthAt(uv,scene_depth))return false;
    gap=clip.w-scene_depth;
    return true;
}
vec3 woodReflection(vec3 base){
    if(history.params.x<=0.0)return base;
    vec3 incident=normalize(in_world_position-history.eye.xyz);
    vec3 direction=reflect(incident,vec3(0,1,0));
    if(direction.y<.015)return base;
    vec3 origin=in_world_position+vec3(0,.035,0);
    vec4 clip_origin=history.projection*vec4(origin,1.0);
    vec4 clip_direction=history.projection*vec4(direction,0.0);
    float previous=.08,previous_gap=0.0;bool in_front=false;
    // A shared mono history is deliberately used for BOTH eyes, as in MiamiVR.
    // Project once, then sixteen coarse + four bounded refinement steps.
    for(int i=1;i<=16;++i){
        float fraction=float(i)*(1.0/16.0);
        float t=.08+history.params.w*fraction*fraction;
        vec2 uv;float gap;
        if(!sceneAt(clip_origin+clip_direction*t,uv,gap)){previous=t;in_front=false;continue;}
        if(gap>=0.0&&in_front){
            float lo=previous,hi=t,lo_gap=previous_gap,hi_gap=gap;
            for(int n=0;n<4;++n){float mid=(lo+hi)*.5;vec2 u;float d;
                // Empty/invalid history cannot establish a valid front bracket.
                if(!sceneAt(clip_origin+clip_direction*mid,u,d))return base;
                if(d>=0.0){hi=mid;hi_gap=d;uv=u;}else{lo=mid;lo_gap=d;}}
            if(!ReflectionHitBracketValid(lo_gap,hi_gap,clip_direction.w*(hi-lo)))return base;
            float hit_depth=clip_origin.w+clip_direction.w*hi-hi_gap;
            vec2 color_size=vec2(textureSize(history_color,0));
            vec2 active_size=color_size*history.params.yz;
            vec2 texel_center=floor(uv*active_size)+vec2(.5);
            float color_depth;
            if(!sceneDepthAt(texel_center/active_size,color_depth)||
               !ReflectionSameDepthLayer(hit_depth,color_depth))return base;
            vec2 texuv=texel_center/color_size;
            vec4 reflected=textureLod(history_color,texuv,0.0);
            // UI/wand are absent from capture. Sky/floors still cannot feed back.
            if(reflected.a>.02)return base;
            // Half-res history already softens the result. Do not blur across
            // an unvalidated depth edge into the dark background of a bean.
            float edge=smoothstep(.01,.12,min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y)));
            float fresnel=.16+.5*pow(1.0-clamp(-incident.y,0.0,1.0),3.0);
            float fade=1.0-smoothstep(history.params.w*.6,history.params.w,hi);
            return mix(base,reflected.rgb,clamp(history.params.x*fresnel*edge*fade,0.0,.65));
        }
        in_front=gap<0.0;previous=t;previous_gap=gap;
    }
    return base;
}

void main() {
    // Collision-only BSP ramps must not cover the authored stair treads.
    if ((in_polygon_flags & 0x00000001u) != 0u) discard;
    bool ui=(in_polygon_flags & 0x40000000u)!=0u;
    vec2 uv=ui?clamp(in_uv,vec2(0.5/256.0),vec2(255.5/256.0)):in_uv;
    vec4 color = texture(map_texture, vec3(uv, float(in_texture_layer)));
    if ((in_polygon_flags & PF_MASKED) != 0u && color.a < 0.5) {
        discard;
    }
    vec3 lighting = in_has_lightmap != 0u
        ? texture(lightmap_texture, in_lightmap_uv).rgb
        : in_light;
    if ((in_polygon_flags & 0x40000000u)!=0u) {
        out_color=vec4(color.rgb*in_light,1.0);
        return;
    }
    const float fog_visibility = abyssFogVisibility();
    // UE1 applies its display brightness/gamma after texture * lightmap.
    // The Quest swapchain is sRGB, but without this pass the upper half of
    // the Great Hall histogram is substantially darker than the PC capture.
    // Keep saturation untouched: Quest is already more saturated than PC.
    vec3 linear_color = max(color.rgb * lighting, vec3(0.0));
    const float ue1_exposure = 1.35;
    const float ue1_display_gamma = 0.92;
    linear_color = pow(linear_color * ue1_exposure,
                       vec3(ue1_display_gamma));
    if ((in_polygon_flags & 0x20000000u)!=0u) {
        out_color=vec4(mix(linear_color,vec3(0.55,0.75,1.0),0.45),0.48);
    } else {
        bool wood=(in_polygon_flags & 0x10000000u)!=0u;
        out_color=vec4(wood?woodReflection(linear_color):linear_color,wood?.25:0.0);
    }
    out_color.rgb *= fog_visibility;
}
