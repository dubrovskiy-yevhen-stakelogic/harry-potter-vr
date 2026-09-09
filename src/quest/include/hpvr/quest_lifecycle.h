#pragma once

#include <cstdint>

namespace hpvr::quest {

enum class HostEvent {
    Start,
    Resume,
    Pause,
    Stop,
    WindowReady,
    WindowLost,
    FocusGained,
    FocusLost,
    Destroy,
};

struct HostSnapshot {
    bool started = false;
    bool resumed = false;
    bool window_ready = false;
    bool focused = false;
    bool destroy_requested = false;
    std::uint64_t session_generation = 0;
};

struct HostTransition {
    bool should_start_session = false;
    bool should_stop_session = false;
    bool should_destroy_runtime = false;
};

class HostLifecycle final {
public:
    [[nodiscard]] HostTransition Apply(HostEvent event) noexcept;
    [[nodiscard]] const HostSnapshot& snapshot() const noexcept;
    [[nodiscard]] bool CanOwnSession() const noexcept;

private:
    HostSnapshot snapshot_{};
};

}  // namespace hpvr::quest
