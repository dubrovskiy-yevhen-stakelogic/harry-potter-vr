#version 450
layout(push_constant) uniform Mirror {
    mat4 projection;
    vec4 plane;
    vec4 object;
    vec4 params;
    vec4 panel;
} mirror;
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in uint layer;
layout(location=3) in uint flags;
layout(location=4) in uint light;
layout(location=5) in vec2 lightmap_uv;
layout(location=6) in uint has_lightmap;
layout(location=0) out vec2 out_uv;
layout(location=1) flat out uint out_layer;
layout(location=2) flat out uint out_flags;
layout(location=3) out vec3 out_light;
layout(location=4) out vec2 out_lightmap_uv;
layout(location=5) flat out uint out_has_lightmap;
layout(location=6) out float out_side;
layout(location=7) out vec2 out_panel_uv;
layout(location=8) out vec3 out_water_normal;
layout(location=9) out vec3 out_world;
void main(){
    float c=cos(mirror.object.w),s=sin(mirror.object.w);
    vec3 world=position;
    out_water_normal=vec3(0,1,0);
    if(mirror.panel.y>.5){
        float a=position.x*7.0+position.z*2.0+mirror.params.w*1.5;
        float b=-position.x*3.0+position.z*10.0-mirror.params.w*1.8;
        float c=position.x*13.0-position.z*9.0+mirror.params.w*2.2;
        world.y+=.004*sin(a)+.002*sin(b)+.001*sin(c);
        out_water_normal=normalize(vec3(-(.028*cos(a)-.006*cos(b)+.013*cos(c)),1,
            -(.008*cos(a)+.020*cos(b)-.009*cos(c))));
    }
    if(mirror.params.x<0.5)world=vec3(c*position.x+s*position.z,position.y,-s*position.x+c*position.z)+mirror.object.xyz;
    out_side=dot(mirror.plane.xyz,world)+mirror.plane.w;
    if(mirror.params.x<0.5)world-=2.0*out_side*mirror.plane.xyz;
    gl_Position=mirror.projection*vec4(world,1);
    out_world=world;
    out_uv=uv;out_layer=layer;out_flags=flags;
    out_light=vec3(float(light&255u),float((light>>8u)&255u),float((light>>16u)&255u))*(1.25/255.0);
    out_lightmap_uv=lightmap_uv;out_has_lightmap=has_lightmap;
    vec3 tangent=vec3(mirror.plane.z,0,-mirror.plane.x);
    out_panel_uv=vec2(dot(position-mirror.object.xyz,tangent)/max(.01,mirror.object.w),
        (position.y-mirror.object.y)/max(.01,mirror.panel.x))+.5;
    if(mirror.panel.y>.5)out_panel_uv=position.xz;
}
