#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hpvr::quest {
struct OriginalSpellParticle {
    std::array<float,3> center{};
    std::array<float,4> color{1,1,1,1};
    float size=0,rotation=0;
};
// Parameters from the user's HPParticle Flip_fly / Flip_hit defaults.
// Fixed seeds and bounded counts give identical geometry in both eyes.
inline std::vector<OriginalSpellParticle> BuildOriginalSpellParticles(
    const std::array<float,3>& origin,const std::array<float,3>& direction,
    float elapsed,float flight_duration,bool impact,std::uint64_t serial){
    std::vector<OriginalSpellParticle> out;out.reserve(178);
    if(!std::isfinite(elapsed)||!std::isfinite(flight_duration)||elapsed<0||flight_duration<0||elapsed>3600||flight_duration>3600)return out;
    for(unsigned axis=0;axis<3;++axis)if(!std::isfinite(origin[axis])||!std::isfinite(direction[axis])||std::abs(origin[axis])>1.0e6F||std::abs(direction[axis])>1.01F)return out;
    const auto random=[&](unsigned index,unsigned salt){
        std::uint32_t n=index*747796405U+salt*2891336453U+static_cast<std::uint32_t>(serial);
        n=((n>>((n>>28U)+4U))^n)*277803737U;n=(n>>22U)^n;
        return float(n&0xffffffU)/16777216.0F;
    };
    const auto emit=[&](unsigned id,float age,float life,float born,bool hit){
        if(age<0||age>=life)return;
        const float z=random(id,7)*2-1,angle=random(id,8)*6.283185307F;
        const float radius=std::sqrt(std::max(0.0F,1-z*z));
        const std::array<float,3> velocity{radius*std::cos(angle),z,radius*std::sin(angle)};
        const float damping=hit?7.0F:1.0F,speed=hit?16.0F:1.5F;
        const float travel=speed*(1-std::exp(-damping*age))/damping;
        OriginalSpellParticle p;
        for(unsigned axis=0;axis<3;++axis)p.center[axis]=origin[axis]+direction[axis]*12.0F*born+velocity[axis]*travel;
        const float phase=age/life;
        const float start=(hit?15.0F:4.0F)+(hit?5.0F:9.0F)*random(id,2);
        p.size=.02F*start*(1+((hit?5.0F:7.0F)-1)*phase);
        const float brightness=1.0F-.52F*random(id,3);
        p.color={brightness+(1-brightness)*phase,brightness*(1-phase)+.0157F*phase,
            brightness*(1-phase)+.0431F*phase,std::min(1.0F,age/.1F)*(1-phase)};
        p.rotation=random(id,4)*6.283185307F+age*(-3+6*random(id,5));
        out.push_back(p);
    };
    const int last=static_cast<int>(std::floor(std::min(elapsed,flight_duration)*100));
    for(int i=std::max(0,last-127);i<=last&&out.size()<128;++i){
        const float born=float(i)/100;emit(static_cast<unsigned>(i),elapsed-born,1,born,false);
    }
    if(impact)for(unsigned i=0;i<50;++i){
        const float age=elapsed-flight_duration-float(i)/500;
        emit(10000+i,age,1+random(i,11),flight_duration,true);
    }
    return out;
}
} // namespace hpvr::quest
