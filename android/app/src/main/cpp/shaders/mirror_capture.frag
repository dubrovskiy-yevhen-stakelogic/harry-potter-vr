#version 450
layout(binding=0) uniform sampler2DArray textures;
layout(binding=1) uniform sampler2D lightmaps;
layout(location=0) in vec2 uv;
layout(location=1) flat in uint layer;
layout(location=2) flat in uint flags;
layout(location=3) in vec3 light;
layout(location=4) in vec2 lightmap_uv;
layout(location=5) flat in uint has_lightmap;
layout(location=6) in float side;
layout(location=0) out vec4 color;
void main(){
    if(side<0.005||(flags&0x04000001u)!=0u)discard;
    vec4 texel=texture(textures,vec3(uv,float(layer)));
    if((flags&2u)!=0u&&texel.a<0.5)discard;
    vec3 lighting=has_lightmap!=0u?texture(lightmaps,lightmap_uv).rgb:light;
    color=vec4(pow(max(texel.rgb*lighting,vec3(0))*1.35,vec3(.92)),1);
}
