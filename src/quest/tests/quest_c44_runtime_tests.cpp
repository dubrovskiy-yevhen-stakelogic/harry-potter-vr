#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"

#include <iostream>
#include <fstream>
#include <stdexcept>

using namespace hpvr::quest;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool SameParticles(const std::vector<OriginalSpellParticle>& a,
                   const std::vector<OriginalSpellParticle>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].center != b[i].center || a[i].color != b[i].color ||
            a[i].size != b[i].size || a[i].rotation != b[i].rotation) return false;
    return true;
}

void TestOriginalSpellParticles() {
    const std::array<float, 3> origin{2.0F, 1.5F, -3.0F}, direction{0, 0, -1};
    const auto first = BuildOriginalSpellParticles(origin, direction, .6F, .5F, true, 27);
    Check(!first.empty(), "original spell emits particles during its flight and impact");
    Check(SameParticles(first, BuildOriginalSpellParticles(origin, direction, .6F, .5F, true, 27)),
          "both eyes receive exactly the same deterministic particles");
    Check(!SameParticles(first, BuildOriginalSpellParticles(origin, direction, .6F, .5F, true, 28)),
          "different casts do not repeat the identical particle seed");

    for (const float duration : {0.0F, .03F, .5F, 10.0F, 3600.0F}) {
        for (const float elapsed : {0.0F, .01F, .1F, .5F, .6F, 1.0F, 2.0F, 3.0F, 3599.9F, 3600.0F}) {
            const auto fly = BuildOriginalSpellParticles(origin, direction, elapsed, duration, false, 123);
            const auto hit = BuildOriginalSpellParticles(origin, direction, elapsed, duration, true, 123);
            Check(fly.size() <= 128 && hit.size() <= 178,
                  "original spell stays within the single-batch GPU particle budget");
            Check(hit.size() >= fly.size(), "impact does not remove the existing trail");
            for (const auto& p : hit) {
                for (const float value : p.center) Check(std::isfinite(value), "particle position is finite");
                for (const float value : p.color)
                    Check(std::isfinite(value) && value >= 0 && value <= 1, "particle color and alpha are bounded");
                Check(std::isfinite(p.size) && p.size > 0 && p.size < 2,
                      "original particle size remains finite and bounded");
                Check(std::isfinite(p.rotation), "particle rotation is finite");
            }
        }
    }
    Check(BuildOriginalSpellParticles(origin, direction, 4, .5F, true, 0).empty(),
          "finished impact cannot leave permanent particles");
    for (const float invalid : {-1.0F, 3601.0F, std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::quiet_NaN()}) {
        Check(BuildOriginalSpellParticles(origin, direction, invalid, .5F, true, 0).empty(),
              "invalid or unbounded particle time is rejected before integer conversion");
        Check(BuildOriginalSpellParticles(origin, direction, .6F, invalid, true, 0).empty(),
              "invalid or unbounded flight duration is rejected");
    }
    for (const float invalid : {std::numeric_limits<float>::max(),
                                -std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::quiet_NaN()}) {
        Check(BuildOriginalSpellParticles({invalid, 0, 0}, direction, .6F, .5F, true, 0).empty(),
              "unbounded particle origin is rejected");
        Check(BuildOriginalSpellParticles(origin, {invalid, 0, 0}, .6F, .5F, true, 0).empty(),
              "unbounded particle direction is rejected before multiplication");
    }
}

void Quad(std::vector<GpuVertex>& out, const std::array<float, 3>& a,
          const std::array<float, 3>& b, const std::array<float, 3>& c,
          const std::array<float, 3>& d) {
    for (const auto& p : {a, b, c, a, c, d})
        out.push_back({{p[0], p[1], p[2]}, {0, 0}, {0, 0}, 0, 0, 0, 0});
}

std::vector<CollisionTriangle> Ledge(float height, bool ceiling = false) {
    std::vector<GpuVertex> vertices;
    Quad(vertices, {-2, 0, -3}, {2, 0, -3}, {2, 0, 0}, {-2, 0, 0});
    Quad(vertices, {-2, 0, 0}, {2, 0, 0}, {2, height, 0}, {-2, height, 0});
    Quad(vertices, {-2, height, 0}, {2, height, 0}, {2, height, 3}, {-2, height, 3});
    if (ceiling)
        Quad(vertices, {-2, height + 1.1F, -3}, {2, height + 1.1F, -3},
                       {2, height + 1.1F, 3}, {-2, height + 1.1F, 3});
    return BuildCollisionTriangles(vertices, static_cast<std::uint32_t>(vertices.size()));
}

