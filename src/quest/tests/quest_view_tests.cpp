#include "hpvr/quest_view.h"

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
    std::cout << "quest view tests passed\n";
    return EXIT_SUCCESS;
}
