#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace hpvr::quest::props {
using Point=std::array<float,3>;
using EmissionPath=std::array<Point,17>;
inline Point Lerp(const Point& a,const Point& b,float fraction){
    Point result{};
    for(unsigned axis=0;axis<3;++axis)result[axis]=a[axis]+(b[axis]-a[axis])*fraction;
    return result;
}
// Sweep returns a safe [0,1] fraction, already accounting for pickup radius.
// A blocked arc drops on the reachable side of the surface rather than letting
// an adjusted destination send the intermediate parabola through the same wall.
template<class Sweep,class Ground>
EmissionPath BuildEmissionPath(const Point& start,const Point& landing,Sweep&& sweep,Ground&& ground){
    EmissionPath result{};result[0]=start;
    bool falling=false;Point fall_start{},fall_end{};std::size_t fall_index=0;
    for(std::size_t index=1;index<result.size();++index){
        Point target{};
        if(falling){
            const float fraction=float(index-fall_index)/float(result.size()-1-fall_index);
            target=Lerp(fall_start,fall_end,fraction);
        }else{
            const float t=float(index)/float(result.size()-1);
            target=Lerp(start,landing,t);target[1]+=.6F*4*t*(1-t);
        }
        float fraction=sweep(result[index-1],target);
        if(!std::isfinite(fraction))fraction=0;
        fraction=std::clamp(fraction,0.0F,1.0F);
        result[index]=Lerp(result[index-1],target,fraction);
        if(!falling&&fraction<1){
            falling=true;fall_start=result[index];fall_end=ground(fall_start);fall_index=index;
            // Keep settling vertical, even if a caller's broad-phase floor query
            // suggests a point on the other side of the blocking surface.
            fall_end[0]=fall_start[0];fall_end[2]=fall_start[2];
            if(!std::isfinite(fall_end[1])||fall_end[1]>fall_start[1])fall_end[1]=fall_start[1];
        }
    }
    return result;
}
inline Point SampleEmissionPath(const EmissionPath& path,float elapsed){
    if(!std::isfinite(elapsed))elapsed=0;
    const float phase=std::clamp(elapsed,0.0F,1.0F)*float(path.size()-1);
    const auto first=std::min(path.size()-1,static_cast<std::size_t>(phase));
    return Lerp(path[first],path[std::min(first+1,path.size()-1)],phase-float(first));
}
} // namespace hpvr::quest::props
