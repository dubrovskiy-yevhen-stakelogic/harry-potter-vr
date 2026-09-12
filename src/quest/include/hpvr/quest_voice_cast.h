#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hpvr::quest {

enum class VoiceCastStatus : std::uint8_t {
    Disabled, NeedsPermission, LoadingModel, Ready, Listening,
    AwaitingNewTarget, ModelUnavailable, MicrophoneUnavailable
};

// The caller issues a NEW, monotonically increasing generation for each
// target-lock/cast attempt. A recognizer result must never select a new target.
struct VoiceCastArm {
    std::uint64_t generation = 0;
    bool enabled = false;
    bool permission_granted = false;
    bool focused = false;
    bool gameplay_allowed = false;
    bool target_locked = false;
};

struct VoiceCastEvent {
    std::uint64_t generation = 0;
    float utterance_seconds = 0;
};

// Numeric diagnostics only. No samples, words or reconstructable audio are
// retained. Lifetime counters reset at Configure; samples include discarded
// warm input and capture_attempts counts physical microphone open attempts.
// The level window covers at most about 250 ms; generation identifies the last
// armed decoder attempt, not permission to decode samples captured while idle.
struct VoiceCastStats {
    std::uint64_t generation = 0, capture_attempts = 0, total_samples = 0;
    std::uint64_t decoder_steps = 0, keyword_hits = 0, duration_rejects = 0;
    std::uint64_t accepted_events = 0, stale_discards = 0, read_zeroes = 0;
    std::uint64_t errors = 0, decoder_errors = 0, retries = 0;
    std::uint32_t input_window_samples = 0, input_nonzero = 0, input_clipped = 0, input_peak = 0;
    float input_rms = 0, last_keyword_seconds = 0;
    int last_error = 0;  // Last AAudio error, not a recognition confidence.
    std::uint64_t stream_renewals = 0;
};

struct VoiceInputMeter {
    std::uint64_t square_sum = 0;
    std::uint32_t samples = 0, nonzero = 0, clipped = 0, peak = 0;
    void Add(const std::int16_t* pcm, std::size_t count) noexcept {
        if (pcm == nullptr) return;
        for (std::size_t index = 0; index < count; ++index) {
            const auto sample = static_cast<std::int32_t>(pcm[index]);
            const auto magnitude = static_cast<std::uint32_t>(sample < 0 ? -sample : sample);
            square_sum += static_cast<std::uint64_t>(static_cast<std::int64_t>(sample) * sample);
            ++samples;
            nonzero += magnitude != 0;
            clipped += magnitude >= 32767;
            if (magnitude > peak) peak = magnitude;
        }
    }
    [[nodiscard]] float Rms() const noexcept {
        return samples == 0 ? 0 : static_cast<float>(std::sqrt(static_cast<double>(square_sum) / samples));
    }
    void Reset() noexcept { *this = {}; }
};

inline constexpr std::uint64_t kVoiceMaxGeneration = (std::uint64_t{1} << 56) - 1;
inline constexpr float kVoiceMinWordSeconds = 0.25F;
inline constexpr float kVoiceMaxWordSeconds = 1.8F;

[[nodiscard]] inline bool VoiceMayListen(const VoiceCastArm& arm) noexcept {
    return arm.enabled && arm.permission_granted && arm.focused &&
           arm.gameplay_allowed && arm.target_locked && arm.generation != 0 &&
           arm.generation <= kVoiceMaxGeneration;
}

// Keep Android input warm only inside explicitly allowed, focused gameplay.
// This is not recognition permission: unarmed samples are discarded.
[[nodiscard]] inline bool VoiceMayCapture(const VoiceCastArm& arm) noexcept {
    return arm.enabled && arm.permission_granted && arm.focused && arm.gameplay_allowed;
}

// One atomic word transports the entire command to the microphone worker.
// The high bit is reserved for worker shutdown, not a valid generation bit.
[[nodiscard]] inline std::uint64_t PackVoiceArm(const VoiceCastArm& arm) noexcept {
    if (arm.generation > kVoiceMaxGeneration) return 0;
    return (arm.generation << 7) | (arm.enabled ? 1ULL : 0ULL) |
        (arm.permission_granted ? 2ULL : 0ULL) | (arm.focused ? 4ULL : 0ULL) |
        (arm.gameplay_allowed ? 8ULL : 0ULL) | (arm.target_locked ? 16ULL : 0ULL);
}

[[nodiscard]] inline VoiceCastArm UnpackVoiceArm(std::uint64_t word) noexcept {
    return {word >> 7, (word & 1) != 0, (word & 2) != 0,
            (word & 4) != 0, (word & 8) != 0, (word & 16) != 0};
}

[[nodiscard]] inline bool VoiceEventIsCurrent(const VoiceCastArm& arm,
                                             const VoiceCastEvent& event) noexcept {
    return VoiceMayListen(arm) && event.generation == arm.generation &&
        std::isfinite(event.utterance_seconds) &&
        event.utterance_seconds >= kVoiceMinWordSeconds &&
        event.utterance_seconds <= kVoiceMaxWordSeconds;
}

// Acoustic acceptance belongs to the keyword decoder, not a volume/VAD test.
// This second gate handles exact keyword identity, duration, stale targets and
// replay protection. It is also used by the real Android worker.
class VoiceCastGate final {
public:
    [[nodiscard]] bool CanListen(const VoiceCastArm& arm) const noexcept {
        return VoiceMayListen(arm) && arm.generation > consumed_generation_;
    }
    [[nodiscard]] bool Accept(const VoiceCastArm& arm,
                              std::uint64_t captured_generation,
                              std::string_view keyword,
                              float seconds,
                              VoiceCastEvent* result) noexcept {
        const VoiceCastEvent event{captured_generation, seconds};
        if (result == nullptr || keyword != "flipendo" || !CanListen(arm) ||
            !VoiceEventIsCurrent(arm, event)) return false;
        consumed_generation_ = captured_generation;
        *result = event;
        return true;
    }
private:
    std::uint64_t consumed_generation_ = 0;
};

}  // namespace hpvr::quest
