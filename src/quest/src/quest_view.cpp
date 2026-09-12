#include "hpvr/quest_view.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace hpvr::quest {
namespace {

constexpr float kTau = 6.28318530717958647692F;
constexpr float kMoveSpeed = 4.0F; // Owned Harry GroundSpeed=200 UU/s at 0.02 m/UU.
constexpr float kMoveDeadzone = 0.18F;
constexpr float kSnapEnter = 0.72F;
constexpr float kSnapRelease = 0.35F;
constexpr float kSnapRadians = 0.52359877559829887308F;

bool NormalizeQuaternion(const std::array<float, 4>& input,
                         std::array<float, 4>* output) {
    const float norm = std::sqrt(input[0] * input[0] +
                                 input[1] * input[1] +
                                 input[2] * input[2] +
                                 input[3] * input[3]);
    if (output == nullptr || !std::isfinite(norm) || norm < 1.0e-6F) {
        return false;
    }
    for (std::size_t index = 0; index < 4; ++index) {
        (*output)[index] = input[index] / norm;
    }
    return true;
}

std::array<float, 4> MultiplyQuaternion(const std::array<float, 4>& left,
                                        const std::array<float, 4>& right) {
    return {
        left[3] * right[0] + left[0] * right[3] +
            left[1] * right[2] - left[2] * right[1],
        left[3] * right[1] - left[0] * right[2] +
            left[1] * right[3] + left[2] * right[0],
        left[3] * right[2] + left[0] * right[1] -
            left[1] * right[0] + left[2] * right[3],
        left[3] * right[3] - left[0] * right[0] -
            left[1] * right[1] - left[2] * right[2],
    };
}

std::array<float, 3> RotateYaw(const std::array<float, 3>& value,
                               const float yaw) {
    const float cosine = std::cos(yaw);
    const float sine = std::sin(yaw);
    return {cosine * value[0] + sine * value[2], value[1],
            -sine * value[0] + cosine * value[2]};
}

}  // namespace

bool BuildTheaterPose(const ViewPose& head,ViewPose* out){
    Matrix4 m;if(!out||!BuildRigidTransform(head,&m))return false;
    const float yaw=std::atan2(m[8],m[10]);
    out->orientation={0,std::sin(yaw/2),0,std::cos(yaw/2)};
    out->position={head.position[0]-std::sin(yaw)*2.5F,head.position[1],head.position[2]-std::cos(yaw)*2.5F};
    return true;
}
bool BuildLookOrientation(const std::array<float,3>& from,const std::array<float,3>& to,std::array<float,4>* out){
    if(!out)return false;
    float x=to[0]-from[0],y=to[1]-from[1],z=to[2]-from[2];
    if(!std::isfinite(x+y+z)||x*x+y*y+z*z<1e-10F)return false;
    float yaw=std::atan2(-x,-z),pitch=std::atan2(y,std::hypot(x,z));
    *out=MultiplyQuaternion({0,std::sin(yaw/2),0,std::cos(yaw/2)},
                           {std::sin(pitch/2),0,0,std::cos(pitch/2)});return true;
}
bool MapCinematicEye(const ViewPose& eye,const ViewPose& head,const ViewPose& camera,
                     const ViewPose& reference,ViewPose* out){
    if(!out)return false;std::array<float,4> ref;
    Matrix4 check;
    if(!BuildRigidTransform(eye,&check)||!BuildRigidTransform(head,&check)||
       !BuildRigidTransform(camera,&check)||!BuildRigidTransform(reference,&check))return false;
    if(!NormalizeQuaternion(reference.orientation,&ref))return false;
    const auto base=MultiplyQuaternion(camera.orientation,{-ref[0],-ref[1],-ref[2],ref[3]});
    Matrix4 rotation;if(!BuildRigidTransform({{0,0,0},base},&rotation))return false;
    out->orientation=MultiplyQuaternion(base,eye.orientation);
    for(unsigned i=0;i<3;++i){out->position[i]=camera.position[i];
        // Retain the entry head position: subtracting the CURRENT head here
        // cancels all room-scale translation and leaves only the eye separation.
        for(unsigned j=0;j<3;++j)out->position[i]+=rotation[j*4+i]*(eye.position[j]-reference.position[j]);}
    return true;
}

Matrix4 MultiplyMatrices(const Matrix4& left, const Matrix4& right) {
    Matrix4 result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t inner = 0; inner < 4; ++inner) {
                result[column * 4 + row] +=
                    left[inner * 4 + row] * right[column * 4 + inner];
            }
        }
    }
    return result;
}

