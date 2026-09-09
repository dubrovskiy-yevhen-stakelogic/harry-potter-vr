#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace hpvr::quest;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(int argc,char** argv){try{
    const auto dir=std::filesystem::temp_directory_path()/("hpvr-c35-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir/"SaveGames");
    {std::ofstream old(dir/"vr-settings.1");old<<"HPVR_VR1 175 40 7 "<<VrSettingsChecksum(175,40,7)<<'\n';}
    auto settings=ReadVrSettings(dir);
    Check(settings.render_scale==175&&settings.ssr==40&&!settings.relaxed_lesson&&!settings.welcome_seen,"legacy render settings kept, difficulty defaults original");
    settings.relaxed_lesson=true;settings.welcome_seen=true;
    Check(WriteVrSettings(dir,settings),"VR2 durable bank");
    auto loaded=ReadVrSettings(dir);Check(loaded.relaxed_lesson&&loaded.welcome_seen&&loaded.render_scale==175,"VR2 fields round trip");
    {std::ofstream broken(dir/"vr-settings.0");broken<<"HPVR_VR2 175 40 8 0 1 0\n";}
    loaded=ReadVrSettings(dir);Check(loaded.generation==7&&!loaded.relaxed_lesson,"corrupt new bank falls back to old format");
    QuestFrontEnd f;f.saves=dir/"SaveGames";f.vr=loaded;f.BeginGame();
    f.ToggleVrMenu();f.Input(0,false,false);f.selection=3;f.Input(0,true,false);
    Check(f.vr.relaxed_lesson&&!f.PausesWorld(),"difficulty selection keeps world live");
    const auto relaxed_key=f.DrawKey();f.Input(0,false,false);f.Input(0,true,false);
    Check(!f.vr.relaxed_lesson&&f.DrawKey()!=relaxed_key,"original selection has its own geometry key");
    f.selection=4;f.Input(0,false,false);Check(f.Input(0,true,false)==FrontAction::Resume&&!f.Visible(),"fifth row closes settings");
    f.ShowDemoNotice(false);Check(f.FloatingPanel()&&f.WorldVisible()&&f.PausesWorld(),"welcome stays in world and holds quest until read");
    Check(f.Input(0,true,false)==FrontAction::None&&f.DemoNotice(),"held trigger cannot skip notice");
    f.Input(0,false,false);f.selection=1;
    Check(f.Input(0,true,false)==FrontAction::OpenCommunity&&f.DemoNotice(),"Discord is explicit action, notice remains");
    f.Input(0,false,false);f.Input(0,false,true);
    Check(!f.Visible()&&ReadVrSettings(dir).welcome_seen,"welcome dismissal persists once across slots");
    f.ShowDemoNotice(true);Check(f.screen==FrontScreen::DemoEnd&&f.WorldVisible(),"end panel available even after welcome seen");
    for(auto screen:{FrontScreen::Vr,FrontScreen::Welcome,FrontScreen::DemoEnd}){
        f.screen=screen;f.selection=0;
        for(const auto& q:f.Quads())Check(q.x>=0&&q.y>=0&&q.x+q.w<=640.01F&&q.y+q.h<=480.01F,"panel content stays within window");
    }
    Check(std::string(kDemoCommunityUrl)=="https://discord.com/channels/747967102895390741/1547254536203407390","exact Discord channel");
    DemoFirstStep motion;
    Check(!motion.Update(false,true,{0,0,0})&&!motion.Update(true,false,{0,0,0}),"menu and idle cannot welcome");
    Check(!motion.Update(true,true,{0,0,0})&&!motion.Update(true,true,{0,.2F,0}),"no greeting from vertical/head movement");
    Check(!motion.Update(true,true,{.04F,.2F,0})&&motion.Update(true,true,{.09F,.2F,0}),"first actual step triggers");
    motion.Reset();Check(!motion.Update(true,true,{1,0,0})&&!motion.Update(true,true,{20,0,0}),"restore/teleport cannot welcome");
    ViewPose eye,shifted,local,reference,mapped;
    Check(BuildDemoActorEye({1,2,3},0,1.655F,&eye)&&BuildDemoActorEye({5,3,9},0,1.655F,&shifted),"actor eyes");
    Check(std::abs(shifted.position[0]-eye.position[0]-4)<.001F&&std::abs(shifted.position[1]-eye.position[1]-1)<.001F,"player follows actor translation, not seated camera");
    local.position={.2F,.1F,0};Check(MapCinematicEye(local,reference,shifted,reference,&mapped),"6DOF exit camera");
    Check(std::abs(std::hypot(mapped.position[0]-shifted.position[0],mapped.position[2]-shifted.position[2])-.2F)<.001F,"lean preserved");
    for(float yaw:{-2.1F,0.0F,1.5F}){
        Check(BuildDemoActorEye({5,3,9},yaw,1.655F,&shifted),"exit camera yaw");
        local.orientation={0,std::sin(.3F),0,std::cos(.3F)};
        reference.orientation={0,std::sin(-.2F),0,std::cos(-.2F)};
        DemoCameraReturn ret;Check(ret.Capture(shifted,local,reference),"queue exit tracking");
        Check(MapCinematicEye(local,local,shifted,reference,&mapped),"mapped end head");
        LocomotionState locomotion;Check(locomotion.ObserveHead(local)&&locomotion.RestoreHead(AddVector(shifted.position,ret.offset),ret.yaw),"restore tracked final pose");
        ViewPose resumed;Check(locomotion.MapPose(local,&resumed),"resumed exit head");
        Matrix4 a{},b{};Check(BuildRigidTransform(mapped,&a)&&BuildRigidTransform(resumed,&b),"end matrices");
        for(unsigned i=0;i<16;++i)Check(std::abs(a[i]-b[i])<.001F,"end cannot snap to old seated heading or discard lean");
    }
    std::filesystem::remove_all(dir);
    std::cout<<"C35_POLICY=PASS settings=VR2_READS_VR1 demo=FIRST_STEP_AND_END difficulty=SELECTABLE exit_camera=ACTOR_6DOF\n";
    if(argc==1)return 0;
    const std::filesystem::path root(argv[1]),map=root/"Maps/Lev_Tut1.unr";
    hpvr_hp1_player_start_report start{};Check(hpvr_hp1_load_player_start_utf8(map.string().c_str(),.02F,0,&start)==0,"owned start");
    IntroCutscene scene;Check(LoadIntroCutscene(hpvr::wand::inspect_hp1_actor_visuals(map),start,start.rotation_units[1]*kTau/65536,&scene,"cutscene60"),"owned exit");
    unsigned moves=0;bool travel=false;for(const auto& t:scene.tracks)if(t.actor_reference==kHarryActorReference){
        for(const auto& raw:t.commands){const auto line=AsciiFold(raw);moves+=line.starts_with("moveto ")?1:0;travel|=line.starts_with("changelevel ");}}
    Check(moves==3&&travel,"Harry has three authored exit movements before demo boundary");
    std::cout<<"C35_OWNED=PASS harry_exit_moves="<<moves<<" end=CHANGELEVEL_BOUNDARY\n";return 0;
}catch(const std::exception& e){std::cerr<<"C35_FAIL="<<e.what()<<'\n';return 1;}}
