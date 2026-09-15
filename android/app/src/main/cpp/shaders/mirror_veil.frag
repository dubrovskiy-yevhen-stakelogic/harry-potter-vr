#version 450
layout(push_constant) uniform Mirror {
    mat4 projection;
    vec4 plane;
    vec4 object;
    vec4 params;
    vec4 panel;
} mirror;
layout(binding=0) uniform sampler2DArray textures;
layout(location=0) in vec2 uv;
layout(location=1) flat in uint layer;
layout(location=7) in vec2 panel_uv;
layout(location=0) out vec4 color;
void main(){
    // The open passage uses the owned MirrorBlur source without a reflection capture.
    float time=mirror.params.w;
    vec2 flow=uv+vec2(.026,-.019)*time;
    flow+=.018*sin(vec2(panel_uv.y,panel_uv.x)*12.0+time*.8);
    float a=texture(textures,vec3(flow,float(layer))).r;
    float b=texture(textures,vec3(uv*.73+vec2(-.017,.012)*time,float(layer))).r;
    float edge=1.0-smoothstep(0.0,.22,min(min(panel_uv.x,1.0-panel_uv.x),min(panel_uv.y,1.0-panel_uv.y)));
    float mist=smoothstep(.22,.75,a*.65+b*.35);
    color=vec4(mix(vec3(.10,.07,.26),vec3(.29,.22,.57),mist),.055+.28*edge+.12*mist);
}