bool BuildRigidTransform(const ViewPose& pose, Matrix4* const output) {
    if (output == nullptr ||
        !std::all_of(pose.position.begin(), pose.position.end(),
                     [](const float value) { return std::isfinite(value); })) {
        return false;
    }
    std::array<float, 4> orientation{};
    if (!NormalizeQuaternion(pose.orientation, &orientation)) {
        return false;
    }
    const float x = orientation[0];
    const float y = orientation[1];
    const float z = orientation[2];
    const float w = orientation[3];
    *output = {
        1.0F - 2.0F * (y * y + z * z),
        2.0F * (x * y + z * w),
        2.0F * (x * z - y * w),
        0.0F,
        2.0F * (x * y - z * w),
        1.0F - 2.0F * (x * x + z * z),
        2.0F * (y * z + x * w),
        0.0F,
        2.0F * (x * z + y * w),
        2.0F * (y * z - x * w),
        1.0F - 2.0F * (x * x + y * y),
        0.0F,
        pose.position[0],
        pose.position[1],
        pose.position[2],
        1.0F,
    };
    return true;
}

LocomotionState::LocomotionState(const float eye_height_m)
    : eye_height_m_(eye_height_m) {
    Reset();
}

void LocomotionState::Reset() {
    translation_ = {0.0F, eye_height_m_, 0.0F};
    restored_head_reference_y_ = 0.0F;
    head_position_ = {};
    head_forward_ = {0.0F, 0.0F, -1.0F};
    yaw_radians_ = 0.0F;
    has_head_ = false;
    snap_armed_ = true;
}

bool LocomotionState::ObserveHead(const ViewPose& local_head) {
    std::array<float, 4> orientation{};
    if (!NormalizeQuaternion(local_head.orientation, &orientation) ||
        !std::all_of(local_head.position.begin(), local_head.position.end(),
                     [](const float value) { return std::isfinite(value); })) {
        return false;
    }
    const float x = orientation[0];
    const float y = orientation[1];
    const float z = orientation[2];
    const float w = orientation[3];
    std::array<float, 3> forward{
        -2.0F * (x * z + y * w),
        -2.0F * (y * z - x * w),
        -(1.0F - 2.0F * (x * x + y * y)),
    };
    const float horizontal_length =
        std::sqrt(forward[0] * forward[0] + forward[2] * forward[2]);
    if (horizontal_length > 1.0e-5F) {
        head_forward_ = {forward[0] / horizontal_length, 0.0F,
                         forward[2] / horizontal_length};
    }
    head_position_ = local_head.position;
    has_head_ = true;
    return true;
}

bool LocomotionState::Tick(const LocomotionInput& input,
                           const float delta_seconds,
                           const LocomotionResolver resolver,
                           void* const resolver_context) {
    if (!std::isfinite(input.move_x) || !std::isfinite(input.move_y) ||
        !std::isfinite(input.turn_x) || !std::isfinite(delta_seconds) ||
        delta_seconds < 0.0F) {
        return false;
    }
    if ((input.move_active || input.physics_active) && has_head_) {
        const float magnitude =
            std::sqrt(input.move_x * input.move_x + input.move_y * input.move_y);
        if (magnitude > kMoveDeadzone || input.physics_active) {
            const float scaled =
                input.move_active?std::clamp((magnitude - kMoveDeadzone) /
                                   (1.0F - kMoveDeadzone),0.0F,1.0F):0.0F;
            const float axis_x = input.move_x / std::max(magnitude,0.001F) * scaled;
            const float axis_y = input.move_y / std::max(magnitude,0.001F) * scaled;
            const std::array<float, 3> local_right{-head_forward_[2], 0.0F,
                                                   head_forward_[0]};
            const std::array<float, 3> local_direction{
                local_right[0] * axis_x + head_forward_[0] * axis_y,
                0.0F,
                local_right[2] * axis_x + head_forward_[2] * axis_y,
            };
            const auto direction = RotateYaw(local_direction, yaw_radians_);
            const float distance = kMoveSpeed * (input.sprint?1.5F:1.0F) * std::min(delta_seconds, 0.05F);
            const std::array<float, 3> requested{
                direction[0] * distance, 0.0F,
                direction[2] * distance};
            LocomotionMove movement{requested, 0, 0};
            if (resolver != nullptr) {
                const auto capsule_center = CapsuleCenter();
                if (!resolver(resolver_context, capsule_center, requested,
                              &movement) ||
                    !std::all_of(
                        movement.displacement.begin(),
                        movement.displacement.end(),
                        [](const float value) {
                            return std::isfinite(value);
                        })) {
                    return false;
                }
            }
            for (std::size_t axis = 0; axis < 3; ++axis) {
                translation_[axis] += movement.displacement[axis];
            }
            blocked_substeps_ += movement.blocked_substeps;
            grounded_substeps_ += movement.grounded_substeps;
            vertical_adjustment_m_ +=
                std::abs(movement.displacement[1]);
            ++move_frames_;
        }
    }

    if (!input.turn_active || std::abs(input.turn_x) <= kSnapRelease) {
        snap_armed_ = true;
    } else if (snap_armed_ && std::abs(input.turn_x) >= kSnapEnter) {
        const auto old_head = RotateYaw(head_position_, yaw_radians_);
        const std::array<float, 3> world_head{
            old_head[0] + translation_[0], old_head[1] + translation_[1],
            old_head[2] + translation_[2]};
        const float sign = input.turn_x > 0.0F ? 1.0F : -1.0F;
        yaw_radians_ = std::fmod(yaw_radians_ - sign * kSnapRadians + kTau,
                                kTau);
        const auto new_head = RotateYaw(head_position_, yaw_radians_);
        translation_ = {world_head[0] - new_head[0],
                        world_head[1] - new_head[1],
                        world_head[2] - new_head[2]};
        snap_armed_ = false;
        ++snap_turns_;
    }
    return true;
}

