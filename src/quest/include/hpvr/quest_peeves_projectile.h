#pragma once
#include "hpvr/quest_peeves_battle.h"
#include "hpvr/quest_grid_motion.h"

namespace hpvr::quest::peeves {
inline constexpr float kAppleRadius=.02F,kAppleSpeed=9.F,kAppleGravity=9.5F;
inline Point BallisticVelocity(const Point& origin,const Point& target,float speed,float gravity){
    const auto delta=grid_motion::Sub(target,origin);
    const float seconds=std::clamp(std::hypot(delta[0],delta[2])/speed,.35F,2.F);
    auto velocity=grid_motion::Scale(delta,1/seconds);
    velocity[1]+=.5F*gravity*seconds;
    return velocity;
}
inline constexpr float kAppleBlastRadius=1.5F;
inline constexpr unsigned kAppleDamagePercent=10,kMaximumApples=16;
struct Apple {
    Point position{},velocity{};
    float age=0,fuse=3.F;
    bool active=false,settled=false;
};
inline bool Throw(Apple& apple,const Point& origin,const Point& target){
    if(apple.active||!FinitePoint(origin)||!FinitePoint(target))return false;
    const auto delta=grid_motion::Sub(target,origin);
    const float length=std::sqrt(grid_motion::Dot(delta,delta));
    if(length<.0001F)return false;
    apple={};apple.position=origin;apple.velocity=BallisticVelocity(origin,target,kAppleSpeed,kAppleGravity);
    apple.active=true;return true;
}
inline float BoxHit(const Point& origin,const Point& movement,const grid_motion::Bounds& box){
    float first=0,last=1;
    for(unsigned axis=0;axis<3;++axis){
        if(std::abs(movement[axis])<1e-8F){
            if(origin[axis]<box.minimum[axis]||origin[axis]>box.maximum[axis])return 2;
        }else{
            float a=(box.minimum[axis]-origin[axis])/movement[axis],b=(box.maximum[axis]-origin[axis])/movement[axis];
            if(a>b)std::swap(a,b);first=std::max(first,a);last=std::min(last,b);
        }
    }
    return first<=last?first:2;
}
template<class Triangles>
unsigned Explode(Apple& apple,const Triangles& triangles,const Point& player,float radius,float half_height){
    apple.active=false;
    auto closest=player;closest[1]=std::clamp(apple.position[1],player[1]-half_height+radius,player[1]+half_height-radius);
    const auto delta=grid_motion::Sub(closest,apple.position);
    const float length=std::sqrt(grid_motion::Dot(delta,delta));
    const float distance=std::max(0.F,length-radius);
    if(distance>=kAppleBlastRadius)return 0;
    const grid_motion::Bounds point{apple.position,apple.position};
    const auto wall=grid_motion::Sweep(triangles,point,delta,0,0);
    if(wall.found&&wall.fraction<.999F)return 0;
    return static_cast<unsigned>(std::ceil(kAppleDamagePercent*(1-distance/kAppleBlastRadius)));
}
template<class Triangles>
unsigned AdvanceApple(Apple& apple,float seconds,const Triangles& triangles,const Point& player,
    float player_radius,float player_half_height,float gravity){
    if(!apple.active||!std::isfinite(seconds)||seconds<=0||!FinitePoint(apple.position)||!FinitePoint(apple.velocity)||
        !std::isfinite(apple.age)||apple.age<0||!std::isfinite(apple.fuse)||
        !FinitePoint(player)||!std::isfinite(gravity)||gravity<0||gravity>100||
        !std::isfinite(player_radius)||player_radius<=0||!std::isfinite(player_half_height)||player_half_height<player_radius)return 0;
    float remaining=std::min(seconds,.25F);
    while(remaining>1e-6F&&apple.active){
        const float dt=std::min(remaining,.01F);remaining-=dt;apple.age+=dt;apple.fuse-=dt;
        if(apple.fuse<=0)return Explode(apple,triangles,player,player_radius,player_half_height);
        if(apple.settled)continue;
        auto movement=grid_motion::Scale(apple.velocity,dt);movement[1]-=.5F*gravity*dt*dt;
        apple.velocity[1]-=gravity*dt;
        const Point extent{kAppleRadius,kAppleRadius,kAppleRadius};
        const grid_motion::Bounds box{grid_motion::Sub(apple.position,extent),grid_motion::Add(apple.position,extent)};
        const auto world=grid_motion::Sweep(triangles,box,movement,0,0);
        const Point player_extent{player_radius+kAppleRadius,player_half_height+kAppleRadius,player_radius+kAppleRadius};
        const float player_hit=BoxHit(apple.position,movement,
            {grid_motion::Sub(player,player_extent),grid_motion::Add(player,player_extent)});
        if(player_hit<=1&&(!world.found||player_hit<world.fraction)){
            apple.position=grid_motion::Add(apple.position,grid_motion::Scale(movement,player_hit));
            return Explode(apple,triangles,player,player_radius,player_half_height);
        }
        if(!world.found){apple.position=grid_motion::Add(apple.position,movement);continue;}
        apple.position=grid_motion::Add(apple.position,grid_motion::Scale(movement,world.fraction));
        auto normal=triangles[world.triangle].normal;
        const float length=std::sqrt(grid_motion::Dot(normal,normal));
        if(length<.0001F){apple.active=false;return 0;}
        normal=grid_motion::Scale(normal,1/length);
        if(grid_motion::Dot(normal,movement)>0)normal=grid_motion::Scale(normal,-1);
        apple.position=grid_motion::Add(apple.position,grid_motion::Scale(normal,.001F));
        apple.velocity=grid_motion::Scale(grid_motion::Sub(apple.velocity,
            grid_motion::Scale(normal,2*grid_motion::Dot(apple.velocity,normal))),.5F);
        if(grid_motion::Dot(apple.velocity,apple.velocity)<.04F){apple.settled=true;apple.velocity={};apple.fuse=std::min(apple.fuse,.1F);}
    }
    return 0;
}
} // namespace hpvr::quest::peeves
