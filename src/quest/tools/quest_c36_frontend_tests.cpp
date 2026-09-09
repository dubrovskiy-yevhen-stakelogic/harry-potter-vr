#include "hpvr/quest_frontend.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace hpvr::quest;
namespace {
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void CheckWindow(const std::vector<FrontQuad>& quads){
    for(const auto& q:quads)Check(q.x>=0&&q.y>=0&&q.x+q.w<=640.01F&&q.y+q.h<=480.01F,"VR panel fits window");
}
}
int main(){try{
    const auto dir=std::filesystem::temp_directory_path()/("hpvr-c36-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir/"SaveGames");
    {std::ofstream f(dir/"vr-settings.1");f<<"HPVR_VR1 175 40 1 "<<VrSettingsChecksum(175,40,1)<<'\n';}
    auto settings=ReadVrSettings(dir);
    Check(settings.render_scale==175&&settings.ssr==40&&settings.first_person_cutscenes,"VR1 keeps rendering, missing camera uses current default");
    settings.relaxed_lesson=true;settings.welcome_seen=true;
    {std::ofstream f(dir/"vr-settings.0");f<<"HPVR_VR2 175 40 2 1 1 "<<VrSettingsChecksumV2(settings,2)<<'\n';}
    settings=ReadVrSettings(dir);
    Check(settings.generation==2&&settings.relaxed_lesson&&settings.welcome_seen&&settings.first_person_cutscenes,"VR2 preserves difficulty and welcome, missing camera uses current default");
    settings.first_person_cutscenes=true;
    Check(WriteVrSettings(dir,settings),"VR3 durable write");
    auto loaded=ReadVrSettings(dir);
    Check(loaded.generation==3&&loaded.first_person_cutscenes&&loaded.relaxed_lesson&&loaded.welcome_seen&&loaded.render_scale==175&&loaded.ssr==40,"VR3 round trip all preferences");
    Check(VrSettingsChecksumV2(loaded,loaded.generation)!=VrSettingsChecksumV3(loaded,loaded.generation),"camera participates in checksum");
    {std::ofstream f(dir/"vr-settings.1");f<<"HPVR_VR3 175 40 3 1 1 2 "<<VrSettingsChecksumV3(settings,3)<<'\n';}
    loaded=ReadVrSettings(dir);Check(loaded.generation==2&&loaded.first_person_cutscenes,"malformed camera flag falls back to VR2 and current camera default");
    {std::ofstream f(dir/"vr-settings.1");f<<"HPVR_VR3 175 40 3 1 1 1 0\n";}
    Check(ReadVrSettings(dir).generation==2,"corrupt camera bank retains prior preferences");

    loaded.first_person_cutscenes=false; // Exercise live theatre-to-Harry toggling independent of defaults.
    QuestFrontEnd front;front.saves=dir/"SaveGames";front.vr=loaded;front.BeginGame();front.ToggleVrMenu();
    Check(!front.PausesWorld()&&!front.PausesAudio()&&front.WorldVisible(),"VR menu keeps cutscene simulation and audio live");
    front.selection=4;
    Check(front.Input(0,true,false)==FrontAction::None&&!front.vr.first_person_cutscenes,"opening chord cannot immediately toggle camera");
    front.Input(0,false,false);const auto theatre_key=front.DrawKey();
    front.Input(0,true,false);
    Check(front.screen==FrontScreen::Vr&&front.vr.first_person_cutscenes&&front.DrawKey()!=theatre_key,"trigger changes camera live without closing panel");
    Check(ReadVrSettings(dir).first_person_cutscenes,"camera choice saves immediately");
    front.Input(0,true,false);Check(front.vr.first_person_cutscenes,"held trigger cannot oscillate mode");
    front.Input(0,false,false);front.Input(0,false,false,-1);
    Check(!front.vr.first_person_cutscenes&&!ReadVrSettings(dir).first_person_cutscenes,"horizontal stick toggles and saves camera");
    front.Input(0,false,false,-1);Check(!front.vr.first_person_cutscenes,"held stick cannot oscillate camera");
    front.Input(0,false,false);front.selection=5;
    Check(front.Input(0,true,false)==FrontAction::Resume&&!front.Visible(),"sixth row returns to ongoing game");
    front.ToggleVrMenu();front.Input(0,false,false);front.Input(1,false,false);
    Check(front.selection==5,"up wraps from first to sixth row");
    front.Input(0,false,false);front.Input(-1,false,false);Check(front.selection==0,"down wraps to first row");
    front.selection=2;front.Input(0,false,false);front.Input(0,true,false);Check(front.screen==FrontScreen::Debug,"debugger stays at index two");
    front.Input(0,false,false);front.Input(0,false,true);Check(front.screen==FrontScreen::Vr&&front.selection==2,"debugger returns to index two");
    front.Input(0,false,false);front.selection=3;front.Input(0,true,false);Check(!front.vr.relaxed_lesson,"difficulty remains at index three");
    std::set<std::string> keys;
    for(bool failed:{false,true})for(bool relaxed:{false,true})for(bool first_person:{false,true})for(unsigned row=0;row<6;++row){
        front.screen=FrontScreen::Vr;front.selection=row;front.vr_save_failed=failed;front.vr.relaxed_lesson=relaxed;front.vr.first_person_cutscenes=first_person;
        Check(keys.insert(front.DrawKey()).second,"all 48 prebuilt menu variants have unique keys");
        CheckWindow(front.Quads());
    }
    for(int scale=50;scale<=175;scale+=5){const auto quads=front.VrValueQuads(scale,true);CheckWindow(quads);Check(quads.front().y==132,"render scale matches row baseline");}
    for(int ssr=0;ssr<=100;ssr+=5){const auto quads=front.VrValueQuads(ssr,false);CheckWindow(quads);Check(quads.front().y==176,"SSR value matches row baseline");}
    std::filesystem::remove_all(dir);
    std::cout<<"C36_FRONTEND=PASS settings=VR3_READS_VR1_VR2 camera=LIVE_PERSISTED six_rows=48_VARIANTS\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"C36_FRONTEND_FAIL="<<e.what()<<'\n';return 1;}}
