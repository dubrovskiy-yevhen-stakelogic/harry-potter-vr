#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"

#include <iostream>
#include <stdexcept>

using namespace hpvr::quest;

namespace {
bool CardUsesOwnedSkin(const PreparedGeometry& geometry,const std::filesystem::path& root,int actor,int texture_reference){
    const auto pickup=std::ranges::find_if(geometry.beans,[&](const auto& b){return b.actor_reference==actor&&b.kind==4;});
    if(pickup==geometry.beans.end())return false;
    const auto layer=geometry.vertices.at(pickup->first).texture_layer;
    const bool masked=(geometry.vertices.at(pickup->first).polygon_flags&2U)!=0;
    const auto texture=hpvr::wand::load_hp1_p8_texture(root/"System/HProps.u",texture_reference,masked);
    if(texture.status!=hpvr::wand::Hp1ProfileStatus::ok||texture.mips.empty())return false;
    const auto& mip=texture.mips.front();
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){
        const auto source=(std::size_t(y*mip.height/256)*mip.width+x*mip.width/256)*4;
        const auto target=(std::size_t(layer)*256*256+y*256+x)*4;
        if(!std::equal(texture.rgba8.begin()+source,texture.rgba8.begin()+source+4,geometry.textures.begin()+target))return false;
    }
    return true;
}
struct SceneSignals {
    bool completed = false;
    std::vector<std::string> triggers;
    std::string travel;
};

// Advance only authored cue dependencies. Rendering, dialogue duration and
// character motion are deliberately outside this CPU progression test.
SceneSignals RunSceneSignals(const IntroCutscene& scene, MapEventGraph* graph = nullptr) {
    SceneSignals result;
    std::vector<std::size_t> cursor(scene.tracks.size());
    std::set<std::string> cues;
    for (unsigned pass = 0; pass < 512; ++pass) {
        bool progress = false, finished = true;
        for (std::size_t i = 0; i < scene.tracks.size(); ++i) {
            const auto& commands = scene.tracks[i].commands;
            if (cursor[i] == commands.size()) continue;
            finished = false;
            std::istringstream command{commands[cursor[i]]};
            std::string op, argument;
            command >> op;
            std::getline(command >> std::ws, argument);
            op = AsciiFold(op);
            argument = AsciiFold(argument);
            if (op == "waitfor" && !cues.contains(argument)) continue;
            ++cursor[i];
            progress = true;
            if (op == "cue") cues.insert(argument);
            if (op == "trigger") {
                result.triggers.push_back(argument);
                if (graph && !graph->Dispatch(argument)) return result;
            }
            if (op == "changelevel") result.travel = argument;
        }
        if (finished) { result.completed = true; return result; }
        if (!progress) return result;
    }
    return result;
}

bool HasEffect(const std::vector<MapEventEffect>& effects, MapEventKind kind, std::int32_t ref) {
    return std::ranges::any_of(effects, [&](const auto& effect) {
        return effect.kind == kind && effect.actor_reference == ref;
    });
}

