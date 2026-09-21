#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <stdexcept>
using namespace hpvr::quest;
namespace wand=hpvr::wand;
namespace {
wand::Hp1ClassDefaultProperty Name(const char* key,const char* value){
    wand::Hp1ClassDefaultProperty p;p.name=key;p.text_value=value;p.text_value_serialized=true;return p;
}
wand::Hp1ActorVisual Actor(int ref,const char* name,const char* type){
    wand::Hp1ActorVisual a;a.actor_reference=ref;a.object_name=name;a.qualified_class_name=type;
    a.location_serialized=true;a.location_unreal={float(ref*10),20,30};return a;
}
wand::Hp1ActorVisualCensus Fixture(){
    wand::Hp1ActorVisualCensus c;c.status=wand::Hp1ProfileStatus::ok;
    c.actors={Actor(1,"Opening","HPBase.CutScene"),Actor(2,"Peeves","Tut3.tut3Peeves"),
        Actor(3,"Harry","HarryPotter.Harry"),Actor(4,"Draco","HarryPotter.BossRailMove"),
        Actor(5,"Entrance","HarryPotter.HPath_A"),Actor(6,"Outgoing","HarryPotter.HPath_B"),
        Actor(7,"Returning","HarryPotter.HPath_C"),Actor(8,"Exit","HarryPotter.HPath_F"),
        Actor(9,"Station0","HPBase.baseStation"),Actor(10,"Station1","HPBase.baseStation"),
        Actor(11,"ExitStation","HPBase.baseStation")};
    wand::Hp1ClassDefaultProperty start;start.name="bLevelLoadStarts";start.boolean_value_serialized=start.boolean_value=true;
    c.actors[0].serialized_properties={start};
    c.actors[1].tag="Encounter";c.actors[1].event="Defeated";
    c.actors[1].serialized_properties={Name("firstPath","Entrance"),Name("stationDestination","Station0"),
        Name("exitfirstPath","Exit"),Name("exitstationdestination","ExitStation")};
    wand::Hp1ClassDefaultProperty route;route.name="aiData";route.station_route=wand::Hp1StationRoute{};
    route.station_route->destination="Station1";route.station_route->first_path="Outgoing";
    c.actors[8].serialized_properties={route};
    route.station_route->destination="Station0";route.station_route->first_path="Returning";
    c.actors[9].serialized_properties={route};return c;
}
}
int main(){try{
    unsigned checks=0;
    const auto check=[&](bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);};
    const auto fixture=Fixture();hpvr_hp1_player_start_report start{};ReturnMetadata result;
    {
        CharacterDraw george;george.actor_reference=10;george.base_origin={0,0,.5F};
        CharacterDraw fred;fred.actor_reference=20;fred.base_origin={0,0,1.5F};
        const std::vector<CharacterDraw> twins{george,fred};
        check(ReturnContactOrder(twins,{0,1.2F,0},20,true).front()==1,"available Fred exchange precedes nearby George dialogue");
        check(ReturnContactOrder(twins,{0,1.2F,0},20,false).front()==0,"ordinary speech chooses proximity instead of merchant priority");
        bool armed=false;
        check(!UpdateMerchantApproach(armed,true,4)&&!armed,"cinematic movement cannot arm a purchase");
        check(!UpdateMerchantApproach(armed,false,1)&&!armed,"cutscene release beside Fred cannot buy a card");
        check(!UpdateMerchantApproach(armed,false,2.1F)&&!armed,"contact boundary jitter cannot arm purchase");
        check(!UpdateMerchantApproach(armed,false,2.4F)&&armed,"leaving Fred arms a later voluntary approach without buying");
        check(!UpdateMerchantApproach(armed,false,8)&&armed,"walking away without returning never buys");
        check(UpdateMerchantApproach(armed,false,1.8F),"returning to Fred enables exchange");
        check(!UpdateMerchantApproach(armed,true,1)&&!armed,"a new cinematic cancels pending approach");
        check(!UpdateMerchantApproach(armed,false,1.8F),"remaining beside Fred after another cinematic cannot buy");
        armed=true;
        check(!UpdateMerchantApproach(armed,false,std::numeric_limits<float>::quiet_NaN())&&!armed,"invalid merchant distance fails closed");
        BeanDraw card;card.kind=4;card.card_min_y=-.65F;card.card_floor=2;
        for(float scale:{0.F,1.F,2.F,3.F})
            check(CardRenderHeight(card,2.18F,scale)+scale*card.card_min_y>=2.119F,
                "card bottom stays above floor throughout pickup growth and collapse");
        check(CardRenderHeight(card,8,1)==8,"airborne card does not snap down to the floor");
        std::vector<GpuVertex> mesh(2);mesh[0].position[1]=-.3F;mesh[1].position[1]=-.8F;
        card.first=0;card.count=1;card.frames=2;
        std::vector<BeanDraw> cards{card};
        check(RestoreCardGroundClearance(mesh,cards,{})&&cards[0].card_min_y==-.8F,
            "card clearance includes every animation frame");
        cards[0].frames=3;
        check(!RestoreCardGroundClearance(mesh,cards,{}),"invalid card vertex range is rejected");
    }
    {
        CharmsRuntime reflection;CharacterDraw harry;harry.player=true;
        harry.clips["run"]={0,1,4};harry.clips["breathe"]={0,1,4};
        std::vector<CharacterDraw> actors{harry};
        UpdateReflectedPlayerAnimation(reflection,actors,{0,0,0},.02F,false);
        check(actors[0].active_clip=="breathe","reflection starts idle");
        UpdateReflectedPlayerAnimation(reflection,actors,{0,0,.08F},.02F,false);
        check(actors[0].active_clip=="run"&&actors[0].animation_loop,"reflection animates walking in gameplay");
        actors[0].animation_time=.23F;
        UpdateReflectedPlayerAnimation(reflection,actors,{0,0,.16F},.02F,false);
        check(actors[0].animation_time==.23F,"continued movement retains animation phase");
        for(unsigned i=0;i<9;++i)UpdateReflectedPlayerAnimation(reflection,actors,{0,0,.16F},.02F,false);
        check(actors[0].active_clip=="breathe","reflection returns to idle after stopping");
        actors[0].active_clip="talk";
        UpdateReflectedPlayerAnimation(reflection,actors,{5,0,0},.02F,true);
        check(actors[0].active_clip=="talk"&&!reflection.reflected_player_valid,"reflection leaves scripted animation untouched");
        UpdateReflectedPlayerAnimation(reflection,actors,{20,0,0},.02F,false);
        check(actors[0].active_clip=="breathe","cutscene release teleport cannot start running");
    }
    {
        CharacterDraw harry;harry.player=true;harry.actor_reference=448;
        CutsceneTrack track;track.alias="Harry";track.actor_reference=448;track.delay_seconds=2;
        IntroCutscene scene;scene.object_name="cutscene8";scene.camera_target="harry";scene.tracks={track};
        scene.locations={{594,"LocName0",{0,0,-1}},{929,"LocName1",{0,0,4}}};
        std::vector<CharacterDraw> actors{harry};actors[0].yaw=kTau/2;
        FaceReturnCinematicTarget(scene,actors);
        check(std::abs(actors[0].yaw)<.001F,"exit faces outward before door-opening delay");
        actors[0].cutscene_offset={0,0,-.5F};actors[0].yaw=kTau/2;
        scene.tracks[0].moving=true;scene.tracks[0].move_target={0,0,-1};
        FaceReturnCinematicTarget(scene,actors);
        check(std::abs(actors[0].yaw)<.001F,"alignment movement cannot turn exit gaze backward");
    }
    {
        DoorDraw left;left.actor_reference=2279;left.tag="stageleft";
        DoorDraw right=left;right.actor_reference=1603;
        DoorDraw other=left;other.actor_reference=1;other.motion.keys[1].rotation_units[1]=8000;
        std::vector<DoorDraw> doors{left,right,other};
        OrientReturnStudentDoors(doors);OrientReturnStudentDoors(doors);
        check(doors[0].motion.keys[1].rotation_units[1]==16384&&doors[1].motion.keys[1].rotation_units[1]==-16384,
            "student leaves swing oppositely and correction is idempotent");
        check(doors[2].motion.keys[1].rotation_units[1]==8000,"unrelated door retains authored direction");
        check(doors[0].motion.pose.rotation_units[1]==0&&!doors[0].opening,"swing correction retains closed initial state");
        auto& saved=doors[0].motion;saved.keys[1].rotation_units[1]=-16384;
        check(movers::Start(saved,true),"legacy door starts opening");
        (void)movers::Advance(saved,.5F);
        const auto bank=movers::SaveMotion(saved);
        auto resumed=doors;OrientReturnStudentDoors(resumed);
        check(movers::RestoreMotion(resumed[0].motion,bank),"legacy intermediate door save remains readable");
        OrientReturnStudentDoors(resumed);
        check(resumed[0].motion.pose.rotation_units[1]==8192,"legacy intermediate pose follows corrected swing");
        (void)movers::Advance(resumed[0].motion,.5F);
        check(resumed[0].motion.pose.rotation_units[1]==16384&&!resumed[0].motion.moving,
            "restored door finishes without reversing across closed pose");
    }
    {
        CharacterDraw harry;harry.player=true;harry.actor_reference=448;
        CharacterDraw owl;owl.actor_reference=1159;owl.base_origin={1,3,0};owl.cutscene_offset={3,0,0};
        CutsceneTrack target;target.alias="Hedwig";target.actor_reference=1159;target.position={0,3,-10};
        IntroCutscene scene;scene.camera_target="hedwig";scene.tracks={target};
        std::vector<CharacterDraw> actors{harry,owl};FaceReturnCinematicTarget(scene,actors);
        check(std::abs(actors[0].yaw-kTau/4)<.001F,"owl cinematic follows current flight instead of stale cast position");
        CharacterDraw girl;girl.class_name="HarryPotter.gen_fem_1";
        check(CharacterMatchesProximity(girl,"basechar"),"generic pupil matches inherited door trigger class");
        check(!CharacterMatchesProximity(girl,"harry"),"pupil cannot activate Harry-only trigger");
        girl.flying=true;check(!CharacterMatchesProximity(girl,"basechar"),"flying actor cannot toggle pupil door");
        std::vector<CollisionTriangle> empty;
        for(float distance:{0.F,.5F,1.2F}){
            cracker::Cracker c;c.position={0,.65F,0};
            check(cracker::Explode(c,empty,{distance,.65F,0},{10,0,0},.3F,.65F).player_damage==14,
                "Malfoy blast retains fixed original Harry damage throughout radius");
        }
        cracker::Cracker c;c.position={0,.65F,0};
        check(cracker::Explode(c,empty,{2,.65F,0},{10,0,0},.3F,.65F).player_damage==0,"Malfoy blast remains range limited");
        IntroCutscene departure;departure.object_name="cutscene4";departure.playing=departure.camera_active=true;
        check(!ReleaseReturnCameraTail(departure),"departure cannot release before authored cue");
        departure.harry_released=true;departure.cues.insert("cutscenedone");
        check(ReleaseReturnCameraTail(departure)&&ReleaseCutsceneControlIfReady(departure),"finished departure releases despite remaining camera travel");
        ChallengeProp chest;chest.name="hprops.bronzechest";chest.minimum={-.4F,2,-.4F};chest.maximum={.4F,3,.4F};
        BeanDraw bean;bean.emission={0,2.7F,0};bean.position={0,0,1};
        std::vector<CollisionTriangle> wall(2);
        wall[0].vertices={{{-1,0,-5},{-1,5,-5},{-1,5,5}}};
        wall[1].vertices={{{-1,0,-5},{-1,5,5},{-1,0,5}}};
        for(auto& t:wall)prop_orientation_restore::Refresh(t);
        const std::array<float,3> approach{-3,.65F,0};
        PrepareChallengeBeanEmission(bean,chest,wall,&approach);
        check(std::abs(bean.emission[0])<.5F&&bean.emission[1]>2,
            "occluded approach cannot relocate elevated chest emission to painting");
    }
    auto cards=fixture;
    cards.actors.push_back(Actor(30,"Chest","HProps.WoodChest"));
    wand::Hp1ClassDefaultProperty reward;reward.name="EjectedObjects";reward.array_index=0;
    reward.object_path={"HProps","WCWaffling"};cards.actors.back().serialized_properties={reward};
    cards.actors.push_back(Actor(31,"Reward","HPBase.SpawnThingy"));
    reward.name="SpawnClass";reward.object_path={"HProps","WCOddball"};cards.actors.back().serialized_properties={reward};
    cards.actors.push_back(Actor(32,"Card","HProps.WCToke"));
    std::vector<BeanDraw> card_pickups(3);
    card_pickups[0].actor_reference=chest::RewardActor(30,0);card_pickups[0].source_actor=30;
    card_pickups[1].actor_reference=0x30000000+31;card_pickups[1].source_actor=31;
    card_pickups[2].actor_reference=32;
    for(auto& pickup:card_pickups)pickup.kind=4;
    check(RestorePickupCardIds(cards,card_pickups)&&card_pickups[0].card_id==24&&
        card_pickups[1].card_id==18&&card_pickups[2].card_id==28,"owned chest, triggered and placed card IDs restore");
    cards.actors.back().qualified_class_name="HProps.UnknownCard";
    check(!RestorePickupCardIds(cards,card_pickups)&&card_pickups[2].card_id==28,
        "unknown card rejects without partially replacing collection IDs");
    auto arena=fixture;
    arena.actors[3].serialized_properties={Name("NavPoint_MoveTagName","Rails"),Name("NavPoint_HarryDistanceTagName","Boundary"),
        Name("TriggerToSendOnFirstThrow","Music"),Name("TrigEventWhenDefeated","Win"),Name("TrigEventWhenVictor","Lose")};
    arena.actors.push_back(Actor(20,"Rail0","Engine.NavigationPoint"));arena.actors.back().tag="Rails";
    arena.actors.push_back(Actor(21,"Rail1","Engine.NavigationPoint"));arena.actors.back().tag="Rails";
    arena.actors.push_back(Actor(22,"HarryLimit","Engine.NavigationPoint"));arena.actors.back().tag="Boundary";
    CharacterDraw draco;draco.actor_reference=4;
    for(const auto* name:{"lookdownhall","strafeleft","straferight","breathe","throw","knockback","knockdown"})
        draco.clips[name]=CharacterClip{0,.4F,1};
    std::vector<CharacterDraw> duel_cast{draco};ReturnMalfoyRuntime duel;
    check(ConfigureReturnMalfoy(arena,start,0,duel_cast,duel)&&duel.rails==std::array<std::int32_t,2>{20,21},"authored Malfoy rails and clips bind");
    auto incomplete=arena;incomplete.actors.pop_back();
    check(!ConfigureReturnMalfoy(incomplete,start,0,duel_cast,duel)&&duel.actor==4,"missing arena boundary rejected transactionally");
    incomplete=arena;incomplete.actors.push_back(Actor(23,"ExtraRail","Engine.NavigationPoint"));incomplete.actors.back().tag="Rails";
    check(!ConfigureReturnMalfoy(incomplete,start,0,duel_cast,duel),"ambiguous third rail rejected");
    auto missing_clip=duel_cast;missing_clip[0].clips.erase("throw");
    check(!ConfigureReturnMalfoy(arena,start,0,missing_clip,duel),"missing throw animation rejected");
    MapEventGraph duel_graph;
    check(duel_graph.Load(arena),"Malfoy test graph loads");
    check(malfoy::Activate(duel.motion,duel.config,duel.config.rail_start),"Malfoy runtime activates");
    for(unsigned frame=0;frame<500;++frame){
        (void)AdvanceReturnMalfoy(duel,duel_cast,duel_graph,.02F,duel.player_boundary);
        check(duel_cast[0].clips.contains(duel_cast[0].active_clip),"Malfoy runtime selects imported animation names");
    }
    const auto duel_bank=SaveReturnDuel(duel);auto duel_copy=duel;
    check(RestoreReturnDuel(duel_copy,duel_bank)&&SaveReturnDuel(duel_copy)==duel_bank,"active duel checkpoint round trip");
    check(!RestoreReturnDuel(duel_copy,duel_bank+" trailing")&&SaveReturnDuel(duel_copy)==duel_bank,"duel trailing data rejected transactionally");
    auto invalid_duel=duel;invalid_duel.motion.hits=99;
    check(!RestoreReturnDuel(duel_copy,SaveReturnDuel(invalid_duel)),"duel invalid health rejected");
    duel.crackers[0].phase=cracker::Phase::Ground;duel.crackers[0].fuse=.5F;
    check(cracker::PickUp(duel.crackers[0])&&ReturnCarrying(duel),"ground cracker is carried");
    const auto held_bank=SaveReturnDuel(duel);
    check(RestoreReturnDuel(duel_copy,held_bank)&&SaveReturnDuel(duel_copy)==held_bank&&ReturnCarrying(duel_copy),"carried cracker survives reload");
    check(!UpdateReturnHand(duel_copy,{1,2,3},{0,0,-1},true,false),"restored hand release alone cannot throw");
    check(!UpdateReturnHand(duel_copy,{1,2,3},{0,0,-1},true,true),"fresh trigger press arms return throw");
    check(UpdateReturnHand(duel_copy,{1,2,3},{0,0,-1},true,false)&&!ReturnCarrying(duel_copy)&&duel_copy.crackers[0].returned,"release throws one carried cracker");
    check(!UpdateReturnHand(duel_copy,{1,2,3},{0,0,-1},true,false),"return throw cannot repeat on held release");
    const auto flight_bank=SaveReturnDuel(duel_copy);
    check(RestoreReturnDuel(duel_copy,flight_bank)&&SaveReturnDuel(duel_copy)==flight_bank,"returned projectile survives reload");
    invalid_duel=duel;invalid_duel.crackers[1]=invalid_duel.crackers[0];
    check(!RestoreReturnDuel(duel_copy,SaveReturnDuel(invalid_duel)),"two carried crackers rejected");
    duel.crackers={};
    {
        auto physical=duel;physical.motion={};physical.crackers={};
        physical.config.rail_start={0,0,0};physical.config.rail_end={.1F,0,0};
        auto actors=duel_cast;actors[0].base_origin={};actors[0].cutscene_offset={};
        actors[0].collision_center={0,.7F,0};actors[0].collision_min_y=.2F;actors[0].collision_max_y=1.2F;
        std::vector<CollisionTriangle> floor(2);
        floor[0].vertices={{{-10,0,-10},{10,0,10},{10,0,-10}}};
        floor[1].vertices={{{-10,0,-10},{-10,0,10},{10,0,10}}};
        for(auto& triangle:floor)prop_orientation_restore::Refresh(triangle);
        auto graph_copy=duel_graph;
        check(malfoy::Activate(physical.motion,physical.config,{}),"physical duel activates");
        bool landed=false;
        bool dodged=false;
        for(unsigned frame=0;frame<1000&&!landed;++frame){
            const auto step=AdvanceReturnDuel(physical,actors,graph_copy,.01F,floor,{4,.65F,dodged?3.F:0.F});
            landed=step.landed;dodged|=step.thrown;
        }
        check(landed,"authored throw falls onto collision floor");
        const auto ground=std::ranges::find_if(physical.crackers,[](const auto& c){return c.phase==cracker::Phase::Ground;});
        check(ground!=physical.crackers.end(),"landed cracker remains available to player");
        auto player=ground->position;player[1]=.65F;
        check(AdvanceReturnDuel(physical,actors,graph_copy,.01F,floor,player).picked_up&&ReturnCarrying(physical),
            "walking onto grounded cracker picks it up through live duel path");
        auto hand=player;hand[1]=.8F;
        const std::array<float,3> aim{-1,0,0};
        (void)UpdateReturnHand(physical,hand,aim,true,true);
        check(UpdateReturnHand(physical,hand,aim,true,false),"live duel releases carried cracker");
        for(unsigned frame=0;frame<150&&!physical.motion.hits;++frame)
            (void)AdvanceReturnDuel(physical,actors,graph_copy,.01F,floor,player);
        check(physical.motion.hits==1,"physical return throw damages Malfoy exactly once");
    }
    for(auto terminal:{malfoy::Phase::Complete,malfoy::Phase::Lost}){
        duel.motion.phase=terminal;duel_cast[0].active_clip="talk2";
        duel_cast[0].animation_time=.25F;duel_cast[0].cutscene_offset={1,2,3};
        (void)AdvanceReturnMalfoy(duel,duel_cast,duel_graph,.1F,duel.player_boundary);
        check(duel_cast[0].active_clip=="talk2"&&duel_cast[0].animation_time==.25F&&
            duel_cast[0].cutscene_offset==std::array<float,3>{1,2,3},"finished duel leaves cutscene pose untouched");
    }
    check(LoadReturnMetadata(fixture,start,0,result),"authored station cycle loads");
    check(result.entrance.first_path==5&&result.entrance.station==9,"initial approach retained separately");
    check(result.patrol.size()==2&&result.patrol[0].first_path==6&&result.patrol[0].station==10&&
        result.patrol[1].first_path==7&&result.patrol[1].station==9,"patrol closes through return path, not entrance");
    check(result.departure.first_path==8&&result.departure.station==11,"departure remains separate from combat loop");
    auto routed_fixture=fixture;routed_fixture.actors.push_back(Actor(24,"ExitCorner","HarryPotter.HPath_F"));
    routed_fixture.actors.back().location_unreal={105,20,30};
    wand::Hp1Navigation navigation;navigation.status=wand::Hp1ProfileStatus::ok;
    for(const auto pair:std::array<std::array<int,2>,4>{{{5,9},{6,10},{7,9},{8,24}}}){
        wand::Hp1ReachSpec edge;edge.start=pair[0];edge.end=pair[1];navigation.paths.push_back(edge);
    }
    auto routed=result;
    check(ResolveReturnRoutes(routed_fixture,navigation,start,0,routed)&&routed.departure.waypoints.size()==3,
        "departure retains intermediate authored path before final non-colliding station approach");
    auto bad_navigation=navigation;bad_navigation.paths.front().pruned=true;
    check(!ResolveReturnRoutes(routed_fixture,bad_navigation,start,0,routed)&&routed.departure.waypoints.size()==3,
        "unreachable combat station rejected without changing configured route");
    const auto rejected=[&](auto mutate,const char* message){
        auto broken=fixture;mutate(broken);ReturnMetadata sentinel;sentinel.opening_scene=999;
        check(!LoadReturnMetadata(broken,start,0,sentinel)&&sentinel.opening_scene==999,message);
    };
    rejected([](auto& c){c.actors.pop_back();},"missing departure target rejected transactionally");
    rejected([](auto& c){c.actors[9].serialized_properties[0].station_route->destination="Missing";},"broken station reference rejected");
    rejected([](auto& c){c.actors[9].serialized_properties[0].station_route->destination="Station1";},"cycle bypassing initial station rejected");
    rejected([](auto& c){c.actors[8].serialized_properties[0].station_route->behavior=4;},"death behavior cannot enter patrol");
    rejected([](auto& c){c.actors[8].serialized_properties.push_back(c.actors[8].serialized_properties[0]);},"ambiguous station route rejected");
    rejected([](auto& c){c.actors[5].qualified_class_name="Engine.Trigger";},"non-path actor rejected");
    rejected([](auto& c){c.actors[5].location_unreal.x=std::numeric_limits<float>::quiet_NaN();},"non-finite route rejected");
    rejected([](auto& c){c.actors.push_back(c.actors[0]);},"duplicate actor identity rejected");
    CharacterDraw ghost;ghost.actor_reference=2;ghost.base_origin={0,0,0};
    for(const auto* name:{"breathe","grab","float","attackfloat","scheming","look","throwobject1","throwobject2","hit"})
        ghost.clips[name]=CharacterClip{0,.1F,1};
    std::vector<CharacterDraw> cast{ghost};ReturnRuntime runtime;
    check(ConfigureReturnPeeves(runtime,result,cast),"combat config binds actual animation durations");
    auto broken_cast=cast;broken_cast[0].clips.erase("hit");
    check(!ConfigureReturnPeeves(runtime,result,broken_cast)&&runtime.metadata.peeves_actor==2,
        "missing combat clip cannot replace valid runtime");
    auto events=fixture;events.actors.push_back(Actor(12,"Victory","HPBase.CutScene"));events.actors.back().tag="Defeated";
    MapEventGraph graph;check(graph.Load(events)&&graph.Dispatch("Encounter"),"authored encounter event dispatches");
    const auto activation=graph.DrainEffects();
    check(activation.size()==1&&activation[0].actor_reference==2&&activation[0].kind==MapEventKind::actor_trigger,
        "Peeves encounter emits actor trigger, not victory");
    check(TriggerReturnPeeves(runtime,cast)&&!TriggerReturnPeeves(runtime,cast),"runtime ignores repeated triggers");
    const auto initial_battle=SaveReturnBattle(runtime);
    auto restored=runtime;
    check(RestoreReturnBattle(restored,initial_battle)&&SaveReturnBattle(restored)==initial_battle,"battle checkpoint round trip");
    check(!RestoreReturnBattle(restored,initial_battle+" trailing")&&SaveReturnBattle(restored)==initial_battle,
        "trailing data rejected without mutation");
    auto malformed=runtime;malformed.battle.motion.hits_left=5;
    check(!RestoreReturnBattle(restored,SaveReturnBattle(malformed)),"invalid health rejected");
    malformed=runtime;malformed.battle.motion.completion_sent=true;
    check(!RestoreReturnBattle(restored,SaveReturnBattle(malformed)),"early victory flag rejected");
    malformed=runtime;malformed.battle.motion.phase=peeves::Phase::Patrol;malformed.battle.flight_started=true;malformed.battle.waypoint=999;
    check(!RestoreReturnBattle(restored,SaveReturnBattle(malformed)),"out of range waypoint rejected");
    check(peeves::Throw(runtime.apples[0],{0,1,0},{2,1,0}),"test apple spawned");
    const auto apple_bank=SaveReturnBattle(runtime);
    check(RestoreReturnBattle(restored,apple_bank)&&restored.apples[0].active&&SaveReturnBattle(restored)==apple_bank,"active apple survives checkpoint");
    runtime.apples={};
    const std::array<float,3> player{100,100,100};unsigned victories=0;
    for(unsigned frame=0;frame<4000&&runtime.battle.motion.phase!=peeves::Phase::Complete;++frame){
        if(runtime.battle.motion.phase==peeves::Phase::Taunt)check(peeves::Hit(runtime.battle),"station hit accepted by runtime");
        (void)AdvanceReturnPeeves(runtime,cast,graph,.01F,player);
        const auto bank=SaveReturnBattle(runtime);
        check(RestoreReturnBattle(restored,bank)&&SaveReturnBattle(restored)==bank,"every flight and defeat phase survives reload");
        for(const auto& effect:graph.DrainEffects())if(effect.kind==MapEventKind::cutscene_start&&effect.actor_reference==12)++victories;
        check(cast[0].clips.contains(cast[0].active_clip),"runtime never selects missing animation");
    }
    check(victories==1&&graph.Find(2)->signaled,"four hits dispatch authored victory exactly once");
    check(RestoreReturnBattle(runtime,SaveReturnBattle(runtime)),"completed battle restores");
    (void)AdvanceReturnPeeves(runtime,cast,graph,.1F,player);
    check(graph.DrainEffects().empty(),"restored victory does not signal twice");
    check(!cast[0].enabled&&runtime.battle.motion.phase==peeves::Phase::Complete,"departure hides finished actor");
    check(AddVector(cast[0].base_origin,cast[0].cutscene_offset)==result.departure.station_position,
        "rendered actor ends at authored departure station");
    ReturnRuntime owls;ReturnOwl owl;owl.actor=45;owl.tag="delivery";
    owl.leg.waypoints={{1,2,0},{2,2,0}};owl.departure={{3,2,0},{5,2,0}};owl.pause=.2F;owls.owls={owl};
    CharacterDraw bird;bird.actor_reference=45;bird.base_origin={0,2,0};
    bird.clips["fly"]={0,1,1};bird.clips["drop"]={0,.4F,1};
    std::vector<CharacterDraw> birds{bird};
    check(LaunchReturnOwl(owls,birds,"delivery"),"owl accepts authored trigger");
    unsigned drops=0;
    for(unsigned frame=0;frame<300;++frame){
        AdvanceReturnOwls(owls,birds,.01F);
        if(owls.owls[0].drop_pending){++drops;owls.owls[0].drop_pending=false;}
    }
    check(drops==1&&owls.owls[0].scroll==std::array<float,3>{2,2,0},"owl drops one scroll at its delivery station");
    check(!birds[0].enabled&&!owls.owls[0].flying,"owl leaves and disappears at departure station");
    check(LaunchReturnOwl(owls,birds,"delivery")&&!birds[0].enabled,"duplicate delivery trigger cannot respawn owl");
    std::cout<<"RETURN_SCENE_TESTS=PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
