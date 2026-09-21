#pragma once

#include "hpvr/hp1_gesture.h"
#include "hpvr/quest_pickup_audio.h"

#include <aaudio/AAudio.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace hpvr::quest {

class QuestAudio final {
public:
    QuestAudio() = default;
    ~QuestAudio();
    QuestAudio(const QuestAudio&) = delete;
    QuestAudio& operator=(const QuestAudio&) = delete;

    bool Configure(const wand::Hp1PcmSound& ambient,
                   const wand::Hp1MpegSound& wand_trace,
                   const wand::Hp1MpegSound& wand_start,
                   const wand::Hp1MpegSound& spell_cast,
                   const wand::Hp1MpegSound& incantation,
                   const wand::Hp1PcmSound& spell_hit,
                   const wand::Hp1MpegSound& basic_cast,
                   const std::vector<wand::Hp1MpegSound>& dialogue,
                   const std::filesystem::path& cache_directory);
    bool Start();
    bool ConfigureTutorialFrog(const wand::Hp1PcmSound& source);
    bool ConfigureScrollPickup(const wand::Hp1PcmSound& source);
    void PlayScrollPickup();
    bool ConfigureBeanPickup(std::size_t index);
    bool ConfigureMusic(const std::vector<wand::Hp1MpegSound>& sources,
                        const std::filesystem::path& cache);
    void SelectMusic(unsigned index);
    void SetPresentationAudio(bool ambient, bool paused);
    void SetAmbientLoopGain(float gain);
    void StopDialogue();
    bool DialogueBusy() const { return dialogue_cursor_.load()!=kIdleCursor; }
    bool SpeechBusy() const { return DialogueBusy() || incantation_cursor_.load()!=kIdleCursor; }
    void Stop();
    void SetWandDrawing(bool drawing);
    void PlaySpellCast(bool speak_flipendo=true);
    void PlayBasicCast();
    void PlaySpellHit();
    [[nodiscard]] bool PlayDialogue(std::size_t index);
    bool PlayWorldEffect(std::size_t index,float gain);
    void PlayBeanPickup();
    bool ConfigureCardPickup(std::size_t index);
    void PlayCardPickup();
    bool ConfigureCardAppearance(std::size_t index);
    void PlayCardAppearance();
    [[nodiscard]] float DialogueDurationSeconds(std::size_t index) const;
    [[nodiscard]] bool DialogueFinished(std::size_t index) const;
    [[nodiscard]] std::size_t DialogueClipCount() const noexcept;
    [[nodiscard]] bool IsConfigured() const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;

private:
    static aaudio_data_callback_result_t DataCallback(
        AAudioStream* stream,
        void* user_data,
        void* audio_data,
        std::int32_t frame_count);
    aaudio_data_callback_result_t Render(
        std::int16_t* output,
        std::int32_t frame_count);
    static std::vector<std::int16_t> ResampleMono48k(
        const wand::Hp1PcmSound& source);
    static wand::Hp1PcmSound DecodeMpeg(
        const wand::Hp1MpegSound& source);

    static constexpr std::uint64_t kIdleCursor = ~std::uint64_t{0};
    std::vector<std::int16_t> ambient_;
    std::vector<std::int16_t> wand_trace_;
    std::vector<std::int16_t> wand_start_;
    std::vector<std::int16_t> spell_cast_;
    std::vector<std::int16_t> basic_cast_;
    std::vector<std::int16_t> incantation_;
    std::vector<std::int16_t> spell_hit_;
    std::vector<std::vector<std::int16_t>> dialogue_;
    std::vector<std::vector<std::int16_t>> music_;
    std::atomic<unsigned> music_requested_{0};
    unsigned music_current_=~0U;
    std::size_t music_cursor_=0;
    std::atomic<bool> ambient_enabled_{false}, narrative_paused_{false};
    std::atomic<float> ambient_loop_gain_{1};
    std::uint64_t ambient_cursor_ = 0;
    std::uint64_t wand_trace_cursor_ = 0;
    std::atomic<std::uint64_t> wand_start_cursor_{kIdleCursor};
    std::atomic<std::uint64_t> spell_cast_cursor_{kIdleCursor};
    std::atomic<std::uint64_t> basic_cast_cursor_{kIdleCursor};
    std::atomic<std::uint64_t> incantation_cursor_{kIdleCursor};
    std::atomic<std::uint64_t> spell_hit_cursor_{kIdleCursor};
    std::atomic<std::uint64_t> dialogue_cursor_{kIdleCursor};
    std::atomic<std::uint64_t> effect_cursor_{kIdleCursor};
    std::atomic<unsigned> effect_index_{0};
    std::atomic<float> effect_gain_{0};
    PickupAudioCue bean_cue_;
    PickupAudioCue card_cue_;
    PickupAudioCue card_appearance_cue_;
    std::size_t card_appearance_index_=std::numeric_limits<std::size_t>::max();
    std::size_t card_sound_index_=std::numeric_limits<std::size_t>::max();
    std::size_t bean_sound_index_ = std::numeric_limits<std::size_t>::max();
    std::size_t scroll_sound_index_ = std::numeric_limits<std::size_t>::max();
    std::atomic<std::uint32_t> dialogue_index_{
        std::numeric_limits<std::uint32_t>::max()};
    std::atomic<bool> wand_drawing_{false};
    AAudioStream* stream_ = nullptr;
};

}  // namespace hpvr::quest
