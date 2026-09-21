#include "hpvr/quest_peeves_battle.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace hpvr::quest::peeves;
int main(){try{
    unsigned checks=0;
    const auto check=[&](bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);};
    const Route route{{{1,0,0},{2,0,0}},{{2,0,2},{0,0,2}},{{{2,0,1},{1,0,1}},{{1,0,0},{2,0,0}}}};
    const Timings timing{.1F,.13F,.1F,.1F,.1F,.1F};
    const Point far{100,100,100};Battle b;
    check(ValidRoute(route)&&Activate(b,{0,0,0},route)&&!Activate(b,{0,0,0},route),"battle activates once");
    const auto tick=[&](float dt){return Advance(b,route,dt,timing,far,.5F);};
    for(unsigned i=0;i<300&&b.motion.phase!=Phase::Patrol;++i)(void)tick(.01F);
    check(b.motion.phase==Phase::Patrol&&b.position==Point{0,0,0},"intro ends without teleporting to entrance");
    check(!Hit(b),"travelling boss rejects Flipendo");
    unsigned arrivals=0;
    for(unsigned i=0;i<30&&b.motion.phase==Phase::Patrol;++i)arrivals+=tick(.01F).arrived;
    check(arrivals==1&&b.position==Point{2,0,0}&&b.entrance_flown&&b.next_patrol==0,"entrance reaches authored station before first patrol");
    unsigned projectiles=0;
    for(unsigned i=0;i<31;++i)projectiles+=tick(.01F).presentation.throw_projectile;
    check(projectiles==1&&b.motion.phase==Phase::Patrol,"station throws exactly once then starts next flight");
    for(unsigned i=0;i<40&&b.motion.phase==Phase::Patrol;++i)(void)tick(.01F);
    check(b.position==Point{1,0,1}&&b.next_patrol==1,"first patrol leg does not repeat entrance");
    unsigned completions=0;
    for(unsigned hit=0;hit<4;++hit){
        check(Hit(b)&&b.motion.hits_left==3-hit,"four successful hits update original health");
        for(unsigned i=0;i<20&&b.motion.phase==Phase::Hit;++i)completions+=tick(.01F).presentation.completed;
        if(hit<3){
            for(unsigned i=0;i<100&&b.motion.phase==Phase::Patrol;++i)(void)tick(.01F);
            check(b.motion.phase==Phase::Taunt,"reaction resumes station cycle");
        }
    }
    const auto final_position=b.position;
    for(unsigned i=0;i<50;++i)completions+=tick(.01F).presentation.completed;
    check(completions==1&&b.motion.phase==Phase::DeparturePause&&!Active(b.motion),"defeat event follows final hit and half-second delay");
    for(unsigned i=0;i<90;++i)completions+=tick(.01F).presentation.completed;
    check(b.position==final_position,"original one-second departure pause retained");
    for(unsigned i=0;i<1000&&b.motion.phase!=Phase::Complete;++i)completions+=tick(.01F).presentation.completed;
    check(completions==1&&b.motion.phase==Phase::Complete&&b.position==Point{0,0,2},"departure finishes without repeated completion");
    check(!Hit(b)&&!Activate(b,{0,0,0},route),"defeated encounter cannot restart");
    Battle corner;corner.position={0,0,0};const Flight path{{1,0,0},{1,0,1}};
    check(!Move(corner,path,1.5F)&&corner.position==Point{1,0,.5F},"movement spends remaining distance after corner");
    check(Move(corner,path,10)&&corner.position==Point{1,0,1},"overshoot stops at final waypoint");
    Battle contact;check(Activate(contact,{0,0,0},route),"contact fixture starts");
    contact.motion.phase=Phase::Patrol;
    check(Advance(contact,route,.25F,timing,{1,0,0},.2F).contact_damage==20,"swept flying contact hits player once");
    check(Advance(contact,route,.01F,timing,contact.position,.5F).contact_damage==0,"contact cooldown survives arrival");
    auto invalid=route;invalid.entrance[0][0]=std::numeric_limits<float>::quiet_NaN();
    check(!ValidRoute(invalid),"non-finite route rejected");
    const auto prior=contact.position;
    (void)Advance(contact,invalid,.25F,timing,far,.5F);
    (void)Advance(contact,route,std::numeric_limits<float>::quiet_NaN(),timing,far,.5F);
    check(contact.position==prior,"invalid route or clock cannot move battle");
    std::cout<<"PEEVES_BATTLE_TESTS=PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
