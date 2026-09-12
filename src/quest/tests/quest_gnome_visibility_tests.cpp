#include "hpvr/quest_gnome_visibility.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using Point=std::array<float,3>;
struct Triangle {std::array<Point,3> vertices{};Point minimum{},maximum{};};
void Check(bool value,const char* label){if(!value){std::cerr<<label<<'\n';std::exit(1);}}
Triangle Make(Point a,Point b,Point c){
    Triangle result{{a,b,c},a,a};
    for(const auto& vertex:result.vertices)for(unsigned axis=0;axis<3;++axis){
        result.minimum[axis]=std::min(result.minimum[axis],vertex[axis]);
        result.maximum[axis]=std::max(result.maximum[axis],vertex[axis]);
    }
    return result;
}
Point Subtract(Point a,Point b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Point Cross(Point a,Point b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
float Dot(Point a,Point b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
// Preserve the previous full-scene nearest-ray implementation as the parity
// oracle, including its .01 m near clip and .03 m target-surface tolerance.
bool Previous(const std::vector<Triangle>& triangles,Point origin,Point target){
    const auto delta=Subtract(target,origin);const float length=std::sqrt(Dot(delta,delta));
    if(length<.01F)return true;if(length>24||!std::isfinite(length))return false;
    const float inverse=1/length;const Point direction{delta[0]*inverse,delta[1]*inverse,delta[2]*inverse};
    float nearest=24;
    for(const auto& triangle:triangles){
        const auto e1=Subtract(triangle.vertices[1],triangle.vertices[0]);
        const auto e2=Subtract(triangle.vertices[2],triangle.vertices[0]);
        const auto p=Cross(direction,e2);const float det=Dot(e1,p);if(std::abs(det)<1e-7F)continue;
        const auto s=Subtract(origin,triangle.vertices[0]);const float u=Dot(s,p)/det;if(u<0||u>1)continue;
        const auto q=Cross(s,e1);const float v=Dot(direction,q)/det;if(v<0||u+v>1)continue;
        const float distance=Dot(e2,q)/det;if(distance>=.01F)nearest=std::min(nearest,distance);
    }
    return nearest+.03F>=length;
}
}
int main(){
    using hpvr::quest::gnome::CanSee;
    std::vector<Triangle> scene{Make({1,-3,-3},{1,3,-3},{1,0,3})};
    Check(!CanSee(scene,{0,0,0},{2,0,0}),"blocking wall stops sight");
    Check(CanSee(scene,{0,0,0},{.8F,0,0}),"wall beyond target cannot block");
    Check(CanSee(scene,{0,0,0},{1.02F,0,0}),"target surface tolerance preserved");
    Check(!CanSee(scene,{0,0,0},{1.04F,0,0}),"past surface tolerance is blocked");
    Check(CanSee(scene,{.995F,0,0},{2,0,0}),"near-origin contact clip preserved");
    Check(CanSee(scene,{1.001F,0,0},{2,0,0}),"wall behind origin ignored");
    Check(CanSee(scene,{0,0,0},{0,0,.005F}),"coincident target visible");
    Check(CanSee(scene,{0,0,0},{0,0,24}),"exact ray range visible");
    Check(!CanSee(scene,{0,0,0},{0,0,24.01F}),"original maximum ray range retained");
    Check(!CanSee(scene,{0,0,0},{0,std::numeric_limits<float>::quiet_NaN(),0}),"invalid target rejected");
    std::uint32_t random=0x34818ac3;
    const auto sample=[&](){random=random*1664525U+1013904223U;return (static_cast<float>(random>>8)/16777216.0F-.5F)*30;};
    for(unsigned i=0;i<300;++i){const Point p{sample(),sample(),sample()};
        scene.push_back(Make(p,{p[0]+sample()*.1F,p[1]+sample()*.1F,p[2]+sample()*.1F},
            {p[0]+sample()*.1F,p[1]+sample()*.1F,p[2]+sample()*.1F}));}
    unsigned visible=0,blocked=0;
    for(unsigned i=0;i<4096;++i){const Point origin{sample(),sample(),sample()},target{sample(),sample(),sample()};
        const bool actual=CanSee(scene,origin,target);
        Check(actual==Previous(scene,origin,target),"segment broad phase must match old full-scene ray");
        actual?++visible:++blocked;
    }
    Check(visible>100&&blocked>100,"comparison contains both clear and occluded segments");
    std::cout<<"GNOME_VISIBILITY=PASS comparisons=4096 visible="<<visible<<" blocked="<<blocked<<" allocation=NONE\n";
}
