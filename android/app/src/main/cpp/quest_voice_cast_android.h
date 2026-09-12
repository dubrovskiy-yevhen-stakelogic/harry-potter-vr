#pragma once

#include "hpvr/quest_voice_cast.h"

#include <filesystem>
#include <memory>

namespace hpvr::quest {

// No microphone or model activity until explicitly enabled by SetListening.
// Permission UX belongs to the caller; this class never opens Android dialogs.
class QuestVoiceCast final {
public:
    QuestVoiceCast();
    ~QuestVoiceCast();
    QuestVoiceCast(const QuestVoiceCast&) = delete;
    QuestVoiceCast& operator=(const QuestVoiceCast&) = delete;

    // Call once with the read-only, already-staged licensed model directory.
    // Configure starts an idle worker; model reads occur on that worker after
    // enabled + permission_granted, never in a frame or audio callback.
    [[nodiscard]] bool Configure(const std::filesystem::path& model_directory);
    // Eligible unanswered windows retry automatically. Acceptance consumes
    // one generation; advance it after playback/cooldown for another cast.
    void SetListening(const VoiceCastArm& arm) noexcept;
    [[nodiscard]] bool PollEvent(const VoiceCastArm& current,
                                 VoiceCastEvent* result) noexcept;
    [[nodiscard]] VoiceCastStatus status() const noexcept;
    [[nodiscard]] int microphone_error() const noexcept;
    // Lock-free numeric snapshot; counters are sampled independently, so a
    // concurrent capture boundary can appear between fields. No PCM leaves
    // the worker. Reading this never opens a microphone or starts recognition.
    [[nodiscard]] VoiceCastStats Stats() const noexcept;
    // Lifecycle teardown only. SetListening(disarmed) is the non-blocking
    // per-frame/focus-loss operation; Shutdown joins the worker.
    void Shutdown();
private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace hpvr::quest
