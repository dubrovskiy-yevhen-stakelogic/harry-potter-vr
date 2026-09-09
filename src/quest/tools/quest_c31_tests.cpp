#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <chrono>
using namespace hpvr::quest;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    ProgressSave p;p.quest_stage=12;RewardApproach approach;
    for(int i=1;i<=24;++i)p.collected_beans.push_back(i);
    Check(!approach.Update(p,4,true),"24 beans never qualify");
    p.collected_beans.push_back(25);
    Check(!approach.Update(p,1,true),"25th pickup beside Fred never starts movie");
    Check(!approach.Update(p,1,true),"standing still never starts movie");
    Check(!approach.Update(p,3,true),"leaving only arms approach");
    Check(!approach.Update(p,1,false),"wall blocks Fred interaction");
    Check(approach.Update(p,1,true),"approach Fred with 25 starts reward");
    Check(!approach.Update(p,1,true),"no repeated reward while beside Fred");
    p.card_awarded=true;
    Check(!approach.Update(p,3,true)&&!approach.Update(p,1,true),"awarded card cannot repeat");
    Check(ApplyFirstPeevesContact(p)&&p.health==90,"mandatory five raw damage out of fifty");
    Check(!ApplyFirstPeevesContact(p)&&p.health==90,"first attack idempotent");
    const auto dir=std::filesystem::temp_directory_path()/("hpvr-c31-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ProgressSave restored;
    Check(WriteProgress(dir,0,&p)&&ReadProgress(dir,0,&restored)&&restored.peeves_first_hit,"V7 first attack survives reload");
    Check(!ApplyFirstPeevesContact(restored)&&restored.health==90,"reload cannot damage twice");
    std::filesystem::remove(dir/"slot1.1.hpvr");std::filesystem::remove(dir);
    QuestFrontEnd front;front.screen=FrontScreen::Objective;front.assets.level_objective="Fixture objective";
    Check(front.Quads().size()>20,"objective has background and text");
    Check(front.Input(0,true,false)==FrontAction::BeginLevel&&!front.Visible(),"objective confirm starts level");
    Check(IsClassroomActor(2279)&&IsClassroomActor(2244)&&!IsClassroomActor(1348),"separate classroom Ron");
    std::cout<<"C31_POLICY=PASS reward_approach=YES mandatory_damage=YES objective=YES\n";
    if(argc==1)return 0;if(argc!=2)return 2;
    const std::filesystem::path root(argv[1]),map=root/"Maps/Lev_Tut1.unr";
    hpvr_hp1_player_start_report start{};
    Check(hpvr_hp1_load_player_start_utf8(map.string().c_str(),.02F,0,&start)==0,"owned start");
    const auto census=hpvr::wand::inspect_hp1_actor_visuals(map);
    const float yaw=start.rotation_units[1]*kTau/65536.0F;
    IntroCutscene transfer,departure;
    Check(LoadIntroCutscene(census,start,yaw,&transfer,"cutscene6"),"owned swap");
    PrepareTwinsTransfer(transfer);
    Check(transfer.tracks.size()==2&&transfer.control_released,"swap does not capture camera");
    for(const auto& t:transfer.tracks)for(const auto& command:t.commands)Check(command.empty(),"no navigation across walls");
    Check(LoadIntroCutscene(census,start,yaw,&departure,"cutscene5"),"owned post-Peeves movie");
    PreparePeevesDeparture(departure);
    Check(departure.camera_active&&departure.camera_position_valid&&departure.camera_target=="target","camera ready on first frame");
    unsigned speeches=0,moves=0;std::set<std::string> cues,waits;
    for(const auto& t:departure.tracks){
        for(const auto& command:t.commands){
            const auto s=AsciiFold(command);
            Check(!s.starts_with("sleep "),"no presentation sleeps");
            if(s.starts_with("talk"))++speeches;
            if(s.starts_with("moveto "))++moves;
            if(s.starts_with("cue "))cues.insert(s.substr(4));
            if(s.starts_with("waitfor "))waits.insert(s.substr(8));
            if(t.camera)Check(!s.starts_with("moveto "),"camera travel never blocks speech");
        }
    }
    Check(speeches==2&&moves==2,"both owned lines and visible exits retained");
    for(const auto& wait:waits)Check(cues.contains(wait),"post-Peeves cues resolve");
    Check(LoadFrontAssets(root,&front.assets)&&!front.assets.level_objective.empty(),"owned localized objective");
    unsigned pupils=0,boards=0;
    for(const auto& a:census.actors){pupils+=IsClassroomActor(a.actor_reference);boards+=AsciiFold(a.qualified_class_name)=="hprops.transblackboard";}
    Check(pupils==7&&boards==2,"owned classroom roster and boards");
    std::cout<<"C31_OWNED=PASS twins_swap=YES post_peeves_lines="<<speeches<<" pupils="<<pupils<<" boards="<<boards<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
