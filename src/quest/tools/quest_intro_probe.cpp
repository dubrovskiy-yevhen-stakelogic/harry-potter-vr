// CPU-only inclusion deliberately exercises the production owned-data loaders.
// No Android app, Vulkan renderer, retail executable or headset is launched.
#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "hpvr/quest_startup.h"

int main(int argc, char** argv) {
    if (argc != 3 && argc!=4) { std::cerr << "usage: hpvr_quest_intro_probe <owned-root> <pcm-cache> [startup-rgba]\n"; return 2; }
    using namespace hpvr::quest;
    try {
        const std::filesystem::path root(argv[1]);
        const auto startup=LoadWarnerStartup(root);if(startup.size()!=640*480*4)return 33;
        if(argc==4){std::ofstream out(argv[3],std::ios::binary);out.write(reinterpret_cast<const char*>(startup.data()),startup.size());}
        const auto map = root / "Maps/Lev_Tut1.unr";
        const auto scene = hpvr::wand::build_hp1_textured_bsp_scene(root, map, kMetersPerUnrealUnit, kMaximumTriangles);
        if (scene.status != hpvr::wand::Hp1ProfileStatus::ok) return 3;
        unsigned reflective=0;
        for(const auto& v:scene.vertices)if(v.texture_layer<scene.texture_layer_names.size()&&
            IsReflectiveWoodFloor(scene.texture_layer_names[v.texture_layer],v.normal.y))++reflective;
        if(reflective<100)return 39;
        std::cout<<"SSR_MATERIALS=PASS wood_floor_vertices="<<reflective<<"\n";
        hpvr_hp1_player_start_report start{};
        if (hpvr_hp1_load_player_start_utf8(map.string().c_str(), kMetersPerUnrealUnit, 0, &start) != HPVR_HP1_PROFILE_OK) return 4;
        const float yaw = start.rotation_units[1] * kTau / 65536.0F;
        WorldMetadata world;
        if (!LoadWorldMetadata(root, map, start, yaw, &world)) return 5;
        const auto census=hpvr::wand::inspect_hp1_actor_visuals(map);IntroCutscene ron;
        if(!LoadIntroCutscene(census,start,yaw,&ron,"CutScene51") ||
           ron.tracks.size()!=5 || ron.locations.size()!=22 || std::abs(ron.trigger_radius-2.6F)>0.001F)return 21;
        std::size_t camera_targets=0;
        IntroCutscene twins,next;
        if(!LoadIntroCutscene(census,start,yaw,&twins,"CutScene52")||twins.tracks.size()!=6||
           !LoadIntroCutscene(census,start,yaw,&next,"CutScene54"))return 26;
        if(std::ranges::count_if(twins.tracks,[](const auto& t){return t.camera;})!=1)return 27;
        for(const auto& t:twins.tracks)if(t.actor_reference==1329&&
            (CutsceneSpeaker(twins,t,"talk")!=1329||CutsceneSpeaker(twins,t,"talk2")!=1326))return 30;
        // Validate the owned cue dependency graph without playing the game.
        auto schedule=twins;unsigned talks=0;
        for(unsigned tick=0;tick<1000;++tick){
            bool advanced=false;
            for(auto& track:schedule.tracks){
                if(track.next_command==track.commands.size())continue;
                const auto& line=track.commands[track.next_command];const auto split=line.find(' ');
                const auto op=AsciiFold(line.substr(0,split));const auto arg=split==std::string::npos?std::string{}:AsciiFold(line.substr(split+1));
                if(op=="waitfor"&&!schedule.cues.contains(arg))continue;
                if(op=="cue")schedule.cues.insert(arg);
                if(op.starts_with("talk"))++talks;
                ++track.next_command;advanced=true;
            }
            if(!advanced)break;
        }
        if(talks!=6||std::ranges::any_of(schedule.tracks,[](const auto& t){return t.next_command!=t.commands.size();}))return 31;
        std::cout<<"TWINS_SCHEDULE=PASS spoken_lines=6 cross_cast_speaker=GEORGE no_cue_deadlock=YES\n";
        for(const auto& t:twins.tracks)std::cout<<"TWINS_CAST slot="<<t.cast_slot<<" actor="<<t.actor_reference<<" camera="<<t.camera<<'\n';
        for(const auto* cut:{&world.intro_cutscene,&ron,&twins}){
            std::set<std::string> cues,waits;
            for(const auto& track:cut->tracks)for(const auto& command:track.commands){
                const auto split=command.find(' ');if(split==std::string::npos)continue;
                const auto op=AsciiFold(command.substr(0,split)),arg=AsciiFold(command.substr(split+1));
                if(op=="cue")cues.insert(arg);if(op=="waitfor")waits.insert(arg);
                if(op=="preface"||op=="moveto"||op=="teleport"||op=="face"){
                    const auto loc=std::ranges::find_if(cut->locations,[&](const auto& l){return AsciiFold(l.alias)==arg;});
                    const auto cast=std::ranges::find_if(cut->tracks,[&](const auto& t){return AsciiFold(t.alias)==arg;});
                    if(loc==cut->locations.end()&&cast==cut->tracks.end())return 22;
                    if(op=="preface")++camera_targets;
                }
            }
            for(const auto& cue:waits)if(!cues.contains(cue))return 23;
        }
        if(camera_targets<6)return 24;
        std::cout<<"AUTHORED_QUEST=PASS scene=CutScene51 triggers=1 camera_targets="<<camera_targets<<" cues=RESOLVED\n";
        std::vector<GpuVertex> vertices;
        for (const auto& v : scene.vertices) {
            const auto p = RotateYaw({v.position_m.x - start.position_m[0], v.position_m.y - start.position_m[1],
                v.position_m.z - start.position_m[2]}, yaw);
            vertices.push_back({{p[0],p[1],p[2]}, {v.texture_uv[0],v.texture_uv[1]},
                {0,0}, v.texture_layer, v.polygon_flags, v.has_lightmap, 0});
        }
        const auto map_count = static_cast<std::uint32_t>(vertices.size());
        const auto triangles = BuildCollisionTriangles(vertices, map_count);
        auto pixels = scene.texture_rgba8;
        auto layers = scene.texture_layer_count;
        std::uint32_t fixture_count=0, frame_vertices=0, frame_count=0;
        std::size_t fixture_actors=0;
        std::vector<KnightDraw> knights;
        const auto initial_flames=world.flames.size();
        if (!LoadOwnedSceneProps(root,map,start,yaw,world.lights,triangles,&vertices,&pixels,&layers,
                &fixture_count,&fixture_actors,&world.glows,&world.flames,&knights)) return 6;
        if(knights.size()!=6)return 6;
        for(std::size_t i=initial_flames;i<world.flames.size();++i){
            float nearest=1000;for(std::size_t j=map_count;j<knights.front().first;++j){
                const auto& v=vertices[j];const auto& p=world.flames[i].position;
                nearest=std::min(nearest,std::sqrt((v.position[0]-p[0])*(v.position[0]-p[0])+(v.position[1]-p[1])*(v.position[1]-p[1])+(v.position[2]-p[2])*(v.position[2]-p[2])));
            }
            if(nearest>.10F)return 35;
        }
        std::cout<<"CANDLE_ANCHORS=PASS count="<<world.flames.size()-initial_flames<<" maximum_mesh_gap_m=0.10\n";
        const auto aim_triangles=BuildPropAimTriangles(vertices,map_count,knights);
        for(const auto& knight:knights){std::array<float,3> low{1000,1000,1000},high{-1000,-1000,-1000};
            for(unsigned i=0;i<knight.count;++i)for(unsigned a=0;a<3;++a){const auto p=vertices[knight.first+i].position[a];low[a]=std::min(low[a],p);high[a]=std::max(high[a],p);}
            std::array<float,3> origin{(low[0]+high[0])*.5F-3,low[1]+(high[1]-low[1])*.6F,(low[2]+high[2])*.5F};
            const float hit=BasicRayDistance(aim_triangles,origin,{1,0,0});
            if(hit>3.8F){std::cerr<<"knight_aim_miss distance="<<hit<<" origin_y="<<origin[1]<<'\n';return 36;}}
        std::cout<<"KNIGHT_AIM=PASS count="<<knights.size()<<'\n';
        std::vector<DoorDraw> doors;
        if (!LoadIntroDoors(root,map,start,yaw,world.lights,scene.texture_layer_width,
                scene.texture_layer_height,&vertices,&pixels,&layers,&doors)) return 7;
        std::vector<CharacterDraw> actors;
        QuestSpellTargets targets;
        if (!LoadOwnedCharacters(root,map,start,yaw,layers,vertices,map_count,world.lights,
                &vertices,&pixels,&layers,&frame_vertices,&frame_count,&actors,&targets)) return 8;
        if (actors.size()!=22+world.children.prototypes.size() || doors.size()!=5) return 9;
        const auto main_actor=std::ranges::find_if(actors,[](const auto& a){return a.actor_reference==1672;});
        if(main_actor==actors.end()) return 9;
        const auto harry=std::ranges::find_if(actors,[](const auto& a){return a.actor_reference==603;});
        if(harry==actors.end())return 18;
        std::set<std::uint32_t> masked_layers;
        for(unsigned i=0;i<harry->vertex_count;++i){
            const auto& v=vertices[harry->first_vertex+i];
            if(v.polygon_flags&2U)masked_layers.insert(v.texture_layer);
        }
        if(masked_layers.empty())return 32;
        for(auto layer:masked_layers){
            unsigned transparent=0,opaque=0;
            for(unsigned i=0;i<256*256;++i){
                auto alpha=pixels[(std::size_t(layer)*256*256+i)*4+3];
                transparent+=alpha==0;opaque+=alpha==255;
            }
            if(!transparent||!opaque)return 32;
        }
        std::cout<<"GLASSES_MASK=PASS transparent_background=YES opaque_frames=YES\n";
        for(const auto& clip:{"breathe","run","look2","scratch","adjustglasses"})
            if(!harry->clips.contains(clip))return 18;
        std::set<std::uint32_t> child_skin_hashes;
        for(const auto& model:actors)if(model.child_template){
            std::set<std::uint32_t> model_layers;
            for(std::uint32_t i=0;i<model.vertex_count;++i)model_layers.insert(vertices[model.first_vertex+i].texture_layer);
            std::uint32_t hash=2166136261U;
            for(auto layer:model_layers){
                const auto begin=static_cast<std::size_t>(layer)*256*256*4;
                for(std::size_t j=0;j<256*256*4;++j)hash=(hash^pixels.at(begin+j))*16777619U;
            }
            child_skin_hashes.insert(hash);
        }
        if(child_skin_hashes.size()!=10)return 19;
        std::cout<<"harry_clips=PASS distinct_child_skins="<<child_skin_hashes.size()<<"\n";
        for (const auto& actor : actors) {
        if(actor.child_template && actor.clips.size()!=3) return 9;
        for (const auto& [name,clip] : actor.clips) {
            float min_foot=1000, max_foot=-1000;
            for (std::uint32_t frame=0;frame<clip.frame_count;++frame) {
                float foot=1000;
                for (std::uint32_t i=0;i<actor.vertex_count;++i)
                    foot=std::min(foot,vertices[clip.first_vertex+frame*actor.vertex_count+i].position[1]);
                min_foot=std::min(min_foot,foot); max_foot=std::max(max_foot,foot);
                if(name!="run"&&name!="walk"&&std::abs(foot-actor.base_origin[1])>0.0001F) return 10;
                if((name=="run"||name=="walk")&&std::abs(foot-actor.base_origin[1])>0.6F)return 10;
            }
            std::cout << "clip=" << name << " duration=" << clip.duration
                      << " feet=" << min_foot << ":" << max_foot << "\n";
        }
        }
        const auto& actor=*main_actor;
        std::size_t route_samples=0, misses=0;
        auto position = actor.base_origin;
        for (const auto& track : world.intro_cutscene.tracks) {
            if (track.actor_reference!=1672) continue;
            position=track.position;
            for (const auto& command : track.commands) {
                if (!AsciiFold(command).starts_with("moveto ")) continue;
                const auto name=AsciiFold(command.substr(7));
                const auto mark=std::ranges::find_if(world.intro_cutscene.locations,
                    [&](const auto& loc) { return AsciiFold(loc.alias)==name; });
                if (mark==world.intro_cutscene.locations.end()) return 11;
                for(int step=0;step<=100;++step) {
                    const auto p=AddVector(position,ScaleVector(SubtractVector(mark->position,position),step/100.0F));
                    float floor=0;
                    ++route_samples;
                    if (!FindPropGroundBelow(triangles,p,&floor)) ++misses;
                }
                position=mark->position;
            }
        }
        if(misses!=0) { std::cerr<<"route_ground_misses="<<misses<<"\n"; return 12; }
        for(const auto& source : world.cutscene_dialogue) {
            std::uint32_t hash=2166136261U;
            for(auto byte:source.encoded_bytes) hash=(hash^byte)*16777619U;
            std::ostringstream name;
            name<<source.object_name<<'.'<<std::hex<<std::setw(8)<<std::setfill('0')<<hash<<".s16";
            const auto pcm=std::filesystem::path(argv[2])/name.str();
            if(!std::filesystem::exists(pcm) || std::filesystem::file_size(pcm)<960) return 13;
        }
        auto expected = std::uint64_t(map_count)+fixture_count;
        for(const auto& door:doors)expected+=door.vertex_count;
        for (const auto& model:actors)
            for (const auto& [name,clip] : model.clips) expected += std::uint64_t(model.vertex_count)*clip.frame_count;
        if(vertices.size()!=expected || pixels.size()!=std::uint64_t(scene.texture_layer_width)*scene.texture_layer_height*layers*4) return 14;
        QuestFrontEnd frontend;
        if(!LoadFrontAssets(root,&frontend.assets))return 17;
        constexpr std::array<const char*,4> appended_effects{"pickup_star","vase_breaking","cauldron_flip","save_game"};
        constexpr std::size_t dialogue_prefix_count=79;
        constexpr std::size_t expected_gameplay_audio=dialogue_prefix_count+appended_effects.size();
        if(frontend.assets.gameplay_audio.size()!=expected_gameplay_audio||frontend.assets.bump_speech.size()!=10||
           frontend.assets.frog_pickup.samples.empty()||frontend.assets.card_pickup.status!=hpvr::wand::Hp1ProfileStatus::ok){
            std::cerr<<"gameplay_audio_count="<<frontend.assets.gameplay_audio.size()<<" expected="<<expected_gameplay_audio
                <<" bump_speech_count="<<frontend.assets.bump_speech.size()<<" expected_bump_speech=10"
                <<" frog_samples="<<frontend.assets.frog_pickup.samples.size()
                <<" card_pickup_ok="<<(frontend.assets.card_pickup.status==hpvr::wand::Hp1ProfileStatus::ok)<<'\n';
            return 25;
        }
        for(std::size_t i=0;i<appended_effects.size();++i){
            const auto& name=frontend.assets.gameplay_audio[dialogue_prefix_count+i].object_name;
            if(AsciiFold(name)!=appended_effects[i]){
                std::cerr<<"gameplay_effect_index="<<dialogue_prefix_count+i<<" actual="<<name<<" expected="<<appended_effects[i]<<'\n';
                return 25;
            }
        }
        for(const auto& source:frontend.assets.gameplay_audio){
            const auto cache=std::filesystem::path(argv[2])/AudioCacheName(source);
            if(!std::filesystem::exists(cache)){std::cerr<<"missing_gameplay_audio_cache="<<cache.string()<<'\n';return 25;}
        }
        std::cout<<"GAMEPLAY_AUDIO=PASS count="<<expected_gameplay_audio
            <<" appended=pickup_star,vase_breaking,cauldron_flip,save_game\n";
        std::vector<BeanDraw> beans;
        if(!LoadOwnedBeans(root,map,start,yaw,vertices,pixels,layers,beans,&triangles))return 28;
        for(const auto& bean:beans){
            if(bean.first!=expected)return 29;expected+=std::uint64_t(bean.count)*bean.frames;
            for(unsigned i=0;i<bean.count;++i)if(vertices[bean.first+i].texture_layer>=layers||!vertices[bean.first+i].packed_light)return 29;
        }
        std::cout<<"OWNED_BEANS=PASS count="<<beans.size()<<'\n';
        const auto frog=std::ranges::find_if(beans,[](const auto& b){return b.kind==1;});
        if(frog==beans.end()||frog->frames<60)return 37;
        float rest_foot=1000,highest_foot=-1000;
        for(unsigned frame=0;frame<frog->frames;++frame){
            float foot=1000;
            for(unsigned v=0;v<frog->count;++v)foot=std::min(foot,vertices[frog->first+frame*frog->count+v].position[1]);
            if(frame==0)rest_foot=foot;highest_foot=std::max(highest_foot,foot);
        }
        float frog_floor=0;
        if(std::abs(rest_foot)>.001F||highest_foot<.10F||
           !FindPropGroundBelow(triangles,frog->position,&frog_floor)||std::abs(frog_floor-frog->position[1])>.001F)return 37;
        std::cout<<"FROG_ANIMATION=PASS frames="<<frog->frames<<" ground_error="<<rest_foot<<" hop_height="<<highest_foot<<'\n';
        if(std::ranges::count_if(actors,[](const auto& a){return IsClassroomActor(a.actor_reference);})!=7||fixture_actors!=60)return 38;
        const auto ghost=std::ranges::find_if(actors,[](const auto& a){return a.actor_reference==3148;});
        if(ghost==actors.end()||ghost->enabled||!ghost->clips.contains("breathe")||
           !(vertices[ghost->clips.at("breathe").first_vertex].polygon_flags&0x20000000U))return 38;
        std::cout<<"CLASSROOM=PASS pupils=7 blackboards=2 ghost=FLOAT_TRANSLUCENT\n";
        std::map<std::string,FrontDrawRange> frontend_draws;std::uint32_t ui_vertices=0;
        if(!AppendFrontGeometry(frontend,vertices,pixels,layers,frontend_draws,ui_vertices))return 17;
        // Runtime appends one animated fire layer after the UI/scene arrays.
        if(vertices.size()!=expected+ui_vertices || pixels.size()!=std::uint64_t(256)*256*layers*4 || layers+1>256) return 17;
        for(const auto& [key,range]:frontend_draws)
            if(range.first+range.count>vertices.size()||range.count%6)return 17;
        std::cout<<"frontend_screens="<<frontend_draws.size()<<" frontend_vertices="<<ui_vertices<<" texture_layers="<<layers<<"\n";
        for(const auto& spawner:world.children.spawners)
            for(const auto& choice:spawner.choices)
                if(std::ranges::none_of(actors,[&](const auto& a){return a.actor_reference==choice.model_reference;})) return 15;
        auto& children=world.children;
        EmitChildEvent(children,doors,triangles,"MOMDis1");
        const auto advance=[&](float seconds) {
            for(int n=0;n<static_cast<int>(std::ceil(seconds*72));++n)
                AdvanceChildren(children,doors,triangles,1.0F/72.0F);
        };
        advance(0.3F);
        EmitChildEvent(children,doors,triangles,"MOMDis2");
        EmitChildEvent(children,doors,triangles,"MOMDis3");
        advance(1.2F);
        EmitChildEvent(children,doors,triangles,"MOMDis4");
        advance(12.0F);
        EmitChildEvent(children,doors,triangles,"KidPath2");
        advance(100.0F);
        std::cout<<"children_spawned="<<children.spawned<<" destroyed="<<children.destroyed
                 <<" ground_misses="<<children.ground_misses<<" pending="<<children.pending.size()<<"\n";
        if(children.spawned!=18 || children.destroyed!=children.spawned || children.ground_misses ||
            !children.pending.empty() || std::ranges::any_of(doors,[](const auto& d){return d.opening;})) return 16;
        std::cout<<"INTRO_CPU_CHECK=PASS actors="<<actors.size()<<" doors="<<doors.size()<<" clips="<<actor.clips.size()
                 <<" dialogue=5 route_ground_samples="<<route_samples<<" vertices="<<vertices.size()<<"\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 20; }
}
