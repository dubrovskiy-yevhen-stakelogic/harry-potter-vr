#pragma once
#include "hpvr/quest_grid_motion.h"
#include <cstdint>

namespace hpvr::quest::malfoy {
using Point=grid_motion::Vec;
enum class Phase { Idle, Patrol, Wait, Throw, Hit, Knockdown, DefeatPause, Complete, Lost };
struct Config {
    Point rail_start{},rail_end{};
    float start_speed=2.F,end_speed=2.75F;
    unsigned maximum_hits=3;
};
struct Timings { float breath=1,throwing=1,knockback=1,knockdown=1; };
struct State {
    Phase phase=Phase::Idle;
    Point position{},target{};
    float elapsed=0;
    unsigned hits=0,volley=0,projectile_count=0;
    std::uint32_t random=0x8f137b21U;
    bool moving_left=false,direction_change=true,first_throw=false,emitted=false;
};
struct Step {
    const char* clip="lookdownhall";
    float clip_seconds=0,clip_rate=1,projectile_fuse=4.5F;
    bool loop=true,throw_projectile=false,first_throw=false,defeated=false;
};
inline bool Finite(const Point& p){return std::all_of(p.begin(),p.end(),[](float v){return std::isfinite(v)&&std::abs(v)<2000;});}
inline float Distance(const Point& a,const Point& b){return std::hypot(a[0]-b[0],a[2]-b[2]);}
inline bool Valid(const Config& c){return Finite(c.rail_start)&&Finite(c.rail_end)&&Distance(c.rail_start,c.rail_end)>.01F&&
    std::isfinite(c.start_speed)&&c.start_speed>0&&c.start_speed<=20&&std::isfinite(c.end_speed)&&c.end_speed>0&&c.end_speed<=20&&
    c.maximum_hits>0&&c.maximum_hits<=100;}
inline float Random(State& s){s.random^=s.random<<13;s.random^=s.random>>17;s.random^=s.random<<5;return float(s.random>>8)*(1.F/16777216.F);}
inline float Speed(const State& s,const Config& c){return c.start_speed+(c.end_speed-c.start_speed)*float(s.hits)/c.maximum_hits;}
inline void ChooseTarget(State& s,const Config& c){
    const auto rail=grid_motion::Sub(c.rail_end,c.rail_start);
    for(unsigned i=0;i<5;++i){
        s.target=grid_motion::Add(c.rail_start,grid_motion::Scale(rail,Random(s)));
        s.target[1]=s.position[1];
        if(Distance(s.target,s.position)>Distance(c.rail_start,c.rail_end)/5)break;
    }
    s.moving_left=grid_motion::Dot(rail,grid_motion::Sub(s.target,s.position))<0;
}
inline bool Active(const State& s){return s.phase>=Phase::Patrol&&s.phase<=Phase::Hit;}
inline bool Activate(State& s,const Config& c,const Point& position){
    if(s.phase!=Phase::Idle||!Valid(c)||!Finite(position))return false;
    const auto seed=s.random;s={};s.random=seed?seed:0x8f137b21U;s.position=position;
    ChooseTarget(s,c);s.phase=Phase::Patrol;return true;
}
inline bool Hit(State& s,const Config& c){
    if(!Valid(c)||!Active(s)||s.phase==Phase::Hit)return false;
    ++s.hits;s.phase=s.hits>=c.maximum_hits?Phase::Knockdown:Phase::Hit;s.elapsed=0;return true;
}
inline bool Lose(State& s){
    if(!Active(s)&&s.phase!=Phase::Knockdown&&s.phase!=Phase::DefeatPause)return false;
    s.phase=Phase::Lost;s.elapsed=0;return true;
}
inline Step Presentation(const State& s){
    Step out;out.clip_seconds=s.elapsed;
    switch(s.phase){
    case Phase::Patrol:out.clip=s.moving_left?"strafeleft":"straferight";out.clip_rate=2;break;
    case Phase::Wait:out.clip="breathe";out.loop=false;break;
    case Phase::Throw:out.clip="throw";out.loop=false;break;
    case Phase::Hit:out.clip="knockback";out.clip_rate=2;out.loop=false;break;
    case Phase::Knockdown:out.clip="knockdown";out.loop=false;break;
    case Phase::DefeatPause:out.clip="knockdown";out.loop=false;break;
    default:break;
    }
    out.clip_seconds*=out.clip_rate;return out;
}
inline float Duration(float t){return std::isfinite(t)&&t>0?std::clamp(t,.01F,30.F):1.F;}
inline Step Advance(State& s,const Config& c,const Timings& t,float seconds){
    auto out=Presentation(s);
    if(!Valid(c)||!Finite(s.position)||!Finite(s.target)||!std::isfinite(seconds)||seconds<=0)return out;
    bool first=false,thrown=false,defeated=false;float fuse=4.5F;
    float remaining=std::min(seconds,.25F);
    while(remaining>1e-6F){
        const float dt=std::min(remaining,.01F);remaining-=dt;s.elapsed+=dt;
        const auto next=[&](Phase p){s.phase=p;s.elapsed=0;};
        switch(s.phase){
        case Phase::Patrol:{
            const float d=Distance(s.position,s.target),travel=Speed(s,c)*dt;
            if(d>0){const float move=std::min(d,travel*(d<.5F?d+.5F:1.F));
                for(unsigned a:{0U,2U})s.position[a]+=(s.target[a]-s.position[a])*move/d;}
            if(Distance(s.position,s.target)<travel){
                if(s.direction_change&&Random(s)<.25F){s.direction_change=false;next(Phase::Wait);}
                else{
                    s.volley=static_cast<unsigned>(Random(s)*static_cast<unsigned>(float(s.hits)*5/c.maximum_hits));
                    s.direction_change=true;s.emitted=false;next(Phase::Throw);
                    if(!s.first_throw){s.first_throw=true;first=true;}
                }
            }
            break;
        }
        case Phase::Wait:
            if(s.elapsed>=Duration(t.breath)){ChooseTarget(s,c);next(Phase::Patrol);}break;
        case Phase::Throw:
            if(!s.emitted&&s.elapsed+.000001F>=.3F){
                s.emitted=true;thrown=true;s.projectile_count=(s.projectile_count+1)%3;
                fuse=s.projectile_count==0?2.75F:4.5F;
            }
            if(s.volley&&s.elapsed+.000001F>=.6F){--s.volley;s.emitted=false;next(Phase::Throw);}
            else if(!s.volley&&s.elapsed>=std::max(.3F,Duration(t.throwing))){ChooseTarget(s,c);next(Phase::Patrol);}
            break;
        case Phase::Hit:if(s.elapsed>=Duration(t.knockback)/2)next(Phase::Patrol);break;
        case Phase::Knockdown:if(s.elapsed>=Duration(t.knockdown))next(Phase::DefeatPause);break;
        case Phase::DefeatPause:if(s.elapsed>=1){next(Phase::Complete);defeated=true;}break;
        default:s.elapsed=0;remaining=0;break;
        }
    }
    out=Presentation(s);if(s.phase==Phase::DefeatPause)out.clip_seconds=Duration(t.knockdown);
    out.first_throw=first;out.throw_projectile=thrown;out.projectile_fuse=fuse;out.defeated=defeated;return out;
}
} // namespace hpvr::quest::malfoy
