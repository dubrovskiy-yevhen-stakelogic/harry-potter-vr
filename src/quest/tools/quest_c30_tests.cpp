#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
using namespace hpvr::quest;
static void Check(bool value,const char* name){if(!value)throw std::runtime_error(name);}
int main(int argc,char**argv){try{
    IntroCutscene scene;scene.camera_target="harryloc";
    scene.locations.push_back({603,"HarryLoc",{99,99,99}});
    std::vector<CharacterDraw> actors(1);actors[0].actor_reference=603;
    actors[0].base_origin={1,2,3};actors[0].cutscene_offset={4,5,6};
    auto target=CinematicTarget(scene,actors);
    Check(target&&std::abs((*target)[0]-5)<.0001F&&std::abs((*target)[1]-7.9F)<.0001F,"camera follows live actor, never map spawn");
    scene.locations[0].actor_reference=999;target=CinematicTarget(scene,actors);
    Check(target&&(*target)[0]==99,"static cutmark remains static");
    scene.camera_target="missing";Check(!CinematicTarget(scene,actors),"unknown camera target rejected");
    QuestFrontEnd front;const auto ready=front.LessonQuads(0,true),wait=front.LessonQuads(0,false);
    const auto ready_glyph=std::ranges::find_if(ready,[](const auto& q){return q.y==415;});
    const auto wait_glyph=std::ranges::find_if(wait,[](const auto& q){return q.y==415;});
    Check(ready_glyph!=ready.end()&&wait_glyph!=wait.end()&&
        (ready_glyph->u!=wait_glyph->u||ready_glyph->v!=wait_glyph->v),"lesson exposes ready versus teacher speaking");
    Check(front.LessonQuads(4,true)[18].u!=ready[18].u,"lesson counter changes after passes");
    scene.playing=true;scene.tracks.resize(1);scene.tracks[0].actor_reference=603;
    actors[0].active_clip="run";scene.tracks[0].moving=true;SettleStoppedActors(scene,actors);
    Check(actors[0].active_clip=="run","moving actor keeps stride");
    scene.tracks[0].moving=false;SettleStoppedActors(scene,actors);
    Check(actors[0].active_clip=="breathe","waiting actor cannot run in place");
    actors[0].active_clip="run";scene.playing=false;SettleStoppedActors(scene,actors);
    Check(actors[0].active_clip=="breathe","completed route cannot leave Hermione running");
    std::cout<<"C30_POLICY=PASS live_camera_targets=YES\n";
    if(argc==1)return 0;if(argc!=2)return 2;
    const std::filesystem::path root(argv[1]),map=root/"Maps/Lev_Tut1.unr";
    hpvr_hp1_player_start_report start{};
    Check(hpvr_hp1_load_player_start_utf8(map.string().c_str(),.02F,0,&start)==0,"owned start");
    const auto census=hpvr::wand::inspect_hp1_actor_visuals(map);
    const float yaw=start.rotation_units[1]*kTau/65536.0F;
    for(const auto name:{"cutscene3","cutscene60"}){
        IntroCutscene cut;Check(LoadIntroCutscene(census,start,yaw,&cut,name),"owned tutorial scene");
        std::set<std::string> cues,waits;bool reward=false,travel=false;
        for(const auto& t:cut.tracks)for(const auto& line:t.commands){const auto s=AsciiFold(line);
            if(s.starts_with("cue "))cues.insert(s.substr(4));
            if(s.starts_with("waitfor "))waits.insert(s.substr(8));
            reward|=s=="trigger spawnwizardcard";travel|=s.starts_with("changelevel ");
        }
        if(travel)cues.insert("cutend");
        for(const auto& wait:waits)Check(cues.contains(wait),"unresolved tutorial cue");
        Check(reward||travel,"tutorial must award card or reach travel boundary");
    }
    FrontAssets assets;Check(LoadFrontAssets(root,&assets),"owned tutorial assets");
    Check(assets.gameplay_audio.size()==78&&!assets.frog_pickup.samples.empty(),"owned speech and actual PCM frog");
    Check(GameplayDialogueIndex(assets,"quirrell_lesson_99").has_value()&&GameplayDialogueIndex(assets,"QUIRRELL_014").has_value(),"lesson intro and exit speech");
    std::cout<<"C30_OWNED=PASS tutorial_cues=RESOLVED speech=78 frog=PCM\n";return 0;
}catch(const std::exception& e){std::cerr<<"C30_FAIL="<<e.what()<<'\n';return 1;}}
