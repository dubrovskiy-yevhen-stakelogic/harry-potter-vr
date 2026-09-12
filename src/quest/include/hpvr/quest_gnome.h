#pragma once

#include <algorithm>
#include <cmath>

namespace hpvr::quest::gnome {

// Gameplay values observed in the owned Tut1Gnome class defaults. Rendering
// durations are supplied by its owned animation clips, not guessed here.
inline constexpr float kRunSpeedMetersPerSecond = 3.0F;
inline constexpr float kCollisionRadiusMeters = .4F;
inline constexpr float kCollisionHalfHeightMeters = .4F;
inline constexpr unsigned kContactDamage = 2;
inline constexpr float kDizzyProximityMeters = 4.0F;

enum class Phase { Dormant, AttackIntro, Attack, Knockback, Dizzy, Seated };
struct Timings {
    float attack_intro = 1;
    float knockback = 1;
    float dizzy = 1;
};
struct Motion {
    Phase phase = Phase::Dormant;
    float elapsed = 0;
    bool completion_sent = false;
};
struct Step {
    const char* clip = "breathe";
    float clip_seconds = 0;
    bool loop = true;
    bool targetable = true;
    bool chasing = false;
    bool completed = false;
};

inline bool Targetable(const Motion& motion) {
    return motion.phase==Phase::Dormant || motion.phase==Phase::AttackIntro || motion.phase==Phase::Attack;
}
inline bool Activate(Motion& motion) {
    if(motion.phase!=Phase::Dormant)return false;
    motion.phase=Phase::AttackIntro;motion.elapsed=0;return true;
}
inline bool Hit(Motion& motion) {
    if(!Targetable(motion))return false;
    motion.phase=Phase::Knockback;motion.elapsed=0;return true;
}
inline void RestoreDefeated(Motion& motion) {
    // Old saves recorded one hit and hid the actor. Recover its seated pose
    // without sending the already-persisted counter event a second time.
    motion={Phase::Seated,0,true};
}
inline void RestoreHit(Motion& motion,bool completion_recorded) {
    if(completion_recorded)RestoreDefeated(motion);
    else {
        // A checkpoint captured during the fall has not yet signaled its
        // counter. Replay that short fall, then complete the pending event.
        motion={};(void)Hit(motion);
    }
}
inline float Duration(float value) {
    return std::isfinite(value) ? std::clamp(value,.01F,30.0F) : 1.0F;
}
inline Step Presentation(const Motion& motion) {
    Step result;result.clip_seconds=motion.elapsed;result.targetable=Targetable(motion);
    switch(motion.phase) {
        case Phase::Dormant:break;
        case Phase::AttackIntro:result.clip="runattack";result.loop=false;result.chasing=true;break;
        case Phase::Attack:result.clip="runattackbite";result.chasing=true;break;
        case Phase::Knockback:result.clip="knockback";result.loop=false;break;
        case Phase::Dizzy:result.clip="downdizzy";result.loop=false;break;
        case Phase::Seated:result.clip="downbreath";break;
    }
    return result;
}
inline Step Advance(Motion& motion,float seconds,const Timings& timings,bool player_near) {
    if(!std::isfinite(seconds)||seconds<0)return Presentation(motion);
    // Only active gameplay time advances. Pause/focus policy stays with caller.
    motion.elapsed+=std::min(seconds,.25F);
    bool completed=false;
    switch(motion.phase) {
        case Phase::AttackIntro:
            if(motion.elapsed>=Duration(timings.attack_intro)){
                motion.phase=Phase::Attack;motion.elapsed=0;
            }
            break;
        case Phase::Knockback:
            if(motion.elapsed>=Duration(timings.knockback)){
                motion.phase=Phase::Dizzy;motion.elapsed=0;
                completed=!motion.completion_sent;motion.completion_sent=true;
            }
            break;
        case Phase::Dizzy:
            if(motion.elapsed>=Duration(timings.dizzy)){
                motion.phase=Phase::Seated;motion.elapsed=0;
            }
            break;
        case Phase::Seated:
            if(motion.elapsed>=2){
                motion.elapsed=0;
                if(player_near)motion.phase=Phase::Dizzy;
            }
            break;
        case Phase::Dormant:case Phase::Attack:
            // Bounded clock: presentation clips loop and need no infinite age.
            if(motion.elapsed>3600)motion.elapsed=0;
            break;
    }
    auto result=Presentation(motion);result.completed=completed;return result;
}

} // namespace hpvr::quest::gnome
