#pragma once

#include <array>

namespace hpvr::quest {

using Matrix4 = std::array<float, 16>;

struct ViewPose {
    std::array<float, 3> position{};
    std::array<float, 4> orientation{0.0F, 0.0F, 0.0F, 1.0F};
};

struct ViewFov {
    float angle_left = 0.0F;
    float angle_right = 0.0F;
    float angle_down = 0.0F;
    float angle_up = 0.0F;
};
// Capture once on presentation entry; the caller retains this world-space pose.
bool BuildTheaterPose(const ViewPose& head,ViewPose* out);
bool BuildLookOrientation(const std::array<float,3>& from,const std::array<float,3>& to,std::array<float,4>* out);
bool MapCinematicEye(const ViewPose& local_eye,const ViewPose& local_head,const ViewPose& camera,
                     const ViewPose& reference,ViewPose* out);

struct LocomotionInput {
    float move_x = 0.0F;
    float move_y = 0.0F;
    float turn_x = 0.0F;
    bool move_active = false;
    bool turn_active = false;
    bool physics_active = false; // Gravity must tick even with a centred stick.
    bool sprint = false;
    float turn_y = 0.0F;
    bool smooth_turn = false;
    float smooth_turn_degrees = 90.0F;
};

struct LocomotionMove {
    std::array<float, 3> displacement{};
    unsigned int blocked_substeps = 0;
    unsigned int grounded_substeps = 0;
};

using LocomotionResolver = bool (*)(
    void* context,
    const std::array<float, 3>& capsule_center,
    const std::array<float, 3>& requested_displacement,
    LocomotionMove* output);

class LocomotionState final {
public:
    explicit LocomotionState(float eye_height_m = 0.815F);

    void Reset();
    [[nodiscard]] bool RestoreHead(const std::array<float,3>& world_head, float yaw);
    // Tracking-origin changes rebase the observed head, never the physical body.
    [[nodiscard]] bool RecenterToCapsule(const std::array<float,3>& world_capsule, float yaw);
    // Physical platform transport is not a teleport or a new input epoch.
    [[nodiscard]] bool TranslateWorld(const std::array<float,3>& displacement);
    [[nodiscard]] bool ObserveHead(const ViewPose& local_head);
    [[nodiscard]] bool Tick(const LocomotionInput& input,
                            float delta_seconds,
                            LocomotionResolver resolver = nullptr,
                            void* resolver_context = nullptr);
    [[nodiscard]] bool MapPose(const ViewPose& local_pose,
                               ViewPose* world_pose) const;
    // Same physical body used by Tick; headset crouching does not move its feet.
    [[nodiscard]] std::array<float,3> CapsuleCenter() const;

    [[nodiscard]] const std::array<float, 3>& translation() const;
    [[nodiscard]] float yaw_radians() const;
    [[nodiscard]] unsigned long long move_frames() const;
    [[nodiscard]] unsigned long long snap_turns() const;
    [[nodiscard]] unsigned long long blocked_substeps() const;
    [[nodiscard]] unsigned long long grounded_substeps() const;
    [[nodiscard]] float vertical_adjustment_m() const;

private:
    float eye_height_m_ = 0.815F;
    float restored_head_reference_y_ = 0.0F;
    std::array<float, 3> translation_{};
    std::array<float, 3> head_position_{};
    std::array<float, 3> head_forward_{0.0F, 0.0F, -1.0F};
    float yaw_radians_ = 0.0F;
    bool has_head_ = false;
    bool snap_armed_ = true;
    bool smooth_turn_ = false;
    unsigned long long move_frames_ = 0;
    unsigned long long snap_turns_ = 0;
    unsigned long long blocked_substeps_ = 0;
    unsigned long long grounded_substeps_ = 0;
    float vertical_adjustment_m_ = 0.0F;
};

[[nodiscard]] Matrix4 MultiplyMatrices(const Matrix4& left,
                                       const Matrix4& right);
[[nodiscard]] bool BuildRigidTransform(const ViewPose& pose,
                                       Matrix4* output);

[[nodiscard]] bool BuildViewProjection(const ViewPose& pose,
                                       const ViewFov& fov,
                                       float eye_height_m,
                                       float near_z,
                                       float far_z,
                                       Matrix4* output);

}  // namespace hpvr::quest
