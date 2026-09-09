#include "hpvr/quest_lifecycle.h"

namespace hpvr::quest {

HostTransition HostLifecycle::Apply(const HostEvent event) noexcept {
    const bool could_own_session = CanOwnSession();

    switch (event) {
        case HostEvent::Start:
            snapshot_.started = true;
            break;
        case HostEvent::Resume:
            snapshot_.resumed = true;
            break;
        case HostEvent::Pause:
            snapshot_.resumed = false;
            break;
        case HostEvent::Stop:
            snapshot_.started = false;
            snapshot_.resumed = false;
            break;
        case HostEvent::WindowReady:
            snapshot_.window_ready = true;
            break;
        case HostEvent::WindowLost:
            snapshot_.window_ready = false;
            break;
        case HostEvent::FocusGained:
            snapshot_.focused = true;
            break;
        case HostEvent::FocusLost:
            snapshot_.focused = false;
            break;
        case HostEvent::Destroy:
            snapshot_.destroy_requested = true;
            snapshot_.started = false;
            snapshot_.resumed = false;
            snapshot_.window_ready = false;
            snapshot_.focused = false;
            break;
    }

    const bool can_own_session = CanOwnSession();
    HostTransition transition{};
    transition.should_start_session = !could_own_session && can_own_session;
    transition.should_stop_session = could_own_session && !can_own_session;
    transition.should_destroy_runtime = snapshot_.destroy_requested;
    if (transition.should_start_session) {
        ++snapshot_.session_generation;
    }
    return transition;
}

const HostSnapshot& HostLifecycle::snapshot() const noexcept {
    return snapshot_;
}

bool HostLifecycle::CanOwnSession() const noexcept {
    return snapshot_.started && snapshot_.resumed && snapshot_.window_ready &&
           !snapshot_.destroy_requested;
}

}  // namespace hpvr::quest
