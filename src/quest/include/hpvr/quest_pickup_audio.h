#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hpvr::quest {

// Game thread requests; only the audio callback owns the playback position.
// A new pickup retriggers at the next block without competing with dialogue
// or world effects. Several pickups in one block share one audible cue.
class PickupAudioCue final {
public:
    void Request() noexcept { requested_.fetch_add(1, std::memory_order_release); }
    void BeginBlock() noexcept {
        const auto requested = requested_.load(std::memory_order_acquire);
        if (requested != consumed_) {
            consumed_ = requested;
            position_ = 0;
            playing_ = true;
        }
    }
    std::int16_t NextSample(std::span<const std::int16_t> samples) noexcept {
        if (!playing_) return 0;
        if (position_ >= samples.size()) { playing_ = false; return 0; }
        return samples[position_++];
    }
    // Only before starting or after joining the audio callback.
    void Reset() noexcept {
        requested_.store(0, std::memory_order_relaxed);
        consumed_ = 0; position_ = 0; playing_ = false;
    }
private:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    std::atomic<std::uint32_t> requested_{0};
    std::uint32_t consumed_ = 0;
    std::size_t position_ = 0;
    bool playing_ = false;
};

} // namespace hpvr::quest