std::array<float,3> LocomotionState::CapsuleCenter() const {
    const auto body_local=RotateYaw({head_position_[0],0.0F,head_position_[2]},yaw_radians_);
    return {body_local[0]+translation_[0],
            translation_[1]-eye_height_m_+restored_head_reference_y_,
            body_local[2]+translation_[2]};
}
bool LocomotionState::RestoreHead(const std::array<float,3>& world_head,float yaw) {
    if(!has_head_||!std::isfinite(yaw)||!std::ranges::all_of(world_head,
        [](float v){return std::isfinite(v)&&std::abs(v)<2000;}))return false;
    yaw_radians_=yaw;
    const auto head=RotateYaw(head_position_,yaw_radians_);
    for(std::size_t i=0;i<3;++i)translation_[i]=world_head[i]-head[i];
    restored_head_reference_y_=head_position_[1];
    snap_armed_=false;
    return true;
}
bool LocomotionState::RecenterToCapsule(const std::array<float,3>& world_capsule,float yaw) {
    if (!has_head_ || !std::isfinite(yaw) || !std::isfinite(eye_height_m_) ||
        !std::ranges::all_of(world_capsule, [](float v) {
            return std::isfinite(v) && std::abs(v) < 2000;
        })) return false;
    const auto head = RotateYaw(head_position_, yaw);
    const std::array<float,3> translation{
        world_capsule[0] - head[0],
        world_capsule[1] + eye_height_m_ - head[1],
        world_capsule[2] - head[2]};
    if (!std::ranges::all_of(translation, [](float v) { return std::isfinite(v); })) return false;
    // Commit only after the complete transform is valid. In particular, the
    // old mapped eye height must not become a new capsule height when crouched.
    translation_ = translation;
    yaw_radians_ = yaw;
    restored_head_reference_y_ = head_position_[1];
    snap_armed_ = false;
    return true;
}
bool LocomotionState::TranslateWorld(const std::array<float,3>& displacement) {
    if(!has_head_||!std::ranges::all_of(displacement,
        [](float v){return std::isfinite(v)&&std::abs(v)<2000;}))return false;
    for(std::size_t i=0;i<3;++i)translation_[i]+=displacement[i];
    return true;
}
bool LocomotionState::MapPose(const ViewPose& local_pose,
                              ViewPose* const world_pose) const {
    if (world_pose == nullptr) {
        return false;
    }
    std::array<float, 4> local_orientation{};
    if (!NormalizeQuaternion(local_pose.orientation, &local_orientation)) {
        return false;
    }
    const auto position = RotateYaw(local_pose.position, yaw_radians_);
    const float half_yaw = yaw_radians_ * 0.5F;
    const std::array<float, 4> yaw_orientation{
        0.0F, std::sin(half_yaw), 0.0F, std::cos(half_yaw)};
    world_pose->position = {position[0] + translation_[0],
                            position[1] + translation_[1],
                            position[2] + translation_[2]};
    world_pose->orientation =
        MultiplyQuaternion(yaw_orientation, local_orientation);
    return std::all_of(world_pose->position.begin(), world_pose->position.end(),
                       [](const float value) { return std::isfinite(value); });
}

