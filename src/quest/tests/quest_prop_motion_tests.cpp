#include "hpvr/quest_prop_motion.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace hpvr::quest::props;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Near(float a,float b){return std::abs(a-b)<.00001F;}
}
int main(){try{
    const Point start{0,.5F,0},landing{2,.18F,0};
    const auto clear=BuildEmissionPath(start,landing,[](const Point&,const Point&){return 1.0F;},[](const Point& p){return p;});
    Check(clear.front()==start&&clear.back()==landing,"clear arc preserves endpoints");
    Check(clear[8][1]>.9F,"clear arc has an upward flight");
    Check(SampleEmissionPath(clear,0)==start&&SampleEmissionPath(clear,1)==landing,"path interpolation preserves endpoints");
    unsigned sweeps=0;
    const auto wall=[&](const Point& from,const Point& to){
        ++sweeps;
        if(to[0]>.9F&&to[0]>from[0])return std::max(0.0F,(.9F-from[0])/(to[0]-from[0]));
        if(to[1]<.18F&&to[1]<from[1])return (.18F-from[1])/(to[1]-from[1]);
        return 1.0F;
    };
    const auto blocked=BuildEmissionPath(start,landing,wall,[](const Point& p){return Point{20,.18F,p[2]};});
    Check(sweeps==16,"path construction has bounded sweep count");
    for(const auto& p:blocked)Check(p[0]<=.90001F&&p[1]>=.17999F,"all blocked-arc samples remain on reachable side");
    Check(Near(blocked.back()[0],.9F)&&Near(blocked.back()[1],.18F),"blocked reward settles near wall");
    for(unsigned index=0;index<=100;++index)Check(SampleEmissionPath(blocked,float(index)/100)[0]<=.90001F,"interpolated path never crosses wall");
    const auto stopped=BuildEmissionPath(start,landing,[](const Point&,const Point&){return 0.0F;},[](const Point& p){return p;});
    for(const auto& p:stopped)Check(p==start,"fully obstructed path remains accessible at source");
    std::cout<<"PROP_MOTION_TESTS=PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