template<class Check>
void TestOwnedRoute(const hpvr::wand::Hp1ActorVisualCensus& census,
                    const CharmsRuntime& charms, Check&& check) {
    ChallengeRuntime route;
    check(LoadChallengeMetadata(census, {}, 0, route, 23), "all twenty-three owned cutscenes and route nodes load");
    check(std::ranges::count_if(route.scenes, [](const auto& entry) { return entry.second.play_on_load; }) == 1 &&
          route.scenes.at(1526).play_on_load && !route.graph.Find(1526)->trigger_enabled &&
          !route.graph.Find(1526)->touch_enabled,
          "the unique level-load scene is CutScene5, separate from trigger-only and proximity scenes");
    for (const auto& [ref, scene] : route.scenes) {
        (void)ref;
        check(RunSceneSignals(scene).completed, "authored cutscene has no deadlocked or missing cue dependency");
    }
    const auto spatial = [&](std::int32_t ref) -> const ChallengeSpatial& {
        const auto found = std::ranges::find_if(route.spatial, [&](const auto& zone) { return zone.reference == ref; });
        if (found == route.spatial.end()) throw std::runtime_error("authored route contact missing from spatial metadata");
        return *found;
    };
    check(spatial(1844).spell_name == "spellaloho" && spatial(1902).spell_name == "spellaloho" &&
          spatial(1810).spell_name == "spellaloho", "three classroom doors retain their Alohomora spell identity");
    check(spatial(1814).proximity_class == "tut3flitwick" && spatial(1544).proximity_class.empty(),
          "Flitwick-only door contact stays separate from the nearby player contact");
    check(std::ranges::count_if(route.spatial, [](const auto& zone) {
        return zone.proximity_class == "wingardiumblock";
    }) == 9, "all nine block-only plate contacts reach the spatial route");

    auto& graph = route.graph;
    check(!graph.Find(1844)->active && !graph.Find(1844)->consumed,
          "lesson door starts untargetable and unconsumed");
    check(!graph.Spell(1844) && graph.DrainEffects().empty(), "Alohomora lesson cannot be skipped by shooting the inactive door");
    check(graph.Touch(1986), "walking to Hermione starts her introduction");
    auto effects = graph.DrainEffects();
    check(HasEffect(effects, MapEventKind::cutscene_start, 1986), "Hermione intro uses its authored proximity scene");
    const auto intro = RunSceneSignals(route.scenes.at(1986), &graph);
    check(intro.completed && intro.triggers == std::vector<std::string>{"alohomorastart"},
          "Hermione introduction broadcasts the authored lesson-start tag");
    effects = graph.DrainEffects();
    check(HasEffect(effects, MapEventKind::actor_trigger, 1200) && !graph.Find(1200)->signaled,
          "lesson start is an actor_trigger callback, not an automatic completion");

    const auto finish_lesson = [&](unsigned index, std::int32_t expected_ref) {
        const auto& metadata = charms.lessons[index];
        check(metadata.actor_reference == expected_ref, "actor callback resolves the correct teacher lesson");
        charms::LessonSession session;
        session.Begin();
        for (const auto pass_mark : metadata.pass_marks) (void)session.Submit(metadata, pass_mark);
        check(session.finished && session.learned && session.points == 50, "four authored passing rounds complete a fifty-point lesson");
        check(graph.Signal(metadata.actor_reference), "lesson callback signals its authored completion event");
    };
    finish_lesson(0, 1200);
    effects = graph.DrainEffects();
    check(HasEffect(effects, MapEventKind::cutscene_start, 2162), "Alohomora completion runs Hermione's post-lesson scene");
    check(graph.Find(1844)->active && !graph.Find(1844)->consumed &&
          !HasEffect(effects, MapEventKind::cutscene_start, 2213),
          "lesson completion arms the first door without casting or opening it");
    check(RunSceneSignals(route.scenes.at(2162), &graph).completed, "Hermione post-lesson cue sequence completes");
    check(graph.Spell(1844) && graph.Advance(2), "first Alohomora cast drives the delayed padlock dispatcher");
    effects = graph.DrainEffects();
    check(graph.Find(1844)->consumed && HasEffect(effects, MapEventKind::cutscene_start, 2213),
          "only the actual Alohomora cast consumes the door and queues the corridor scene");
    check(std::ranges::any_of(effects, [&](const auto& effect) {
        return effect.kind == MapEventKind::mover_trigger && graph.Find(effect.actor_reference)->tag == "ald1";
    }), "first Alohomora cast moves the actual authored door");
    check(RunSceneSignals(route.scenes.at(2213), &graph).completed, "corridor scene can release Harry after the door opens");
    check(graph.Spell(1902) && graph.Spell(1810) && graph.Advance(4), "remaining two authored Alohomora doors advance the corridor");
    effects = graph.DrainEffects();
    check(HasEffect(effects, MapEventKind::cutscene_start, 2387), "last corridor door starts the students entering Charms");
    check(RunSceneSignals(route.scenes.at(2387), &graph).completed, "class-entry actors can finish all their shared cues");
    check(graph.Touch(2917), "walking into Charms starts Flitwick's introduction");
    effects = graph.DrainEffects();
    check(HasEffect(effects, MapEventKind::cutscene_start, 2917), "Flitwick intro is not accidentally consumed by a shared CutScriptII tag");
    const auto wing_intro = RunSceneSignals(route.scenes.at(2917), &graph);
    check(wing_intro.completed && wing_intro.triggers == std::vector<std::string>{"charmsstart"},
          "Flitwick introduction broadcasts the Wingardium lesson tag");
    effects = graph.DrainEffects();
    check(HasEffect(effects, MapEventKind::actor_trigger, 3237) && !graph.Find(3237)->signaled,
          "Wingardium lesson reaches its actor_trigger callback before completion");
    finish_lesson(1, 3237);
    effects = graph.DrainEffects();
    check(HasEffect(effects, MapEventKind::cutscene_start, 2771), "Wingardium completion selects the classroom exit cutscene");
    check(RunSceneSignals(route.scenes.at(2771), &graph).completed, "classroom-exit cue sequence releases both Harry and camera");

    check(!graph.Signal(1869), "unmoved tutorial pedestal cannot send an early completion");
    check(graph.Touch(1530), "matching block contact activates the tutorial plate");
    effects = graph.DrainEffects();
    check(HasEffect(effects, MapEventKind::mover_trigger, 1869) &&
          HasEffect(effects, MapEventKind::mover_trigger, 1896) &&
          !HasEffect(effects, MapEventKind::cutscene_start, 1878),
          "tutorial plate moves pedestal and exit door before the teacher continuation");
    check(graph.Signal(1869), "pedestal movement completion emits WingardiumCutScene1");
    effects = graph.DrainEffects();
    check(HasEffect(effects, MapEventKind::cutscene_start, 1878), "teacher's block feedback waits for actual pedestal completion");
    check(RunSceneSignals(route.scenes.at(1878), &graph).completed, "teacher's block feedback has no cue deadlock");
    check(graph.Find(1795)->remaining == 1 && graph.Find(3248)->remaining == 2,
          "duplicate DoorCounter tags retain separate one-plate and two-plate thresholds");
    check(graph.Touch(1543) && graph.Advance(2), "first puzzle block activates its sparse delayed dispatcher");
    effects = graph.DrainEffects();
    check(graph.Find(1795)->consumed && graph.Find(3248)->remaining == 1 &&
          HasEffect(effects, MapEventKind::mover_trigger, 2092) && !HasEffect(effects, MapEventKind::mover_trigger, 2996),
          "first plate opens only DoorBarTwo, without demanding all four plates");
    check(graph.Touch(1543) && graph.Advance(2), "standing on one plate again is harmless");
    effects = graph.DrainEffects();
    check(graph.Find(3248)->remaining == 1 && !HasEffect(effects, MapEventKind::mover_trigger, 2996),
          "the same consumed plate cannot count as a second block");
    check(graph.Touch(1528) && graph.Advance(2), "second distinct block completes the second door counter");
    effects = graph.DrainEffects();
    check(graph.Find(3248)->consumed && HasEffect(effects, MapEventKind::mover_trigger, 2996),
          "second plate opens DoorBarOne through dispatcher slot two");
    check(graph.Touch(1541) && graph.Advance(2), "separate WingOut plate dispatches its authored exit steps");
    effects = graph.DrainEffects();
    check(std::ranges::any_of(effects, [&](const auto& effect) {
        return effect.kind == MapEventKind::mover_trigger && graph.Find(effect.actor_reference)->tag == "wingoutpad";
    }), "WingOut contact is not lost from the nine block-only plates");

    constexpr std::array<std::int32_t, 6> stars{1897, 2020, 1941, 2098, 2866, 2770};
    for (const auto count : {0U, 2U, 3U, 5U, 6U}) {
        MapEventGraph ending;
        check(ending.Load(census), "fresh ending graph loads");
        for (unsigned i = 0; i < count; ++i) check(ending.CollectStar(stars[i]), "authored star can be collected once");
        (void)ending.DrainEffects();
        check(ending.Touch(2019), "StarsTrigger evaluates the collected-star total");
        effects = ending.DrainEffects();
        const auto ending_ref = count >= 6 ? 1988 : count >= 3 ? 2469 : 2249;
        check(HasEffect(effects, MapEventKind::cutscene_start, ending_ref), "zero/three/six-star thresholds select the original teacher result");
        check(RunSceneSignals(route.scenes.at(ending_ref), &ending).completed, "teacher result can finish without an unresolved cue");
        check(ending.Touch(1813), "walking to the final door broadcasts ExitWing");
        effects = ending.DrainEffects();
        check(HasEffect(effects, MapEventKind::mover_trigger, 1853) &&
              HasEffect(effects, MapEventKind::mover_trigger, 1802) &&
              HasEffect(effects, MapEventKind::cutscene_start, 1411), "ExitWing starts both door leaves and the original travel scene");
        const auto exit = RunSceneSignals(route.scenes.at(1411), &ending);
        check(exit.completed && exit.travel == "lev_tut3b.unr", "chapter exit retains the original Lev_Tut3b destination");
    }
    std::cout << "OWNED_CHARMS_ROUTE=PASS lessons=2 plates=9 endings=5 scenes=23\n";
}