const std::array<float, 3>& LocomotionState::translation() const {
    return translation_;
}

float LocomotionState::yaw_radians() const { return yaw_radians_; }
unsigned long long LocomotionState::move_frames() const { return move_frames_; }
unsigned long long LocomotionState::snap_turns() const { return snap_turns_; }
unsigned long long LocomotionState::blocked_substeps() const {
    return blocked_substeps_;
}
unsigned long long LocomotionState::grounded_substeps() const {
    return grounded_substeps_;
}
float LocomotionState::vertical_adjustment_m() const {
    return vertical_adjustment_m_;
}

bool BuildViewProjection(const ViewPose& pose,
                         const ViewFov& fov,
                         const float eye_height_m,
                         const float near_z,
                         const float far_z,
                         Matrix4* const output) {
    if (output == nullptr || !std::isfinite(eye_height_m) ||
        !std::isfinite(near_z) || !std::isfinite(far_z) || near_z <= 0.0F ||
        far_z <= near_z) {
        return false;
    }
    const float left = std::tan(fov.angle_left);
    const float right = std::tan(fov.angle_right);
    const float down = std::tan(fov.angle_down);
    const float up = std::tan(fov.angle_up);
    if (!std::isfinite(left) || !std::isfinite(right) ||
        !std::isfinite(down) || !std::isfinite(up) || right <= left ||
        up <= down) {
        return false;
    }
    const Matrix4 projection{
        2.0F / (right - left),
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        2.0F / (up - down),
        0.0F,
        0.0F,
        (right + left) / (right - left),
        (up + down) / (up - down),
        far_z / (near_z - far_z),
        -1.0F,
        0.0F,
        0.0F,
        far_z * near_z / (near_z - far_z),
        0.0F,
    };

    float x = pose.orientation[0];
    float y = pose.orientation[1];
    float z = pose.orientation[2];
    float w = pose.orientation[3];
    const float norm = std::sqrt(x * x + y * y + z * z + w * w);
    if (!std::isfinite(norm) || norm < 1.0e-6F ||
        !std::all_of(pose.position.begin(), pose.position.end(),
                     [](const float value) { return std::isfinite(value); })) {
        return false;
    }
    x /= norm;
    y /= norm;
    z /= norm;
    w /= norm;
    const Matrix4 eye_to_world{
        1.0F - 2.0F * (y * y + z * z),
        2.0F * (x * y + z * w),
        2.0F * (x * z - y * w),
        0.0F,
        2.0F * (x * y - z * w),
        1.0F - 2.0F * (x * x + z * z),
        2.0F * (y * z + x * w),
        0.0F,
        2.0F * (x * z + y * w),
        2.0F * (y * z - x * w),
        1.0F - 2.0F * (x * x + y * y),
        0.0F,
        pose.position[0],
        pose.position[1] + eye_height_m,
        pose.position[2],
        1.0F,
    };
    Matrix4 world_to_eye{
        eye_to_world[0], eye_to_world[4], eye_to_world[8],  0.0F,
        eye_to_world[1], eye_to_world[5], eye_to_world[9],  0.0F,
        eye_to_world[2], eye_to_world[6], eye_to_world[10], 0.0F,
        0.0F,            0.0F,            0.0F,             1.0F,
    };
    const float tx = eye_to_world[12];
    const float ty = eye_to_world[13];
    const float tz = eye_to_world[14];
    world_to_eye[12] =
        -(world_to_eye[0] * tx + world_to_eye[4] * ty +
          world_to_eye[8] * tz);
    world_to_eye[13] =
        -(world_to_eye[1] * tx + world_to_eye[5] * ty +
          world_to_eye[9] * tz);
    world_to_eye[14] =
        -(world_to_eye[2] * tx + world_to_eye[6] * ty +
          world_to_eye[10] * tz);
    *output = MultiplyMatrices(projection, world_to_eye);
    return std::all_of(output->begin(), output->end(),
                       [](const float value) { return std::isfinite(value); });
}

}  // namespace hpvr::quest