struct AutonomousMantle {
    const std::vector<CollisionTriangle>* collision = nullptr;
    ClimbMotion climb;
    float seconds = 1.0F / 72;
    unsigned calls = 0;
    std::array<float, 3> last_center{};
};

// CPU integration of LocomotionState's physics-only path with the same pure
// mantle helper and time budget used by QuestScene::ResolvePlayerMovement.
// QuestScene's Android/Vulkan State itself is not instantiated by this test.
bool MantleResolver(void* context, const std::array<float, 3>& current,
                    const std::array<float, 3>& requested, LocomotionMove* output) {
    auto& state = *static_cast<AutonomousMantle*>(context);
    ++state.calls;
    Check(requested == std::array<float, 3>{}, "released stick submits no locomotion displacement");
    if (!StepClimb(*state.collision, state.climb, current,
                   std::clamp(state.seconds, 0.0F, .05F) * 1.6F, output)) return false;
    Check(std::sqrt(DotVector(output->displacement, output->displacement)) <= .0801F,
          "autonomous mantle remains bounded after long frames");
    state.last_center = AddVector(current, output->displacement);
    Check(!OverlapsCollisionWall(*state.collision, state.last_center), "autonomous mantle never crosses the ledge wall");
    return true;
}

void CompleteWithoutStick(const std::vector<CollisionTriangle>& collision,
                          const std::array<float, 3>& current, ClimbMotion climb,
                          float seconds) {
    LocomotionState locomotion;
    ViewPose local_head;
    local_head.position = {0, 1.65F, 0};
    Check(locomotion.ObserveHead(local_head), "valid tracked head initializes locomotion");
    auto head = current;
    head[1] += kPlayerEyeHeightMeters;
    Check(locomotion.RestoreHead(head, 0), "restore keeps the capsule-center convention");
    AutonomousMantle state{&collision, climb, seconds, 0, current};
    LocomotionInput input;
    input.physics_active = true;
    for (unsigned frame = 0; frame < 500 && state.climb.active; ++frame)
        Check(locomotion.Tick(input, seconds, MantleResolver, &state), "physics-only tick advances mantle");
    Check(state.calls > 1 && !state.climb.active, "mantle finishes autonomously instead of waiting for a stick");
    Check(std::sqrt(DotVector(SubtractVector(state.last_center, climb.target),
                             SubtractVector(state.last_center, climb.target))) < .002F,
          "autonomous mantle reaches its captured landing position");
    const unsigned calls = state.calls;
    input.physics_active = false;
    Check(locomotion.Tick(input, seconds, MantleResolver, &state) && state.calls == calls,
          "inactive physics does not accidentally call a completed mantle");
}

