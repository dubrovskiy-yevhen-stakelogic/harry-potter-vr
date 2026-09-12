#include "hpvr/quest_view.h"
#include "hpvr/quest_recenter.h"
#include "hpvr/quest_reflection_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void Expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "quest view test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool Near(const float left, const float right) {
    return std::abs(left - right) < 1.0e-4F;
}

std::array<float, 4> Transform(const hpvr::quest::Matrix4& matrix,
                               const std::array<float, 4>& point) {
    std::array<float, 4> result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            result[row] += matrix[column * 4 + row] * point[column];
        }
    }
    return result;
}

void ExpectPosition(const std::array<float,3>& actual, const std::array<float,3>& expected,
                    const char* message) {
    for (unsigned axis = 0; axis < 3; ++axis) Expect(Near(actual[axis], expected[axis]), message);
}

void TestCapsuleRecenter() {
    using namespace hpvr::quest;
    constexpr float eye_height = .815F;
    LocomotionState player(eye_height);
    const std::array body{4.F, 2.F, -7.F};
    const auto initial_translation = player.translation();
    Expect(!player.RecenterToCapsule(body, .7F) && player.translation() == initial_translation,
           "recenter before an observed head fails atomically");
    ViewPose head{{.2F, 1.6F, -.3F}};
    Expect(player.ObserveHead(head) && player.RecenterToCapsule(body, .7F), "initial capsule recenter");

    LocomotionInput movement;
    movement.move_active = true; movement.move_y = 1; movement.turn_active = true; movement.turn_x = 1;
    const auto resolver = [](void*, const std::array<float,3>&, const std::array<float,3>& requested,
                             LocomotionMove* result) {
        *result = {requested, 2, 3}; result->displacement[1] = .1F; return true;
    };
    Expect(player.Tick(movement, .02F, resolver), "prime movement, snap and collision counters");
    const auto preserved = player.CapsuleCenter();
    const auto move_frames = player.move_frames(), snap_turns = player.snap_turns();
    const auto blocked = player.blocked_substeps(), grounded = player.grounded_substeps();
    const auto vertical = player.vertical_adjustment_m();
    const auto yaw = player.yaw_radians();
    head.position[1] = .7F;
    Expect(player.ObserveHead(head), "lower head before tracking recovers");
    ExpectPosition(player.CapsuleCenter(), preserved, "crouching never changes physical capsule height");
    Expect(player.RecenterToCapsule(preserved, yaw), "recenter while crouching");
    ExpectPosition(player.CapsuleCenter(), preserved, "crouched resume preserves complete capsule XYZ");
    ViewPose world;
    Expect(player.MapPose(head, &world), "map recentered head");
    ExpectPosition(world.position, {preserved[0], preserved[1] + eye_height, preserved[2]},
                   "observed head is rebased to body plus configured eye height");
    Expect(player.move_frames() == move_frames && player.snap_turns() == snap_turns &&
           player.blocked_substeps() == blocked && player.grounded_substeps() == grounded &&
           player.vertical_adjustment_m() == vertical, "recenter preserves all movement counters");
    LocomotionInput held_turn; held_turn.turn_active = true; held_turn.turn_x = 1;
    Expect(player.Tick(held_turn, .02F) && player.snap_turns() == snap_turns,
           "recenter does not retrigger a held snap-turn stick");

    for (unsigned attempt = 0; attempt < 30; ++attempt) {
        // Simulate a new LOCAL tracking origin, including seated/crouched height.
        head.position = {float(attempt) * .21F - 3.F, .3F + float(attempt % 5) * .27F,
                         2.F - float(attempt) * .14F};
        const float new_yaw = -.7F + float(attempt) * .11F;
        Expect(player.ObserveHead(head) && player.RecenterToCapsule(preserved, new_yaw), "repeated tracking-origin rebase");
        ExpectPosition(player.CapsuleCenter(), preserved, "repeated rebases never accumulate body drop");
        Expect(player.MapPose(head, &world), "repeated rebase maps head");
        ExpectPosition(world.position, {preserved[0], preserved[1] + eye_height, preserved[2]},
                       "new local XYZ origin maps to unchanged world capsule");
        Expect(Near(player.yaw_radians(), new_yaw), "supplied world yaw retained");
    }
    head.position[1] -= .25F;
    Expect(player.ObserveHead(head) && player.MapPose(head, &world), "crouch after recenter");
    ExpectPosition(player.CapsuleCenter(), preserved, "new reference retains room-scale crouch without body drop");
    Expect(Near(world.position[1], preserved[1] + eye_height - .25F), "physical crouch remains visible after rebase");
    const std::array transport{.4F, .15F, -.3F};
    Expect(player.TranslateWorld(transport), "moving platform transport before tracking loss");
    const auto transported = player.CapsuleCenter();
    ExpectPosition(transported, {preserved[0] + transport[0], preserved[1] + transport[1], preserved[2] + transport[2]},
                   "platform moves physical capsule in all axes");
    head.position = {-.8F, .2F, 1.4F};
    Expect(player.ObserveHead(head) && player.RecenterToCapsule(transported, yaw), "resume after platform transport");
    ExpectPosition(player.CapsuleCenter(), transported, "recenter preserves transported capsule rather than old head");
    LocomotionState recreated(eye_height);
    Expect(recreated.ObserveHead(head) && recreated.RecenterToCapsule(transported, yaw),
           "new session can rebase to saved platform-transported body");
    ExpectPosition(recreated.CapsuleCenter(), transported, "session recreation does not lose capsule placement");

    const auto unchanged = player;
    const auto check_unchanged = [&]() {
        Expect(player.translation() == unchanged.translation() && player.CapsuleCenter() == unchanged.CapsuleCenter() &&
               player.yaw_radians() == unchanged.yaw_radians() && player.move_frames() == unchanged.move_frames() &&
               player.snap_turns() == unchanged.snap_turns() && player.blocked_substeps() == unchanged.blocked_substeps() &&
               player.grounded_substeps() == unchanged.grounded_substeps() &&
               player.vertical_adjustment_m() == unchanged.vertical_adjustment_m(), "invalid rebase leaves all state unchanged");
    };
    for (unsigned axis = 0; axis < 3; ++axis) for (const float bad : {
         std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 2000.F}) {
        auto invalid = transported; invalid[axis] = bad;
        Expect(!player.RecenterToCapsule(invalid, 1.F), "malformed capsule rejected"); check_unchanged();
    }
    Expect(!player.RecenterToCapsule(transported, std::numeric_limits<float>::quiet_NaN()), "invalid yaw rejected");
    check_unchanged();
    LocomotionState overflow;
    head.position = {std::numeric_limits<float>::max(), 1.F, std::numeric_limits<float>::max()};
    Expect(overflow.ObserveHead(head), "finite extreme local pose for overflow validation");
    const auto before_overflow = overflow.translation();
    Expect(!overflow.RecenterToCapsule(body, .785398163F) && overflow.translation() == before_overflow &&
           overflow.yaw_radians() == 0, "computed overflow rejected before committing any transform");
}

