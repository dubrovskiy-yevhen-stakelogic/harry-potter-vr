#include "hpvr/quest_frontend.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
using namespace hpvr::quest;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Temporary {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("hpvr-c44-settings-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary(){Check(std::filesystem::create_directory(path),"isolated settings test directory");}
    ~Temporary(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
std::uint32_t Checksum(const std::string& fields){
    std::uint32_t hash=2166136261U;
    for(unsigned char value:fields)hash=(hash^value)*16777619U;
    return hash;
}
void Fixture(const std::filesystem::path& root,int bank,const std::string& line){
    std::filesystem::create_directories(root);
    std::ofstream file(root/("vr-settings."+std::to_string(bank)),std::ios::trunc);
    file<<line<<'\n';file.close();Check(!file.fail(),"write synthetic settings fixture");
}
std::string Record(const char* magic,const std::string& fields){
    std::string spaced=fields;
    std::replace(spaced.begin(),spaced.end(),':',' ');
    return std::string(magic)+" "+spaced+" "+std::to_string(Checksum(fields));
}
void Press(QuestFrontEnd& front){front.Input(0,false,false);front.Input(0,true,false);}
std::string TextAt(const QuestFrontEnd& front,float y,float minimum_x=0){
    std::string result;
    for(const auto& quad:front.Quads())if(quad.texture==front.assets.font&&std::abs(quad.y-y)<.01F&&quad.x>=minimum_x){
        const auto column=static_cast<unsigned>(std::lround(quad.u*256-2))/16;
        const auto row=static_cast<unsigned>(std::lround(quad.v*256-2))/16;
        result+=static_cast<char>(row*16+column);
    }
    return result;
}
}

int main(){try{
    using namespace hpvr::quest;
    Temporary temporary;
    const auto untouched=temporary.path/"untouched";
    const auto defaults=ReadVrSettings(untouched);
    Check(defaults.casting_mode==CastingMode::Classic&&!defaults.voice_cast&&!defaults.voice_hints,"classic, voice-off and hidden-hint defaults");
    Check(defaults.render_scale==100&&defaults.ssr==30&&defaults.relaxed_lesson&&defaults.first_person_cutscenes&&
          !defaults.welcome_seen&&defaults.refresh_rate==90,"existing defaults preserved");
    Check(!std::filesystem::exists(untouched),"reading absent settings does not create files");
    Check(!UsesGestureCasting(CastingMode::Classic)&&!ShowsGestureTrace(CastingMode::Classic),"classic gesture policy");
    Check(UsesGestureCasting(CastingMode::VisibleGesture)&&ShowsGestureTrace(CastingMode::VisibleGesture),"visible gesture policy");
    Check(UsesGestureCasting(CastingMode::Gesture)&&!ShowsGestureTrace(CastingMode::Gesture),"unassisted gesture policy");

    for(int legacy=0;legacy<2;++legacy){
        const auto root=temporary.path/("legacy4-"+std::to_string(legacy));
        const auto fields="145:55:12:0:1:0:"+std::to_string(legacy)+":120";
        Fixture(root,0,Record("HPVR_VR4",fields));
        auto migrated=ReadVrSettings(root);
        Check(migrated.generation==12&&migrated.casting_mode==(legacy?CastingMode::Gesture:CastingMode::Classic)&&
              !migrated.voice_cast&&!migrated.voice_hints,"VR4 manual flag migrates with voice and hints off");
        Check(migrated.render_scale==145&&migrated.ssr==55&&!migrated.relaxed_lesson&&migrated.welcome_seen&&
              !migrated.first_person_cutscenes&&migrated.refresh_rate==120,"VR4 unrelated choices preserved");
        Check(WriteVrSettings(root,migrated)&&migrated.generation==13,"migrated settings write new generation");
        std::ifstream saved(root/"vr-settings.1");std::string magic;saved>>magic;
        Check(magic=="HPVR_VR6","migrated save upgrades to VR6");
        Check(ReadVrSettings(root).casting_mode==migrated.casting_mode,"migration survives second read");
    }
    for(int version=1;version<=3;++version){
        const auto root=temporary.path/("legacy"+std::to_string(version));
        std::string fields="125:50:8";
        if(version>=2)fields+=":0:1";
        if(version>=3)fields+=":0";
        Fixture(root,0,Record(("HPVR_VR"+std::to_string(version)).c_str(),fields));
        auto migrated=ReadVrSettings(root);
        Check(migrated.generation==8&&migrated.casting_mode==CastingMode::Classic&&!migrated.voice_cast&&!migrated.voice_hints&&
              migrated.refresh_rate==90,"older schemas retain default new controls");
        Check(migrated.relaxed_lesson==(version==1)&&migrated.welcome_seen==(version>=2)&&
              migrated.first_person_cutscenes==(version<3),"older schema choices migrate unchanged");
    }
    for(int mode=0;mode<3;++mode)for(bool voice:{false,true}){
        const auto root=temporary.path/("legacy5-"+std::to_string(mode)+(voice?"-voice":"-silent"));
        const auto fields="155:65:20:0:1:0:"+std::to_string(mode)+":72:"+(voice?"1":"0");
        Fixture(root,0,Record("HPVR_VR5",fields));
        auto migrated=ReadVrSettings(root);
        Check(migrated.generation==20&&migrated.casting_mode==static_cast<CastingMode>(mode)&&
              migrated.voice_cast==voice&&!migrated.voice_hints,"VR5 voice choice survives with hints hidden");
        Check(migrated.render_scale==155&&migrated.ssr==65&&!migrated.relaxed_lesson&&migrated.welcome_seen&&
              !migrated.first_person_cutscenes&&migrated.refresh_rate==72,"VR5 migration preserves unrelated choices");
        Check(WriteVrSettings(root,migrated),"VR5 settings upgrade to VR6");
        const auto read=ReadVrSettings(root);
        Check(read.generation==21&&read.voice_cast==voice&&!read.voice_hints&&read.casting_mode==migrated.casting_mode,
              "VR5 migrated preferences survive reload");
    }

    const auto round_trip=temporary.path/"round-trip";
    VrSettings saved;saved.render_scale=175;saved.ssr=45;saved.relaxed_lesson=false;saved.first_person_cutscenes=false;
    saved.welcome_seen=true;saved.refresh_rate=80;
    for(int mode=0;mode<3;++mode)for(bool voice:{false,true})for(bool hints:{false,true}){
        saved.casting_mode=static_cast<CastingMode>(mode);saved.voice_cast=voice;saved.voice_hints=hints;
        Check(WriteVrSettings(round_trip,saved),"save each mode and independent voice and hint choices");
        const auto read=ReadVrSettings(round_trip);
        Check(read.casting_mode==saved.casting_mode&&read.voice_cast==voice&&read.voice_hints==hints&&
              read.generation==saved.generation,"twelve casting combinations round-trip");
        Check(read.render_scale==175&&read.ssr==45&&!read.relaxed_lesson&&!read.first_person_cutscenes&&
              read.welcome_seen&&read.refresh_rate==80,"saving casting leaves other settings unchanged");
    }
    const auto generation=saved.generation;
    saved.casting_mode=static_cast<CastingMode>(3);
    Check(!WriteVrSettings(round_trip,saved)&&saved.generation==generation,"invalid enum rejected without write");
    Check(ReadVrSettings(round_trip).casting_mode==CastingMode::Gesture,"invalid write preserves previous bank");

    const auto invalid=temporary.path/"invalid";
    const auto good=Record("HPVR_VR5","100:30:4:1:0:1:1:90:1");
    Fixture(invalid,0,good);
    for(const auto& bad:{Record("HPVR_VR5","100:30:5:1:0:1:3:90:1"),
                         Record("HPVR_VR5","100:30:5:1:0:1:-1:90:1"),
                         Record("HPVR_VR5","100:30:5:1:0:1:2:90:2"),
                         Record("HPVR_VR5","100:30:5:1:0:1:2:75:0"),
                         Record("HPVR_VR5","100:30:5:1:0:1:2:90:0")+" extra",
                         std::string("HPVR_VR5 100 30 5 1 0 1 2 90"),
                         std::string("HPVR_VR5 100 30 5 1 0 1 2 90 0 0"),
                         Record("HPVR_VR6","100:30:5:1:0:1:2:90:0"),
                         Record("HPVR_VR6","100:30:5:1:0:1:2:90:0:2"),
                         Record("HPVR_VR6","100:30:5:1:0:1:2:90:0:-1"),
                         Record("HPVR_VR6","100:30:5:1:0:1:2:90:0:1")+" extra",
                         std::string("HPVR_VR6 100 30 5 1 0 1 2 90 0 1 0"),
                         Record("HPVR_VR7","100:30:5:1:0:1:2:90:0:1")}){
        Fixture(invalid,1,bad);
        const auto read=ReadVrSettings(invalid);
        Check(read.generation==4&&read.casting_mode==CastingMode::VisibleGesture&&read.voice_cast&&!read.voice_hints,
              "invalid newer bank falls back to valid mode, voice and migrated hidden hints");
    }
    Fixture(invalid,0,Record("HPVR_VR6","100:30:6:1:0:1:1:90:1:1"));
    Fixture(invalid,1,"HPVR_VR6 100 30 7 1 0 1 2 90 0 0 0");
    auto fallback=ReadVrSettings(invalid);
    Check(fallback.generation==6&&fallback.voice_hints&&fallback.voice_cast&&
          fallback.casting_mode==CastingMode::VisibleGesture,"corrupt newer VR6 bank preserves explicitly enabled hints");
    Fixture(invalid,1,Record("HPVR_VR5","100:30:7:1:0:1:2:120:1"));
    fallback=ReadVrSettings(invalid);
    Check(fallback.generation==7&&!fallback.voice_hints&&fallback.voice_cast&&
          fallback.casting_mode==CastingMode::Gesture&&fallback.refresh_rate==120,
          "newer VR5 bank cannot inherit hints from an older VR6 bank");

    QuestFrontEnd front;front.saves=temporary.path/"frontend"/"saves";
    front.assets.font=123;front.assets.white=124;front.BeginGame();front.ToggleVrMenu();
    Check(front.VrPanel()&&front.WorldVisible()&&!front.PausesWorld()&&!front.PausesAudio(),"VR menu remains an unpaused in-world panel");
    front.selection=5;
    Press(front);Check(front.vr.casting_mode==CastingMode::VisibleGesture,"classic cycles to visible gesture");
    Press(front);Check(front.vr.casting_mode==CastingMode::Gesture,"visible cycles to gesture");
    Press(front);Check(front.vr.casting_mode==CastingMode::Classic,"gesture cycles to classic");
    front.Input(0,false,false,0);front.Input(0,false,false,-1);
    Check(front.vr.casting_mode==CastingMode::Gesture,"left adjustment cycles backwards");
    front.Input(0,false,false,-1);Check(front.vr.casting_mode==CastingMode::Gesture,"held adjustment does not repeat");
    for(int mode=0;mode<3;++mode){
        front.vr.casting_mode=static_cast<CastingMode>(mode);front.vr.voice_cast=false;front.selection=7;
        Press(front);Check(front.vr.voice_cast&&front.vr.casting_mode==static_cast<CastingMode>(mode),"voice enables without changing wand mode");
        const auto read=ReadVrSettings(front.saves.parent_path());
        Check(read.voice_cast&&read.casting_mode==front.vr.casting_mode,"voice option persists immediately");
        Press(front);Check(!front.vr.voice_cast,"voice disables independently");
    }
    for(bool voice:{false,true}){
        front.vr.voice_cast=voice;front.vr.voice_hints=false;front.selection=8;
        const auto mode=front.vr.casting_mode;
        Press(front);
        Check(front.vr.voice_hints&&front.vr.voice_cast==voice&&front.vr.casting_mode==mode,
              "hints enable independently of voice and wand mode");
        const auto read=ReadVrSettings(front.saves.parent_path());
        Check(read.voice_hints&&read.voice_cast==voice&&read.casting_mode==mode,"hint option persists immediately");
        Press(front);Check(!front.vr.voice_hints&&front.vr.voice_cast==voice,"hints disable without changing voice");
    }
    std::set<std::string> draw_keys;
    for(int mode=0;mode<3;++mode)for(bool voice:{false,true})for(bool hints:{false,true}){
        front.vr.casting_mode=static_cast<CastingMode>(mode);front.vr.voice_cast=voice;front.vr.voice_hints=hints;front.selection=5;
        Check(draw_keys.insert(front.DrawKey()).second,"each casting/voice/hint combination has distinct geometry key");
        Check(TextAt(front,VrMenuRowY(5),400)==(mode==0?"CLASSIC":mode==1?"VISIBLEGESTURE":"GESTURE"),"casting label matches mode");
        Check(TextAt(front,VrMenuRowY(7),400)==(voice?"ON":"OFF"),"voice label matches toggle");
        Check(TextAt(front,VrMenuRowY(8),400)==(hints?"ON":"OFF"),"hint label matches toggle");
        for(const auto& quad:front.Quads())Check(quad.x>=0&&quad.y>=0&&quad.x+quad.w<=640&&quad.y+quad.h<=480,"VR menu geometry fits panel");
    }
    Check(front.VrValueQuads(175,true).front().y==VrMenuRowY(0)&&front.VrValueQuads(30,false).front().y==VrMenuRowY(1)&&
          front.VrRefreshQuads(90).front().y==VrMenuRowY(6),"live numeric overlays align with compact rows");
    front.selection=0;front.Input(0,false,false);front.Input(1,false,false);
    Check(kVrMenuRowCount==11&&front.selection==10,"selection wraps through all eleven rows");
    Press(front);Check(front.screen==FrontScreen::Game,"return row closes VR menu");
    const auto settings_generation=ReadVrSettings(front.saves.parent_path()).generation;
    front.page=11;front.progress.page=11;
    front.ToggleVrMenu();front.selection=kVrControlsRow;Press(front);
    Check(front.screen==FrontScreen::Controls&&front.controls_page==0&&front.selection==0,"controls opens at basics");
    Check(front.VrPanel()&&front.FloatingPanel()&&front.WorldVisible()&&!front.PausesWorld()&&!front.PausesAudio(),
          "controls retains live in-world VR panel behavior");
    front.Input(0,true,false);Check(front.controls_page==0,"opening trigger cannot skip basics");
    std::set<std::string> controls_keys;
    for(unsigned page=0;page<kControlsPageCount;++page){
        Check(front.controls_page==page&&controls_keys.insert(front.DrawKey()).second,"each controls page has unique prebake key");
        for(const auto& quad:front.Quads())Check(quad.x>=0&&quad.y>=0&&quad.x+quad.w<=640&&quad.y+quad.h<=480,
                                              "controls text fits panel");
        if(page==0)Check(TextAt(front,319)=="RECENTER-CLICKBOTHSTICKSTOGETHER","basics explains recenter chord");
        if(page==1)Check(TextAt(front,263)=="RELEASETHETRIGGERTOCASTFLIPENDO.","classic describes release to cast");
        if(page==2){
            Check(TextAt(front,179)=="HOLDTHERIGHTTRIGGER."&&
                  TextAt(front,207)=="AIMATAFLIPENDOSYMBOLTOLOCKON."&&
                  TextAt(front,235)=="DRAWTHEFLIPENDOGESTURE,THENRELEASE.","gesture hold-lock-draw-release sequence");
            Check(TextAt(front,347)=="INLESSONS,FOLLOWTHESHOWNPATTERN.","lesson precision distinguished from gameplay");
        }
        if(page==3)Check(TextAt(front,263)=="KEEPHOLDINGTOCASTAGAINBYVOICE."&&
                         TextAt(front,319)=="EXPERIMENTAL-ONLYTESTEDBYTHEAUTHOR.","voice repeat and experimental warning");
        Press(front);
    }
    Check(front.controls_page==0,"controls next wraps to basics");
    front.Input(0,false,false);front.Input(0,false,false,-1);
    Check(front.controls_page==3,"left stick visits previous help page");
    front.Input(0,false,false,-1);Check(front.controls_page==3,"held stick cannot skip through help");
    front.Input(0,false,false);front.Input(0,false,true);
    Check(front.screen==FrontScreen::Vr&&front.selection==kVrControlsRow,"B returns to controls row in settings");
    front.Input(0,false,true);Check(front.screen==FrontScreen::Vr,"held B cannot close settings too");
    Check(front.page==11&&front.progress.page==11&&ReadVrSettings(front.saves.parent_path()).generation==settings_generation,
          "reading controls does not change story position or write settings");
    Press(front);front.ToggleVrMenu();
    Check(front.screen==FrontScreen::Game,"VR chord closes controls directly to original game");
    front.BeginStory(7);front.ToggleVrMenu();front.selection=kVrControlsRow;Press(front);
    front.Input(0,false,false);front.Input(-1,false,false);
    front.ToggleVrMenu();
    Check(front.screen==FrontScreen::Story&&front.page==7,"controls cannot advance underlying story page");
    front.ShowDemoNotice(false);front.ToggleVrMenu();front.selection=kVrControlsRow;Press(front);
    Check(front.WorldVisible()&&front.PausesWorld(),"controls opened over welcome retains notice pause");
    front.ToggleVrMenu();
    Check(front.screen==FrontScreen::Welcome,"controls closes back to welcome without dismissing it");
    Check(TextAt(front,78)=="0.1.1ALPHA-WELCOME"&&TextAt(front,122)=="VERYEARLYALPHA-EXPECTBUGS."&&
          TextAt(front,194)=="ITHASONLYBEENTESTEDBYTHEAUTHOR.","welcome states alpha status and author-only voice testing");
    for(unsigned map:{0U,1U})for(auto screen:{FrontScreen::Welcome,FrontScreen::DemoEnd})for(unsigned row:{0U,1U}){
        front.assets.map_id=map;front.screen=screen;front.selection=row;
        for(const auto& quad:front.Quads())Check(quad.x>=0&&quad.y>=0&&quad.x+quad.w<=640&&quad.y+quad.h<=480,
                                              "notice text and both actions fit on both maps");
    }
    std::cout<<"C44_SETTINGS_TESTS=PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
