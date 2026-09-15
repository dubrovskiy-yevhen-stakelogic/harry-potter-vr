#pragma once
#include <algorithm>
#include <cmath>

namespace hpvr::quest {
struct HousePointHud {
    unsigned target=0,from=0;
    float elapsed=0,remaining=0;
    bool initialized=false;
    void Reset(unsigned total){target=from=std::min(total,1000000U);elapsed=remaining=0;initialized=true;}
    unsigned Value()const{
        const float phase=std::clamp(elapsed/.8F,0.0F,1.0F);
        return from+static_cast<unsigned>(float(target-from)*phase);
    }
    void Advance(unsigned total,float seconds,bool paused){
        total=std::min(total,1000000U);
        if(!initialized||total<target){Reset(total);return;}
        if(total>target){from=Value();target=total;elapsed=0;remaining=4;}
        if(paused||!std::isfinite(seconds)||seconds<=0)return;
        const float step=std::min(seconds,.1F);
        elapsed=std::min(.8F,elapsed+step);remaining=std::max(0.0F,remaining-step);
    }
};
}
