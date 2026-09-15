#include "hpvr/quest_frontend.h"
#include "hpvr/hp1_package_linker.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace hpvr::quest;
namespace {
unsigned checks=0;
void Check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
struct TempDirectory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("hpvr-c38-progress-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDirectory(){std::filesystem::create_directory(path);}
    ~TempDirectory(){std::error_code ec;std::filesystem::remove_all(path,ec);}
};
std::uint32_t Hash(const std::string& body){
    std::uint32_t hash=2166136261U;for(unsigned char c:body)hash=(hash^c)*16777619U;return hash;
}
void Bank(const std::filesystem::path& directory,const std::string& body,const std::string& tail={}){
    std::ofstream file(directory/"slot1.1.hpvr",std::ios::binary);
    file<<body<<'#'<<std::hex<<Hash(body)<<'\n'<<tail;
}
std::string Body(unsigned version,unsigned stage=12){
    std::ostringstream body;body<<"HPVR_PROGRESS "<<version<<" 5 2 14 1 2 3 0.5 ";
    const unsigned actors=version==1?2U:version==2?3U:version==3?5U:11U;
    for(unsigned i=0;i<actors;++i)body<<i<<" 2 3 0.25 ";
    body<<"0 1 ";
    if(version>=2)body<<stage<<' ';
    if(version>=3)body<<"2 101 202 ";
    if(version>=5)body<<"2 1 1 18 ";
    if(version>=6)body<<"80 4 1 1 1 ";
    if(version>=7)body<<"1 ";
    return body.str();
}
void OwnedAssets(const std::filesystem::path& root){
    FrontAssets intro,challenge,flying;
    Check(LoadFrontAssets(root,&intro),"owned tutorial frontend");
    const std::array<std::string_view,4> common_effects{
        "pickup_star","vase_breaking","cauldron_flip","save_game"};
    Check(intro.gameplay_audio.size()>common_effects.size(),"intro contains dialogue and common effects");
    const auto prefix_size=intro.gameplay_audio.size()-common_effects.size();
    const auto check_audio_prefix=[&](const FrontAssets& other){
        Check(other.gameplay_audio.size()>=intro.gameplay_audio.size(),"map preserves common audio");
        for(std::size_t i=0;i<prefix_size;++i)
            Check(intro.gameplay_audio[i].object_name==other.gameplay_audio[i].object_name&&
                  intro.gameplay_audio[i].encoded_bytes==other.gameplay_audio[i].encoded_bytes,
                  "legacy dialogue prefix and payloads stable");
        for(std::size_t i=0;i<common_effects.size();++i){
            const auto& baseline=intro.gameplay_audio[prefix_size+i];
            const auto& effect=other.gameplay_audio[other.gameplay_audio.size()-common_effects.size()+i];
            Check(baseline.object_name==common_effects[i]&&effect.object_name==common_effects[i]&&
                  baseline.encoded_bytes==effect.encoded_bytes,"common effect suffix and payloads stable");
        }
    };
    Check(LoadFrontAssets(root,&challenge,1),"owned challenge frontend");
    Check(intro.map_id==0&&challenge.map_id==1,"owned asset map association");
    Check(challenge.level_objective=="Collect the challenge stars.","owned localized challenge objective");
    Check(challenge.music.size()>intro.music.size(),"authored challenge music loaded");
    Check(challenge.level_music_index==3,"entry music inherits Hogwarts until authored trigger");
    for(std::size_t i=0;i<intro.music.size();++i)
        Check(intro.music[i].object_name==challenge.music[i].object_name,"legacy music indices stable");
    Check(challenge.gameplay_audio.size()>intro.gameplay_audio.size(),"challenge voices added");
    check_audio_prefix(challenge);
    const auto cue=std::ranges::find_if(challenge.music_cues,[](const auto& c){return c.tag=="MU1";});
    Check(cue!=challenge.music_cues.end()&&cue->music_index>=4,"entry music cue linked");
    Check(challenge.music.at(static_cast<std::size_t>(cue->music_index)).object_name=="Arg_SecretCauldron_loop",
        "entry trigger uses owned Secret Cauldron music");
    Check(std::ranges::any_of(challenge.gameplay_audio,[](const auto& s){return s.object_name=="QUIRRELL_015";}),
        "challenge introductory voice loaded");
    const auto census=hpvr::wand::inspect_hp1_actor_visuals(root/"Maps/Lev_Tut1b.unr");
    Check(census.status==hpvr::wand::Hp1ProfileStatus::ok,"owned challenge census");
    Check(!challenge.bump_speech.empty(),"challenge bump dialogue loaded");
    for(const auto& speech:challenge.bump_speech)
        Check(std::ranges::any_of(census.actors,[&](const auto& a){
            return a.actor_reference==speech.actor_reference&&
                std::ranges::any_of(a.serialized_properties,[](const auto& p){return p.name=="BumpLines";});
        }),"bump reference belongs to current map");
    const auto cast=hpvr::wand::build_hp1_character_manifest(root,root/"Maps/Lev_Tut1b.unr");
    const auto decorated=hpvr::wand::build_hp1_character_manifest(root,root/"Maps/Lev_Tut1b.unr",0,{},true);
    Check(cast.status==hpvr::wand::Hp1ProfileStatus::ok&&decorated.status==hpvr::wand::Hp1ProfileStatus::ok,
        "owned actor manifests");
    Check(std::ranges::none_of(cast.actors,[](const auto& a){return a.qualified_class_name.starts_with("HProps.");}),
        "character-only manifest excludes props");
    Check(std::ranges::count_if(decorated.actors,[](const auto& a){return a.qualified_class_name=="HProps.Star";})==8,
        "decoration manifest resolves all eight authored stars");
    Check(std::ranges::any_of(decorated.actors,[](const auto& a){return a.qualified_class_name=="HProps.RectangleWoodTable";}),
        "decorations stop at owned baseProps visual boundary");
    Check(LoadFrontAssets(root,&flying,kBroomstickTrainingMapId),"owned flying frontend");
    check_audio_prefix(flying);
    Check(flying.map_id==2&&flying.level_objective=="Flying lesson with Madam Hooch. Fly Harry through the hoops.",
        "owned flying objective and map identity");
    Check(flying.music.size()==7&&flying.level_music_index==4&&flying.music[4].object_name=="Arg_trollchase_loop",
        "flying entry uses LevelInfo-owned music");
    for(std::size_t i=0;i<intro.music.size();++i)
        Check(intro.music[i].object_name==flying.music[i].object_name,"flying keeps original music prefix");
    for(const auto& name:{"HOOCH_001","HOOCH_010","HOOCH_011","HOOCH_013","HOOCH_012","HOOCH_004","HOOCH_005",
                          "HOOCH_006","HOOCH_007","HOOCH_008","HOOCH_009","Q_whistle_short","Q_through_hoop",
                          "Q_through_hoop01","Q_through_hoop15","broom_accel"})
        Check(std::ranges::any_of(flying.gameplay_audio,[&](const auto& sound){return sound.object_name==name;}),
            "flying authored stage commentary and effects loaded");
    const auto neutral=std::ranges::find_if(flying.music_cues,[](const auto& cue){return cue.tag=="Neutral07Music";});
    const auto chase=std::ranges::find_if(flying.music_cues,[](const auto& cue){return cue.tag=="RememberallChaseMusic";});
    Check(neutral!=flying.music_cues.end()&&neutral->music_index==5&&
          chase!=flying.music_cues.end()&&chase->music_index==6,"flying authored music event links");
}
}
int main(int argc,char** argv){
    try{
        TempDirectory temp;
        const auto journal=temp.path/"journal";
        ProgressSave save,read;
        save.phase=2;save.page=14;save.quest_stage=23;save.lesson_passes=4;
        save.collected_beans={101,202};save.card_awarded=true;save.card_taken=true;
        Check(WriteProgress(journal,0,&save),"tutorial v8 checkpoint");
        save.map_id=1;save.quest_stage=0;save.banked_beans=2;save.collected_beans={101};
        save.activated_events={101,301};save.challenge_stars=3;
        save.graph_state="MAP_EVENTS 1\n17 \"trigger\" \\pending";
        save.world_state="MAP_WORLD 1\n13 \"mover\" \\state";
        Check(WriteProgress(journal,0,&save)&&ReadProgress(journal,0,&read),"challenge v8 checkpoint");
        Check(read.map_id==1&&read.quest_stage==0&&read.banked_beans==2&&read.collected_beans==save.collected_beans,
            "same local bean reference on next map not confused with previous map");
        Check(read.activated_events==save.activated_events&&read.challenge_stars==3&&read.graph_state==save.graph_state&&read.world_state==save.world_state,
            "events stars and escaped graph/world checkpoint round trip");
        Check(read.lesson_passes==4&&read.card_taken&&read.card_awarded,"learned spell and card cross map boundary");
        Check(read.generation==2,"save banks span map transition");
        for(unsigned stage=0;stage<=64;++stage){
            save.quest_stage=stage;
            Check(WriteProgress(journal,1,&save)&&ReadProgress(journal,1,&read)&&read.quest_stage==stage,
                "challenge stage round trip");
        }
        save.quest_stage=65;Check(!WriteProgress(journal,1,&save),"challenge stage overflow rejected");
        save.map_id=0;save.quest_stage=24;Check(!WriteProgress(journal,1,&save),"tutorial bounds preserved");
        save.map_id=4;save.quest_stage=0;Check(!WriteProgress(journal,1,&save),"unknown map rejected");
        save.map_id=1;save.banked_beans=1000001;Check(!WriteProgress(journal,1,&save),"bean counter bound");
        save.banked_beans=2;save.challenge_stars=1025;Check(!WriteProgress(journal,1,&save),"star counter bound");
        save.challenge_stars=3;save.activated_events={1,1};Check(!WriteProgress(journal,1,&save),"duplicate event rejected");
        save.activated_events={2,1};Check(!WriteProgress(journal,1,&save),"unsorted event rejected");
        for(std::int32_t ref:{-1,0,100001}){
            save.activated_events={ref};Check(!WriteProgress(journal,1,&save),"invalid event reference rejected");
            save.activated_events={1};save.collected_beans={ref};
            Check(!WriteProgress(journal,1,&save),"invalid bean reference rejected");
        }
        save.collected_beans.clear();save.activated_events.clear();
        for(std::int32_t i=1;i<=1024;++i){save.collected_beans.push_back(80000+i);save.activated_events.push_back(90000+i);}
        save.graph_state=std::string(16384,'"');
        save.world_state=std::string(32768,'\\');
        Check(WriteProgress(journal,2,&save)&&ReadProgress(journal,2,&read),"maximum vectors and escaped graph fit journal");
        Check(read.collected_beans==save.collected_beans&&read.activated_events==save.activated_events&&
            read.graph_state==save.graph_state&&read.world_state==save.world_state,"maximum checkpoint exact");
        save.collected_beans.push_back(81025);Check(!WriteProgress(journal,2,&save),"bean vector bound");
        save.collected_beans.pop_back();save.activated_events.push_back(91025);
        Check(!WriteProgress(journal,2,&save),"event vector bound");save.activated_events.pop_back();
        save.graph_state.push_back('x');Check(!WriteProgress(journal,2,&save),"graph byte bound");
        save.graph_state=std::string("bad\0state",9);Check(!WriteProgress(journal,2,&save),"graph NUL rejected");
        save.graph_state.clear();save.world_state.push_back('x');
        Check(!WriteProgress(journal,2,&save),"world state byte bound");
        save.world_state=std::string("bad\0state",9);
        Check(!WriteProgress(journal,2,&save),"world state NUL rejected");save.world_state.clear();
        save.graph_state.clear();save.player[1]=std::numeric_limits<float>::infinity();
        Check(!WriteProgress(journal,2,&save),"nonfinite player rejected");
        const auto legacy=temp.path/"legacy";std::filesystem::create_directory(legacy);
        for(unsigned version=1;version<=7;++version){
            Bank(legacy,Body(version));
            Check(ReadProgress(legacy,0,&read),"v1-v7 legacy format readable");
            Check(read.map_id==0&&read.banked_beans==0&&read.challenge_stars==0&&
                read.activated_events.empty()&&read.graph_state.empty()&&read.world_state.empty(),"legacy migration defaults map0 without invented events");
            Check(read.quest_stage==(version==1?1U:12U)&&read.cast[1][0]==1,"legacy stage and cast preserved");
            if(version>=3)Check(read.collected_beans==std::vector<std::int32_t>{101,202},"legacy bean IDs preserved");
            if(version>=6)Check(read.health==80&&read.lesson_passes==4&&read.card_taken,"legacy lesson state preserved");
            if(version==7)Check(read.peeves_first_hit,"v7 first-contact flag preserved");
        }
        const auto malformed=temp.path/"malformed";std::filesystem::create_directory(malformed);
        const auto expect_bad=[&](const std::string& body,const char* reason,const std::string& tail=std::string{}){
            Bank(malformed,body,tail);ProgressSave unchanged;unchanged.banked_beans=17;
            Check(!ReadProgress(malformed,0,&unchanged)&&unchanged.banked_beans==17,reason);
        };
        Bank(malformed,Body(8)+"1 0 0 0 \"\" \"\"\n");
        Check(ReadProgress(malformed,0,&read)&&read.map_id==1,"known-good synthetic v8 fixture");
        expect_bad(Body(10)+"0 0 0 0 \"\" \"\"\n","future format rejected");
        expect_bad(Body(8)+"4 0 0 0 \"\" \"\"\n","corrupt map rejected despite valid checksum");
        expect_bad(Body(8)+"1 0 0 1025 \"\" \"\"\n","oversized event allocation rejected");
        expect_bad(Body(8)+"1 0 0 -1 \"\" \"\"\n","negative event count rejected");
        expect_bad(Body(8)+"1 0 0 2 11 11 \"\" \"\"\n","duplicate serialized events rejected");
        expect_bad(Body(8)+"1 0 0 1 -1 \"\" \"\"\n","negative serialized event rejected");
        expect_bad(Body(8)+"1 0 0 0 not-quoted\n","graph framing required");
        expect_bad(Body(8)+"1 0 0 0 \"unfinished\n","truncated graph rejected");
        expect_bad(Body(8)+"1 0 0 0 \""+std::string(16385,'x')+"\" \"\"\n","oversized parsed graph rejected");
        expect_bad(Body(8)+"1 0 0 0 \"\" \""+std::string(32769,'x')+"\"\n","oversized parsed world state rejected");
        expect_bad(Body(8)+"1 0 0 0 \"\" unquoted\n","world state framing required");
        expect_bad(Body(8)+"1 0 0 0 \"\" \"\" trailing\n","trailing body rejected");
        expect_bad(Body(8)+"1 0 0 0 \"\" \"\"\n","trailing checksum data rejected","garbage");
        {std::ofstream bad(malformed/"slot1.1.hpvr",std::ios::binary);bad<<std::string(131073,'x');}
        Check(!ReadProgress(malformed,0,&read),"oversized file rejected before parsing");
        {std::ofstream bad(journal/"slot1.0.hpvr",std::ios::binary);bad<<"truncated next map";}
        Check(ReadProgress(journal,0,&read)&&read.map_id==0&&read.quest_stage==23&&read.collected_beans.size()==2,
            "corrupted map transition falls back to complete previous-map checkpoint");
        ProgressSave tutorial;tutorial.quest_stage=12;tutorial.banked_beans=25;
        Check(!TutorialRewardReady(tutorial),"banked beans cannot complete tutorial gate");
        for(std::int32_t i=1;i<=25;++i)tutorial.collected_beans.push_back(i);
        Check(TutorialRewardReady(tutorial),"local tutorial beans complete original gate");
        tutorial.map_id=1;Check(!TutorialRewardReady(tutorial),"same map2 stage cannot trigger tutorial reward");
        Check(!ApplyFirstPeevesContact(tutorial)&&tutorial.health==100&&!tutorial.peeves_first_hit,
            "tutorial Peeves damage cannot run on map2");
        QuestFrontEnd front;front.progress=read;front.progress.map_id=1;front.progress.quest_stage=64;
        front.assets.map_id=1;
        front.progress.challenge_stars=5;front.progress.banked_beans=25;
        front.assets.level_objective="Collect the challenge stars.";
        front.screen=FrontScreen::Pause;
        Check(!front.Quads().empty(),"map2 pause objective handles entire stage range");
        const auto map2_key=front.DrawKey();front.progress.quest_stage=0;
        Check(front.DrawKey()==map2_key,"map2 objective uses stable prebuilt key throughout challenge");
        front.screen=FrontScreen::Report;front.progress.map_id=1;
        const auto report_key=front.DrawKey();++front.progress.challenge_stars;
        Check(front.DrawKey()==report_key&&!front.Quads().empty(),"report background stays cached while counters use overlays");
        Check(front.ChallengeStarQuads(0).size()==front.ChallengeStarQuads(8).size()&&
            !front.ChallengeStarQuads(3,true).empty(),"challenge counter overlays bounded for all eight stars");
        for(unsigned stars=0;stars<=8;++stars)for(bool report:{false,true}){
            const auto quads=front.ChallengeStarQuads(stars,report);
            Check(std::ranges::all_of(quads,[](const auto& q){return q.x>=0&&q.y>=0&&q.x+q.w<=640&&q.y+q.h<=480;}),
                "every challenge counter fits the cached panel");
            if(!report)Check(std::ranges::all_of(quads,[](const auto& q){return q.y>357;}),
                "star counter does not overlap beans");
        }
        Check(front.ChallengeStarQuads(1000000).size()==front.ChallengeStarQuads(8).size(),
            "invalid large display count stays bounded");
        FrontAssets assets;
        Check(!LoadFrontAssets({},&assets,4)&&!assets.error.empty(),"unsupported asset map fails before loading");
        const auto flying_journal=temp.path/"flying";
        ProgressSave flying;flying.map_id=kBroomstickTrainingMapId;flying.phase=2;flying.page=14;
        flying.lesson_passes=4;flying.banked_beans=73;flying.collected_beans={21,81};flying.activated_events={34};
        flying.graph_state="FLYING_TEST 1";flying.world_state="FLYING_WORLD_TEST 1";flying.quest_stage=64;
        Check(WriteProgress(flying_journal,0,&flying)&&ReadProgress(flying_journal,0,&read)&&
            read.map_id==2&&read.banked_beans==73&&read.collected_beans==flying.collected_beans&&
            read.activated_events==flying.activated_events&&read.graph_state==flying.graph_state&&read.world_state==flying.world_state,
            "third-map progress retains map-local state and inherited bean bank");
        Check(!TutorialRewardReady(read)&&!ApplyFirstPeevesContact(read),"third-map progress cannot enter introduction-only events");
        flying.quest_stage=65;Check(!WriteProgress(flying_journal,0,&flying),"third-map stage remains bounded");
        flying.quest_stage=0;flying.collected_beans={0x20000000+16};
        Check(!WriteProgress(flying_journal,0,&flying),"challenge-only spawned bean IDs cannot enter flying map");
        front.assets.map_id=2;front.progress.map_id=2;front.progress.quest_stage=64;front.screen=FrontScreen::Pause;
        const auto flying_key=front.DrawKey();front.progress.quest_stage=0;
        Check(front.DrawKey()==flying_key&&!front.Quads().empty(),"third-map objective uses stable cached panel");
        if(argc==2)OwnedAssets(argv[1]);
        else Check(argc==1,"usage: test [owned-data-root]");
        std::cout<<"C38_PROGRESS=PASS checks="<<checks<<" version=9 legacy=1-8 maps=4 corruption=PASS\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"C38_PROGRESS=FAIL "<<e.what()<<'\n';return 1;}
}
