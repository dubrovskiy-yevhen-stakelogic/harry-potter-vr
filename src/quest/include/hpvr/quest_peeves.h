#pragma once

#include <algorithm>
#include <cmath>

namespace hpvr::quest::peeves {

inline constexpr unsigned kMaximumHits=4;
inline constexpr unsigned kContactDamagePercent=20;
inline constexpr float kContactCooldown=1;

enum class Phase { Dormant, Grab, IntroFloat, Patrol, Taunt, ThrowStart, ThrowEnd, Hit, DepartureDelay, DeparturePause, Departing, Complete };
struct Timings {
    float grab=1,scheming=1,look=1,throw_start=1,throw_end=1,hit=1;
};
struct Motion {
    Phase phase=Phase::Dormant;
    unsigned hits_left=kMaximumHits,taunt=0;
    float elapsed=0,contact_cooldown=0;
    bool completion_sent=false;
};
struct Step {
    const char* clip="breathe";
    float clip_seconds=0,clip_rate=1,opacity=1;
    bool loop=true,moving=false,throw_projectile=false,completed=false;
};

inline bool Active(const Motion& m){
    return m.phase!=Phase::Dormant&&m.phase!=Phase::DepartureDelay&&m.phase!=Phase::DeparturePause&&
        m.phase!=Phase::Departing&&m.phase!=Phase::Complete;
}
inline bool Vulnerable(const Motion& m){return Active(m)&&m.phase!=Phase::Patrol&&m.hits_left>0;}
inline float Health(const Motion& m){return float(std::min(m.hits_left,kMaximumHits))/kMaximumHits;}
inline bool Activate(Motion& m){
    if(m.phase!=Phase::Dormant)return false;
    m=Motion{};m.phase=Phase::Grab;return true;
}
inline bool Hit(Motion& m){
    if(!Vulnerable(m))return false;
    --m.hits_left;m.phase=Phase::Hit;m.elapsed=0;return true;
}
inline bool Arrive(Motion& m){
    if(m.phase==Phase::Patrol){m.phase=Phase::Taunt;m.elapsed=0;return true;}
    if(m.phase==Phase::Departing){m.phase=Phase::Complete;m.elapsed=0;return true;}
    return false;
}
inline unsigned Contact(Motion& m){
    if(!Active(m)||m.contact_cooldown>0||m.hits_left==0)return 0;
    m.contact_cooldown=kContactCooldown;return kContactDamagePercent;
}
inline float Duration(float v){return std::isfinite(v)&&v>0?std::clamp(v,.01F,30.F):1.F;}
inline Step Presentation(const Motion& m){
    Step out;out.clip_seconds=m.elapsed;
    switch(m.phase){
    case Phase::Grab:out.clip="grab";out.loop=false;break;
    case Phase::IntroFloat:out.clip="float";out.clip_rate=.7F;break;
    case Phase::Patrol:out.clip="attackfloat";out.clip_rate=.7F;out.opacity=.3F;out.moving=true;break;
    case Phase::Taunt:out.clip=m.taunt%2?"look":"scheming";out.clip_rate=m.taunt%2?1.F:1.3F;out.loop=false;break;
    case Phase::ThrowStart:out.clip="throwobject1";out.loop=false;break;
    case Phase::ThrowEnd:out.clip="throwobject2";out.loop=false;break;
    case Phase::Hit:out.clip="hit";out.loop=false;break;
    case Phase::DepartureDelay:case Phase::DeparturePause:out.clip="float";break;
    case Phase::Departing:out.clip="float";out.opacity=.3F;out.moving=true;break;
    case Phase::Dormant:case Phase::Complete:break;
    }
    out.clip_seconds*=out.clip_rate;return out;
}
inline Step Advance(Motion& m,float seconds,const Timings& t){
    if(!std::isfinite(seconds)||seconds<=0)return Presentation(m);
    float remaining=std::min(seconds,.25F);
    m.contact_cooldown=std::max(0.F,m.contact_cooldown-remaining);
    bool projectile=false,completed=false;
    for(unsigned transitions=0;transitions<16&&remaining>0;++transitions){
        float duration=0;Phase next=m.phase;
        switch(m.phase){
        case Phase::Grab:duration=Duration(t.grab);next=Phase::IntroFloat;break;
        case Phase::IntroFloat:duration=2.2F;next=Phase::Patrol;break;
        case Phase::Taunt:duration=m.taunt%2?Duration(t.look):Duration(t.scheming)/1.3F;next=Phase::ThrowStart;break;
        case Phase::ThrowStart:duration=Duration(t.throw_start);next=Phase::ThrowEnd;break;
        case Phase::ThrowEnd:duration=Duration(t.throw_end);next=Phase::Patrol;break;
        case Phase::Hit:duration=Duration(t.hit);next=m.hits_left?Phase::Patrol:Phase::DepartureDelay;break;
        case Phase::DepartureDelay:duration=.5F;next=Phase::DeparturePause;break;
        case Phase::DeparturePause:duration=1.F;next=Phase::Departing;break;
        case Phase::Dormant:case Phase::Complete:return Presentation(m);
        case Phase::Patrol:case Phase::Departing:
            m.elapsed=std::fmod(m.elapsed+remaining,3600.F);remaining=0;continue;
        }
        const float used=std::min(remaining,std::max(0.F,duration-m.elapsed));
        m.elapsed+=used;remaining-=used;
        if(m.elapsed+.000001F<duration)break;
        if(m.phase==Phase::ThrowStart)projectile=true;
        if(m.phase==Phase::ThrowEnd)m.taunt=(m.taunt+1)%4;
        if(m.phase==Phase::DepartureDelay){completed=!m.completion_sent;m.completion_sent=true;}
        m.phase=next;m.elapsed=0;
    }
    auto out=Presentation(m);out.throw_projectile=projectile;out.completed=completed;return out;
}

} // namespace hpvr::quest::peeves
