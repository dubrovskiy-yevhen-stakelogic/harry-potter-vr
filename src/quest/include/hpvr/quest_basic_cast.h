#pragma once
#include <array>
#include <algorithm>
#include <cmath>
namespace hpvr::quest {
// Basic spellnone input is separate from learned gesture spells.
struct BasicCast {
    std::array<float,3> aim{}, origin{}, destination{};
    bool charging=false, require_release=true, flying=false;
    float age=0, duration=0.3F;
    bool Observe(bool active,bool held,const std::array<float,3>& tip,
                 const std::array<float,3>& hit,float seconds) {
        if(std::isfinite(seconds)&&seconds>0){
            age+=std::min(seconds,0.05F);if(age>=duration)flying=false;
        }
        if(!active){charging=false;require_release=true;flying=false;return false;}
        if(require_release){if(!held)require_release=false;return false;}
        if(held){charging=true;aim=hit;return false;}
        if(!charging)return false;
        charging=false;origin=tip;destination=aim;age=0;flying=true;
        // Keep a short, harmless air puff even when aiming into empty space.
        duration=0.3F;
        return true;
    }
};
}
