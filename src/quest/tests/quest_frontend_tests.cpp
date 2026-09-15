#include "hpvr/quest_frontend.h"
#include "hpvr/quest_view.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <limits>
#include <cmath>
using namespace hpvr::quest;
static void Check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
int main(){
 try{
    const auto dir=std::filesystem::temp_directory_path()/("hpvr-progress-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    ProgressSave s,r;
    {
        QuestFrontEnd front;front.assets.star_icon=42;
        for(unsigned count=0;count<=8;++count){
            const auto quads=front.StarPickupQuads(count);
            Check(quads.size()==3,"star pickup has frame, original icon and count");
        }
        Check(front.StarPickupQuads(12).size()==4,"star pickup supports multi-digit counts");
    }
    {
        HousePointHud counter;
        counter.Advance(100,.05F,false);Check(counter.remaining==0&&counter.Value()==100,"loaded points do not replay awards");
        counter.Advance(105,.05F,false);Check(counter.remaining>0&&counter.Value()<105,"award starts visible counting crest");
        for(unsigned i=0;i<20;++i)counter.Advance(105,.05F,false);
        Check(counter.Value()==105,"crest counter reaches exact total");
        const float remaining=counter.remaining;counter.Advance(105,1,true);
        Check(counter.remaining==remaining,"pause retains award notification");
        counter.Advance(115,.05F,false);counter.Advance(120,.05F,false);
        for(unsigned i=0;i<100;++i)counter.Advance(120,.05F,false);
        Check(counter.Value()==120&&counter.remaining==0,"consecutive awards accumulate and notification expires");
        counter.Advance(90,.05F,false);Check(counter.Value()==90&&counter.remaining==0,"checkpoint rollback does not replay lost points");
    }
    {
        ProgressSave charms;charms.map_id=3;charms.phase=2;
        charms.collected_beans={1568,0x20000000+1730*16,0x30000000+2576};
        Check(WriteProgress(dir,2,&charms)&&ReadProgress(dir,2,&r)&&r.collected_beans==charms.collected_beans,
              "charms book persists frog, chest and knight spawner rewards");
        for(unsigned map:{0U,1U,2U}){charms.map_id=map;Check(!WriteProgress(dir,2,&charms),"spawner rewards cannot enter another map");}
        charms.map_id=3;charms.collected_beans={0x30000000+100001};
        Check(!WriteProgress(dir,2,&charms),"out-of-range spawner rejected");
        std::filesystem::remove(dir/"slot3.1.hpvr");
    }
    Check(!ReadProgress(dir,0,&r),"empty save");
    s.page=4;s.player={2,1.4F,-8};
    Check(WriteProgress(dir,0,&s)&&s.generation==1,"first durable write");
    s.page=8;Check(WriteProgress(dir,0,&s)&&s.generation==2,"second bank");
    Check(ReadProgress(dir,0,&r)&&r.page==8,"latest generation");
    {std::ofstream bad(dir/"slot1.0.hpvr",std::ios::binary);bad<<"truncated";}
    Check(ReadProgress(dir,0,&r)&&r.page==4,"recover prior bank after corruption");
    s.page=14;s.phase=2;s.cast[0]={1,2,3,0.6F};
    s.quest_stage=3;s.cast[2]={4,5,6,0.9F};
    Check(WriteProgress(dir,1,&s)&&ReadProgress(dir,1,&r)&&r.phase==2&&r.cast[0]==s.cast[0],"slot isolation and actor pose");
    Check(r.quest_stage==3&&r.cast[2]==s.cast[2],"Ron quest and pose journaled");
    s.quest_stage=7;s.cast[3]={3,4,5,0.2F};s.cast[4]={5,6,7,0.7F};s.collected_beans={2145,2369};
    Check(WriteProgress(dir,1,&s)&&ReadProgress(dir,1,&r)&&r.collected_beans==s.collected_beans&&
        r.cast[3]==s.cast[3]&&r.cast[4]==s.cast[4]&&r.quest_stage==7,"twins and beans durable");
    for(unsigned stage=8;stage<=23;++stage){
        s.quest_stage=stage;Check(WriteProgress(dir,1,&s)&&ReadProgress(dir,1,&r)&&r.quest_stage==stage,"jump quest checkpoint");
    }
    s.collected_beans={2145,2145};Check(!WriteProgress(dir,1,&s),"duplicate beans rejected");
    s.collected_beans={2369,2145};Check(!WriteProgress(dir,1,&s),"unsorted beans rejected");
    s.collected_beans={2145};
    s.peeves_phase=2;s.twins_departed=true;s.filch_resume_stage=18;s.filch_seen=true;
    Check(WriteProgress(dir,1,&s)&&ReadProgress(dir,1,&r)&&r.peeves_phase==2&&r.twins_departed&&r.filch_resume_stage==18&&r.filch_seen,"V5 side encounters durable");
    s.peeves_phase=4;Check(!WriteProgress(dir,1,&s),"invalid Peeves phase rejected");s.peeves_phase=2;
    s.health=85;s.lesson_passes=3;s.frog_taken=true;s.card_awarded=true;s.card_taken=true;
    Check(WriteProgress(dir,1,&s)&&ReadProgress(dir,1,&r)&&r.health==85&&r.lesson_passes==3&&r.frog_taken&&r.card_taken,"V6 tutorial state durable");
    ProgressSave tutorial;tutorial.quest_stage=12;
    for(int i=1;i<=24;++i)tutorial.collected_beans.push_back(i);
    Check(!TutorialRewardReady(tutorial),"24 beans cannot open reward gate");
    tutorial.collected_beans.push_back(25);Check(TutorialRewardReady(tutorial),"25 beans allow card scene");
    tutorial.card_awarded=true;Check(!TutorialRewardReady(tutorial),"reward cannot repeat");
    ApplyTutorialDamage(tutorial);Check(tutorial.health==90,"Peeves contact damages normalized health");
    tutorial.health=1;ApplyTutorialDamage(tutorial);Check(tutorial.health==1,"tutorial contact cannot underflow health");
    std::ostringstream legacy4;legacy4<<"HPVR_PROGRESS 4 7 2 14 1 2 3 0.5 ";
    for(unsigned i=0;i<44;++i)legacy4<<"0 ";legacy4<<"1 1 16 0\n";
    std::uint32_t hash4=2166136261U;for(unsigned char c:legacy4.str())hash4=(hash4^c)*16777619U;
    {std::ofstream old(dir/"slot3.1.hpvr",std::ios::binary);old<<legacy4.str()<<'#'<<std::hex<<hash4<<'\n';}
    Check(ReadProgress(dir,2,&r)&&r.quest_stage==16&&r.peeves_phase==3&&!r.filch_seen,"V4 keeps Draco progress and restores optional Filch");
    const std::string legacy3="HPVR_PROGRESS 3 7 2 14 1 2 3 0.5 0 0 0 0 1 2 3 4 4 5 6 0.9 3 4 5 0.2 5 6 7 0.7 1 1 12 2 2145 2369\n";
    std::uint32_t hash3=2166136261U;for(unsigned char c:legacy3)hash3=(hash3^c)*16777619U;
    {std::ofstream old(dir/"slot3.1.hpvr",std::ios::binary);old<<legacy3<<'#'<<std::hex<<hash3<<'\n';}
    Check(ReadProgress(dir,2,&r)&&r.quest_stage==12&&r.collected_beans.size()==2&&r.cast[4][0]==5&&r.cast[10][0]==0,"version 3 migration retains twins and beans");
    r.quest_stage=20;r.cast[10]={1,2,3,0.5F};
    Check(WriteProgress(dir,2,&r)&&ReadProgress(dir,2,&s)&&s.cast[10]==r.cast[10],"migrated save writes Quirrell pose");
    std::filesystem::remove(dir/"slot3.0.hpvr");
    const std::string legacy2="HPVR_PROGRESS 2 7 2 14 1 2 3 0.5 0 0 0 0 1 2 3 4 4 5 6 0.9 1 1 4\n";
    std::uint32_t hash2=2166136261U;for(unsigned char c:legacy2)hash2=(hash2^c)*16777619U;
    {std::ofstream old(dir/"slot3.1.hpvr",std::ios::binary);old<<legacy2<<'#'<<std::hex<<hash2<<'\n';}
    Check(ReadProgress(dir,2,&r)&&r.quest_stage==4&&r.collected_beans.empty()&&r.cast[2][0]==4,"version 2 migration keeps Ron progress");
    // A real version-1 layout (two actors) must migrate without redoing the intro.
    const std::string legacy="HPVR_PROGRESS 1 7 2 14 1 2 3 0.5 0 0 0 0 1 2 3 4 1 1\n";
    std::uint32_t hash=2166136261U;for(unsigned char c:legacy)hash=(hash^c)*16777619U;
    {std::ofstream old(dir/"slot3.1.hpvr",std::ios::binary);old<<legacy<<'#'<<std::hex<<hash<<'\n';}
    Check(ReadProgress(dir,2,&r)&&r.phase==2&&r.quest_stage==1&&r.player[1]==2,"version 1 migration");
    std::filesystem::remove(dir/"slot3.1.hpvr");
    Check(ReadProgress(dir,0,&r)&&r.page==4,"other slot unchanged");
    s.player[0]=std::numeric_limits<float>::quiet_NaN();
    Check(!WriteProgress(dir,1,&s),"non-finite rejected");
    Check(!WriteProgress(dir,3,&r),"out of range slot");
    {std::ofstream incomplete(dir/"slot1.0.hpvr.tmp");incomplete<<"unfinished";}
    Check(ReadProgress(dir,0,&r)&&r.page==4,"incomplete temp ignored");
    QuestFrontEnd f;f.saves=dir;f.assets.story.resize(14);f.RefreshSlots();
    Check(f.VoiceAimQuads(0).empty(),"voice hint hidden while disabled");
    for(unsigned voice_status:{1U,2U,3U,4U,5U,6U,7U,99U}){
        const auto hint=f.VoiceAimQuads(voice_status);
        Check(!hint.empty()&&hint.size()<24,"voice aim hint remains a single bounded text batch");
        for(const auto& quad:hint)Check(quad.x>200&&quad.x+quad.w<440&&quad.y>=230&&quad.y+quad.h<250,
            "voice hint is centered for its shared world-space transform");
    }
    Check(f.screen==FrontScreen::Main&&f.occupied[0]&&f.occupied[1]&&!f.occupied[2],"menu and slots");
    auto click=[&](){f.Input(0,false,false);return f.Input(0,true,false);};
    Check(click()==FrontAction::None&&f.screen==FrontScreen::Slots,"start opens slots");
    Check(f.Input(0,true,false)==FrontAction::None&&f.screen==FrontScreen::Slots,"held trigger no duplicate");
    click();Check(f.screen==FrontScreen::Slot,"slot selected");
    Check(click()==FrontAction::Continue&&f.progress.page==4,"continue checkpoint page");
    f.selection=1;click();Check(f.screen==FrontScreen::Replace&&f.selection==1,"replace defaults to no");
    click();Check(f.screen==FrontScreen::Slot,"cancel replace retains save");
    f.BeginStory(4);f.progress.page=4;
    Check(f.TickStory(2,5)==FrontAction::None&&f.page==4,"page waits voice");
    Check(f.TickStory(6,5)==FrontAction::None&&f.page==5,"page boundary autosave");
    f.BeginStory(13);
    Check(f.TickStory(20,5)==FrontAction::StoryDone&&f.progress.phase==1&&f.progress.page==14,"story enters intro once");
    Check(ReadProgress(dir,0,&r)&&r.phase==1&&r.page==14,"story complete durable");
    f.paused=FrontScreen::Story;f.BeginGame();
    Check(f.paused==FrontScreen::Game,"loaded game cannot inherit old book pause state");
    f.screen=FrontScreen::Main;f.selection=4;click();
    Check(f.screen==FrontScreen::Levels,"level select opens from main menu");
    f.selection=2;click();
    Check(f.screen==FrontScreen::LevelSlots&&f.selected_map==2,"third level selects its stable map ID");
    f.selection=2;click();
    Check(f.screen==FrontScreen::LevelStart&&f.slot==2&&f.selection==1,"third-level overwrite confirmation defaults to keep save");
    click();Check(f.screen==FrontScreen::LevelSlots,"third-level cancel returns without starting or writing");
    f.selection=3;click();Check(f.screen==FrontScreen::Levels&&f.selection==2,"slot back returns to third level row");
    f.selection=static_cast<unsigned>(kQuestMaps.size());click();
    Check(f.screen==FrontScreen::Main&&f.selection==4,"level-select back is after every supported map");
    for(unsigned row=0;row<=kQuestMaps.size();++row){
        f.screen=FrontScreen::Levels;f.selection=row;
        for(const auto& quad:f.Quads())Check(quad.x>=0&&quad.y>=0&&quad.x+quad.w<=640&&quad.y+quad.h<=480,
                                            "supported-level menu fits existing panel");
    }
    const auto blocked=dir/"not-a-directory";
    {std::ofstream block(blocked);block<<"occupied";}
    f.saves=blocked;f.BeginStory(6);f.progress.page=6;f.progress.phase=0;
    Check(f.TickStory(20,5)==FrontAction::None&&f.page==6&&f.progress.page==6,
        "failed autosave does not advance checkpoint");
    Check(f.screen==FrontScreen::Stub&&f.DrawKey().ends_with("F"),"save failure visible");
    LocomotionState movement;ViewPose local{{0.4F,0.2F,-0.3F},{0,0,0,1}};
    Check(movement.ObserveHead(local),"head sample");
    Check(movement.RestoreHead({4,2,-7},1.1F),"restore pose");
    ViewPose world;Check(movement.MapPose(local,&world),"restored mapping");
    Check(std::abs(world.position[0]-4)<0.00001F&&std::abs(world.position[1]-2)<0.00001F&&
          std::abs(world.position[2]+7)<0.00001F,"restore independent of headset origin");
    std::array<float,3> body{};
    auto capture=[](void* ctx,const std::array<float,3>& center,const std::array<float,3>&,LocomotionMove* out){
        *static_cast<std::array<float,3>*>(ctx)=center;*out={};return true;
    };
    LocomotionInput input;input.move_active=true;input.move_y=1;
    Check(movement.Tick(input,0.01F,capture,&body),"restored movement");
    Check(std::abs(body[1]-(2.0F-0.815F))<0.00001F,"body ground independent of restored tracking Y");
    // Remove only files in the unique directory created by this test.
    std::filesystem::remove_all(dir);
    std::cout<<"FRONTEND_TESTS=PASS journal_corruption=RECOVERED slots=3 story_resume=PASS headset_origin=PASS\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
