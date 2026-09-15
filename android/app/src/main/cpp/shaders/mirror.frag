#version 450
layout(push_constant) uniform Mirror {
    mat4 projection;
    vec4 plane;
    vec4 object;
    vec4 params;
    vec4 panel;
} mirror;
layout(binding=6) uniform sampler2DArray reflected_scene;
layout(location=0) in vec2 uv;
layout(location=1) flat in uint layer;
layout(location=2) flat in uint flags;
layout(location=3) in vec3 light;
layout(location=4) in vec2 lightmap_uv;
layout(location=5) flat in uint has_lightmap;
layout(location=6) in float side;
layout(location=7) in vec2 panel_uv;
layout(location=8) in vec3 water_normal;
layout(location=9) in vec3 world;
layout(location=0) out vec4 color;
void main(){
    vec2 screen_uv=gl_FragCoord.xy/mirror.params.yz;
    vec2 ripple=vec2(0);
    if(mirror.panel.y>.5){
        vec3 n=normalize(water_normal);
        vec4 p=mirror.projection*vec4(world,1);
        vec4 q=mirror.projection*vec4(world+vec3(n.x,0,n.z)*.15,1);
        ripple=(q.xy/q.w-p.xy/p.w)*vec2(.5,-.5);
        screen_uv+=ripple;
    }
    vec3 reflection=texture(reflected_scene,vec3(clamp(screen_uv,vec2(.001),vec2(.999)),mirror.panel.z)).rgb;
    if(mirror.panel.y>.5){
        vec3 n=normalize(water_normal);
        float glint=pow(max(0,dot(n,normalize(vec3(.18,1,.12)))),64);
        reflection=mix(reflection,vec3(.16,.18,.28),.12)+vec3(.032,.037,.05)*glint;
    }
    color=vec4(reflection,1);
}