void TestAirborneMantle() {
    const auto collision = Ledge(1.4F);
    const std::array<float, 3> start{0, kPlayerCapsuleHalfHeightMeters, -.65F};
    auto current = start;
    JumpMotion jump;
    ClimbMotion climb;
    Check(StartJump(collision, current, jump), "jump starts on the floor");
    bool grabbed = false;
    for (unsigned frame = 0; frame < 80 && jump.active; ++frame) {
        const std::array<float, 3> request{0, 0, .035F};
        LocomotionMove move;
        Check(StepJump(collision, jump, current, request, 1.0F / 72, &move), "airborne jump advances");
        current = AddVector(current, move.displacement);
        if (jump.active && move.blocked_substeps && BeginClimb(collision, current, request, &climb)) {
            Check(current[1] > start[1], "ledge is captured while airborne, not after landing");
            climb.start = jump.safe_origin;
            jump = {};
            grabbed = true;
            break;
        }
    }
    Check(grabbed && climb.active && !jump.active, "airborne wall contact transfers from jump into mantle");
    auto captured_head = current;
    captured_head[1] += kPlayerEyeHeightMeters;
    auto safe_head = start;
    safe_head[1] += kPlayerEyeHeightMeters;
    Check(SafeCheckpointHead(captured_head, climb, jump) == safe_head,
          "checkpoint during airborne mantle retains the last safe ground");
    CompleteWithoutStick(collision, current, climb, 1.0F / 72);
    CompleteWithoutStick(collision, current, climb, 1.0F / 120);
    CompleteWithoutStick(collision, current, climb, .25F);

    ClimbMotion blocked;
    const std::array<float,3> almost_above{0,kPlayerCapsuleHalfHeightMeters+1.15F,-.31F};
    Check(BeginClimb(collision,almost_above,{0,0,.035F},&blocked,.04F),
          "airborne feet close to ledge still acquire a mantle");
    Check(!BeginClimb(Ledge(1.4F, true), current, {0, 0, .035F}, &blocked),
          "mantle acquisition rejects a ceiling over its landing");
    Check(!BeginClimb(collision, current, {}, &blocked), "a zero direction cannot acquire a new ledge");
    Check(!BeginClimb(collision,{0,kPlayerCapsuleHalfHeightMeters,-.8F},{0,0,.035F},&blocked),
          "a distant platform cannot pull the player across a gap");
    std::vector<GpuVertex> slab;
    Quad(slab,{-2,1.4F,0},{2,1.4F,0},{2,1.4F,3},{-2,1.4F,3});
    Quad(slab,{-2,2.7F,0},{2,2.7F,0},{2,2.7F,3},{-2,2.7F,3});
    Quad(slab,{-2,1.4F,0},{2,1.4F,0},{2,2.7F,0},{-2,2.7F,0});
    const auto thick=BuildCollisionTriangles(slab,static_cast<std::uint32_t>(slab.size()));
    const std::array<float,3> below_top{0,.5F+kPlayerCapsuleHalfHeightMeters,-.31F};
    Check(BeginClimb(thick,below_top,{0,0,.035F},&blocked)&&
          std::abs(blocked.target[1]-kPlayerCapsuleHalfHeightMeters-2.7F)<.001F,
          "blocked underside does not hide the clear top of a thick platform");
    CompleteWithoutStick(thick,below_top,blocked,1.F/90);
}

void TestInterruptedMantleCheckpoint() {
    hpvr::wand::Hp1ActorVisualCensus census;census.status=hpvr::wand::Hp1ProfileStatus::ok;
    hpvr::wand::Hp1ActorVisual trigger;trigger.actor_reference=1;
    trigger.qualified_class_name="Engine.Trigger";trigger.object_name="Entry";trigger.event="Gate";
    hpvr::wand::Hp1ClassDefaultProperty once;once.name="bTriggerOnceOnly";
    once.boolean_value_serialized=true;once.boolean_value=true;trigger.serialized_properties.push_back(once);
    census.actors.push_back(trigger);
    hpvr::wand::Hp1ActorVisual gate;gate.actor_reference=2;gate.qualified_class_name="Engine.Mover";
    gate.object_name="Gate";gate.tag="Gate";gate.initial_state="TriggerToggle";census.actors.push_back(gate);
    MapEventGraph graph;Check(graph.Load(census),"interrupted mantle graph loads");
    movers::Motion door;door.seconds=2;door.keys[1].offset_unreal[2]=-208;
    const auto checkpoint_graph=graph.Serialize(),checkpoint_door=movers::SaveMotion(door);
    const std::array<float,3> checkpoint_head{-18,.7F,81},current{0,1.5F,0};
    std::vector<GpuVertex> intruding_column;
    Quad(intruding_column,{-2,0,.02F},{2,0,.02F},{2,4,.02F},{-2,4,.02F});
    const auto blocked=BuildCollisionTriangles(intruding_column,static_cast<std::uint32_t>(intruding_column.size()));
    for(unsigned cycle=0;cycle<3;++cycle){
        Check(graph.Touch(1),"entry trigger remains usable after each checkpoint restore");
        const auto effects=graph.DrainEffects();
        Check(effects.size()==1&&effects[0].actor_reference==2&&effects[0].kind==MapEventKind::mover_trigger,
              "entry emits one closing door action");
        Check(movers::Start(door,true)&&movers::Advance(door,3).finished&&graph.Signal(2),"one-shot entry closes door");
        (void)graph.DrainEffects();
        auto safe=checkpoint_head;safe[1]-=kPlayerEyeHeightMeters;
        JumpMotion fall{true,0,0,safe};
        ClimbMotion climb{true,current,AddVector(current,{0,1,0}),AddVector(current,{0,1,.7F})};
        climb.start=fall.safe_origin;fall={};
        LocomotionMove move;auto unobstructed=climb;
        Check(StepClimb({},unobstructed,current,.02F,&move)&&unobstructed.active,
              "same captured mantle progresses before the column intrudes");
        Check(!StepClimb(blocked,climb,current,.02F,&move)&&!climb.active,
              "moving column can invalidate an already captured mantle");
        auto former_teleport=climb.start;former_teleport[1]+=kPlayerEyeHeightMeters;
        Check(former_teleport==checkpoint_head&&door.pose.offset_unreal[2]==-208&&graph.Find(1)->consumed,
              "player-only fallback reproduces return behind a closed consumed gate");
        // BeginChallengeDeath clears active movement. Its normal restore adopts
        // the same checkpoint's graph, physical movers and player together.
        climb={};fall={};
        Check(graph.Restore(checkpoint_graph)&&movers::RestoreMotion(door,checkpoint_door),"complete checkpoint restores world");
        const auto restored_head=checkpoint_head;
        Check(restored_head==former_teleport&&door.pose.offset_unreal[2]==0&&!graph.Find(1)->consumed&&
              !climb.active&&!fall.active,"checkpoint returns behind an open gate without restarting failed mantle");
    }
}

