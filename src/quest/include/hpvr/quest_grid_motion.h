#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace hpvr::quest::grid_motion {
using Vec = std::array<float,3>;
struct Bounds { Vec minimum{},maximum{}; };
struct State {
    float vertical_velocity=0,retirement_y=0;
    bool initialized=false,grounded=false,dynamic_support=false,finish_pending=false,retired=false;
};
inline bool RestoredCompletionPending(bool completed,const Vec& offset,const Vec& target){
    return !completed&&(std::hypot(target[0]-offset[0],target[2]-offset[2])>.001F||
        std::abs(offset[0])>.001F||std::abs(offset[1])>.001F||std::abs(offset[2])>.001F);
}
struct Hit {
    float fraction=1;
    std::size_t triangle=std::numeric_limits<std::size_t>::max();
    bool found=false;
};
inline Vec Add(Vec a,const Vec& b){for(unsigned i=0;i<3;++i)a[i]+=b[i];return a;}
inline Vec Sub(Vec a,const Vec& b){for(unsigned i=0;i<3;++i)a[i]-=b[i];return a;}
inline Vec Scale(Vec a,float scale){for(auto& v:a)v*=scale;return a;}
inline float Dot(const Vec& a,const Vec& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline Vec Cross(const Vec& a,const Vec& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
inline Bounds Translate(Bounds box,const Vec& offset){box.minimum=Add(box.minimum,offset);box.maximum=Add(box.maximum,offset);return box;}

// Sweep the complete box, including thin walls between face samples. Parallel
// floor contact and contacts we are leaving do not obstruct a slide out of a niche.
template<class Triangle>
bool SweepTriangle(const Bounds& box,const Vec& move,const Triangle& triangle,float* fraction){
    for(unsigned i=0;i<3;++i){
        if(std::max(box.maximum[i],box.maximum[i]+move[i])<triangle.minimum[i]-1e-5F||
           std::min(box.minimum[i],box.minimum[i]+move[i])>triangle.maximum[i]+1e-5F)return false;
    }
    const auto center=Scale(Add(box.minimum,box.maximum),.5F);
    const auto half=Scale(Sub(box.maximum,box.minimum),.5F);
    const std::array<Vec,3> edges{Sub(triangle.vertices[1],triangle.vertices[0]),
        Sub(triangle.vertices[2],triangle.vertices[1]),Sub(triangle.vertices[0],triangle.vertices[2])};
    float enter=-std::numeric_limits<float>::infinity(),leave=std::numeric_limits<float>::infinity();
    const auto axis_test=[&](const Vec& axis){
        const float length_squared=Dot(axis,axis);
        if(length_squared<1e-12F)return true;
        const float epsilon=.0002F*std::sqrt(length_squared);
        float low=Dot(Sub(triangle.vertices[0],center),axis),high=low;
        for(unsigned i=1;i<3;++i){const float value=Dot(Sub(triangle.vertices[i],center),axis);low=std::min(low,value);high=std::max(high,value);}
        const float extent=half[0]*std::abs(axis[0])+half[1]*std::abs(axis[1])+half[2]*std::abs(axis[2]);
        if(std::abs(low-extent)<=epsilon)low=extent;
        if(std::abs(high+extent)<=epsilon)high=-extent;
        const float speed=Dot(move,axis);
        if(std::abs(speed)<1e-9F*std::sqrt(length_squared))return extent>low+epsilon&&-extent<high-epsilon;
        float first=(low-extent)/speed,last=(high+extent)/speed;
        if(first>last)std::swap(first,last);
        enter=std::max(enter,first);leave=std::min(leave,last);
        return enter<=leave+1e-6F;
    };
    const std::array<Vec,3> basis{{{1,0,0},{0,1,0},{0,0,1}}};
    for(const auto& axis:basis)if(!axis_test(axis))return false;
    if(!axis_test(Cross(edges[0],edges[1])))return false;
    for(const auto& edge:edges)for(const auto& axis:basis)if(!axis_test(Cross(edge,axis)))return false;
    if(leave<=1e-6F||enter>1||leave<0)return false;
    *fraction=std::max(0.0F,enter);return true;
}
template<class Triangles>
Hit Sweep(const Triangles& triangles,const Bounds& box,const Vec& move,std::size_t skip_first,std::size_t skip_count,bool support_only=false){
    Hit result;
    for(std::size_t i=0;i<triangles.size();++i){
        if(i>=skip_first&&i-skip_first<skip_count)continue;
        if(support_only&&std::abs(triangles[i].normal[1])<.55F)continue;
        float fraction=1;
        if(SweepTriangle(box,move,triangles[i],&fraction)&&(!result.found||fraction<result.fraction))result={fraction,i,true};
    }
    return result;
}
struct StepResult { Vec offset{}; bool blocked=false,left_support=false,landed=false; };
template<class Triangles>
StepResult Advance(State& state,const Bounds& box,const Vec& horizontal,float seconds,const Triangles& triangles,
                   std::size_t static_count,std::size_t skip_first,std::size_t skip_count){
    StepResult result;
    if(!std::isfinite(seconds)||seconds<=0)return result;
    if(state.retired)return result;
    seconds=std::min(seconds,.05F);
    const bool moving=std::hypot(horizontal[0],horizontal[2])>1e-7F;
    // Static support cannot disappear: settled, idle blocks cost no world scan.
    if(state.initialized&&state.grounded&&!state.dynamic_support&&!moving)return result;
    if(!state.initialized){
        float lowest=std::numeric_limits<float>::infinity();
        for(std::size_t i=0;i<std::min(static_count,triangles.size());++i)lowest=std::min(lowest,triangles[i].minimum[1]);
        if(!std::isfinite(lowest))lowest=box.minimum[1]-100;
        state.retirement_y=lowest-(box.maximum[1]-box.minimum[1])-2;
        state.initialized=true;
    }
    if(box.minimum[1]<=state.retirement_y){state.retired=true;state.grounded=false;state.vertical_velocity=0;return result;}
    if(moving){
        const auto hit=Sweep(triangles,box,horizontal,skip_first,skip_count);
        result.offset=Scale(horizontal,hit.found?hit.fraction:1.0F);result.blocked=hit.found&&hit.fraction<1;
    }
    const auto shifted=Translate(box,result.offset);
    const auto support=Sweep(triangles,shifted,{0,-.012F,0},skip_first,skip_count,true);
    if(support.found&&std::abs(triangles[support.triangle].normal[1])>=.55F){
        result.offset[1]=-.012F*support.fraction;result.landed=!state.grounded;
        state.grounded=true;state.vertical_velocity=0;state.dynamic_support=support.triangle>=static_count;return result;
    }
    result.left_support=state.grounded||moving;state.grounded=false;state.dynamic_support=false;
    // Falling is independent of the old horizontal target's height. Sweep the
    // full downward step so even a thin floor catches a fast-falling block.
    state.vertical_velocity=std::max(-30.0F,state.vertical_velocity-19.0F*seconds);
    const Vec fall{0,state.vertical_velocity*seconds,0};
    const auto landing=Sweep(triangles,shifted,fall,skip_first,skip_count,true);
    result.offset=Add(result.offset,Scale(fall,landing.found?landing.fraction:1.0F));
    if(landing.found){result.landed=true;state.grounded=true;state.vertical_velocity=0;state.dynamic_support=landing.triangle>=static_count;}
    if(shifted.minimum[1]+result.offset[1]<=state.retirement_y){
        // The entire block is now below every authored solid. Stop simulation
        // here rather than scan forever or let finite saved offsets overflow.
        result.offset[1]=state.retirement_y-box.minimum[1];
        state.retired=true;state.grounded=false;state.vertical_velocity=0;
    }
    return result;
}
} // namespace hpvr::quest::grid_motion
