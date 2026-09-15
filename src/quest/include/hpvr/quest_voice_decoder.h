#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include "hpvr/quest_voice_cast.h"

namespace hpvr::quest {

inline constexpr double kVoiceKeywordThreshold = 0.25;

struct VoiceDecoderStats {
    // Actual neural decoder calls, not input frames or inferred speech activity.
    std::uint64_t steps = 0, keyword_hits = 0, duration_rejects = 0;
    std::uint64_t stream_renewals = 0;
    float last_keyword_seconds = 0;
};

// Worker-thread-only acoustic decoder. It reads the licensed model at Load;
// Begin/Process/End neither save microphone samples nor use the network.
class QuestVoiceDecoder final {
public:
    QuestVoiceDecoder();
    ~QuestVoiceDecoder();
    QuestVoiceDecoder(const QuestVoiceDecoder&) = delete;
    QuestVoiceDecoder& operator=(const QuestVoiceDecoder&) = delete;
    // Optional confidence threshold for offline qualification; not microphone
    // gain and not a personal pronunciation calibration.
    [[nodiscard]] bool Load(const std::filesystem::path& model_directory,
                            double keyword_threshold = kVoiceKeywordThreshold);
    [[nodiscard]] bool Begin(VoiceSpell spell = VoiceSpell::Flipendo);
    [[nodiscard]] bool Process(const std::int16_t* mono_16khz,
                               std::size_t samples,
                               float* detected_word_seconds);
    void End();
    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] bool failed() const noexcept;
    // Worker owner only, like Process. Counters reset when Load is called.
    [[nodiscard]] VoiceDecoderStats Stats() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace hpvr::quest