void TestInterruptedMantleSceneWiring() {
    const auto directory=std::filesystem::path(__FILE__).parent_path()/"../../../android/app/src/main/cpp";
    const auto read=[](const auto& path){std::ifstream input(path);Check(input.good(),"recovery source available");
        return std::string{std::istreambuf_iterator<char>(input),{}};};
    const auto scene=read(directory/"quest_scene.cpp");
    const auto resolver=scene.find("bool QuestScene::ResolvePlayerMovement(");
    const auto failed=scene.find("if(state.climb.active){",resolver);
    const auto legacy=scene.find("state.placement=state.frontend.progress;state.placement.player=state.climb.start;",failed);
    Check(resolver!=std::string::npos&&failed!=std::string::npos&&legacy!=std::string::npos,"mantle fallback located");
    const auto branch=scene.substr(failed,legacy-failed);
    Check(branch.find("if(StepClimb(")<branch.find("if(IsWalkingSpellMap(state.map_id))")&&
          branch.find("if(IsWalkingSpellMap(state.map_id)){BeginChallengeDeath(\"MANTLE_INTERRUPTED\");*output={};return true;}")!=std::string::npos,
          "failed challenge mantle enters world recovery and returns before player-only placement");
    const auto runtime=read(directory/"quest_challenge_runtime.inl");
    const auto death=runtime.find("void QuestScene::BeginChallengeDeath(");
    const auto advance=runtime.find("void QuestScene::AdvanceChallengeDeath(",death);
    const auto begin=runtime.substr(death,advance-death);
    Check(begin.find("s.death_checkpoint=std::move(checkpoint)")!=std::string::npos&&
          begin.find("s.jump={};s.climb={};s.jump_pending=false;")!=std::string::npos,
          "world recovery captures checkpoint and clears interrupted movement");
    Check(scene.substr(resolver,failed-resolver).find("if(state.death_time>=0){*output={};return true;}")!=std::string::npos&&
          runtime.find("RestoreTransferredProgress(checkpoint,slot);",advance)!=std::string::npos,
          "subsequent physics frames wait for the normal whole-world checkpoint restore");
}