template<class Check>
void TestOwnedFlipendoTriggers(const std::filesystem::path& root, Check&& check) {
    const auto census = hpvr::wand::inspect_hp1_actor_visuals(root / "Maps/Lev_Tut1b.unr");
    MapEventGraph original;
    check(original.Load(census), "original Flipendo challenge graph still loads");
    unsigned direct_casts = 0, disabled = 0;
    for (const auto& node : original.nodes()) {
        if (node.kind != MapEventNodeKind::spell_trigger) continue;
        auto cast = original;
        const bool result = cast.Spell(node.actor_reference);
        check(result == node.active, "legacy spell contact remains separate from external event arming");
        if (!node.active) { ++disabled; continue; }
        ++direct_casts;
        check(cast.Find(node.actor_reference)->consumed == node.once,
              "legacy direct spell still consumes each one-shot contact");
        if (node.state == "othertriggerturnson" || node.state == "othertriggerturnsoff" ||
            node.state == "othertriggertoggles") {
            auto arm = original;
            check(arm.Dispatch(node.tag) && !arm.Find(node.actor_reference)->consumed,
                  "legacy externally armed spell contact does not consume itself");
        }
    }
    check(direct_casts > 0, "legacy test actually exercises targetable Flipendo contacts");
    std::cout << "OWNED_FLIPENDO_TRIGGER_COMPATIBILITY=PASS casts=" << direct_casts << " disabled=" << disabled << '\n';
}
}  // namespace

