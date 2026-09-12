#include "hpvr/quest_gnome.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

using namespace hpvr::quest::gnome;
void Check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    const Timings timings{.3F,.6F,.8F};Motion actor;
    Check(Targetable(actor)&&std::string_view(Presentation(actor).clip)=="breathe","waiting gnome stays visible/targetable");
    Check(Activate(actor)&&!Activate(actor),"authored trigger activates once");
    Check(Presentation(actor).chasing&&std::string_view(Presentation(actor).clip)=="runattack","activation starts authored attack run");
    for(unsigned i=0;i<7;++i)(void)Advance(actor,.05F,timings,false);
    Check(actor.phase==Phase::Attack&&std::string_view(Presentation(actor).clip)=="runattackbite","attack intro enters bite run");
    Check(Hit(actor)&&!Hit(actor)&&!Targetable(actor),"one hit enters knockback and rejects repeat hits");
    Check(!Presentation(actor).chasing&&!Presentation(actor).loop&&std::string_view(Presentation(actor).clip)=="knockback","hit plays fall, never hides model");
    unsigned events=0;
    for(unsigned i=0;i<10;++i){const auto step=Advance(actor,.05F,timings,true);events+=step.completed;}
    Check(events==0&&actor.phase==Phase::Knockback,"counter event waits for physical knockback completion");
    for(unsigned i=0;i<8;++i){const auto step=Advance(actor,.05F,timings,true);events+=step.completed;}
    Check(events==1&&actor.phase==Phase::Dizzy,"counter event fires at stunned transition once");
    bool saw_seated=false,saw_repeat_dizzy=false;
    for(unsigned i=0;i<800;++i){
        const auto step=Advance(actor,.05F,timings,true);events+=step.completed;
        Check(!step.targetable&&!step.chasing,"defeated gnome stays harmless");
        saw_repeat_dizzy|=saw_seated&&actor.phase==Phase::Dizzy;saw_seated|=actor.phase==Phase::Seated;
    }
    Check(events==1&&saw_seated&&saw_repeat_dizzy,"nearby player sees seated head animation without repeated gate events");
    RestoreDefeated(actor);Check(actor.phase==Phase::Seated&&!Targetable(actor)&&!Activate(actor),"old hidden-hit save restores seated defeated actor");
    for(unsigned i=0;i<400;++i)Check(!Advance(actor,.05F,timings,false).completed,"save migration never replays completion");
    Check(actor.phase==Phase::Seated&&std::string_view(Presentation(actor).clip)=="downbreath","distant player leaves seated breathing pose");
    RestoreHit(actor,false);events=0;
    Check(actor.phase==Phase::Knockback&&!actor.completion_sent,"pending hit save restarts visible fall");
    for(unsigned i=0;i<80;++i)events+=Advance(actor,.05F,timings,false).completed;
    Check(events==1&&!Targetable(actor),"pending hit save eventually emits exactly one counter event");
    RestoreHit(actor,true);
    for(unsigned i=0;i<80;++i)Check(!Advance(actor,.05F,timings,true).completed,"recorded hit save never repeats graph completion");
    const auto before=actor.elapsed;
    (void)Advance(actor,std::numeric_limits<float>::quiet_NaN(),timings,true);
    (void)Advance(actor,-1,timings,true);Check(actor.elapsed==before,"invalid time cannot advance AI");
    Check(kRunSpeedMetersPerSecond==3&&kCollisionRadiusMeters==.4F&&kCollisionHalfHeightMeters==.4F&&kContactDamage==2,"owned class defaults remain explicit");
    std::cout<<"GNOME_TESTS=PASS one_hit=SEATED completion=ONCE legacy_hits=RESTORED chase=AUTHORED_CLIPS\n";
}