void TestRecenterChord() {
    using hpvr::quest::RecenterChord;
    RecenterChord chord;
    Expect(!chord.Update(true, true, true, true) && chord.consumed, "held startup chord cannot fire");
    Expect(!chord.Update(true, true, false, true) && chord.consumed, "one release cannot arm startup chord");
    Expect(!chord.Update(true, true, false, false) && !chord.consumed, "neutral arms chord");
    Expect(!chord.Update(true, true, true, false) && !chord.consumed, "single click alone remains available");
    Expect(chord.Update(true, true, true, true) && chord.consumed, "second click fires chord once");
    for (unsigned held = 0; held < 5; ++held)
        Expect(!chord.Update(true, true, true, true) && chord.consumed, "held chord never repeats");
    Expect(!chord.Update(true, true, false, true) && chord.consumed, "right click consumed until both released");
    Expect(!chord.Update(true, true, true, false) && chord.consumed, "alternating releases do not rearm");
    Expect(!chord.Update(true, true, true, true) && chord.consumed, "repressing one side still cannot repeat");
    Expect(!chord.Update(true, true, false, false) && !chord.consumed, "both released rearms exactly once");
    Expect(chord.Update(true, true, true, true), "fresh full chord accepted");
    for (bool lose_focus : {false, true}) {
        Expect(!chord.Update(!lose_focus, lose_focus, false, false), "focus or tracking loss cancels armed epoch");
        Expect(!chord.Update(true, true, true, true) && chord.consumed, "resume with held inputs cannot recenter");
        Expect(!chord.Update(true, true, false, false), "fresh tracked focused neutral required");
        Expect(chord.Update(true, true, true, true), "fresh chord works after focus or tracking recovery");
    }
    chord.Reset();
    Expect(!chord.Update(true, true, true, true) && chord.consumed, "session reset requires neutral again");
}