void TestRepeatedVoiceCooldown() {
    VoiceRepeatCooldown repeat;
    for(unsigned cast=0;cast<24;++cast){
        repeat.Begin();
        for(unsigned frame=0;frame<144;++frame)
            Check(!repeat.Advance(true,true,true,false,1.F/72),"incantation never rearms microphone");
        Check(!repeat.Advance(true,true,false,true,100),"projectile cannot skip speaker tail");
        Check(!repeat.Advance(true,true,false,false,std::numeric_limits<float>::quiet_NaN()),"invalid time cannot rearm");
        for(unsigned frame=0;frame<24;++frame)
            Check(!repeat.Advance(true,true,false,false,1.F/72),"speaker echo tail stays suppressed");
        Check(repeat.Advance(true,true,false,false,.05F),"held trigger rearms without a release");
        Check(!repeat.Advance(true,true,false,false,.05F),"cooldown produces one rearm event only");
    }
    repeat.Begin();Check(!repeat.Advance(false,true,false,false,.05F)&&!repeat.pending,"focus/menu loss cancels rearm");
    repeat.Begin();Check(!repeat.Advance(true,false,false,false,.05F)&&!repeat.pending,"release cancels old rearm");
}
void TestBasicCastCapture() {
    BasicCast cast;
    const std::array<float, 3> tip{1, 2, 3}, first_hit{4, 5, 6}, last_hit{7, 8, 9};
    Check(!cast.Observe(true, true, tip, first_hit, .01F) && !cast.charging,
          "entering gameplay with a held trigger cannot cast accidentally");
    Check(!cast.Observe(true, false, tip, first_hit, .01F), "trigger release arms the neutral cast");
    Check(!cast.Observe(true, true, tip, first_hit, .01F) && cast.charging,
          "fresh trigger press starts the original aiming phase");
    Check(!cast.Observe(true, true, tip, last_hit, .01F) && cast.aim == last_hit,
          "original neutral cast can aim freely before release");
    Check(cast.Observe(true, false, tip, first_hit, .01F) && cast.flying,
          "one trigger release launches exactly one neutral spell");
    Check(cast.origin == tip && cast.destination == last_hit,
          "release captures the wand origin and last aimed hit");
    Check(!cast.Observe(true, false, {}, {}, .01F) && cast.destination == last_hit,
          "flying projectile does not retarget when the wand moves");
    Check(!cast.Observe(false, true, tip, first_hit, .01F) && !cast.flying && cast.require_release,
          "leaving gameplay cancels the cast and requires a fresh release");
}
void TestWalkWithPlatformCarry(){
    LocomotionState player;ViewPose local{};local.position={0,1.6F,0};
    Check(player.ObserveHead(local),"platform test has tracked head");
    ViewPose before{};Check(player.MapPose(local,&before),"initial platform pose");
    LocomotionInput input{};input.move_active=true;input.move_y=1;
    auto expected=before.position;
    for(unsigned frame=0;frame<240;++frame){
        ViewPose pre{};Check(player.MapPose(local,&pre),"pre-walk head");
        Check(player.Tick(input,1.0F/72),"walking frame accepted");
        ViewPose walked{};Check(player.MapPose(local,&walked),"post-walk head sampled before platform");
        const auto step=SubtractVector(walked.position,pre.position);
        Check(std::hypot(step[0],step[2])>.001F,"walking contributes displacement every frame");
        const std::array<float,3> transport{.002F,.001F,0};
        // Match XR ordering: observe post-Tick head, then apply platform delta.
        expected=AddVector(expected,AddVector(step,transport));
        Check(player.TranslateWorld(transport),"platform carry applied");
        ViewPose actual{};Check(player.MapPose(local,&actual),"carried head readable");
        for(unsigned axis=0;axis<3;++axis)Check(std::abs(actual.position[axis]-expected[axis])<.0001F,
            "platform transport preserves accumulated player walking");
    }
}
void TestTurnWithPlatformCarry(){
    LocomotionState player;ViewPose local{};local.position={.15F,1.6F,-.1F};
    Check(player.ObserveHead(local),"turn test has tracked head");
    LocomotionInput input{};
    const auto frame=[&](){
        Check(player.Tick(input,1.0F/72),"platform turn frame");
        Check(player.TranslateWorld({.002F,.001F,0}),"carry does not reset input");
    };
    frame();input.turn_active=true;input.turn_x=1;frame();
    Check(player.snap_turns()==1,"right stick turns while riding");
    for(unsigned i=0;i<120;++i)frame();
    Check(player.snap_turns()==1,"held stick does not repeat during carry");
    input.turn_x=0;frame();input.turn_x=-1;frame();
    Check(player.snap_turns()==2,"neutral re-arms opposite turn during carry");
    ViewPose head{};Check(player.MapPose(local,&head),"head before actual teleport");
    Check(player.RestoreHead(head.position,player.yaw_radians()),"actual teleport still supported");
    frame();Check(player.snap_turns()==2,"actual teleport preserves held-stick safety");
    input.turn_x=0;frame();input.turn_x=1;frame();
    Check(player.snap_turns()==3,"turn works after teleport release");
}

