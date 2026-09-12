#include "hpvr/quest_voice_hint.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Point=std::array<float,3>;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void CheckPlacement(const Point& low,const Point& high,const Point& viewer){
    const auto marker=hpvr::quest::PlaceTargetMarker(low,high,viewer);
    const auto hint=hpvr::quest::PlaceVoiceHint(low,high,viewer);
    Check(marker.valid&&hint.valid,"valid bounds and viewer produce hint");
    Point delta{};for(unsigned axis=0;axis<3;++axis)delta[axis]=hint.center[axis]-marker.center[axis];
    const float normal_offset=hpvr::quest::target_marker_detail::Dot(delta,marker.normal);
    const float up_offset=hpvr::quest::target_marker_detail::Dot(delta,marker.up);
    Check(std::abs(normal_offset)<1e-5F,"text stays on viewer-facing support plane");
    Check(up_offset-.025F>=marker.size*.5F+.0749F,"entire glyph strip clears upper marker edge");
    Check(up_offset<=1.301F,"hint stays target-adjacent even for wide targets");
    Check(std::abs(hpvr::quest::target_marker_detail::Dot(hint.right,hint.up))<1e-5F,"orthogonal text basis");
    Check(std::abs(hpvr::quest::target_marker_detail::Dot(hint.right,hint.right)-1)<1e-5F,"unit text basis");
    Check(hint.center[1]-.025F>=low[1],"floor-target hint cannot disappear underneath floor");
}
}
int main(){
    CheckPlacement({-.5F,0,-.5F},{.5F,.6F,.5F},{0,1.6F,5});
    CheckPlacement({-1.5F,0,-.7F},{1.5F,.35F,.7F},{2,1.6F,5});
    CheckPlacement({-.35F,0,-.1F},{.35F,4,.1F},{0,1.6F,4});
    CheckPlacement({-.5F,0,-.5F},{.5F,.6F,.5F},{1,6,1});
    CheckPlacement({-.5F,0,-.5F},{.5F,.6F,.5F},{0,6,0});
    Check(!hpvr::quest::PlaceVoiceHint({},{},{0,1,2}).valid,"empty bounds rejected");
    Check(!hpvr::quest::PlaceVoiceHint({1,1,1},{-1,-1,-1},{0,1,2}).valid,"reversed bounds rejected");
    Check(!hpvr::quest::PlaceVoiceHint({-1,-1,-1},{1,1,1},{}).valid,"viewer at target center rejected");
    Check(!hpvr::quest::PlaceVoiceHint({},{1,1,1},{0,std::numeric_limits<float>::quiet_NaN(),2}).valid,"nonfinite viewer rejected");
    std::cout<<"PASS: voice hint above floor and marker; bounded viewer-facing placement\n";
}
