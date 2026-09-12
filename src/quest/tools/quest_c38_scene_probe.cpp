#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
using namespace hpvr::quest;
void Check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
int main(int argc,char** argv){try{
    IntroCutscene lesson;lesson.object_name="cutscene54";lesson.playing=true;lesson.camera_active=true;lesson.harry_released=true;
    CutsceneTrack camera;camera.camera=true;camera.moving=true;camera.delay_seconds=8;camera.next_command=2;
    camera.commands[4]="Waitfor CutEnd";camera.commands[5]="RELEASE";camera.commands[6]="Trigger HelpWithJumping";
    lesson.tracks.push_back(camera);
    Check(!FinishJumpCameraTour(lesson)&&lesson.tracks[0].moving,"camera not interrupted before final dialogue cue");
    lesson.cues.insert("cutend");
    Check(FinishJumpCameraTour(lesson)&&lesson.tracks[0].next_command==5&&!lesson.tracks[0].moving&&
        lesson.tracks[0].delay_seconds==0&&ReleaseCutsceneControlIfReady(lesson),"jump ending releases control without camera tail");
    Check(lesson.tracks[0].commands[6]=="Trigger HelpWithJumping","jump tutorial trigger preserved");
    lesson.object_name="cutscene60";lesson.tracks[0]=camera;
    Check(!FinishJumpCameraTour(lesson),"other scene camera tours unaffected");
    CardPickupEffect card{5,0,2,1};
    Check(card.active()&&card.scale()==1,"card starts at source size");
    card.Advance(1.4F);Check(card.scale()>2.9F&&card.angle()>1,"card grows and spins");
    card.Advance(.3F);Check(card.active()&&card.scale()<2,"card contracts before disappearance");
    card.Advance(1);Check(!card.active()&&card.scale()==0,"card finishes once");
    DoorDraw mover;mover.pivot={1,0,2};mover.open_offset={0,2,0};mover.phase=.5F;
    Check(MoverPoint(mover,{1,0,2})==std::array<float,3>{1,1,2},"mover translation");
    ProgressSave p;p.map_id=1;RememberChallengeEvent(p,5);RememberChallengeEvent(p,3);RememberChallengeEvent(p,5);
    Check(p.activated_events==std::vector<std::int32_t>{3,5},"event identities stable");
    if(argc==1){std::cout<<"C38_SCENE_POLICY=PASS\n";return 0;}
    const std::filesystem::path root=argv[1],map=root/"Maps/Lev_Tut1b.unr";
    hpvr::wand::Hp1PackageReadScope read_cache;
    hpvr_hp1_player_start_report start{};
    Check(hpvr_hp1_load_player_start_utf8(map.string().c_str(),kMetersPerUnrealUnit,0,&start)==0,"player start");
    const float yaw=start.rotation_units[1]*kTau/65536;
    const auto census=hpvr::wand::inspect_hp1_actor_visuals(map);ChallengeRuntime challenge;
    Check(LoadChallengeMetadata(census,start,yaw,challenge),"challenge metadata");
    const auto topology=hpvr::wand::load_hp1_bsp_topology(map);
    Check(challenge.zones.Load(topology,census)&&challenge.zones.lethal_count()==2,"two authored lethal BSP zones");
    WorldMetadata world;Check(LoadWorldMetadata(root,map,start,yaw,&world),"world metadata");
    auto scene=hpvr::wand::build_hp1_textured_bsp_scene(root,map,kMetersPerUnrealUnit,kMaximumTriangles);
    Check(scene.status==hpvr::wand::Hp1ProfileStatus::ok,"BSP decode");
    Check(scene.omitted_triangle_count==0,"no truncated level geometry");
    std::vector<GpuVertex> vertices;
    for(const auto& v:scene.vertices){const auto p=RotateYaw({v.position_m.x-start.position_m[0],v.position_m.y-start.position_m[1],v.position_m.z-start.position_m[2]},yaw);
        vertices.push_back({{p[0],p[1],p[2]},{v.texture_uv[0],v.texture_uv[1]},{v.lightmap_uv[0],v.lightmap_uv[1]},v.texture_layer,v.polygon_flags,v.has_lightmap,0xffffff});}
    const auto map_count=static_cast<std::uint32_t>(vertices.size());
    auto collision=BuildCollisionTriangles(vertices,map_count);auto pixels=std::move(scene.texture_rgba8);auto layers=scene.texture_layer_count;
    Check(LoadChallengeProps(root,map,start,yaw,world.lights,vertices,pixels,layers,collision,challenge.props,world.flames,world.glows),"props");
    const auto fixture_end=vertices.size();
    std::vector<DoorDraw> doors;Check(LoadIntroDoors(root,map,start,yaw,world.lights,256,256,&vertices,&pixels,&layers,&doors),"movers");
    Check(doors.size()==67,"all 67 mover brushes");
    for(const auto& door:doors)for(std::size_t i=door.first_vertex;i<door.first_vertex+door.vertex_count;++i){
        const auto layer=vertices[i].texture_layer;bool transparent=false;
        for(std::size_t pixel=0;pixel<256*256;++pixel)if(pixels[(std::size_t(layer)*256*256+pixel)*4+3]==0){transparent=true;break;}
        if(transparent)Check((vertices[i].polygon_flags&2U)!=0,"mover transparency is not covered by an opaque face");
    }
    Check(std::ranges::count_if(challenge.props,[](const auto& prop){return prop.name=="harrypotter.savepoint";})==3,"all three owned save books visible");
    Check(std::ranges::any_of(challenge.props,[](const auto& prop){return prop.reference==3118&&prop.spell_target;}),"clay vase opposite first save book accepts Flipendo");
    std::uint64_t expected_vertices=fixture_end;
    for(const auto& d:doors){
        Check(d.vertex_count>0&&d.actor_reference>0,"valid brush draw");
        Check(d.first_vertex==expected_vertices,"mover ranges are contiguous");
        expected_vertices+=d.vertex_count;
    }
    std::vector<CharacterDraw> characters;QuestSpellTargets targets;std::uint32_t frame_vertices=0,frames=0;
    Check(LoadOwnedCharacters(root,map,start,yaw,layers,vertices,map_count,world.lights,&vertices,&pixels,&layers,&frame_vertices,&frames,&characters,&targets),"characters");
    Check(std::ranges::count_if(characters,[](const auto& a){return a.player;})==1,"one physical Harry player");
    const auto player=std::ranges::find(characters,1102,&CharacterDraw::actor_reference);
    Check(player!=characters.end()&&player->player,"authored Harry1102 owns first-person player");
    const auto barrel=std::ranges::find_if(characters,[](const auto& a){return AsciiFold(a.class_name)=="tut1.flipbarrel";});
    Check(barrel!=characters.end(),"owned Flipendo barrel present");
    for(std::size_t i=barrel->first_vertex;i<barrel->first_vertex+barrel->vertex_count;++i)
        for(unsigned axis=0;axis<3;++axis)Check(vertices[i].position[axis]>=barrel->visual_minimum[axis]&&
            vertices[i].position[axis]<=barrel->visual_maximum[axis],"marker bounds cover rendered barrel, not narrowed walking capsule");
    const auto sparkle=hpvr::wand::load_hp1_p8_texture(root/"system/HPParticle.u",3);
    Check(sparkle.status==hpvr::wand::Hp1ProfileStatus::ok&&sparkle.object_name=="Sparkle_3"&&
        !sparkle.mips.empty()&&sparkle.mips.front().width==32&&sparkle.mips.front().height==32,"owned original target particle");
    Check(BuildTargetMarkerAtlas(sparkle.rgba8,32,32).size()==256*256*4,"owned target atlas prepared");
    const auto marker=hpvr::wand::load_hp1_spell_profile(root/"system/HPBase.u",root/"Maps/Lev_Tut1.unr","FlipPattern","spellFlip");
    Check(marker.status==hpvr::wand::Hp1ProfileStatus::ok,"owned target emission pattern");
    std::vector<std::array<float,2>> marker_points;
    for(const auto& point:marker.template_points)marker_points.push_back({point.x,point.y});
    Check(BuildTargetMarkerBatch(marker_points).valid(),"owned batched target effect");
    for(const std::int32_t ref:{2370,2400}){
        const auto cut=std::ranges::find(characters,ref,&CharacterDraw::actor_reference);
        Check(cut!=characters.end()&&!cut->player,"cutscene Harry clone is independent of player visibility");
    }
    std::uint64_t pose_vertices=0,quirrell_vertices=0;
    std::map<std::uint32_t,std::uint64_t> clip_ranges;
    for(const auto& a:characters){
        Check(a.clips.contains(a.active_clip),"active character animation exists");
        std::uint64_t count=0;
        for(const auto& [name,clip]:a.clips){
            const auto size=std::uint64_t(a.vertex_count)*clip.frame_count;
            Check(clip.duration>0&&clip_ranges.emplace(clip.first_vertex,size).second,"unique nonempty animation range");
            count+=size;
        }
        pose_vertices+=count;if(AsciiFold(a.class_name)=="tut1.tut1quirrell")quirrell_vertices+=count;
        std::cout<<"C38_CAST_STORAGE actor="<<a.object_name<<" clips="<<a.clips.size()<<" vertices="<<count<<'\n';
    }
    for(const auto& [first,size]:clip_ranges){
        Check(first==expected_vertices,"animation ranges contain no discarded or untracked poses");
        expected_vertices+=size;
    }
    Check(expected_vertices==vertices.size(),"all character vertices admitted by runtime layout");
    std::vector<BeanDraw> pickups;Check(LoadOwnedBeans(root,map,start,yaw,vertices,pixels,layers,pickups,&collision),"beans");
    Check(LoadChallengeStars(root,map,start,yaw,vertices,pixels,layers,pickups),"eight stars");
    Check(std::ranges::count_if(pickups,[](const auto& p){return p.source_actor==3118;})==1,"clay vase drops its authored bean");
    Check(std::ranges::count_if(pickups,[](const auto& p){return p.source_actor==4658;})==3,"bronze cauldron ejects three authored beans");
    for(const auto& pickup:pickups){
        Check(pickup.first==expected_vertices,"pickup ranges are contiguous");
        expected_vertices+=std::uint64_t(pickup.count)*pickup.frames;
    }
    Check(expected_vertices==vertices.size(),"all pickup vertices admitted by runtime layout");
    QuestFrontEnd front;Check(LoadFrontAssets(root,&front.assets,1),"frontend");
    std::map<std::string,FrontDrawRange> ranges;std::uint32_t front_count=0;
    Check(AppendFrontGeometry(front,vertices,pixels,layers,ranges,front_count),"frontend geometry");
    expected_vertices+=front_count;
    Check(expected_vertices==vertices.size(),"all frontend vertices admitted by runtime layout");
    Check(layers<=kMaximumCombinedTextureLayers,"texture budget");
    for(unsigned stars=0;stars<=8;++stars){
        Check(ranges.contains("stars_"+std::to_string(stars)),"all pause star counters cached");
        Check(ranges.contains("report_stars_"+std::to_string(stars)),"all report star counters cached");
    }
    front.screen=FrontScreen::Pause;front.paused=FrontScreen::Game;front.progress.map_id=1;front.progress.quest_stage=64;
    Check(ranges.contains(front.DrawKey()),"late challenge pause uses cached objective key");
    const auto reads=read_cache.stats();
    std::cout<<"PACKAGE_READ_CACHE reads="<<reads.reads<<" hits="<<reads.hits<<" retained_bytes="<<reads.retained_bytes<<'\n';
    std::cout<<"C38_SCENE_STORAGE vertex_bytes="<<vertices.size()*sizeof(GpuVertex)
        <<" pose_vertices="<<pose_vertices<<" quirrell_vertices="<<quirrell_vertices<<'\n';
    std::cout<<"C38_OWNED_SCENE=PASS props="<<challenge.props.size()<<" movers="<<doors.size()<<" characters="<<characters.size()
        <<" pickups="<<pickups.size()<<" vertices="<<vertices.size()<<" layers="<<layers<<" lightmaps="<<scene.decoded_lightmap_count
        <<" collision="<<collision.size()<<" flames="<<world.flames.size()<<" scenes="<<challenge.scenes.size()<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"C38_SCENE=FAIL "<<e.what()<<'\n';return 1;}}