struct PlatformPhysics {
    std::vector<CollisionTriangle> collision;
    std::array<float,3> observed{};
    bool falling=false;
};
bool PlatformResolver(void* context,const std::array<float,3>& center,
                      const std::array<float,3>& requested,LocomotionMove* output){
    auto& state=*static_cast<PlatformPhysics*>(context);state.observed=center;
    float floor=0;
    state.falling=!FindCollisionGroundHeight(state.collision,center[0]+requested[0],center[2]+requested[2],
        center[1]-kPlayerCapsuleHalfHeightMeters,&floor);
    return ResolveCollisionMovement(state.collision,center,requested,output);
}
void TestPhysicalPlatformSupport(){
    for(bool restored:{false,true}){
        LocomotionState player;ViewPose local{};local.position={.1F,1.6F,-.1F};
        Check(player.ObserveHead(local),"platform physical head initialized");
        if(restored)Check(player.RestoreHead({.1F,kPlayerEyeHeightMeters+kPlayerCapsuleHalfHeightMeters,-.1F},0),
            "platform restored body initialized");
        std::array<float,3> platform{0,player.CapsuleCenter()[1]-kPlayerCapsuleHalfHeightMeters,0};
        unsigned old_probe_misses=0;
        for(unsigned frame=0;frame<480;++frame){
            // Room-scale height motion changes the view, never the feet.
            local.position[1]=1.6F+.35F*std::sin(float(frame)*.11F);
            Check(player.ObserveHead(local),"moving physical head accepted");
            std::vector<GpuVertex> vertices;
            Quad(vertices,AddVector(platform,{-1,0,-1}),AddVector(platform,{1,0,-1}),
                AddVector(platform,{1,0,1}),AddVector(platform,{-1,0,1}));
            PlatformPhysics physics{BuildCollisionTriangles(vertices,static_cast<std::uint32_t>(vertices.size()))};
            LocomotionInput input{};input.physics_active=true;input.turn_active=true;
            input.turn_x=frame%80==20?1.0F:0.0F;
            const auto physical_before=player.CapsuleCenter();
            Check(player.Tick(input,1.0F/72,PlatformResolver,&physics),"platform physics tick succeeds");
            Check(!physics.falling,"idle descending platform never starts a false fall");
            for(unsigned axis=0;axis<3;++axis)Check(std::abs(physics.observed[axis]-physical_before[axis])<.00001F,
                "support and collision share the canonical capsule");
            const auto body=player.CapsuleCenter();bool supported=false;
            for(const auto& triangle:physics.collision){float floor=0;
                if(CollisionTriangleSupportHeightAtXZ(triangle,body[0],body[2],&floor)&&
                   std::abs(floor-(body[1]-kPlayerCapsuleHalfHeightMeters))<.06F)supported=true;
            }
            Check(supported,"platform support survives standing/crouching and right-stick turning");
            ViewPose head{};Check(player.MapPose(local,&head),"platform view readable");
            if(std::abs(head.position[1]-kPlayerEyeHeightMeters-kPlayerCapsuleHalfHeightMeters-platform[1])>=.06F)
                ++old_probe_misses;
            const std::array<float,3> carry{.002F,frame<240?-.015F:.015F,0};
            Check(player.TranslateWorld(carry),"idle player carried horizontally and vertically");
            platform=AddVector(platform,carry);
            const auto after=player.CapsuleCenter();
            for(unsigned axis=0;axis<3;++axis)Check(std::abs(after[axis]-body[axis]-carry[axis])<.00001F,
                "carry shifts physical capsule without reanchoring");
        }
        Check(old_probe_misses>100,"regression fixture reproduces the former head-derived support failure");
        Check(player.snap_turns()==6,"right-stick snap turns remain free during carry");
    }
    std::vector<GpuVertex> vertices;Quad(vertices,{-1,0,-1},{1,0,-1},{1,0,1},{-1,0,1});
    const auto collision=BuildCollisionTriangles(vertices,static_cast<std::uint32_t>(vertices.size()));
    float expected=0;const float x=1+kPlayerCapsuleRadiusMeters*.5F;
    Check(FindCollisionGroundHeight(collision,x,0,0,&expected),"capsule edge remains supported by normal physics");
    bool supported=false;
    for(const auto& triangle:collision){float floor=0;
        if(CollisionTriangleSupportHeightAtXZ(triangle,x,0,&floor)&&std::abs(floor-expected)<.00001F)supported=true;
    }
    Check(supported,"mover and static support use the same capsule edge envelope");
}
} // namespace

