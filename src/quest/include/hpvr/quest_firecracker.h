#pragma once
#include "hpvr/quest_peeves_projectile.h"

namespace hpvr::quest::cracker {
using Point=grid_motion::Vec;
inline constexpr unsigned kCapacity=16;
inline constexpr float kRadius=.06F,kGravity=11.F,kBlastRadius=1.F;
enum class Phase { Inactive, Flying, Ground, Carried };
struct Cracker {
    Point position{},velocity{};
    float fuse=4.5F,age=0;
    Phase phase=Phase::Inactive;
    bool returned=false;
};
struct Result { unsigned player_damage=0;bool boss_hit=false,exploded=false,landed=false; };
inline bool Active(const Cracker& c){return c.phase!=Phase::Inactive;}
inline bool Spawn(Cracker& c,const Point& origin,const Point& target,float fuse){
    if(Active(c)||!peeves::FinitePoint(origin)||!peeves::FinitePoint(target)||!std::isfinite(fuse)||fuse<=0||fuse>5)return false;
    const auto d=grid_motion::Sub(target,origin);const float length=std::sqrt(grid_motion::Dot(d,d));
    if(length<.001F)return false;
    c={};c.position=origin;c.velocity=peeves::BallisticVelocity(origin,target,9.F,kGravity);
    c.fuse=fuse;c.phase=Phase::Flying;return true;
}
inline bool PickUp(Cracker& c){if(c.phase!=Phase::Ground)return false;c.phase=Phase::Carried;c.velocity={};c.fuse=std::max(1.F,c.fuse);return true;}
inline bool Release(Cracker& c,const Point& origin,const Point& direction){
    if(c.phase!=Phase::Carried||!peeves::FinitePoint(origin)||!peeves::FinitePoint(direction))return false;
    const float length=std::hypot(direction[0],direction[2]);if(length<.001F)return false;
    c.position=origin;c.velocity={direction[0]*11.F/length,4.5F,direction[2]*11.F/length};
    c.fuse=std::max(c.fuse,1.5F);
    c.phase=Phase::Flying;c.returned=true;c.age=0;return true;
}
template<class Triangles>
float Blast(const Point& origin,const Point& target,float radius,float half_height,const Triangles& triangles){
    auto nearest=target;nearest[1]=std::clamp(origin[1],target[1]-half_height+radius,target[1]+half_height-radius);
    const auto d=grid_motion::Sub(nearest,origin);const float length=std::sqrt(grid_motion::Dot(d,d));
    if(length-radius>=kBlastRadius)return 0;
    const auto wall=grid_motion::Sweep(triangles,{origin,origin},d,0,0);
    if(wall.found&&wall.fraction<.999F)return 0;
    return 1-std::max(0.F,length-radius)/kBlastRadius;
}
template<class Triangles>
Result Explode(Cracker& c,const Triangles& triangles,const Point& player,const Point& boss,float radius,float half_height){
    c.phase=Phase::Inactive;
    return {Blast(c.position,player,radius,half_height,triangles)>0?14U:0U,
        c.returned&&Blast(c.position,boss,.32F,.42F,triangles)>0,true,false};
}
template<class Triangles>
Result Advance(Cracker& c,float seconds,const Triangles& triangles,const Point& player,const Point& boss,
               float player_radius,float player_half_height){
    Result result;
    if(!Active(c)||!std::isfinite(seconds)||seconds<=0||!peeves::FinitePoint(c.position)||
       !peeves::FinitePoint(c.velocity)||!std::isfinite(c.fuse)||!std::isfinite(c.age)||c.age<0||
       !peeves::FinitePoint(player)||!peeves::FinitePoint(boss)||!std::isfinite(player_radius)||
       !std::isfinite(player_half_height)||player_radius<=0||player_half_height<player_radius)return result;
    float remaining=std::min(seconds,.25F);
    while(remaining>1e-6F){
        const float dt=std::min(remaining,.01F);remaining-=dt;c.age+=dt;c.fuse-=dt;
        if(c.phase==Phase::Carried){c.fuse=std::max(1.F,c.fuse);continue;}
        if(c.fuse<=0)return Explode(c,triangles,player,boss,player_radius,player_half_height);
        if(c.phase==Phase::Ground)continue;
        auto movement=grid_motion::Scale(c.velocity,dt);movement[1]-=.5F*kGravity*dt*dt;c.velocity[1]-=kGravity*dt;
        const Point extent{kRadius,kRadius,kRadius};
        const auto world=grid_motion::Sweep(triangles,{grid_motion::Sub(c.position,extent),grid_motion::Add(c.position,extent)},movement,0,0);
        const auto victim=c.returned?boss:player;
        const Point target_extent{(c.returned?.32F:player_radius)+kRadius,(c.returned?.42F:player_half_height)+kRadius,(c.returned?.32F:player_radius)+kRadius};
        const float hit=peeves::BoxHit(c.position,movement,{grid_motion::Sub(victim,target_extent),grid_motion::Add(victim,target_extent)});
        if(hit<=1&&(!world.found||hit<=world.fraction)){
            c.position=grid_motion::Add(c.position,grid_motion::Scale(movement,hit));
            return Explode(c,triangles,player,boss,player_radius,player_half_height);
        }
        if(!world.found){c.position=grid_motion::Add(c.position,movement);continue;}
        c.position=grid_motion::Add(c.position,grid_motion::Scale(movement,world.fraction));
        auto normal=triangles[world.triangle].normal;const float length=std::sqrt(grid_motion::Dot(normal,normal));
        if(length<.001F){c.phase=Phase::Inactive;return result;}
        normal=grid_motion::Scale(normal,1/length);if(grid_motion::Dot(normal,movement)>0)normal=grid_motion::Scale(normal,-1);
        c.position=grid_motion::Add(c.position,grid_motion::Scale(normal,.002F));
        if(normal[1]>.8F){
            if(c.returned)return Explode(c,triangles,player,boss,player_radius,player_half_height);
            c.phase=Phase::Ground;c.velocity={};result.landed=true;
        }else {c.velocity[0]=c.velocity[2]=0;}
    }
    return result;
}
} // namespace hpvr::quest::cracker
