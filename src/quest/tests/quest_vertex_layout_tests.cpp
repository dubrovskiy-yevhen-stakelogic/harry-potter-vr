#include "hpvr/quest_vertex_layout.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
struct Clip {unsigned first_vertex=0;float duration=1;unsigned frame_count=1;};
struct Actor {std::map<std::string,Clip> clips;std::string active_clip="idle";unsigned vertex_count=3;};
struct Pickup {unsigned first=0,count=3,frames=1;};
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    using hpvr::quest::ValidateAnimatedVertexLayout;
    std::vector<Actor> actors{{{{"idle",{12,1,2}}},"idle",3}};
    std::vector<Pickup> pickups{{18,3,2}};
    Check(ValidateAnimatedVertexLayout(actors,pickups,12,24),"original cache layout");
    actors[0].clips.emplace("knockback",Clip{24,2,3});
    Check(ValidateAnimatedVertexLayout(actors,pickups,12,33),"recovered clips after pickups");
    actors[0].active_clip="knockback";
    Check(ValidateAnimatedVertexLayout(actors,pickups,12,33),"active recovered clip");
    Check(!ValidateAnimatedVertexLayout(actors,pickups,12,32),"outside vertex buffer");
    Check(!ValidateAnimatedVertexLayout(actors,pickups,12,34),"unclaimed tail");
    actors[0].clips["knockback"].first_vertex=25;
    Check(!ValidateAnimatedVertexLayout(actors,pickups,12,34),"gap");
    actors[0].clips["knockback"].first_vertex=21;
    Check(!ValidateAnimatedVertexLayout(actors,pickups,12,30),"partial pickup overlap");
    actors[0].clips["knockback"].first_vertex=18;
    Check(!ValidateAnimatedVertexLayout(actors,pickups,12,33),"same start as pickup");
    actors[0].clips["knockback"]={24,2,3};
    actors[0].active_clip="missing";
    Check(!ValidateAnimatedVertexLayout(actors,pickups,12,33),"missing active clip");
    actors[0].active_clip="idle";
    actors[0].clips["knockback"].duration=std::numeric_limits<float>::quiet_NaN();
    Check(!ValidateAnimatedVertexLayout(actors,pickups,12,33),"invalid duration");
    actors[0].clips["knockback"]={24,2,0};
    Check(!ValidateAnimatedVertexLayout(actors,pickups,12,24),"empty clip");
    Check(!ValidateAnimatedVertexLayout(actors,pickups,33,12),"inverted bounds");
    std::cout<<"VERTEX_LAYOUT=PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
