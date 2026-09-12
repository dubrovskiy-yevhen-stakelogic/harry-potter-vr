#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hpvr::quest::ambient {
inline constexpr std::size_t kMaximumAmbientEmitters=64;
inline constexpr std::size_t kMaximumAmbientParticlesPerEmitter=16;
struct Emitter {
    std::int32_t actor_reference=0;
    std::array<float,3> position{},source_right{1,0,0},source_up{0,0,1},direction{0,1,0};
    float source_width_m=0,source_height_m=0,speed_mps=0,speed_range_mps=0;
    float lifetime=1,lifetime_range=0,size_m=.08F,size_range_m=0;
    float size_end_scale=0,size_end_range=0,rate=20,rate_range=0;
    float alpha_start=1,alpha_range=0,phase=0;
    std::array<float,3> color_start{1,1,1},color_end{1,1,1};
};
struct Particle {
    std::array<float,3> position{};
    float size_m=0;
    std::array<float,4> color{};
};
inline std::uint32_t ParticleHash(std::uint32_t value){
    value^=value>>16;value*=0x7feb352dU;value^=value>>15;
    value*=0x846ca68bU;value^=value>>16;return value;
}
inline float ParticleUnit(std::uint32_t seed){return float(ParticleHash(seed)>>8)*(1.0F/16777216.0F);}

// Caller supplies shared-head culling and a fixed output buffer once per frame.
// No simulation state, heap allocation, scene lights or eye-dependent randomness.
inline std::size_t BuildAmbientParticles(const std::vector<Emitter>& emitters,float elapsed,
    const std::array<float,3>& shared_head,Particle* output,std::size_t capacity){
    if(!output||!capacity||!std::isfinite(elapsed)||elapsed<0)return 0;
    for(const auto value:shared_head)if(!std::isfinite(value))return 0;
    std::size_t count=0;
    for(std::size_t emitter_index=0;emitter_index<std::min(emitters.size(),kMaximumAmbientEmitters);++emitter_index){
        const auto& e=emitters[emitter_index];float distance_squared=0;
        for(unsigned axis=0;axis<3;++axis){const float d=e.position[axis]-shared_head[axis];distance_squared+=d*d;}
        if(!std::isfinite(distance_squared)||distance_squared>28*28)continue;
        const std::array<float,13> scalars{e.source_width_m,e.source_height_m,e.speed_mps,e.speed_range_mps,
            e.lifetime,e.lifetime_range,e.size_m,e.size_range_m,e.size_end_scale,e.size_end_range,e.rate,e.alpha_start,e.phase};
        if(!std::all_of(scalars.begin(),scalars.end(),[](float value){return std::isfinite(value);})||e.rate<=0)continue;
        const float nominal_lifetime=std::clamp(e.lifetime+e.lifetime_range*.5F,.05F,10.0F);
        const auto slots=static_cast<std::size_t>(std::clamp(std::ceil(e.rate*nominal_lifetime),1.0F,float(kMaximumAmbientParticlesPerEmitter)));
        for(std::size_t slot=0;slot<slots&&count<capacity;++slot){
            const auto seed=ParticleHash(std::uint32_t(e.actor_reference)^std::uint32_t(slot*65537));
            const float life=std::clamp(e.lifetime+std::max(0.0F,e.lifetime_range)*ParticleUnit(seed+1),.05F,10.0F);
            const float time=std::min(elapsed,1000000.0F)+ParticleUnit(seed+2)*life;
            const float cycle=std::floor(time/life),age=time-cycle*life,t=age/life;
            const auto birth=seed^ParticleHash(static_cast<std::uint32_t>(cycle));
            const float x=(ParticleUnit(birth+3)-.5F)*std::clamp(e.source_width_m,0.0F,20.0F);
            const float y=(ParticleUnit(birth+4)-.5F)*std::clamp(e.source_height_m,0.0F,20.0F);
            const float travel=age*std::clamp(e.speed_mps+e.speed_range_mps*ParticleUnit(birth+5),-3.0F,3.0F);
            Particle p;
            for(unsigned axis=0;axis<3;++axis){
                p.position[axis]=e.position[axis]+e.source_right[axis]*x+e.source_up[axis]*y+e.direction[axis]*travel;
                p.color[axis]=std::clamp(e.color_start[axis]*(1-t)+e.color_end[axis]*t,0.0F,1.0F);
            }
            const float start=std::clamp(e.size_m+e.size_range_m*ParticleUnit(birth+6),.005F,1.0F);
            const float end=std::clamp(e.size_end_scale+e.size_end_range*ParticleUnit(birth+7),0.0F,8.0F);
            p.size_m=std::clamp(start*((1-t)+end*t),.001F,1.0F);
            const float fade=std::min({1.0F,t/.08F,(1-t)/.22F});
            p.color[3]=std::clamp(e.alpha_start+e.alpha_range*ParticleUnit(birth+8),0.0F,1.0F)*fade;
            if(!std::all_of(p.position.begin(),p.position.end(),[](float value){return std::isfinite(value);})||
               !std::all_of(p.color.begin(),p.color.end(),[](float value){return std::isfinite(value);})||!std::isfinite(p.size_m))continue;
            output[count++]=p;
        }
        if(count==capacity)break;
    }
    return count;
}
} // namespace hpvr::quest::ambient
