#pragma once

#include "hpvr/quest_peeves.h"
#include <array>
#include <span>
#include <vector>

namespace hpvr::quest::peeves {

using Point=std::array<float,3>;
using Flight=std::vector<Point>;
struct Route {
    Flight entrance,departure;
    std::vector<Flight> patrol;
};
struct Battle {
    Motion motion;
    Point position{};
    unsigned next_patrol=0,waypoint=0;
    bool entrance_flown=false,flight_started=false;
};
struct BattleStep {
    Step presentation;
    unsigned contact_damage=0;
    bool arrived=false;
};
inline bool FinitePoint(const Point& p){
    return std::all_of(p.begin(),p.end(),[](float v){return std::isfinite(v)&&std::abs(v)<2000;});
}
inline bool ValidFlight(const Flight& path){
    return !path.empty()&&path.size()<=128&&std::all_of(path.begin(),path.end(),FinitePoint);
}
inline bool ValidRoute(const Route& route){
    return ValidFlight(route.entrance)&&ValidFlight(route.departure)&&!route.patrol.empty()&&
        route.patrol.size()<=16&&std::all_of(route.patrol.begin(),route.patrol.end(),ValidFlight);
}
inline bool Activate(Battle& b,const Point& position,const Route& route){
    if(!FinitePoint(position)||!ValidRoute(route)||b.motion.phase!=Phase::Dormant)return false;
    b=Battle{};b.position=position;return Activate(b.motion);
}
inline bool Hit(Battle& b){return Hit(b.motion);}
inline float SegmentDistanceSquared(const Point& from,const Point& to,const Point& point){
    float length=0,dot=0;
    for(unsigned i=0;i<3;++i){const float d=to[i]-from[i];length+=d*d;dot+=(point[i]-from[i])*d;}
    const float t=length>0?std::clamp(dot/length,0.F,1.F):0;
    float squared=0;for(unsigned i=0;i<3;++i){const float d=point[i]-from[i]-(to[i]-from[i])*t;squared+=d*d;}
    return squared;
}

// Consume a distance budget across all crossed waypoints, never teleporting
// past a corner when a frame ends in the middle of a flight leg.
inline bool Move(Battle& b,std::span<const Point> path,float distance){
    if(!std::isfinite(distance)||distance<0||path.empty()||path.size()>128||
        b.waypoint>=path.size()||!FinitePoint(b.position)||
        !std::all_of(path.begin(),path.end(),FinitePoint))return false;
    while(b.waypoint<path.size()){
        const auto& target=path[b.waypoint];float squared=0;
        for(unsigned i=0;i<3;++i){const float d=target[i]-b.position[i];squared+=d*d;}
        const float length=std::sqrt(squared);
        if(length>distance&&length>.00001F){
            for(unsigned i=0;i<3;++i)b.position[i]+=(target[i]-b.position[i])*distance/length;
            return false;
        }
        b.position=target;distance=std::max(0.F,distance-length);++b.waypoint;
    }
    return true;
}

inline BattleStep Advance(Battle& b,const Route& route,float seconds,const Timings& timings,
    const Point& player_center,float contact_radius){
    BattleStep out;out.presentation=Presentation(b.motion);
    if(!std::isfinite(seconds)||seconds<=0||!ValidRoute(route)||!FinitePoint(b.position)||
        !FinitePoint(player_center)||!std::isfinite(contact_radius)||contact_radius<0||contact_radius>10||
        b.next_patrol>=route.patrol.size())return out;
    // Small substeps retain contact and animation events even on slow frames.
    float remaining=std::min(seconds,.25F);
    bool completed=false,projectile=false;
    while(remaining>.000001F){
        const float step=std::min(remaining,.01F);remaining-=step;
        const auto phase=b.motion.phase;const auto from=b.position;
        const auto pose=Advance(b.motion,step,timings);
        completed|=pose.completed;projectile|=pose.throw_projectile;
        const bool flying=phase==Phase::Patrol||phase==Phase::Departing;
        if(flying&&b.motion.phase==phase){
            if(!b.flight_started){b.waypoint=0;b.flight_started=true;}
            const auto& path=phase==Phase::Departing?route.departure:
                !b.entrance_flown?route.entrance:route.patrol[b.next_patrol];
            if(Move(b,path,step*(phase==Phase::Departing?2.F:8.F))){
                if(phase==Phase::Patrol){
                    if(b.entrance_flown)b.next_patrol=(b.next_patrol+1)%static_cast<unsigned>(route.patrol.size());
                    b.entrance_flown=true;
                }
                out.arrived|=Arrive(b.motion);b.flight_started=false;b.waypoint=0;
            }
        }
        if(SegmentDistanceSquared(from,b.position,player_center)<=contact_radius*contact_radius)
            out.contact_damage+=Contact(b.motion);
    }
    out.presentation=Presentation(b.motion);out.presentation.completed=completed;
    out.presentation.throw_projectile=projectile;return out;
}

} // namespace hpvr::quest::peeves