int main(int argc,char** argv){try{
    unsigned checks=0;
    const auto check=[&](bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);};
    {
        MirrorSurface mirror;mirror.mover_reference=12;
        std::vector<MirrorSurface> mirrors{mirror};DoorDraw blocker;blocker.actor_reference=12;
        check(IsMirrorBlocker(blocker,mirrors)&&MirrorClosed(blocker),"closed mirror blocker is collision-only in both render passes");
        blocker.opening=true;blocker.motion.moving=true;
        check(IsMirrorBlocker(blocker,mirrors)&&!MirrorClosed(blocker),"opening mirror blocker cannot flash as a visible brick wall");
        blocker.motion.moving=false;blocker.motion.current=1;
        check(IsMirrorBlocker(blocker,mirrors)&&!MirrorClosed(blocker),"open passage keeps the veil but not the brick blocker");
        blocker.actor_reference=13;
        check(!IsMirrorBlocker(blocker,mirrors),"ordinary door rendering is unchanged");
    }
    ChallengeRuntime layout_graph;CharmsRuntime layout_charms;
    for(unsigned i=0;i<23;++i)layout_graph.scenes.emplace(i,IntroCutscene{});
    for(unsigned i=0;i<7;++i)layout_charms.blocks.emplace(i,CharmsBlock{});
    for(unsigned i=0;i<9;++i)layout_charms.valid_plates.insert(i);
    for(auto& lesson:layout_charms.lessons)lesson.valid=true;
    std::vector<CharacterDraw> roster(12);
    const std::array<int,4> students{1117,1196,1225,2237};
    for(unsigned i=0;i<students.size();++i){roster[i].actor_reference=students[i];roster[i].clips.emplace("breathe",CharacterClip{});}
    check(ValidateCharmsSceneLayout(layout_graph,layout_charms,58,1,roster),"runtime accepts the restored twelve-character cast");
    auto incomplete=roster;incomplete.resize(8);
    check(!ValidateCharmsSceneLayout(layout_graph,layout_charms,58,1,incomplete),"legacy eight-character roster cannot satisfy the restored map contract");
    incomplete=roster;incomplete[0].clips.clear();
    check(!ValidateCharmsSceneLayout(layout_graph,layout_charms,58,1,incomplete),"missing student animation is rejected");
    check(!ValidateCharmsSceneLayout(layout_graph,layout_charms,57,1,roster),"missing mover is still rejected");
    layout_charms.lessons[0].valid=false;
    check(!ValidateCharmsSceneLayout(layout_graph,layout_charms,58,1,roster),"invalid lesson is still rejected");
    std::vector<CharacterDraw> checkpoint_actors(2);
    checkpoint_actors[0].actor_reference=10;checkpoint_actors[0].class_name="HarryPotter.Hermione";
    checkpoint_actors[1].actor_reference=11;checkpoint_actors[1].class_name="HarryPotter.Gen_Fem_1";
    std::istringstream old_characters("1 10 1 .2 1 2 3");
    check(ReadCheckpointCharacters(old_characters,checkpoint_actors,true),"old checkpoint accepts newly restored students");
    check(checkpoint_actors[0].cutscene_offset==std::array<float,3>{1,2,3},"existing character state is preserved");
    for(const auto* invalid:{"1 11 1 0 0 0 0","2 10 1 0 0 0 0 10 1 0 0 0 0","1 99 1 0 0 0 0","3"}){
        std::istringstream input(invalid);const auto before=checkpoint_actors[0].cutscene_offset;
        check(!ReadCheckpointCharacters(input,checkpoint_actors,true)&&before==checkpoint_actors[0].cutscene_offset,
              "invalid character bank cannot partially mutate runtime state");
    }
    CharmsRuntime original;
    for(auto& lesson:original.lessons){lesson.valid=true;lesson.pass_marks={.5F,.65F,.8F,.95F};lesson.house_points={5,10,15,20};}
    original.blocks.emplace(10,CharmsBlock{{-1,0,-1},{1,1.28F,1}});
    original.blocks.emplace(20,CharmsBlock{{3,0,-1},{5,1.28F,1}});
    original.valid_plates={101,102};
    const auto fresh=SaveCharmsState(original);
    auto restored=original;
    check(RestoreCharmsState(restored,fresh)&&SaveCharmsState(restored)==fresh,"fresh save round-trip");
    auto state=original;state.blocks.at(10).plate=101;state.blocks.at(10).offset={2,-.2F,3};
    state.lesson.Begin();(void)state.lesson.Submit(state.lessons[0],1);state.active_lesson=0;
    state.held_block=20;state.blocks.at(20).motion.was_held=true;
    const auto saved=SaveCharmsState(state);
    check(RestoreCharmsState(restored,saved)&&SaveCharmsState(restored)==saved&&!restored.held_block&&
          !restored.blocks.at(20).motion.was_held,"partial lesson and placed block restore without held physics");
    const auto assert_rejected=[&](const CharmsRuntime& invalid,const char* message){
        check(!RestoreCharmsState(restored,SaveCharmsState(invalid))&&SaveCharmsState(restored)==saved,message);
    };
    auto invalid=state;invalid.blocks.at(10).plate=999;assert_rejected(invalid,"unknown plate rejected atomically");
    invalid=state;invalid.blocks.at(20).plate=101;assert_rejected(invalid,"two blocks cannot own one plate");
    invalid=state;invalid.blocks.at(10).plate=-1;assert_rejected(invalid,"negative plate reference rejected");
    invalid=state;invalid.blocks.at(10).offset[0]=200;assert_rejected(invalid,"out-of-range block offset rejected");
    invalid=state;invalid.blocks.at(10).offset[0]=std::numeric_limits<float>::quiet_NaN();assert_rejected(invalid,"non-finite block offset rejected");
    invalid=state;invalid.lesson.points=50;assert_rejected(invalid,"impossible lesson award rejected");
    invalid=state;invalid.lesson.finished=true;assert_rejected(invalid,"contradictory completion flags rejected");
    invalid=state;invalid.active_lesson=-1;assert_rejected(invalid,"unfinished lesson requires its active teacher");
    check(!RestoreCharmsState(restored,saved+" trailing")&&SaveCharmsState(restored)==saved,"trailing checkpoint data rejected");
    check(!RestoreCharmsState(restored,saved.substr(0,saved.size()/2))&&SaveCharmsState(restored)==saved,"truncated checkpoint rejected");
    std::vector<CollisionTriangle> triangles(2);
    AppendCharmsCollision(restored,triangles);
    check(triangles.size()==26&&restored.blocks.at(10).collision_first==2&&restored.blocks.at(20).collision_first==14&&
          restored.blocks.at(10).collision_count==12,"collision rebuild produces exact distinct self-exclusion ranges");
    check(RestoreCharmsState(restored,"")&&SaveCharmsState(restored)==fresh,"fresh-map reset clears block and lesson state");
    if(argc>=2){
        for(const float depth:{.32F,2.0F}){
            std::vector<CollisionTriangle> trench;
            charms_block::AppendBox(trench,{{-5,-3,-2},{-1,0,2}});
            charms_block::AppendBox(trench,{{-1,-3,-2},{1,-depth,2}});
            charms_block::AppendBox(trench,{{1,-3,-2},{5,0,2}});
            std::array<float,3> feet{-2,0,0};
            for(unsigned tick=0;tick<100;++tick)feet=MoveChallengeGnome(trench,feet,{.04F,0,0});
            std::cout<<"GNOME_TRACK depth="<<depth<<" x="<<feet[0]<<" y="<<feet[1]<<'\n';
            check(depth<.35F?(feet[0]>1.9F&&std::abs(feet[1])<.002F):feet[0]<-1,
                "gnome crosses a shallow track but never walks over a deep pit");
        }
        const std::filesystem::path root(argv[1]);
        const auto census=hpvr::wand::inspect_hp1_actor_visuals(root/"Maps/Lev_Tut3.unr");
        check(census.status==hpvr::wand::Hp1ProfileStatus::ok,"owned charms map loads");
        for(const auto& actor:census.actors)if(actor.tag=="Mirror1"||actor.tag=="Mirror3"){
            std::int32_t brush=0;
            for(const auto& p:actor.serialized_properties)if(AsciiFold(p.name)=="brush")brush=p.object_reference;
            const auto model=hpvr::wand::build_hp1_textured_bsp_scene(root,root/"Maps/Lev_Tut3.unr",kMetersPerUnrealUnit,4096,brush);
            for(const auto& name:model.texture_layer_names)std::cout<<"MIRROR_MATERIAL actor="<<actor.actor_reference<<" name="<<name<<'\n';
        }
        if(argc>=3){
            cache::Reader reader(argv[2],3,cache::ComputeSourceFingerprint(root,3));
            const auto geometry=ReadPreparedGeometry(reader);reader.Finish();
            for(const auto ref:{1526,2771}){
                auto doors=geometry.doors;
                const auto wait=OpenCharmsCutsceneDoors(ref,doors);
                check(wait>0,"cutscene opens its existing door before actors move");
                unsigned count=0;
                for(auto& door:doors){
                    if(!door.cutscene_hold)continue;
                    ++count;for(unsigned frame=0;frame<200;++frame)(void)movers::Advance(door.motion,.05F);
                    check(door.tag==CharmsCutsceneDoorTag(ref)&&door.motion.current+1==door.motion.count,"cutscene opens both correct leaves fully");
                }
                check(count==2,"unrelated doors remain untouched by cutscene entry");
            }
            {
                hpvr_hp1_player_start_report block_start{};
                check(hpvr_hp1_load_player_start_utf8((root/"Maps/Lev_Tut3.unr").string().c_str(),kMetersPerUnrealUnit,0,&block_start)==0,"block test player start loads");
                const float block_yaw=block_start.rotation_units[1]*kTau/65536;
                CharmsRuntime probe;LoadCharmsMetadata(root,census,block_start,block_yaw,geometry.challenge_props,probe,&geometry.vertices);
                auto collision=geometry.collision;const auto base=collision.size();
                for(const auto& door:geometry.doors){
                    std::vector<GpuVertex> verts(geometry.vertices.begin()+door.first_vertex,geometry.vertices.begin()+door.first_vertex+door.vertex_count);
                    auto extra=BuildCollisionTriangles(verts,verts.size());collision.insert(collision.end(),extra.begin(),extra.end());
                }
                AppendCharmsCollision(probe,collision);
                check(probe.blocks.size()==7,"all seven authored levitation blocks are tested");
                for(auto& [ref,b]:probe.blocks){
                    const auto box=CharmsBlockBounds(b);
                    const auto center=ScaleVector(AddVector(box.minimum,box.maximum),.5F);
                    const auto result=charms_block::Advance(b.motion,box,AddVector(center,{0,1,0}),true,.05F,15,collision,base,b.collision_first,b.collision_count,b.collision_yaw);
                    std::cout<<"BLOCK_LIFT ref="<<ref<<" lift="<<result.offset[1]<<" blocked="<<result.blocked<<'\n';
                    check(std::abs(result.offset[1]-.2F)<.001F,"levitation block lifts clear of its real floor and surrounding ledges");
                }
                auto sky_scene=hpvr::wand::build_hp1_textured_bsp_scene(root,root/"Maps/Lev_Tut3.unr",kMetersPerUnrealUnit,kMaximumTriangles);
                check(RestoreBroomSky(root/"Maps/Lev_Tut3.unr",hpvr::wand::load_hp1_bsp_topology(root/"Maps/Lev_Tut3.unr"),census,sky_scene),"original balcony sky restores six textured faces");
                const auto sky_count=std::count_if(geometry.vertices.begin(),geometry.vertices.begin()+geometry.map_vertices,[](const auto& v){return (v.polygon_flags&kBroomSkyFlag)!=0;});
                std::cout<<"PREPARED_SKY count="<<sky_count<<'\n';
                check(sky_count==36,"prepared balcony contains sky geometry");
                check(std::ranges::none_of(geometry.vertices,[](const auto& v){return (v.polygon_flags&128U)&&!(v.polygon_flags&1U);}),"brick fake-backdrops are hidden in prepared scene");
            }
            const auto desk=std::ranges::find_if(geometry.challenge_props,[](const auto& p){return p.name=="harrypotter.transteachersdesk";});
            check(desk!=geometry.challenge_props.end()&&desk->count>0,"original Flitwick desk is included in prepared scene");
            for(const auto& actor:geometry.characters)if(AsciiFold(actor.class_name)=="tut3.tut3flitwick")
                check(std::abs(actor.collision_min_y-desk->maximum[1])<.002F,"Flitwick feet meet the visible tabletop");
            for(const auto& prop:geometry.challenge_props)if(prop.reference==2588||prop.reference==2863)
                check(std::abs(prop.minimum[1]-desk->maximum[1])<.002F,"both lesson book stacks rest on the visible tabletop");
            for(const auto ref:{1798,1852}){
                const auto gate=std::ranges::find_if(geometry.doors,[&](const auto& d){return d.actor_reference==ref;});
                check(gate!=geometry.doors.end(),"authored iron gate present");
                bool masked=false;
                for(std::size_t i=gate->first_vertex;i<gate->first_vertex+gate->vertex_count;++i){
                    const auto& vertex=geometry.vertices[i];if(!(vertex.polygon_flags&2U))continue;
                    const auto first=std::size_t(vertex.texture_layer)*256*256*4;
                    for(std::size_t p=first+3;p<first+256*256*4;p+=4)if(geometry.textures[p]==0){masked=true;break;}
                    if(masked)break;
                }
                check(masked,"iron gate uses masked polygons and a real transparent palette mask");
            }
            for(const auto& door:geometry.doors)if(door.grid){
                auto collision=geometry.collision;const auto static_count=collision.size();
                std::size_t skip=collision.size(),skip_count=0;
                for(const auto& other:geometry.doors){
                    std::vector<GpuVertex> verts(geometry.vertices.begin()+other.first_vertex,geometry.vertices.begin()+other.first_vertex+other.vertex_count);
                    auto extra=BuildCollisionTriangles(verts,verts.size());
                    if(other.actor_reference==door.actor_reference){skip=collision.size();skip_count=extra.size();}
                    collision.insert(collision.end(),extra.begin(),extra.end());
                }
                grid_motion::Bounds box{{1e9F,1e9F,1e9F},{-1e9F,-1e9F,-1e9F}};
                const float yaw=door.placement.player_yaw;
                for(std::size_t i=door.first_vertex;i<door.first_vertex+door.vertex_count;++i){
                    const auto& v=geometry.vertices[i].position;const auto point=grid_motion::Yaw({v[0],v[1],v[2]},-yaw);
                    for(unsigned axis=0;axis<3;++axis){box.minimum[axis]=std::min(box.minimum[axis],point[axis]);box.maximum[axis]=std::max(box.maximum[axis],point[axis]);}
                }
                for(const unsigned a:{0U,2U}){box.minimum[a]+=.001F;box.maximum[a]-=.001F;}
                for(const auto direction:std::array<std::array<float,3>,4>{{{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}}}){
                    auto current=box;grid_motion::State motion;std::array<float,3> total{};
                    for(unsigned frame=0;frame<100;++frame){
                        const float distance=std::hypot(total[0],total[2]);
                        if(distance>=door.grid_increment-.0001F)break;
                        const auto requested=grid_motion::Scale(direction,std::min(.05F,door.grid_increment-distance));
                        const auto step=grid_motion::Advance(motion,current,requested,.02F,collision,static_count,skip,skip_count,0,yaw);
                        current=grid_motion::Translate(current,step.offset);total=grid_motion::Add(total,step.offset);
                        if(step.blocked)break;
                    }
                    check(std::abs(total[1])<.002F&&motion.grounded,"grid lower trim remains seated in its recessed track");
                    if(direction[0]>0)
                        check(std::abs(total[0]-door.grid_increment)<.002F,"both gnome shields travel the full authored increment along the actual track");
                    else if(direction[0]<0)
                        check(std::abs(total[0])<.33F,"rear niche wall stops a backwards push");
                    else check(std::abs(total[2])<.002F,"track side walls stop cross-track pushes without raising the column");
                    std::cout<<"GRID_TRACK ref="<<door.actor_reference<<" direction="<<direction[0]<<','<<direction[2]
                        <<" offset="<<total[0]<<','<<total[1]<<','<<total[2]<<'\n';
                }
            }
            check(CardUsesOwnedSkin(geometry,root,2918,1817),"Tilly Toke card uses the original Tilly skin, not Dumbledore");
            check(CardUsesOwnedSkin(geometry,root,chest::RewardActor(2846,0),1787),"chest card uses the original Burdock Muldoon skin");
            check(std::ranges::any_of(geometry.beans,[](const auto& b){return b.actor_reference==1568&&b.kind==1&&b.frames>1;}),
                  "mirror recess contains original animated chocolate frog");
            if(argc>=4){
                cache::Reader broom_reader(argv[3],2,cache::ComputeSourceFingerprint(root,2));
                const auto broom_geometry=ReadPreparedGeometry(broom_reader);broom_reader.Finish();
                check(CardUsesOwnedSkin(broom_geometry,root,265,1784),"broom secret card uses the original Merlin skin");
            }
            const auto world_collision=BuildCollisionTriangles(geometry.vertices,geometry.map_vertices);
            auto mirrors=FindMirrorSurfaces(geometry.vertices,geometry.map_vertices);
            BindMirrorMovers(mirrors,geometry.doors);
            check(!mirrors.empty(),"owned mirror polygons are marked for planar reflection");
            for(const auto& mirror:mirrors){
                check(!mirror.ranges.empty()&&std::abs(mirror.normal[1])<.02F,"mirror has a vertical plane and finite draw ranges");
                std::cout<<"PREPARED_MIRROR center="<<mirror.center[0]<<','<<mirror.center[1]<<','<<mirror.center[2]<<'\n';
                check(mirror.mover_reference==1848||mirror.mover_reference==2995,"mirror binds to original opening mover");
                const auto door=std::ranges::find_if(geometry.doors,[&](const auto& d){return d.actor_reference==mirror.mover_reference;});
                auto opened=*door;
                check(IsMirrorBlocker(*door,mirrors)&&door->vertex_count>0,
                      "owned mirror brush remains present for collision but is excluded from both render passes");
                float nearest=1e9F,farthest=-1e9F;
                for(unsigned v=door->first_vertex;v<door->first_vertex+door->vertex_count;++v){
                    const auto& p=geometry.vertices.at(v).position;
                    const auto world=MoverPoint(*door,{p[0],p[1],p[2]});
                    const float side=DotVector(mirror.normal,SubtractVector(world,mirror.center));
                    nearest=std::min(nearest,side);farthest=std::max(farthest,side);
                }
                std::cout<<"MIRROR_BLOCKER_EXCLUDED actor="<<door->actor_reference<<" vertices="<<door->vertex_count
                    <<" plane_min="<<nearest<<" plane_max="<<farthest<<'\n';
                check(MirrorClosed(opened),"fresh mirror reflects before casting");
                opened.opening=true;check(!MirrorClosed(opened),"Alohomora removes reflection immediately");
            }
            hpvr_hp1_player_start_report start{};
            check(hpvr_hp1_load_player_start_utf8((root/"Maps/Lev_Tut3.unr").string().c_str(),kMetersPerUnrealUnit,0,&start)==0,"prepared diagnostic player start loads");
            const float yaw=start.rotation_units[1]*kTau/65536;
            for(const auto& prop:geometry.challenge_props)if(chest::IsChest(prop.name)){
                float floor=0;const auto center=ScaleVector(AddVector(prop.minimum,prop.maximum),.5F);
                check(FindPropGroundBelow(world_collision,center,&floor)&&prop.minimum[1]>floor+.008F,"chest bottom clears the BSP floor");
                for(auto bean:geometry.beans)if(bean.source_actor==prop.reference){
                    PrepareChallengeBeanEmission(bean,prop,geometry.collision);
                    float support=0;const auto probe=AddVector(bean.position,std::array<float,3>{0,.2F,0});
                    check(FindPropGroundBelow(world_collision,probe,&support)&&bean.position[1]<support+.5F,"chest bean settles near floor, not at emission height");
                }
            }
            zones::Query zone_query;
            unsigned knight_beans=0;
            for(auto bean:geometry.beans)if(bean.source_actor){
                const auto source=BeanSpawnerSource(bean,geometry.challenge_props);
                if(source.name!="hprops.knight")continue;
                ++knight_beans;
                const auto anchor=ScaleVector(AddVector(source.minimum,source.maximum),.5F);
                auto approach=anchor;bool reachable=false;
                for(const auto& direction:std::array<std::array<float,3>,4>{{{0,0,2},{0,0,-2},{2,0,0},{-2,0,0}}}){
                    const auto candidate=AddVector(anchor,direction);float floor=0;
                    if(FindPropGroundBelow(world_collision,candidate,&floor)&&ChallengeBeanSweepFraction(geometry.collision,source,candidate,anchor)>.99F){approach=candidate;reachable=true;break;}
                }
                check(reachable,"knight has an unobstructed player approach");
                PrepareChallengeBeanEmission(bean,source,geometry.collision,&approach);
                float floor=0;
                std::cout<<"KNIGHT_REWARD ref="<<bean.source_actor<<" knight="<<source.reference<<" landing="<<bean.position[0]<<','<<bean.position[1]<<','<<bean.position[2]<<" approach="<<approach[0]<<','<<approach[1]<<','<<approach[2]<<'\n';
                check(FindPropGroundBelow(world_collision,AddVector(bean.position,std::array<float,3>{0,.2F,0}),&floor)&&bean.position[1]<floor+.5F,"knight reward drops clear of its source and down to floor");
            }
            check(knight_beans>=4,"owned knight reward spawners exercise emission grounding");
            check(zone_query.Load(hpvr::wand::load_hp1_bsp_topology(root/"Maps/Lev_Tut3.unr"),census),"owned lethal-zone query loads");
            for(const auto& actor:census.actors)if(actor.actor_reference==1475||actor.actor_reference==2882){
                const auto& u=actor.location_unreal;
                check(!zone_query.IsLethal({u.x,u.y,u.z+40}),"bookcase doorway is not an authored death zone");
            }
            for(const auto& prop:geometry.challenge_props)if(prop.name=="hprops.knight"){
                const auto actor=std::ranges::find_if(census.actors,[&](const auto& a){return a.actor_reference==prop.reference;});
                float floor=0;
                check(actor!=census.actors.end()&&FindPropGroundBelow(world_collision,ActorLocalPosition(*actor,start,yaw),&floor),"knight has an authored BSP support");
                std::cout<<"PREPARED_KNIGHT ref="<<prop.reference<<" foot="<<prop.minimum[1]<<" bsp_support="<<floor<<'\n';
            }
            for(const auto& character:geometry.characters)if(character.actor_reference==1117||character.actor_reference==1196||character.actor_reference==1225){
                const auto& clip=character.clips.at("breathe");
                check(clip.frame_count>1,"student authored idle has multiple frames");
                unsigned moving=0;
                for(std::size_t i=0;i<character.vertex_count;++i)for(unsigned axis=0;axis<3;++axis)
                    if(std::abs(geometry.vertices[clip.first_vertex+i].position[axis]-geometry.vertices[clip.first_vertex+character.vertex_count*(clip.frame_count/2)+i].position[axis])>.001F){++moving;break;}
                check(moving>0,"student rest clip contains visible conversational motion");
            }
            check(geometry.fallback_materials==0,"prepared map has no missing BSP materials");
        }
        std::vector<ChallengeProp> props;
        for(const auto& actor:census.actors)if(AsciiFold(actor.qualified_class_name)=="hprops.wingardiumblock"){
            ChallengeProp prop;prop.reference=actor.actor_reference;prop.name="hprops.wingardiumblock";
            prop.minimum={-1,0,-1};prop.maximum={1,1.28F,1};props.push_back(prop);
        }
        CharmsRuntime owned;
        check(LoadCharmsMetadata(root,census,{},0,props,owned),"owned charms runtime metadata loads");
        std::cout<<"OWNED_CHARMS_SCENE blocks="<<owned.blocks.size()<<" plates="<<owned.valid_plates.size()<<'\n';
        check(owned.blocks.size()==7&&owned.valid_plates.size()==9,
              "owned map has seven blocks and nine authored block-only plates");
        check(std::ranges::all_of(owned.blocks,[](const auto& entry){return entry.second.maximum_hold_seconds==15;}),
              "owned actor overrides retain fifteen-second levitation limit");
        check(RestoreCharmsState(owned,SaveCharmsState(owned)),"owned fresh map checkpoint round-trip");
        TestOwnedRoute(census,owned,check);
        {
            MapEventGraph secret;check(secret.Load(census),"secret route graph loads");
            check(secret.Dispatch("Bob")&&secret.Spell(2578)&&secret.Advance(2),"Alohomora opens the secret picture after lesson arming");
            auto effects=secret.DrainEffects();
            check(secret.Find(1392)->remaining==2&&!HasEffect(effects,MapEventKind::mover_trigger,1944),
                "picture alone does not skip the two original knight conditions");
            for(const auto knight:{1858,1334})for(unsigned hit=0;hit<4;++hit){
                check(secret.Spell(knight)&&secret.Advance(2),"secret knight reward chain advances");
            }
            check(secret.Advance(5),"secret reveal retains its authored four-second delay");
            effects=secret.DrainEffects();
            check(secret.Find(1392)->consumed&&HasEffect(effects,MapEventKind::mover_trigger,1944),
                "picture and both knights reveal the wall hiding two chests");
            for(const auto ref:{2775,2776})check(std::ranges::any_of(census.actors,[&](const auto& a){
                return a.actor_reference==ref&&AsciiFold(a.qualified_class_name)=="hprops.bronzechest"&&!a.hidden;
            }),"original secret chest is behind the moving wall, not an invented spawn");
        }
        for(const auto knight:{false,true}){
            const auto package=root/(knight?"System/HProps.u":"System/HPModels.u");
            const int reference=knight?152:694;
            LoadedStaticMesh mesh;
            check(LoadStaticMesh(package.string(),reference,0,&mesh),"owned prop bind mesh loads");
            const auto original_vertices=mesh.vertices;
            const float old_floor=mesh.report.bounds_min_m[1];
            check(PoseStaticMesh(package,reference,knight?"idle":"start",mesh),"owned prop rest animation loads");
            unsigned changed=0;
            for(std::size_t i=0;i<mesh.vertices.size();++i){
                const auto& v=mesh.vertices[i];
                for(unsigned axis=0;axis<3;++axis)
                    if(std::abs(v.position_m[axis]-original_vertices[i].position_m[axis])>.001F){++changed;break;}
            }
            std::set<std::array<std::array<float,3>,3>> faces;
            std::map<unsigned,unsigned> flag_counts;
            unsigned duplicate_faces=0,degenerate_faces=0;float visible_floor=1e9F,volume=0;
            for(std::size_t i=0;i+2<mesh.vertices.size();i+=3){
                ++flag_counts[mesh.vertices[i].polygon_flags];
                std::array<std::array<float,3>,3> face{};
                for(unsigned j=0;j<3;++j)for(unsigned axis=0;axis<3;++axis)face[j][axis]=mesh.vertices[i+j].position_m[axis];
                const auto n=CrossVector(SubtractVector(face[1],face[0]),SubtractVector(face[2],face[0]));
                volume+=DotVector(face[0],CrossVector(face[1],face[2]))/6.0F;
                if(DotVector(n,n)<1e-12F){++degenerate_faces;continue;}
                for(const auto& p:face)visible_floor=std::min(visible_floor,p[1]);
                std::sort(face.begin(),face.end());if(!faces.insert(face).second)++duplicate_faces;
            }
            std::cout<<"OWNED_PROP_REST kind="<<(knight?"knight":"chest")<<" corrected_vertices="<<changed
                <<" floor_before="<<old_floor<<" floor_after="<<mesh.report.bounds_min_m[1]<<" duplicate_faces="<<duplicate_faces
                <<" degenerate_faces="<<degenerate_faces<<" visible_floor="<<visible_floor<<" signed_volume="<<volume<<'\n';
            for(const auto& [flags,count]:flag_counts)std::cout<<"PROP_FLAGS kind="<<(knight?"knight":"chest")<<" flags="<<flags<<" faces="<<count<<'\n';
            check(flag_counts.size()==1&&flag_counts.contains(0),"owned chest and knight materials require single-sided rendering");
            check(std::isfinite(mesh.report.bounds_min_m[1]),"authored visible rest pose has finite grounding bounds");
        }
        const auto mirror=hpvr::wand::load_hp1_p8_texture(root/"System/HPParticle.u",671,false);
        check(mirror.status==hpvr::wand::Hp1ProfileStatus::ok&&!mirror.mips.empty(),"owned MirrorBlur wet texture resolves its source image");
        MapEventGraph knights;
        check(knights.Load(census),"knight reward event graph loads");
        auto old_census=census;
        std::erase_if(old_census.actors,[](const auto& actor){const auto name=AsciiFold(actor.qualified_class_name);
            return actor.event.empty()&&(name=="hpbase.spawnthingy"||name=="hprops.padlock");});
        MapEventGraph old_graph;
        check(old_graph.Load(old_census)&&old_graph.Spell(1858)&&old_graph.Advance(2),"legacy map graph can reach a saved state");
        check(knights.Restore(old_graph.Serialize()),"new receiver nodes accept exact legacy map fingerprint");
        check(knights.Load(census),"reset graph after migration test");
        std::set<std::int32_t> emitted;
        for(unsigned cast=0;cast<4;++cast){
            check(knights.Spell(1858)&&knights.Advance(2),"each original knight hit advances its reward sequence");
            for(const auto& effect:knights.DrainEffects()){
                const auto* node=knights.Find(effect.actor_reference);
                if(effect.kind==MapEventKind::actor_trigger&&node&&node->class_name=="hpbase.spawnthingy")
                    emitted.insert(effect.actor_reference);
            }
        }
        check(emitted.contains(2548)&&emitted.contains(2574)&&emitted.contains(2560),
              "knight dispatchers emit the authored bean spawners");
        check(!knights.Spell(1858),"the final knight reward disables further farming");
        TestOwnedFlipendoTriggers(root,check);
        std::vector<std::uint8_t> particle_pixels;
        std::uint32_t particle_layers=0,lock_layer=99;
        std::vector<AmbientParticleEmitter> locks;
        check(LoadLockParticles(root,census,{},0,256,256,particle_pixels,particle_layers,lock_layer,locks),
              "owned padlock class and original sparkle texture load");
        check(locks.size()==3&&particle_layers==1&&lock_layer==0,"three padlocks share one texture layer");
        for(const auto& lock:locks){
            const auto actor=std::ranges::find_if(census.actors,[&](const auto& item){return item.actor_reference==lock.actor_reference;});
            check(actor!=census.actors.end()&&lock.position==ActorLocalPosition(*actor,{},0),"lock particles retain authored height");
            std::array<AmbientParticle,ambient::kMaximumAmbientParticlesPerEmitter> before{},after{};
            check(BuildAmbientParticles({lock},1,lock.position,before.data(),before.size())>0,"lock emits blue particles");
            check(BuildAmbientParticles({lock},1.2F,lock.position,after.data(),after.size())>0&&before[0].position!=after[0].position,
                  "lock particles animate over time");
            auto hidden=lock;hidden.enabled=false;
            check(BuildAmbientParticles({hidden},1,lock.position,before.data(),before.size())==0,"removed lock has no emitter");
            ChallengeProp prop;prop.name="hprops.padlock";prop.first=2;prop.count=3;
            check(ChallengePropDrawRange(prop,false,0).second==3&&ChallengePropDrawRange(prop,true,0).second==0,"unlocked padlock disappears");
            check(knights.Find(lock.actor_reference)!=nullptr,"padlock receives authored trigger events");
        }
    }
    std::cout<<"CHARMS_SCENE_TESTS=PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
