#include "hpvr/quest_malfoy.h"
#include <iostream>
#include <stdexcept>

namespace m=hpvr::quest::malfoy;
int main(){try{
    unsigned checks=0;const auto check=[&](bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);};
    m::Config config{{0,0,0},{6,0,0}};m::Timings timings{.1F,.8F,.4F,.5F};m::State state;
    check(m::Activate(state,config,{3,0,0})&&!m::Activate(state,config,{3,0,0}),"encounter starts once");
    check(m::Speed(state,config)==2,"authored starting speed");
    unsigned throws=0,first=0;
    for(unsigned i=0;i<10000&&throws<6;++i){
        const auto step=m::Advance(state,config,timings,.02F);
        first+=step.first_throw;
        check(state.position[0]>=0&&state.position[0]<=6&&state.position[1]==0&&state.position[2]==0,"movement stays on rail");
        if(step.throw_projectile){++throws;check(step.projectile_fuse==(throws%3==0?2.75F:4.5F),"every third cracker has a short fuse");}
    }
    check(throws==6&&first==1,"repeated attacks emit first-throw event once");
    check(m::Hit(state,config)&&!m::Hit(state,config),"hit recovery ignores repeated damage");
    check(std::abs(m::Speed(state,config)-2.25F)<.0001F,"first hit increases strafe speed");
    m::Advance(state,config,timings,.25F);
    check(m::Hit(state,config)&&state.hits==2,"second hit after recovery");
    m::Advance(state,config,timings,.25F);
    check(m::Hit(state,config)&&state.phase==m::Phase::Knockdown,"third hit knocks Malfoy down");
    unsigned defeated=0;
    for(unsigned i=0;i<5;++i)defeated+=m::Advance(state,config,timings,.25F).defeated;
    check(defeated==0,"defeat waits for knockdown and one-second pause");
    for(unsigned i=0;i<20;++i)defeated+=m::Advance(state,config,timings,.25F).defeated;
    check(defeated==1&&state.phase==m::Phase::Complete,"victory dispatched exactly once");
    check(!m::Hit(state,config)&&!m::Activate(state,config,{}),"finished encounter cannot restart");
    state={};m::Activate(state,config,{3,0,0});check(m::Lose(state)&&!m::Lose(state),"player defeat is one-shot");
    check(!m::Advance(state,config,timings,.25F).throw_projectile,"lost encounter stops attacks");
    state={};auto invalid=config;invalid.rail_end=invalid.rail_start;
    check(!m::Activate(state,invalid,{3,0,0})&&state.phase==m::Phase::Idle,"degenerate rail rejected");
    invalid=config;invalid.maximum_hits=0;check(!m::Activate(state,invalid,{}),"zero hit count rejected");
    m::Activate(state,config,{3,0,0});const auto old=state.position;
    m::Advance(state,config,timings,-1);check(state.position==old,"negative time ignored");
    std::cout<<"MALFOY_TESTS=PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
