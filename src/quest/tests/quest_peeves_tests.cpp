#include "hpvr/quest_peeves.h"
#include "hpvr/quest_frontend.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace hpvr::quest;
int main(){try{
    unsigned checks=0;
    const auto check=[&](bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);};
    peeves::Motion state;peeves::Timings timing{.1F,.13F,.1F,.1F,.1F,.1F};
    check(!peeves::Active(state)&&!peeves::Hit(state)&&peeves::Health(state)==1,"dormant encounter is not hittable");
    check(peeves::Activate(state)&&!peeves::Activate(state),"encounter starts once");
    check(peeves::Contact(state)==20&&peeves::Contact(state)==0,"contact damage has one-second cooldown");
    for(unsigned i=0;i<10;++i)(void)peeves::Advance(state,.25F,timing);
    check(state.phase==peeves::Phase::Patrol&&!peeves::Hit(state),"original patrol is invulnerable");
    check(peeves::Presentation(state).opacity==.3F&&peeves::Presentation(state).moving,"patrol uses translucent moving presentation");
    check(peeves::Contact(state)==20,"cooldown expires during active encounter");
    check(peeves::Arrive(state)&&peeves::Vulnerable(state),"authored station opens attack window");
    unsigned throws=0;
    for(unsigned i=0;i<8;++i)throws+=peeves::Advance(state,.05F,timing).throw_projectile;
    check(throws==1&&state.phase==peeves::Phase::Patrol&&state.taunt==1,"one projectile per original throw cycle");
    unsigned completed=0;
    for(unsigned hit=0;hit<4;++hit){
        check(peeves::Arrive(state)&&peeves::Hit(state),"station accepts Flipendo");
        check(state.hits_left==3-hit&&peeves::Health(state)==float(3-hit)/4,"exactly four accepted hits deplete health");
        completed+=peeves::Advance(state,.1F,timing).completed;
        if(hit<3)check(state.phase==peeves::Phase::Patrol&&!peeves::Hit(state),"hit reaction returns to invulnerable flight");
    }
    check(completed==0&&state.phase==peeves::Phase::DepartureDelay&&!peeves::Active(state),"death waits for final hit animation and hides health");
    for(unsigned i=0;i<8;++i)completed+=peeves::Advance(state,.25F,timing).completed;
    check(completed==1&&state.phase==peeves::Phase::Departing&&!peeves::Hit(state),"defeat event emitted once after original half-second delay");
    check(peeves::Contact(state)==0&&peeves::Arrive(state)&&!peeves::Activate(state),"departed boss cannot damage or restart");
    const auto before=state;
    (void)peeves::Advance(state,std::numeric_limits<float>::quiet_NaN(),timing);
    check(state.phase==before.phase&&state.elapsed==before.elapsed,"invalid clock does not mutate encounter");
    QuestFrontEnd front;front.assets.has_boss_art=true;front.assets.boss_empty=10;front.assets.peeves_health=11;
    check(front.PeevesHealthQuads(4,false).empty(),"health bar hidden outside encounter");
    for(unsigned hit=0;hit<=4;++hit){
        const auto quads=front.PeevesHealthQuads(hit,true);
        check(quads.size()==2&&quads[0].texture==10&&quads[1].texture==11,"original background and Peeves art used");
        check(quads[1].w==97.F+159.F*hit/4&&quads[1].uw==quads[1].w/256&&quads[1].h==256,
            "health clips original art without shrinking portrait");
        check(quads[0].x==8&&quads[0].y==316,"original 640x480 HUD placement retained");
    }
    check(front.PeevesHealthQuads(999,true)[1].w==256,"health crop is bounded");
    front.assets.malfoy_health=12;
    check(front.MalfoyHealthQuads(3,false).empty(),"Malfoy bar hidden outside duel");
    for(unsigned hits=0;hits<=3;++hits){
        const auto quads=front.MalfoyHealthQuads(hits,true);
        check(quads.size()==2&&quads[1].texture==12&&quads[1].w==97.F+159.F*hits/3,
            "Malfoy original portrait and three-hit health crop");
    }
    std::cout<<"PEEVES_TESTS=PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
