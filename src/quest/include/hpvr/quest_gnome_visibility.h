#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace hpvr::quest::gnome {

// Same near-contact and visibility tolerance as the scene's targeting ray,
// but only the segment to Harry matters. Triangle bounds reject distant rooms
// before any ray/triangle work; the first blocker finishes the query. No
// allocation, spatial-cache rebuild, or frame-dependent visibility delay.
template<class Triangles>
bool CanSee(const Triangles& triangles,const std::array<float,3>& origin,
            const std::array<float,3>& target) {
    std::array<float,3> delta{},minimum{},maximum{};
    for(unsigned axis=0;axis<3;++axis){
        if(!std::isfinite(origin[axis])||!std::isfinite(target[axis]))return false;
        delta[axis]=target[axis]-origin[axis];
        // A conservative pad retains edge/coplanar hits despite rounding in
        // transformed mover bounds. The exact intersection still decides.
        minimum[axis]=std::min(origin[axis],target[axis])-.0001F;
        maximum[axis]=std::max(origin[axis],target[axis])+.0001F;
    }
    const auto dot=[](const auto& a,const auto& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    const auto subtract=[](const auto& a,const auto& b)->std::array<float,3>{return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};};
    const auto cross=[](const auto& a,const auto& b)->std::array<float,3>{
        return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    };
    const float distance=std::sqrt(dot(delta,delta));
    if(distance<.01F)return true;
    if(!std::isfinite(distance)||distance>24)return false;
    const float inverse=1/distance;
    const std::array<float,3> direction{delta[0]*inverse,delta[1]*inverse,delta[2]*inverse};
    for(const auto& triangle:triangles){
        if(triangle.maximum[0]<minimum[0]||triangle.minimum[0]>maximum[0]||
           triangle.maximum[1]<minimum[1]||triangle.minimum[1]>maximum[1]||
           triangle.maximum[2]<minimum[2]||triangle.minimum[2]>maximum[2])continue;
        const auto e1=subtract(triangle.vertices[1],triangle.vertices[0]);
        const auto e2=subtract(triangle.vertices[2],triangle.vertices[0]);
        const auto p=cross(direction,e2);const float determinant=dot(e1,p);
        if(std::abs(determinant)<1e-7F)continue;
        const auto relative=subtract(origin,triangle.vertices[0]);
        const float u=dot(relative,p)/determinant;if(u<0||u>1)continue;
        const auto q=cross(relative,e1);const float v=dot(direction,q)/determinant;
        if(v<0||u+v>1)continue;
        const float hit=dot(e2,q)/determinant;
        if(hit>=.01F&&hit+.03F<distance)return false;
    }
    return true;
}

} // namespace hpvr::quest::gnome
