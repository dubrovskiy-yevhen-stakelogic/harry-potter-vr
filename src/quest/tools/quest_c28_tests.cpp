#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <stdexcept>
using namespace hpvr::quest;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    Check(BumpProfileActor(1672,0,false)==0&&BumpProfileActor(1672,1,false)==1672,"Dumbledore gating");
    Check(BumpProfileActor(1329,5,false)==0&&BumpProfileActor(1329,6,false)==1329,"twins no spoilers");
    Check(BumpProfileActor(1326,8,false)==1233&&BumpProfileActor(1326,10,true)==1506,"twins remap");
    IntroCutscene lead;lead.tracks.resize(2);lead.tracks[0].actor_reference=1348;
    lead.tracks[0].commands[0]="MoveTo a";lead.tracks[0].commands[1]="Sleep 1";
    lead.tracks[0].commands[2]="MoveTo RonHallLoc";
    lead.tracks[1].commands[0]="Capture";
    lead.locations={{0,"a",{0,0,0}},{0,"RonHallLoc",{10,0,0}}};MakeRonLead(lead);
    Check(lead.tracks.size()==1&&lead.control_released&&lead.harry_released,"no camera capture");
    std::vector<CharacterDraw> actors(1);actors[0].actor_reference=1348;actors[0].base_origin={7,0,0};
    Check(!RonAtTwins(lead,actors),"scene must wait for Ron");ResumeRonLead(lead,actors);
    Check(lead.tracks[0].next_command==1,"resume forward segment");
    actors[0].base_origin={10,0,0};Check(RonAtTwins(lead,actors),"Ron arrival");
    actors[0].base_origin[1]=-3;Check(!RonAtTwins(lead,actors),"Ron wrong floor");
    std::vector<GpuVertex> wall;
    for(const auto p:std::array<std::array<float,3>,6>{{{0,0,-5},{0,4,-5},{0,4,5},{0,0,-5},{0,4,5},{0,0,5}}})
        wall.push_back({{p[0],p[1],p[2]},{0,0},{0,0},0,0,0,0});
    const auto contacts=BuildCollisionTriangles(wall,6);LocomotionMove move;move.displacement={1,0,0};
    ResolveMoverContacts(contacts,{-5,1,0},move);Check(move.displacement[0]==1,"wall must not require a second floor");
    move.displacement={4,0,0};ResolveMoverContacts(contacts,{-2,1,0},move);Check(move.displacement[0]<2,"closed wall sweep");
    move.displacement={4,0,0};ResolveMoverContacts(contacts,{-2,3,0},move);Check(move.displacement[0]<2,"closed wall airborne sweep");
    std::cout<<"C28_POLICY=PASS\n";if(argc==1)return 0;if(argc!=2)return 2;
    const std::filesystem::path root(argv[1]),map=root/"Maps/Lev_Tut1.unr";
    hpvr_hp1_player_start_report start{};
    Check(hpvr_hp1_load_player_start_utf8(map.string().c_str(),kMetersPerUnrealUnit,0,&start)==HPVR_HP1_PROFILE_OK,"start");
    const float yaw=start.rotation_units[1]*kTau/65536.0F;
    const auto census=hpvr::wand::inspect_hp1_actor_visuals(map);IntroCutscene ron;
    Check(LoadIntroCutscene(census,start,yaw,&lead,"CutScene0"),"owned lead");MakeRonLead(lead);
    Check(LoadIntroCutscene(census,start,yaw,&ron,"CutScene51"),"owned intro");
    const auto bsp=hpvr::wand::build_hp1_textured_bsp_scene(root,map,kMetersPerUnrealUnit,kMaximumTriangles);
    Check(bsp.status==hpvr::wand::Hp1ProfileStatus::ok,"owned BSP");std::vector<GpuVertex> vertices;
    for(const auto& v:bsp.vertices){const auto p=RotateYaw({v.position_m.x-start.position_m[0],v.position_m.y-start.position_m[1],v.position_m.z-start.position_m[2]},yaw);
        vertices.push_back({{p[0],p[1],p[2]},{0,0},{0,0},0,v.polygon_flags,0,0});}
    const auto triangles=BuildCollisionTriangles(vertices,static_cast<unsigned>(vertices.size()));
    auto pixels=bsp.texture_rgba8;auto layers=bsp.texture_layer_count;std::vector<DoorDraw> doors;
    Check(LoadIntroDoors(root,map,start,yaw,{},bsp.texture_layer_width,bsp.texture_layer_height,&vertices,&pixels,&layers,&doors),"owned movers");
    Check(doors[0].tag=="grandhalldoors"&&doors[1].tag=="grandhalldoors"&&doors[2].tag=="fgsec1","save door order");
    Check(!doors[2].opening&&doors[2].phase==0,"secret wall initially closed");
    std::vector<GpuVertex> secret(vertices.begin()+doors[2].first_vertex,vertices.begin()+doors[2].first_vertex+doors[2].vertex_count);
    Check(!BuildCollisionTriangles(secret,static_cast<unsigned>(secret.size())).empty(),"secret wall solid faces");
    auto& actor=actors[0];actor.base_origin={};actor.cutscene_offset={};bool found=false;
    for(const auto& loc:ron.locations)if(AsciiFold(loc.alias)=="ronwait"){
        Check(GroundScriptActor(triangles,loc.position,loc.position,false,&actor.cutscene_offset),"RonWait floor");found=true;}
    Check(found,"RonWait");ResumeRonLead(lead,actors);auto position=actor.cutscene_offset;unsigned samples=0;float largest_step=0;
    const auto& track=lead.tracks[0];
    for(auto i=track.next_command;i<track.commands.size();++i){const auto cmd=AsciiFold(track.commands[i]);if(!cmd.starts_with("moveto "))continue;
        for(const auto& loc:lead.locations)if(AsciiFold(loc.alias)==cmd.substr(7)){
            const auto delta=SubtractVector(loc.position,position),origin=position;
            const unsigned steps=std::max(1U,static_cast<unsigned>(std::ceil(std::hypot(delta[0],delta[2])/.05F)));
            for(unsigned j=1;j<=steps;++j){auto request=AddVector(origin,ScaleVector(delta,float(j)/steps)),next=position;
                Check(GroundScriptActor(triangles,position,request,true,&next),"route support");
                largest_step=std::max(largest_step,std::abs(next[1]-position[1]));position=next;++samples;}
        }
    }
    actor.cutscene_offset=position;Check(RonAtTwins(lead,actors),"grounded arrival");
    Check(largest_step<.4F,"Ron route vertical discontinuity");
    FrontAssets assets;Check(LoadFrontAssets(root,&assets),assets.error.c_str());
    for(const auto& profile:assets.bump_speech)for(const auto& line:profile.lines)
        Check(GameplayDialogueIndex(assets,line).has_value(),"bump audio mapping");
    Check(assets.bump_speech.size()>=8,"bump profiles missing");
    std::cout<<"C28_OWNED=PASS route_samples="<<samples<<" max_step="<<largest_step<<" profiles="<<assets.bump_speech.size()
             <<" dialogue_clips="<<18+assets.gameplay_audio.size()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"C28_FAIL="<<e.what()<<'\n';return 1;}}
