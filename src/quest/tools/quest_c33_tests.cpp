#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include "hpvr/quest_performance.h"
#include <iostream>
using namespace hpvr::quest;
namespace wand=hpvr::wand;
void Require(bool pass,const char* what){if(!pass)throw std::runtime_error(what);}
int main(int argc,char** argv){try{
    QuestFrontEnd front;front.screen=FrontScreen::Game;
    front.ToggleVrMenu();Require(front.Visible()&&front.WorldVisible(),"VR panel preserves world");
    front.Input(0,false,false);front.selection=2;front.Input(0,true,false);
    Require(front.screen==FrontScreen::Debug&&front.WorldVisible(),"debugger preserves world");
    front.Input(0,false,false);front.Input(0,false,true);
    Require(front.screen==FrontScreen::Vr&&front.selection==2,"debug back returns settings");
    front.Input(0,false,false);front.Input(0,true,false);
    front.Input(0,false,false);front.Input(0,true,false);
    Require(front.screen==FrontScreen::Game&&front.debug_pinned&&!front.Visible(),"pin resumes gameplay");
    front.ToggleVrMenu();Require(!front.debug_pinned&&front.screen==FrontScreen::Vr,"chord hides pinned debugger");
    front.ToggleVrMenu();Require(front.screen==FrontScreen::Game,"chord restores game");
    Require(VrRenderExtent(2000,175,5000)==3500&&VrRenderExtent(2000,190,3200)==3200,"175 percent / runtime max");
    Require(std::abs(TimestampMilliseconds(250,10,8,1000000)-16)<.001,"GPU timestamp wrap");
    Require(PerformanceLines({})[1].find("N/A")!=std::string::npos,"no invented load");
    std::cout<<"C33_POLICY=PASS floating_panels=YES scale=175 timestamp_wrap=YES unavailable=N/A\n";
    if(argc==1)return 0;
    const std::filesystem::path root(argv[1]),map=root/"Maps/Lev_Tut1.unr";
    hpvr_hp1_player_start_report start{};
    Require(hpvr_hp1_load_player_start_utf8(map.string().c_str(),.02F,0,&start)==0,"start");
    const float yaw=start.rotation_units[1]*kTau/65536;
    const auto census=wand::inspect_hp1_actor_visuals(map);std::array<IntroCutscene,4> scenes;
    const std::array<const char*,4> names{"cutscene56","cutscene1","cutscene58","cutscene59"};
    for(unsigned i=0;i<4;++i)Require(LoadIntroCutscene(census,start,yaw,&scenes[i],names[i]),"scene");
    const auto& lesson=scenes[3];
    std::cout<<"LESSON_TRIGGER center="<<lesson.trigger_position[0]<<','<<lesson.trigger_position[1]<<','<<lesson.trigger_position[2]
        <<" radius="<<lesson.trigger_radius<<" width="<<lesson.trigger_width<<" height="<<lesson.trigger_height<<" yaw="<<lesson.trigger_yaw<<"\n";
    Require(lesson.trigger_box&&std::abs(lesson.trigger_width-2.8F)<.001F,"original wide classroom trigger");
    for(const auto& loc:lesson.locations)std::cout<<"LESSON_LOC "<<loc.alias<<'='<<loc.position[0]<<','<<loc.position[1]<<','<<loc.position[2]<<"\n";
    auto edge=lesson.trigger_position;edge[1]+=kPlayerEyeHeightMeters;
    edge[0]+=2*std::cos(yaw);edge[2]-=2*std::sin(yaw);
    Require(SelectStoryEncounter(18,scenes,edge,true)==3,"wide doorway activates lesson");
    Require(!SelectStoryEncounter(16,scenes,edge,true),"lesson cannot precede Hermione");
    unsigned urns=0;for(const auto& a:census.actors)if(AsciiFold(a.qualified_class_name)=="hprops.hogwartsurn")++urns;
    Require(urns==2,"two owned frog-room urns");
    Require(SelectStoryEncounter(18,scenes,{39.6532555F,25.0901165F,-111.846046F},true)==3,"C32 missed-entry save recovers");
    if(!LoadFrontAssets(root,&front.assets))throw std::runtime_error(front.assets.error);
    Require(front.assets.card_pickup.status==wand::Hp1ProfileStatus::ok,"original card pickup MPEG");
    const auto bsp=wand::build_hp1_textured_bsp_scene(root,map,kMetersPerUnrealUnit,kMaximumTriangles);
    Require(bsp.status==wand::Hp1ProfileStatus::ok,"owned BSP");
    unsigned floors=0,old_walls=0;
    for(const auto& v:bsp.vertices)if(v.texture_layer<bsp.texture_layer_names.size()){
        const auto& name=bsp.texture_layer_names[v.texture_layer];
        if(IsReflectiveWoodFloor(name,v.normal.y)){++floors;Require(std::abs(v.normal.z)<.1F,"selected reflective floor is horizontal");}
        if(IsReflectiveWoodFloor(name,v.normal.z))++old_walls;
    }
    Require(floors>100&&old_walls>0,"regression: Z-up selection picked walls instead of floors");
    std::cout<<"SSR_AXIS=PASS floor_vertices="<<floors<<" previous_wall_vertices="<<old_walls<<"\n";
    std::cout<<"C33_OWNED=PASS urns="<<urns<<" card_sound=pickup_wizardcard2\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
