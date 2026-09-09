#include "hpvr/quest_lifecycle.h"
#include "hpvr/quest_view.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void Expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "quest lifecycle test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool HalfMovementResolver(
    void* const context,
    const std::array<float, 3>& capsule_center,
    const std::array<float, 3>& requested_displacement,
    hpvr::quest::LocomotionMove* const output) {
    if (context == nullptr || output == nullptr ||
        std::abs(capsule_center[1]) > 1.0e-6F) {
        return false;
    }
    *static_cast<bool*>(context) = true;
    output->displacement = {
        requested_displacement[0] * 0.5F,
        0.04F,
        requested_displacement[2] * 0.5F};
    output->blocked_substeps = 2;
    output->grounded_substeps = 1;
    return true;
}

}  // namespace

int main() {
    using hpvr::quest::HostEvent;
    using hpvr::quest::HostLifecycle;

    HostLifecycle host;
    Expect(!host.Apply(HostEvent::Start).should_start_session,
           "start without window or resume must stay idle");
    Expect(!host.Apply(HostEvent::WindowReady).should_start_session,
           "window before resume must stay idle");

    auto transition = host.Apply(HostEvent::Resume);
    Expect(transition.should_start_session,
           "resume plus window must request the first session");
    Expect(host.snapshot().session_generation == 1,
           "first session generation must be one");

    transition = host.Apply(HostEvent::FocusLost);
    Expect(!transition.should_stop_session,
           "Android focus loss must not destroy an otherwise visible session");

    transition = host.Apply(HostEvent::Pause);
    Expect(transition.should_stop_session,
           "pause must stop the active session");
    transition = host.Apply(HostEvent::Resume);
    Expect(transition.should_start_session,
           "resume must recreate the session");
    Expect(host.snapshot().session_generation == 2,
           "resume must advance the session generation");

    transition = host.Apply(HostEvent::WindowLost);
    Expect(transition.should_stop_session,
           "window loss must stop the active session");
    transition = host.Apply(HostEvent::WindowReady);
    Expect(transition.should_start_session,
           "window recreation must recreate the session");
    Expect(host.snapshot().session_generation == 3,
           "window recreation must advance the generation");

    transition = host.Apply(HostEvent::Destroy);
    Expect(transition.should_stop_session,
           "destroy must stop an active session");
    Expect(transition.should_destroy_runtime,
           "destroy must release the OpenXR runtime");
    Expect(!host.CanOwnSession(), "destroyed host must never own a session");

    using hpvr::quest::LocomotionInput;
    using hpvr::quest::LocomotionState;
    using hpvr::quest::ViewPose;
    LocomotionState locomotion(0.815F);
    const ViewPose head{{0.31F, 1.62F, -0.18F}, {0.0F, 0.0F, 0.0F, 1.0F}};
    Expect(locomotion.ObserveHead(head), "head pose must be accepted");
    LocomotionInput move{};
    move.move_active = true;
    move.move_y = 1.0F;
    Expect(locomotion.Tick(move, 0.05F), "movement tick must be accepted");
    Expect(std::abs(locomotion.translation()[2] + 0.20F) < 1.0e-5F,
           "authored forward locomotion must cover 20 cm in 50 ms");

    locomotion.Reset();
    Expect(locomotion.ObserveHead(head),
           "head pose must be restored for constrained movement");
    bool resolver_called = false;
    Expect(locomotion.Tick(move, 0.05F, HalfMovementResolver,
                           &resolver_called),
           "constrained movement tick must be accepted");
    Expect(resolver_called, "movement resolver must receive active movement");
    Expect(std::abs(locomotion.translation()[2] + 0.10F) < 1.0e-5F,
           "resolver must be able to shorten horizontal movement");
    Expect(std::abs(locomotion.translation()[1] - 0.855F) < 1.0e-5F,
           "resolver vertical correction must follow floors and stairs");
    Expect(locomotion.blocked_substeps() == 2,
           "blocked collision substeps must accumulate");
    Expect(locomotion.grounded_substeps() == 1,
           "grounded collision substeps must accumulate");
    Expect(std::abs(locomotion.vertical_adjustment_m() - 0.04F) < 1.0e-5F,
           "vertical collision adjustment must accumulate");

    ViewPose before{};
    Expect(locomotion.MapPose(head, &before), "head pose must map before turn");
    LocomotionInput turn{};
    turn.turn_active = true;
    turn.turn_x = 1.0F;
    Expect(locomotion.Tick(turn, 0.01F), "snap turn must be accepted");
    ViewPose after{};
    Expect(locomotion.MapPose(head, &after), "head pose must map after turn");
    const float pivot_error = std::sqrt(
        (before.position[0] - after.position[0]) *
            (before.position[0] - after.position[0]) +
        (before.position[1] - after.position[1]) *
            (before.position[1] - after.position[1]) +
        (before.position[2] - after.position[2]) *
            (before.position[2] - after.position[2]));
    Expect(pivot_error < 1.0e-5F,
           "snap turn must preserve virtual head position");
    Expect(locomotion.snap_turns() == 1,
           "held turn stick must produce one snap");
    Expect(locomotion.Tick(turn, 0.01F), "held turn tick must be accepted");
    Expect(locomotion.snap_turns() == 1,
           "held turn stick must stay latched");

    std::cout << "quest lifecycle tests passed\n";
    return EXIT_SUCCESS;
}
