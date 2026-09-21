// Installer-only CPU preparation. Input game files are always read-only.
#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <iomanip>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
using namespace hpvr::quest;
namespace fs=std::filesystem;
bool Within(const fs::path& child,const fs::path& parent){
    auto c=child.begin();
    for(auto p=parent.begin();p!=parent.end();++p,++c)
        if(c==child.end()||AsciiFold(c->generic_string())!=AsciiFold(p->generic_string()))return false;
    return true;
}
void RejectRedirects(const fs::path& path){
    fs::path current;
    for(const auto& part:fs::absolute(path).lexically_normal()){
        current/=part;
#ifdef _WIN32
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("preparation paths must not contain junctions or symbolic links");
#else
        if(fs::is_symlink(fs::symlink_status(current)))
            throw std::runtime_error("preparation paths must not contain symbolic links");
#endif
    }
}
int Run(const std::vector<fs::path>& args){
    if(args.size()!=6&&args.size()!=7){
        std::cerr<<"usage: hpvr_quest_prepare_assets <owned-root> --output <private-output-outside-root> --map <0|1|2|3|4> [--verify]\n";
        return 2;
    }
    try{
        const auto map_argument=args[5].string();
        const auto map=std::ranges::find_if(kQuestMaps,[&](const auto& item){return std::to_string(item.id)==map_argument;});
        if(args[2]!="--output"||args[4]!="--map"||
            map==kQuestMaps.end()||(args.size()==7&&args[6]!="--verify"))
            throw std::runtime_error("invalid preparation arguments");
        RejectRedirects(args[1]);RejectRedirects(args[3]);
        const auto root=fs::canonical(args[1]);
        const auto output=fs::weakly_canonical(fs::absolute(args[3]));
        if(!fs::is_directory(root)||output==output.root_path()||Within(output,root)||Within(root,output))
            throw std::runtime_error("output must be a dedicated private folder outside the input game tree");
        const unsigned map_id=map->id;
        const auto path=output/("map-"+std::to_string(map_id)+".hpvc");
        RejectRedirects(path);
        const bool verify=args.size()==7;
        if(!verify&&fs::exists(path))throw std::runtime_error("prepared file already exists; use a new output folder");
        const auto began=std::chrono::steady_clock::now();
        const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();};
        hpvr::wand::Hp1PackageReadScope package_reads;
        const auto fingerprint=cache::ComputeSourceFingerprint(root,map_id);
        const auto fingerprint_seconds=elapsed();
        if(!verify){
            const auto package=root/map->package_path;
            hpvr_hp1_player_start_report start{};
            if(hpvr_hp1_load_player_start_utf8(package.string().c_str(),kMetersPerUnrealUnit,0,&start)!=HPVR_HP1_PROFILE_OK||
                start.status!=HPVR_HP1_PROFILE_OK||start.abi_version!=HPVR_HP1_PLAYER_START_ABI_VERSION||
                !start.location_serialized||start.rotation_units[0]!=0||start.rotation_units[2]!=0)
                throw std::runtime_error("owned player start is invalid");
            const float yaw=static_cast<float>(start.rotation_units[1])*kTau/65536.0F;
            WorldMetadata world;
            if(!LoadWorldMetadata(root,package,start,yaw,&world))throw std::runtime_error("owned scene metadata could not be loaded");
            PreparedGeometry geometry;
            if(!PrepareGeometryFromOwnedData(root,map_id,start,yaw,world,geometry,[&](const char* stage){
                std::cout<<"PREPARE_STAGE="<<stage<<" map="<<map_id<<" seconds="<<elapsed()<<std::endl;
            }))throw std::runtime_error("owned scene preparation failed");
            std::string layout_error;
            if(!ValidatePreparedGeometry(geometry,&layout_error))
                throw std::runtime_error("prepared scene layout is invalid: "+layout_error);
            if(cache::ComputeSourceFingerprint(root,map_id)!=fingerprint)
                throw std::runtime_error("input game files changed during preparation");
            const auto prepared_at=elapsed();
            cache::Writer writer(path,map_id,fingerprint);
            WritePreparedGeometry(writer,geometry);writer.Finish();
            std::cout<<"PREPARE_WRITE=PASS map="<<map_id<<" vertices="<<geometry.vertices.size()
                <<" layers="<<geometry.texture_layers<<" prepare_seconds="<<prepared_at
                <<" write_seconds="<<elapsed()-prepared_at<<std::endl;
        }
        // Free preparation arrays before the independent streamed read-back.
        const auto read_at=elapsed();
        cache::Reader reader(path,map_id,fingerprint);
        auto candidate=ReadPreparedGeometry(reader,verify?prepared_codec::kMaxFrontendVertexReserve:0);reader.Finish();
        if(!ValidatePreparedGeometry(candidate))throw std::runtime_error("prepared read-back layout is invalid");
        const auto read_seconds=elapsed()-read_at;
        if(verify){
            if(!RestorePickupCardIds(hpvr::wand::inspect_hp1_actor_visuals(root/map->package_path),candidate.beans))
                throw std::runtime_error("wizard card collection IDs could not be restored");
            std::uint32_t card_mask=0;
            for(const auto& pickup:candidate.beans)if(pickup.kind==4)card_mask|=campaign::CardMask(pickup.card_id);
            if(map_id==4&&card_mask!=(campaign::CardMask(24)|campaign::CardMask(8)|campaign::CardMask(18)))
                throw std::runtime_error("return map wizard card collection differs from owned rewards");
            std::cout<<"PREPARED_CARD_IDS=PASS mask="<<card_mask<<'\n';
            std::cout<<"PREPARED_BEAN_TOTAL="<<std::ranges::count_if(candidate.beans,[](const auto& b){return b.kind==0;})<<'\n';
            if(map_id==kHogwartsReturnMapId){
                const auto census=hpvr::wand::inspect_hp1_actor_visuals(root/map->package_path);
                hpvr_hp1_player_start_report start{};
                if(hpvr_hp1_load_player_start_utf8((root/map->package_path).string().c_str(),kMetersPerUnrealUnit,0,&start)!=0)
                    throw std::runtime_error("return map player start is invalid");
                const float yaw=start.rotation_units[1]*kTau/65536;
                ChallengeRuntime challenge;ReturnMetadata metadata;
                if(!LoadChallengeMetadata(census,start,yaw,challenge,16,true)||!LoadReturnMetadata(census,start,yaw,metadata)||
                   !ResolveReturnRoutes(census,hpvr::wand::inspect_hp1_navigation(root/map->package_path),start,yaw,metadata))
                    throw std::runtime_error("return map event graph or flight routes are incomplete");
                ReturnRuntime battle;
                if(!LoadReturnOwls(census,hpvr::wand::inspect_hp1_navigation(root/map->package_path),start,yaw,battle.owls)||battle.owls.size()!=2)
                    throw std::runtime_error("return map owl delivery routes are missing");
                auto delivery_cast=candidate.characters;
                for(const auto& owl:battle.owls)if(owl.departure.empty()||!LaunchReturnOwl(battle,delivery_cast,owl.tag))
                    throw std::runtime_error("owl departure path or trigger is missing");
                unsigned deliveries=0;
                for(unsigned frame=0;frame<6000;++frame){
                    AdvanceReturnOwls(battle,delivery_cast,.01F);
                    for(auto& owl:battle.owls)if(owl.drop_pending){++deliveries;owl.drop_pending=false;}
                }
                for(const auto& owl:battle.owls)if(!owl.arrived||owl.flying||!owl.scroll_visible)
                    throw std::runtime_error("owl failed to deliver and leave");
                if(deliveries!=2)throw std::runtime_error("owl delivery repeated or missing");
                const auto neville=std::ranges::find_if(candidate.characters,[](const auto& a){return a.actor_reference==1086;});
                if(neville==candidate.characters.end()||!neville->clips.contains("nevilleremeberallwalk"))
                    throw std::runtime_error("Neville walk animation missing");
                if(metadata.merchant!=1043||metadata.sale_price!=25||AsciiFold(metadata.sale_scene)!="beanscs")
                    throw std::runtime_error("Fred card sale differs from authored price or scene");
                {
                    const auto harry=std::ranges::find_if(candidate.characters,[](const auto& a){return a.player;});
                    if(harry==candidate.characters.end()||!harry->clips.contains("run")||!harry->clips.contains("breathe")||
                        harry->clips.at("run").frame_count<2)throw std::runtime_error("reflected Harry locomotion clip missing");
                    const auto& scene=challenge.scenes.at(2222);
                    auto actors=candidate.characters;
                    FaceReturnCinematicTarget(scene,actors);
                    const auto player=std::ranges::find_if(actors,[](const auto& a){return a.player;});
                    const auto exit=std::ranges::find_if(scene.locations,[](const auto& l){return AsciiFold(l.alias)=="locname1";});
                    if(exit==scene.locations.end())throw std::runtime_error("castle exit mark missing");
                    const auto delta=SubtractVector(exit->position,AddVector(player->base_origin,player->cutscene_offset));
                    if(std::cos(player->yaw-std::atan2(delta[0],delta[2]))<.999F)
                        throw std::runtime_error("castle exit faces away from final mark");
                    std::cout<<"PREPARED_PLAYER_PRESENTATION=PASS reflection_run=YES exit_facing=OUTWARD\n";
                }
                if(!RestoreCardGroundClearance(candidate.vertices,candidate.beans,candidate.collision))
                    throw std::runtime_error("card render bounds are invalid");
                unsigned grounded_cards=0;
                for(const auto& card:candidate.beans)if(card.kind==2||card.kind==4){
                    if(!std::isfinite(card.card_floor))throw std::runtime_error("card landing has no floor");
                    for(float scale:{0.F,1.F,2.F,3.F})
                        if(CardRenderHeight(card,card.position[1]-.06F,scale)+scale*card.card_min_y<card.card_floor+.119F)
                            throw std::runtime_error("card intersects floor during pickup");
                    ++grounded_cards;
                }
                if(!grounded_cards)throw std::runtime_error("return map cards missing");
                std::cout<<"PREPARED_CARD_CLEARANCE=PASS cards="<<grounded_cards<<" scales=0,1,2,3\n";
                std::cout<<"PREPARED_RETURN_DELIVERY=PASS owls=2 scrolls=2 departures=2 neville_walk=YES sale_price=25\n";
                {
                    auto girl=*std::ranges::find_if(candidate.characters,[](const auto& actor){return actor.actor_reference==962;});
                    const auto& scene=challenge.scenes.at(1883);
                    const auto mark=[&](std::string_view alias){
                        for(const auto& loc:scene.locations)if(AsciiFold(loc.alias)==alias)return loc.position;
                        throw std::runtime_error("student route mark missing");
                    };
                    auto graph=challenge.graph;auto zones=challenge.spatial;std::vector<int> contacts;
                    const auto from=mark("locname1"),to=mark("locname2");
                    for(unsigned step=0;step<=100;++step){
                        girl.cutscene_offset=SubtractVector(AddVector(from,ScaleVector(SubtractVector(to,from),float(step)/100)),girl.base_origin);
                        for(auto& zone:zones)if(zone.reference==1355||zone.reference==1789){
                            const bool inside=CharacterTouchesProximity(girl,zone);
                            if(inside&&!zone.inside){contacts.push_back(zone.reference);graph.Touch(zone.reference);}
                            zone.inside=inside;
                        }
                    }
                    if(contacts!=std::vector<int>{1355,1789})throw std::runtime_error("student door contacts are not open-then-close");
                    unsigned moves=0;for(const auto& event:graph.DrainEffects())
                        moves+=event.kind==MapEventKind::mover_trigger&&(event.actor_reference==2279||event.actor_reference==1603);
                    if(moves!=4)throw std::runtime_error("student door did not toggle both leaves twice");
                    auto doors=candidate.doors;OrientReturnStudentDoors(doors);unsigned leaves=0;
                    for(auto& door:doors)if(door.actor_reference==2279||door.actor_reference==1603){
                        std::array<float,3> center{};
                        for(std::size_t i=door.first_vertex;i<door.first_vertex+door.vertex_count;++i)
                            for(unsigned axis=0;axis<3;++axis)center[axis]+=candidate.vertices[i].position[axis]/float(door.vertex_count);
                        const auto opened=movers::TransformPoint(center,door.placement,door.motion.keys[1]);
                        if(DotVector(SubtractVector(opened,center),SubtractVector(to,from))<=0)
                            throw std::runtime_error("student door swings toward approaching pupil");
                        if(!movers::Start(door.motion,true))throw std::runtime_error("student door cannot open");
                        (void)movers::Advance(door.motion,door.open_seconds+1);
                        if(!movers::Start(door.motion,false))throw std::runtime_error("student door cannot close");
                        (void)movers::Advance(door.motion,door.close_seconds+door.open_seconds+1);
                        if(door.motion.current!=0||door.motion.moving)throw std::runtime_error("student door remains open");
                        ++leaves;
                    }
                    if(leaves!=2)throw std::runtime_error("student door leaves missing");
                    std::cout<<"PREPARED_STUDENT_DOOR=PASS contacts=2 leaves=2 order=OPEN_CLOSE swing=AWAY_FROM_PUPIL\n";
                }
                for(const auto& a:census.actors)if(a.actor_reference==1588){
                    const auto origin=ActorLocalPosition(a,start,yaw);
                    for(float rate:{72.F,90.F,120.F})for(float speed:{4.F,6.F}){
                        const float dt=1/rate;
                        auto position=AddVector(origin,{3,-2.56F+kPlayerCapsuleHalfHeightMeters,0});
                        const std::array<std::array<float,3>,4> offsets{{{3,0,2.58F},{3,0,-1.72F},{3,0,1.98F},{3,0,-1.72F}}};
                        for(unsigned ledge=0;ledge<offsets.size();++ledge){
                            auto dir=SubtractVector(AddVector(origin,offsets[ledge]),position);dir[1]=0;
                            const auto request=ScaleVector(dir,speed*dt/std::hypot(dir[0],dir[2]));
                            JumpMotion jump;ClimbMotion climb;
                            if(!StartJump(candidate.collision,position,jump))throw std::runtime_error("inner secret jump not on floor");
                            for(unsigned frame=0;frame<unsigned(rate*4)&&jump.active;++frame){
                                LocomotionMove move;StepJump(candidate.collision,jump,position,request,dt,&move);
                                position=AddVector(position,move.displacement);
                                if(jump.active&&BeginClimb(candidate.collision,position,request,&climb,.04F))jump={};
                            }
                            if(!climb.active)throw std::runtime_error("inner secret ledge "+std::to_string(ledge)+" not caught at "+std::to_string(rate)+" Hz");
                            for(unsigned frame=0;frame<unsigned(rate*4)&&climb.active;++frame){
                                LocomotionMove move;
                                if(!StepClimb(candidate.collision,climb,position,dt*1.6F,&move))throw std::runtime_error("inner secret mantle interrupted");
                                position=AddVector(position,move.displacement);
                            }
                            if(climb.active||OverlapsCollisionWall(candidate.collision,position))throw std::runtime_error("inner secret mantle has no clear landing");
                        }
                        if(position[1]-kPlayerCapsuleHalfHeightMeters<origin[1]+6.87F)
                            throw std::runtime_error("secret route did not reach the upper exit");
                    }
                    std::cout<<"PREPARED_SECRET_ROUTE=PASS jumps=4 rates=72,90,120 speeds=WALK_SPRINT upper_exit=YES\n";
                    for(float setback:{.46F,.8F,1.2F}){
                        auto position=AddVector(origin,{-setback,-2.56F+kPlayerCapsuleHalfHeightMeters,0});
                        JumpMotion jump;ClimbMotion climb;
                        if(!StartJump(candidate.collision,position,jump))throw std::runtime_error("mirror approach cannot jump from floor");
                        for(unsigned frame=0;frame<180&&jump.active;++frame){
                            LocomotionMove move;
                            StepJump(candidate.collision,jump,position,{.03F,0,0},.02F,&move);
                            position=AddVector(position,move.displacement);
                            if(jump.active&&BeginClimb(candidate.collision,position,{.03F,0,0},&climb,.04F))jump={};
                        }
                        std::cout<<"MIRROR_JUMP setback="<<setback<<" grabbed="<<climb.active<<" position="<<position[0]<<','<<position[1]<<','<<position[2]<<'\n';
                        if(!climb.active)throw std::runtime_error("mirror full jump did not catch ledge");
                    }
                    ClimbMotion airborne;
                    const auto near_edge=AddVector(origin,{-.46F,-2.56F+.95F+kPlayerCapsuleHalfHeightMeters,0});
                    if(!BeginClimb(candidate.collision,near_edge,{.03F,0,0},&airborne,.04F))
                        throw std::runtime_error("airborne mirror-secret ledge grab rejected");
                    auto position=near_edge;
                    for(unsigned frame=0;frame<200&&airborne.active;++frame){
                        LocomotionMove move;
                        if(!StepClimb(candidate.collision,airborne,position,.02F,&move))
                            throw std::runtime_error("mirror-secret mantle interrupted by geometry");
                        position=AddVector(position,move.displacement);
                    }
                    if(airborne.active)throw std::runtime_error("mirror-secret mantle never finishes");
                    std::cout<<"PREPARED_SECRET_MANTLE=PASS airborne_grab=YES landing_clearance=YES\n";
                }
                ReturnMalfoyRuntime malfoy_battle;
                if(!ConfigureReturnMalfoy(census,start,yaw,candidate.characters,malfoy_battle)||
                   malfoy_battle.actor!=572||malfoy_battle.rails!=std::array<std::int32_t,2>{635,641})
                    throw std::runtime_error("Malfoy arena or combat animations are incomplete");
                auto malfoy_cast=candidate.characters;auto malfoy_graph=challenge.graph;
                if(!malfoy::Activate(malfoy_battle.motion,malfoy_battle.config,malfoy_battle.config.rail_start))
                    throw std::runtime_error("Malfoy encounter rejected");
                unsigned cracker_throws=0,music_starts=0,malfoy_defeats=0;
                for(unsigned frame=0;frame<30000&&malfoy_battle.motion.phase!=malfoy::Phase::Complete;++frame){
                    if(cracker_throws>=3&&malfoy_battle.motion.phase==malfoy::Phase::Patrol)
                        (void)malfoy::Hit(malfoy_battle.motion,malfoy_battle.config);
                    const auto step=AdvanceReturnMalfoy(malfoy_battle,malfoy_cast,malfoy_graph,.01F,malfoy_battle.player_boundary);
                    if(step.throw_projectile){++cracker_throws;
                        if(step.projectile_fuse!=(cracker_throws%3==0?2.75F:4.5F))throw std::runtime_error("Malfoy cracker fuse sequence differs");}
                    for(const auto& effect:malfoy_graph.DrainEffects()){
                        music_starts+=effect.actor_reference==1173;
                        malfoy_defeats+=effect.kind==MapEventKind::cutscene_start&&effect.actor_reference==1552;
                    }
                }
                if(cracker_throws!=3||music_starts!=1||malfoy_defeats!=1||malfoy_battle.motion.phase!=malfoy::Phase::Complete)
                    throw std::runtime_error("Malfoy encounter events differ from authored sequence");
                std::cout<<"PREPARED_MALFOY=PASS rails=2 hits=3 throws=3 short_fuse=1 music=1 defeat_scene=1\n";
                std::vector<GpuVertex> apple_vertices;std::vector<std::uint8_t> apple_pixels;
                {
                    std::vector<GpuVertex> vertices;std::vector<std::uint8_t> pixels;std::uint32_t layers=0;ReturnCrackerVisual visual;
                    if(!LoadReturnCracker(root,vertices,pixels,layers,visual)||visual.clips.size()!=4||visual.total!=vertices.size())
                        throw std::runtime_error("original firecracker model or animations failed to load");
                    std::cout<<"PREPARED_FIRECRACKER=PASS vertices="<<visual.total<<" clips="<<visual.clips.size()<<'\n';
                }
                std::uint32_t apple_layers=0;ReturnAppleVisual apple_visual;
                if(!LoadReturnApple(root,apple_vertices,apple_pixels,apple_layers,apple_visual)||
                   !apple_visual.count||apple_visual.count!=apple_vertices.size()||
                   apple_pixels.size()!=static_cast<std::size_t>(apple_layers)*256*256*4)
                    throw std::runtime_error("Peeves original apple model or texture failed to load");
                std::cout<<"PREPARED_PEEVES_APPLE=PASS vertices="<<apple_visual.count<<" layers="<<apple_layers<<'\n';
                if(!ConfigureReturnPeeves(battle,metadata,candidate.characters))
                    throw std::runtime_error("Peeves battle animation or route data is incomplete");
                auto battle_cast=candidate.characters;
                if(!TriggerReturnPeeves(battle,battle_cast))throw std::runtime_error("Peeves battle trigger rejected");
                auto battle_graph=challenge.graph;
                unsigned victories=0,throws=0,victory_scenes=0,exit_doors=0;
                for(unsigned frame=0;frame<30000&&battle.battle.motion.phase!=peeves::Phase::Complete;++frame){
                    // Let the first station attack finish before testing all four hits.
                    if(throws&&battle.battle.motion.phase==peeves::Phase::Taunt)(void)peeves::Hit(battle.battle);
                    const auto step=AdvanceReturnPeeves(battle,battle_cast,battle_graph,.01F,{1000,1000,1000});
                    auto restored_battle=battle;
                    const auto bank=SaveReturnBattle(battle);
                    if(!RestoreReturnBattle(restored_battle,bank)||SaveReturnBattle(restored_battle)!=bank)
                        throw std::runtime_error("Peeves checkpoint round trip failed");
                    throws+=step.presentation.throw_projectile;victories+=step.presentation.completed;
                    (void)battle_graph.Advance(.01F);
                    for(const auto& effect:battle_graph.DrainEffects()){
                        victory_scenes+=effect.kind==MapEventKind::cutscene_start&&effect.actor_reference==1766;
                        exit_doors+=effect.kind==MapEventKind::mover_trigger&&effect.actor_reference==1456;
                    }
                }
                if(victories!=1||throws!=1||victory_scenes!=1||exit_doors!=1||battle.battle.motion.hits_left!=0||
                   battle.battle.motion.phase!=peeves::Phase::Complete||!battle_graph.healthy()||
                   !battle_graph.Find(metadata.peeves_actor)||!battle_graph.Find(metadata.peeves_actor)->signaled)
                    throw std::runtime_error("Peeves authored battle simulation failed");
                std::cout<<"PREPARED_PEEVES_BATTLE=PASS hits=4 throw_events=1 defeat_events=1 victory_scenes=1 exit_doors=1 departure=COMPLETE\n";
                if(metadata.opening_scene!=1550||metadata.peeves_actor!=1017||metadata.harry_actor!=448||
                    metadata.malfoy_actor!=572||metadata.entrance.first_path!=800||metadata.entrance.station!=510||
                    metadata.patrol.size()!=4||candidate.doors.size()!=22)
                    throw std::runtime_error("return map identities differ: opening="+std::to_string(metadata.opening_scene)+
                        " peeves="+std::to_string(metadata.peeves_actor)+" harry="+std::to_string(metadata.harry_actor)+
                        " malfoy="+std::to_string(metadata.malfoy_actor)+" patrol="+std::to_string(metadata.patrol.size())+
                        " movers="+std::to_string(candidate.doors.size()));
                const auto chest_count=std::ranges::count_if(candidate.challenge_props,[](const auto& p){return chest::IsChest(p.name);});
                const auto cards=std::ranges::count_if(candidate.beans,[](const auto& b){return b.kind==4;});
                const auto frogs=std::ranges::count_if(candidate.beans,[](const auto& b){return b.kind==1;});
                if(chest_count!=5||cards!=3||frogs!=3)
                    throw std::runtime_error("return map rewards differ: chests="+std::to_string(chest_count)+
                        " cards="+std::to_string(cards)+" frogs="+std::to_string(frogs));
                for(const auto& prop:candidate.challenge_props)if(chest::IsChest(prop.name)&&
                    (!prop.spell_target||!prop.animation_frames||!prop.settled_frames))
                    throw std::runtime_error("return map chest animation missing");
                std::cout<<"PREPARED_RETURN=PASS scenes=16 movers=22 patrol_stations=4 chests=5 cards=3 frogs=3\n";
                for(const auto& leg:metadata.patrol)
                    std::cout<<"PEEVES_ROUTE first="<<leg.first_path<<" station="<<leg.station<<'\n';
                const std::array<std::pair<int,int>,4> expected_patrol{{{807,797},{820,849},{813,507},{771,510}}};
                for(unsigned i=0;i<expected_patrol.size();++i)if(metadata.patrol[i].first_path!=expected_patrol[i].first||
                    metadata.patrol[i].station!=expected_patrol[i].second)
                    throw std::runtime_error("Peeves patrol does not return through its authored final path");
            }
            if(map_id==kCharmsTrainingMapId){
                const auto census=hpvr::wand::inspect_hp1_actor_visuals(root/map->package_path);
                hpvr_hp1_player_start_report start{};
                if(hpvr_hp1_load_player_start_utf8((root/map->package_path).string().c_str(),kMetersPerUnrealUnit,0,&start)!=0)
                    throw std::runtime_error("prepared charms player start is invalid");
                const float yaw=start.rotation_units[1]*kTau/65536;
                CharmsRuntime charms;ChallengeRuntime challenge;
                if(!LoadCharmsMetadata(root,census,start,yaw,candidate.challenge_props,charms,&candidate.vertices)||
                   !LoadChallengeMetadata(census,start,yaw,challenge,23)||
                   !ValidateCharmsSceneLayout(challenge,charms,candidate.doors.size(),candidate.fixture_actors,candidate.characters))
                    throw std::runtime_error("prepared charms runtime metadata mismatch");
                unsigned chests=0,rewards=0,cards=0;
                const auto mirrors=FindMirrorSurfaces(candidate.vertices,candidate.map_vertices);
                if(mirrors.size()!=3||std::ranges::count_if(mirrors,[](const auto& m){return std::abs(m.normal[1])>.99F;})!=1)
                    throw std::runtime_error("prepared charms must contain two wall mirrors and one reflecting pool");
                for(const auto& mirror:mirrors)std::cout<<"PREPARED_MIRROR normal="<<mirror.normal[0]<<','<<mirror.normal[1]<<','<<mirror.normal[2]
                    <<" center="<<mirror.center[0]<<','<<mirror.center[1]<<','<<mirror.center[2]<<'\n';
                for(const auto& prop:candidate.challenge_props)if(prop.name=="hprops.knight"){
                    auto center=ScaleVector(AddVector(prop.minimum,prop.maximum),.5F);center[1]=prop.minimum[1];
                    std::cout<<"PREPARED_KNIGHT ref="<<prop.reference<<" foot="<<center[0]<<','<<center[1]<<','<<center[2]<<'\n';
                    float visible_top=-1e9F;
                    for(unsigned i=0;i+2<candidate.map_vertices;i+=3){
                        if(candidate.vertices[i].polygon_flags&1U)continue;
                        std::vector<GpuVertex> tri(candidate.vertices.begin()+i,candidate.vertices.begin()+i+3);
                        const auto faces=BuildCollisionTriangles(tri,3);
                        for(const auto& face:faces){float h=0;if(CollisionTriangleHeightAtXZ(face,center[0],center[2],&h)&&
                            h<center[1]+.05F&&h>center[1]-.8F)visible_top=std::max(visible_top,h);}
                    }
                    if(std::abs(visible_top-center[1])>.005F)throw std::runtime_error("prepared knight is not grounded on its visible pedestal");
                }
                unsigned attached_stars=0;
                auto moving_doors=candidate.doors;auto moving_pickups=candidate.beans;
                for(auto& door:moving_doors)if(door.tag=="secretjumpledge"){
                    (void)movers::Start(door.motion,true);
                    for(unsigned i=0;i<100;++i)(void)movers::Advance(door.motion,.05F);
                }
                UpdateCharmsPickupAttachments(charms,moving_doors,moving_pickups);
                for(const auto& pickup:moving_pickups)if(pickup.kind==3&&charms.prop_attachments.contains(pickup.actor_reference)){
                    ++attached_stars;
                    if(std::abs(pickup.attachment_offset[1]-8.64F)>.005F||BeanWorldPosition(pickup)==pickup.position)
                        throw std::runtime_error("prepared final star does not follow the lifted platform");
                }
                if(attached_stars!=1)throw std::runtime_error("prepared final star attachment is missing");
                auto entry_doors=candidate.doors;
                if(OpenCharmsCutsceneDoors(1526,entry_doors)<=0)throw std::runtime_error("entry door does not open for Harry");
                for(auto& door:entry_doors)for(unsigned i=0;i<100;++i)(void)movers::Advance(door.motion,.05F);
                if(CloseCharmsEntryDoors(entry_doors)<=0)throw std::runtime_error("entry door does not close after Harry");
                for(auto& door:entry_doors)for(unsigned i=0;i<100;++i)(void)movers::Advance(door.motion,.05F);
                if(CloseCharmsEntryDoors(entry_doors)!=0)throw std::runtime_error("closed entry door would hold player control");
                std::cout<<"PREPARED_C70=PASS mirrors=3 grounded_knights=4 attached_stars=1 entry_door=OPEN_CLOSE\n";
                auto classroom_doors=candidate.doors;
                if(OpenCharmsCutsceneDoors(1878,classroom_doors)<=0)throw std::runtime_error("professor return door does not open");
                for(auto& door:classroom_doors)for(unsigned i=0;i<100;++i)(void)movers::Advance(door.motion,.05F);
                if(CloseCharmsDoors(classroom_doors,"aloroom2",true)<=0)throw std::runtime_error("professor return door does not close");
                for(auto& door:classroom_doors)for(unsigned i=0;i<100;++i)(void)movers::Advance(door.motion,.05F);
                if(CloseCharmsDoors(classroom_doors,"aloroom2",true)!=0)throw std::runtime_error("classroom door closure never finishes");
                std::cout<<"PREPARED_CLASSROOM_RETURN=PASS sequence=OPEN_CROSS_CLOSE\n";
                for(int ref:{1989,1990,2018,1979,1546,1547}){
                    auto scene=challenge.scenes.at(ref);scene.camera_target="locname1";
                    if(!CharmsFirstPersonFocus(scene,candidate.characters))throw std::runtime_error("charms reveal has no authored focus");
                }
                if(!CharmsFirstPersonFocus(challenge.scenes.at(1411),candidate.characters))
                    throw std::runtime_error("charms exit has no authored destination");
                std::cout<<"PREPARED_C71=PASS reveal_targets=6 exit_destination=VALID\n";
                auto collision=candidate.collision;const auto static_count=collision.size();
                for(const auto& door:candidate.doors){
                    std::vector<GpuVertex> vertices(candidate.vertices.begin()+door.first_vertex,candidate.vertices.begin()+door.first_vertex+door.vertex_count);
                    auto extra=BuildCollisionTriangles(vertices,vertices.size());collision.insert(collision.end(),extra.begin(),extra.end());
                }
                AppendCharmsCollision(charms,collision);
                for(auto& [ref,block]:charms.blocks){
                    const auto box=CharmsBlockBounds(block);
                    const auto center=ScaleVector(AddVector(box.minimum,box.maximum),.5F);
                    const auto lift=charms_block::Advance(block.motion,box,AddVector(center,{0,1,0}),true,.05F,15,
                        collision,static_count,block.collision_first,block.collision_count,block.collision_yaw);
                    std::cout<<"PREPARED_BLOCK_LIFT ref="<<ref<<" rise="<<lift.offset[1]<<'\n';
                    if(std::abs(lift.offset[1]-.2F)>.001F)
                        throw std::runtime_error("prepared levitation block is wedged in starting geometry");
                }
                const auto sky_count=std::count_if(candidate.vertices.begin(),candidate.vertices.begin()+candidate.map_vertices,
                    [](const auto& vertex){return (vertex.polygon_flags&kBroomSkyFlag)!=0;});
                if(sky_count!=36)throw std::runtime_error("prepared balcony sky must contain only its six authored faces");
                std::cout<<"PREPARED_BALCONY_SKY=PASS vertices="<<sky_count<<'\n';
                for(const auto& prop:candidate.challenge_props)if(chest::IsChest(prop.name)){
                    if(!prop.spell_target||prop.animation_frames!=74||prop.settled_frames!=6)
                        throw std::runtime_error("prepared chest animation is incomplete");
                    ++chests;
                    for(const auto& reward:candidate.beans)if(reward.source_actor==prop.reference){++rewards;cards+=reward.kind==4;}
                }
                if(chests!=11||rewards!=50||cards!=1||charms.valid_plates.size()!=9)
                    throw std::runtime_error("prepared charms rewards or plates are incomplete");
                std::cout<<"PREPARED_CHARMS=PASS scenes=23 blocks=7 plates=9 chests=11 rewards=50 chest_cards=1\n";
            }
            if(map_id==2){
                const auto player=std::ranges::find_if(candidate.characters,[](const auto& draw){return draw.player;});
                BroomAvatar avatar;
                if(player==candidate.characters.end()||!BuildBroomAvatar(root,candidate.vertices,*player,avatar))
                    throw std::runtime_error("prepared mounted avatar is incomplete");
                std::cout<<"PREPARED_BROOM_AVATAR=PASS vertices="<<avatar.vertex_count<<" frames="<<avatar.frame_count
                    <<" body="<<!avatar.broom_only<<" broom_triangles="<<avatar.broom_triangles<<'\n';
            }
            const auto* vertex_address=candidate.vertices.data();
            const auto* texture_address=candidate.textures.data();
            auto runtime_mirrors=FindMirrorSurfaces(candidate.vertices,candidate.map_vertices);
            if(map_id==kCharmsTrainingMapId){
                auto& mirrors=runtime_mirrors;
                const auto before=candidate.vertices.size();AppendWaterSurfaceGeometry(mirrors,candidate.vertices);
                if(std::ranges::count_if(mirrors,[](const auto& m){return m.water_draw.second>0;})!=1)
                    throw std::runtime_error("exactly one water surface must receive wave geometry");
                std::cout<<"PREPARED_WATER_WAVES=PASS added_vertices="<<candidate.vertices.size()-before<<'\n';
            }
            QuestFrontEnd front;
            if(!LoadFrontAssets(root,&front.assets,map_id))throw std::runtime_error("prepared scene frontend assets are incomplete: "+front.assets.error);
            if(map_id==kHogwartsReturnMapId){
                if(front.assets.owl_letter.empty())throw std::runtime_error("owned Hermione letter is missing");
                for(int ref:{1043,1086})if(std::ranges::none_of(front.assets.bump_speech,[&](const auto& p){return p.actor_reference==ref&&!p.lines.empty();}))
                    throw std::runtime_error("Fred or Neville proximity dialogue is missing");
                std::cout<<"PREPARED_RETURN_DIALOGUE=PASS merchant=YES neville=YES letter=YES\n";
            }
            // Reserve the same two extra layers used by fire and the target
            // marker. This check does not render or alter the cooked file.
            if(candidate.texture_layers+2+front.assets.textures.size()>kMaximumCombinedTextureLayers)
                throw std::runtime_error("prepared scene leaves insufficient frontend texture layers");
            candidate.texture_layers+=2;
            candidate.textures.resize(std::uint64_t(candidate.texture_layers)*256*256*4);
            if(map_id==3){
                unsigned feather_layer=0;
                if(!LoadWingFeatherTexture(root,256,256,candidate.textures,candidate.texture_layers,feather_layer))
                    throw std::runtime_error("original Wingardium feather texture could not be loaded");
            }
            std::map<std::string,FrontDrawRange> ranges;std::uint32_t front_vertices=0;
            std::uint64_t projectile_vertices=0;
            if(!AppendFrontGeometry(front,candidate.vertices,candidate.textures,candidate.texture_layers,ranges,front_vertices)||
                candidate.vertices.data()!=vertex_address||candidate.textures.data()!=texture_address)
                throw std::runtime_error("frontend exceeded prepared-scene allocation headroom");
            if(map_id==kHogwartsReturnMapId){
                if(!front.assets.has_boss_art)throw std::runtime_error("original Peeves health art missing");
                for(unsigned hits=0;hits<=4;++hits){
                    const auto draw=ranges.find("peeves_health_"+std::to_string(hits));
                    if(draw==ranges.end()||draw->second.count!=12)throw std::runtime_error("Peeves health geometry missing");
                }
                std::cout<<"PREPARED_PEEVES_HUD=PASS original_art=2 health_states=5\n";
                ReturnAppleVisual scroll;
                if(!LoadReturnScroll(root,candidate.vertices,candidate.textures,candidate.texture_layers,scroll)||!scroll.count)
                    throw std::runtime_error("owned Hedwig scroll mesh missing");
                front_vertices+=scroll.count;
                const auto base_end=candidate.vertices.size()-front_vertices;
                ReturnAppleVisual apple;ReturnCrackerVisual cracker;
                if(!LoadReturnApple(root,candidate.vertices,candidate.textures,candidate.texture_layers,apple)||
                   !LoadReturnCracker(root,candidate.vertices,candidate.textures,candidate.texture_layers,cracker)||
                   apple.first!=base_end+front_vertices||cracker.first!=apple.first+apple.count||
                   cracker.first+cracker.total!=candidate.vertices.size()||candidate.vertices.data()!=vertex_address||
                   candidate.textures.data()!=texture_address)
                    throw std::runtime_error("boss projectile vertex ranges exceed prepared allocation");
                std::cout<<"PREPARED_RETURN_VERTEX_LAYOUT=PASS apple="<<apple.count<<" cracker="<<cracker.total<<'\n';
                projectile_vertices=std::uint64_t(apple.count)+cracker.total;
            }
            {
                std::uint64_t animated_begin=std::uint64_t(candidate.map_vertices)+candidate.fixture_vertices;
                for(const auto& door:candidate.doors)animated_begin+=door.vertex_count;
                if(!ValidateRuntimeVertexLayout(candidate.characters,candidate.beans,runtime_mirrors,
                    animated_begin,candidate.vertices.size()-front_vertices-projectile_vertices))
                    throw std::runtime_error("runtime water and animation vertex layout rejected");
                std::cout<<"PREPARED_RUNTIME_VERTEX_LAYOUT=PASS map="<<map_id<<'\n';
            }
            std::cout<<"PREPARED_FRONTEND=PASS map="<<map_id<<" vertices="<<front_vertices
                <<" layers="<<candidate.texture_layers<<" scene_buffer_reallocated=NO\n";
        }
        std::cout<<std::fixed<<std::setprecision(3)<<"PREPARED_SCENE=PASS map="<<map_id
            <<" schema="<<cache::kSchema<<" cook="<<cache::CookRevision(map_id)<<" bytes="<<fs::file_size(path)
            <<" vertices="<<candidate.vertices.size()<<" fingerprint_seconds="<<fingerprint_seconds
            <<" read_seconds="<<read_seconds<<" total_seconds="<<elapsed()<<" mode="<<(verify?"VERIFY":"PREPARE")<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<"PREPARED_SCENE=FAILED reason="<<e.what()<<'\n';return 1;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return Run(args);}
#else
int main(int argc,char** argv){std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return Run(args);}
#endif