void TestCoverWaterAndExit(){
    std::vector<GpuVertex> vertices;
    Quad(vertices,{-2,-2,-2},{2,-2,-2},{2,2,-2},{-2,2,-2});
    auto collision=BuildCollisionTriangles(vertices,static_cast<unsigned>(vertices.size()));
    DoorDraw column;column.grid=true;column.actor_reference=10;column.collision_first=0;column.collision_count=collision.size();
    std::vector<DoorDraw> columns{column};
    Check(!SpellTargetExposed(collision,columns,20,{0,0,0},{0,0,-3}),"hidden symbol centre stays blocked even when its trigger protrudes");
    Check(SpellTargetExposed(collision,columns,10,{0,0,0},{0,0,-3}),"cover itself remains a valid Flipendo target");
    for(auto& triangle:collision)for(auto& p:triangle.vertices)p[0]+=8;
    for(auto& triangle:collision){triangle.minimum[0]+=8;triangle.maximum[0]+=8;}
    Check(SpellTargetExposed(collision,columns,20,{0,0,0},{0,0,-3}),"moving the column exposes the original target");
    std::vector<GpuVertex> water(3);
    water[1].position[0]=16;water[2].position[2]=16;
    water[1].texture_uv[0]=1;water[2].texture_uv[1]=1;
    water[1].lightmap_uv[0]=1;water[2].lightmap_uv[1]=1;
    MirrorSurface pool;pool.normal={0,1,0};pool.ranges={{0,3}};
    std::vector<MirrorSurface> pools{pool};AppendWaterSurfaceGeometry(pools,water);
    Check(pools[0].water_draw.second==768,"water tessellation keeps the expected triangle count");
    for(std::size_t i=3;i<water.size();++i){const auto& v=water[i];
        Check(std::abs(v.texture_uv[0]-v.position[0]/16)<1e-6F&&std::abs(v.texture_uv[1]-v.position[2]/16)<1e-6F,
            "water surface interpolates original UVs across every new triangle");
        Check(v.texture_uv[0]==v.lightmap_uv[0]&&v.texture_uv[1]==v.lightmap_uv[1],"water lightmap interpolation follows geometry");
    }
    DoorDraw door;door.tag="aloroom2";door.motion.count=2;door.motion.keys[1].offset_unreal[0]=100;
    door.open_seconds=.2F;door.close_seconds=.2F;std::vector<DoorDraw> doors{door};
    Check(OpenCharmsCutsceneDoors(1878,doors)>0&&doors[0].cutscene_hold,"professor return opens the classroom before crossing");
    for(unsigned i=0;i<20;++i)(void)movers::Advance(doors[0].motion,.05F);
    Check(CloseCharmsDoors(doors,"aloroom2",true)>0&&!doors[0].opening&&doors[0].cutscene_hold,
        "return closes explicitly while suppressing duplicate proximity toggles");
    for(unsigned i=0;i<20;++i)(void)movers::Advance(doors[0].motion,.05F);
    Check(CloseCharmsDoors(doors,"aloroom2",true)==0&&doors[0].motion.current==0,"control resumes only after the door is closed");
    const auto source=std::filesystem::path(__FILE__).parent_path()/"../../../android/app/src/main/cpp/quest_scene.cpp";
    std::ifstream input(source);const std::string text{std::istreambuf_iterator<char>(input),{}};
    Check(text.find("const bool capture_target=manual;")!=std::string::npos&&text.find("if(!manual&&active&&aiming)state.basic_cast.aim=aim;")!=std::string::npos,
        "classic keeps current aim through release even with voice enabled");
}
int main() {
    try {
        TestCoverWaterAndExit();
        TestAirborneMantle();
        TestInterruptedMantleCheckpoint();
        TestInterruptedMantleSceneWiring();
        TestBasicCastCapture();
        TestRepeatedVoiceCooldown();
        TestOriginalSpellParticles();
        TestWalkWithPlatformCarry();
        TestTurnWithPlatformCarry();
        TestPhysicalPlatformSupport();
        std::cout << "C44_RUNTIME_TESTS=PASS particles=BOUNDED mantle=AIRBORNE_AUTONOMOUS ceiling=BLOCKED cast=CAPTURED\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