void TestSmoothTurning() {
    using namespace hpvr::quest;
    constexpr float pi=3.14159265358979323846F;
    ViewPose head{{.4F,1.6F,-.25F}};
    for(int speed:{30,60,90,120,150,180})for(unsigned fps:{20U,30U,60U,72U,80U,90U,120U}){
        LocomotionState player;
        Expect(player.ObserveHead(head),"smooth turn has tracked head");
        ViewPose before,after;Expect(player.MapPose(head,&before),"map head before smooth turn");
        const auto capsule=player.CapsuleCenter();
        LocomotionInput input;input.turn_active=true;input.turn_x=1;input.smooth_turn=true;
        input.smooth_turn_degrees=static_cast<float>(speed);
        for(unsigned frame=0;frame<fps;++frame)Expect(player.Tick(input,1.0F/static_cast<float>(fps)),"smooth turn frame accepted");
        Expect(std::abs(player.yaw_radians()-(2*pi-float(speed)*pi/180))<.00015F,
               "smooth turn uses seconds rather than frames at every supported refresh rate");
        Expect(player.MapPose(head,&after),"map head after smooth turn");
        ExpectPosition(after.position,before.position,"smooth turn pivots around the head without orbiting");
        ExpectPosition(player.CapsuleCenter(),capsule,"smooth turn does not translate the physical capsule");
        Expect(player.snap_turns()==0,"smooth frames are not counted as snap turns");
    }
    for(float axis:{-1.F,-.625F,0.F,.25F,.625F,1.F,4.F}){
        LocomotionState player;Expect(player.ObserveHead(head),"analog turn tracked head");
        LocomotionInput input;input.turn_active=true;input.turn_x=axis;input.smooth_turn=true;
        Expect(player.Tick(input,.05F),"analog smooth turn accepted");
        const float strength=std::clamp((std::abs(axis)-.25F)/.75F,0.F,1.F);
        const float rotation=-(axis>0?1.F:-1.F)*strength*90.F*pi/180.F*.05F;
        const float expected=rotation<0?2*pi+rotation:rotation;
        Expect(Near(player.yaw_radians(),expected),"analog deadzone, direction and clamping are continuous");
    }
    LocomotionState player;Expect(player.ObserveHead(head),"smooth lifecycle tracked head");
    LocomotionInput input;input.turn_active=true;input.turn_x=1;input.smooth_turn=true;
    const auto initial=player.translation();
    Expect(player.Tick(input,0)&&player.yaw_radians()==0&&player.translation()==initial,"zero time cannot turn");
    input.turn_active=false;
    Expect(player.Tick(input,.05F)&&player.yaw_radians()==0,"menu, cutscene or tracking suppression cannot turn");
    input.turn_active=true;
    Expect(player.Tick(input,.02F),"smooth turn resumes when input active");
    const auto yaw=player.yaw_radians();const auto translation=player.translation();
    for(float bad:{-1.F,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        Expect(!player.Tick(input,bad)&&player.yaw_radians()==yaw&&player.translation()==translation,
               "invalid time rejects atomically without a turn");
    }
    for(float speed:{0.F,29.F,181.F,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        input.smooth_turn_degrees=speed;
        Expect(!player.Tick(input,.02F)&&player.yaw_radians()==yaw&&player.translation()==translation,
               "invalid turning speed rejects atomically");
    }
    input.smooth_turn_degrees=90;
    LocomotionState limited=player;
    Expect(player.Tick(input,5.F)&&limited.Tick(input,.05F),"stalled frame rotation is bounded");
    Expect(Near(player.yaw_radians(),limited.yaw_radians()),"long frames cannot cause an unexpected large turn");
    const auto before_snap=player.yaw_radians();
    input.smooth_turn=false;
    Expect(player.Tick(input,.02F)&&Near(player.yaw_radians(),before_snap)&&player.snap_turns()==0,
           "switching smooth to snap while held cannot add a snap");
    input.turn_x=0;Expect(player.Tick(input,.02F),"neutral rearms snap after switching mode");
    input.turn_x=1;Expect(player.Tick(input,.02F)&&player.snap_turns()==1,"fresh stick gets original snap after live switch");
    const auto snapped=player.yaw_radians();
    Expect(player.Tick(input,.02F)&&Near(player.yaw_radians(),snapped)&&player.snap_turns()==1,"held snap still does not repeat");
    input.smooth_turn=true;
    Expect(player.Tick(input,.02F)&&!Near(player.yaw_radians(),snapped),"switching back to smooth works immediately");
    LocomotionState untracked;
    Expect(untracked.Tick(input,.02F)&&untracked.yaw_radians()==0,"smooth turning without a tracked head cannot move world");
}

void TestEyeRelativeSkyProjection() {
    using namespace hpvr::quest;
    const std::array<ViewFov,2> fovs{{{-.9F,.68F,-.76F,.83F},{-.68F,.9F,-.76F,.83F}}};
    for(const auto position:std::array<std::array<float,3>,3>{{{0,1.6F,0},{12,8,-21},{-70,35,81}}})
    for(const auto look:std::array<std::array<float,3>,3>{{{0,0,-1},{1,.5F,-1},{-.4F,-.7F,1}}}){
        ViewPose head{position};
        const std::array target{position[0]+look[0],position[1]+look[1],position[2]+look[2]};
        Expect(BuildLookOrientation(position,target,&head.orientation),"sky test look direction");
        Matrix4 head_model;Expect(BuildRigidTransform(head,&head_model),"sky test head basis");
        for(unsigned eye=0;eye<2;++eye){
            ViewPose pose=head;
            const float offset=eye?.032F:-.032F;
            for(unsigned axis=0;axis<3;++axis)pose.position[axis]+=head_model[axis]*offset;
            Matrix4 vp,inverse;
            Expect(BuildViewProjection(pose,fovs[eye],0,.05F,250,&vp)&&InvertReflectionMatrix(vp,inverse),
                   "asymmetric stereo sky projection has inverse");
            Expect(std::abs(inverse[11])>1e-6F,"perspective camera extraction has finite divisor");
            const std::array origin{inverse[8]/inverse[11],inverse[9]/inverse[11],inverse[10]/inverse[11]};
            for(unsigned axis=0;axis<3;++axis)Expect(std::abs(origin[axis]-pose.position[axis])<.0001F,
                "inverse VP column two recovers each eye, not a shared mono origin");
            for(const auto local:std::array<std::array<float,4>,3>{{{2,1,-15,0},{-3,4,-20,0},{0,0,15,0}}}){
                const auto direction=Transform(head_model,local);
                const auto expected=Transform(vp,{direction[0],direction[1],direction[2],0});
                auto actual=Transform(vp,{direction[0]+origin[0],direction[1]+origin[1],direction[2]+origin[2],1});
                for(unsigned axis:{0U,1U,3U})Expect(std::abs(expected[axis]-actual[axis])<.00015F,
                    "eye-relative sky cancels translation while preserving perspective orientation");
                actual[2]=actual[3]*.99999F;
                if(local[2]<0){
                    Expect(actual[3]>0&&actual[2]>0&&actual[2]<actual[3],"front-facing sky remains inside Vulkan depth clipping");
                    Expect(Near(actual[2]/actual[3],.99999F),"sky is pinned near far depth, independent of cube size");
                    const auto foreground_direction=Transform(head_model,{0,0,-10,0});
                    const auto foreground=Transform(vp,{foreground_direction[0]+origin[0],foreground_direction[1]+origin[1],
                        foreground_direction[2]+origin[2],1});
                    Expect(foreground[2]/foreground[3]<actual[2]/actual[3],"courtyard foreground occludes sky");
                }else Expect(actual[3]<0&&actual[2]<0,"behind-eye sky retains the correct homogeneous clipping sign");
            }
        }
    }
}

}  // namespace

int main() {
    using hpvr::quest::BuildViewProjection;
    using hpvr::quest::Matrix4;
    using hpvr::quest::ViewFov;
    using hpvr::quest::ViewPose;

    constexpr float kQuarterTurn = 0.7853981633974483F;
    constexpr float kEyeHeight = 0.815F;
    constexpr ViewFov symmetric_fov{-kQuarterTurn, kQuarterTurn,
                                    -kQuarterTurn, kQuarterTurn};
    Matrix4 matrix{};
    Expect(BuildViewProjection(ViewPose{}, symmetric_fov, kEyeHeight, 0.05F,
                               250.0F, &matrix),
           "identity OpenXR view must produce a matrix");
    const auto forward =
        Transform(matrix, {0.0F, kEyeHeight, -1.0F, 1.0F});
    Expect(Near(forward[0], 0.0F) && Near(forward[1], 0.0F),
           "a point directly ahead must remain centered");
    Expect(Near(forward[3], 1.0F),
           "a point one metre ahead must have unit clip W");
    Expect(forward[2] > 0.0F && forward[2] < forward[3],
           "a visible point must lie inside Vulkan depth zero to one");

    ViewPose translated{};
    translated.position = {0.032F, 0.1F, -0.2F};
    Expect(BuildViewProjection(translated, symmetric_fov, kEyeHeight, 0.05F,
                               250.0F, &matrix),
           "translated OpenXR view must produce a matrix");
    const auto translated_forward = Transform(
        matrix, {0.032F, 0.1F + kEyeHeight, -1.2F, 1.0F});
    Expect(Near(translated_forward[0], 0.0F) &&
               Near(translated_forward[1], 0.0F) &&
               Near(translated_forward[3], 1.0F),
           "view translation must be inverted exactly");

    ViewPose scaled_quaternion{};
    scaled_quaternion.orientation = {0.0F, 0.0F, 0.0F, 2.0F};
    Expect(BuildViewProjection(scaled_quaternion, symmetric_fov, kEyeHeight,
                               0.05F, 250.0F, &matrix),
           "finite non-unit quaternion must be normalized");

    ViewPose invalid_quaternion{};
    invalid_quaternion.orientation = {0.0F, 0.0F, 0.0F, 0.0F};
    Expect(!BuildViewProjection(invalid_quaternion, symmetric_fov, kEyeHeight,
                                0.05F, 250.0F, &matrix),
           "zero quaternion must be rejected");
    ViewPose invalid_position{};
    invalid_position.position[0] =
        std::numeric_limits<float>::quiet_NaN();
    Expect(!BuildViewProjection(invalid_position, symmetric_fov, kEyeHeight,
                                0.05F, 250.0F, &matrix),
           "non-finite position must be rejected");
    Expect(!BuildViewProjection(ViewPose{}, symmetric_fov, kEyeHeight, 1.0F,
                                0.5F, &matrix),
           "inverted depth range must be rejected");
    Expect(!BuildViewProjection(ViewPose{}, ViewFov{}, kEyeHeight, 0.05F,
                                250.0F, &matrix),
           "degenerate FOV must be rejected");
    Expect(!BuildViewProjection(ViewPose{}, symmetric_fov, kEyeHeight, 0.05F,
                                250.0F, nullptr),
           "null output must be rejected");

    ViewPose camera;camera.position={5,3,-2};
    for(const auto& target:std::array<std::array<float,3>,4>{{{5,3,-5},{7,4,-2},{3,1,4},{5,8,-2}}}){
        Expect(hpvr::quest::BuildLookOrientation(camera.position,target,&camera.orientation),"authored camera target");
        Expect(BuildRigidTransform(camera,&matrix),"camera transform");
        float length=0;for(unsigned i=0;i<3;++i)length+=(target[i]-camera.position[i])*(target[i]-camera.position[i]);
        length=std::sqrt(length);
        for(unsigned i=0;i<3;++i)
            Expect(Near(-matrix[8+i],(target[i]-camera.position[i])/length),"negative Z points at target");
    }
    ViewPose head;head.orientation={0,0.70710678F,0,0.70710678F};
    camera.orientation={0,0,0,1};
    ViewPose eye=head, mapped;eye.position={0,0,0.032F};
    Expect(MapCinematicEye(eye,head,camera,head,&mapped),"reference aligned cinematic eye");
    Expect(Near(mapped.orientation[3],1)&&Near(mapped.orientation[1],0),"initial head heading cancels");
    float eye_distance=0;for(unsigned i=0;i<3;++i)eye_distance+=(mapped.position[i]-camera.position[i])*(mapped.position[i]-camera.position[i]);
    Expect(Near(std::sqrt(eye_distance),0.032F),"cinematic IPD preserved");
    eye.orientation={0,1,0,0};
    Expect(MapCinematicEye(eye,head,camera,head,&mapped)&&
           Near(mapped.orientation[1],0.70710678F),"physical head turns remain relative");
    Expect(!hpvr::quest::BuildLookOrientation(camera.position,camera.position,&camera.orientation),"zero look direction rejected");
    Expect(!MapCinematicEye(invalid_position,head,camera,head,&mapped),"invalid cinematic eye rejected");
    const auto reference=head;head.position={0,.3F,.4F};eye.position={0,.3F,.432F};
    Expect(MapCinematicEye(eye,head,camera,reference,&mapped),"6DoF lean accepted");
    Expect(Near(mapped.position[0],camera.position[0]-.432F)&&
           Near(mapped.position[1],camera.position[1]+.3F),"lean and crouch survive camera mapping");
    ViewPose theater;
    head.position={1,2,3};head.orientation={0,0.70710678F,0,0.70710678F};
    Expect(hpvr::quest::BuildTheaterPose(head,&theater),"theater anchor");
    Expect(Near(theater.position[0],-1.5F)&&Near(theater.position[1],2)&&Near(theater.position[2],3),"theater at menu distance and eye height");
    const auto anchored=theater;head.orientation={0,0,0,1};
    Expect(theater.position==anchored.position&&theater.orientation==anchored.orientation,"retained anchor independent of subsequent head rotation");
    hpvr::quest::LocomotionState walk,sprint;
    Expect(walk.ObserveHead(head)&&sprint.ObserveHead(head),"movement head");
    hpvr::quest::LocomotionInput input;input.move_active=true;input.move_y=1;
    Expect(walk.Tick(input,0.05F),"walk step");input.sprint=true;
    Expect(sprint.Tick(input,0.05F),"sprint step");
    Expect(Near(sprint.translation()[2],walk.translation()[2]*1.5F),"L3 sprint is 6 m/s, walk unchanged");
    TestCapsuleRecenter();
    TestRecenterChord();
    TestSmoothTurning();
    TestEyeRelativeSkyProjection();
    std::cout << "quest view tests passed\n";
    return EXIT_SUCCESS;
}
