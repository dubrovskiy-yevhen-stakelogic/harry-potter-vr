// Exercise the real Android worker with deterministic fake capture/recognition.
// These tests establish lifecycle/stale-result safety, not acoustic accuracy.
#include "quest_voice_cast_android.h"
#include "hpvr/quest_voice_decoder.h"
#include <aaudio/AAudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
using namespace hpvr::quest;
unsigned checks = 0;
std::atomic<unsigned> opened{0}, closed{0}, model_loads{0}, decoded{0};
std::atomic<unsigned> decoder_begins{0};
std::atomic<unsigned> pretarget_keyword_decodes{0};
std::atomic<std::int64_t> queued_keyword_frames{0};
std::atomic<bool> emit_keyword{false}, hold_decoder{false}, decoder_entered{false};
std::atomic<bool> privacy_sensitive{false}, mismatch_format{false};
std::atomic<bool> empty_capture{false};
std::atomic<bool> read_failure{false};
std::atomic<std::int16_t> input_sample{1};
std::atomic<bool> emit_short_keyword{false};
std::atomic<bool> throw_decoder{false};
std::atomic<bool> emit_renewal{false};
void Check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class Predicate> void Wait(Predicate predicate, const char* message,
                                    std::chrono::milliseconds timeout = 1500ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(2ms);
    Check(predicate(), message);
}
VoiceCastArm Arm(std::uint64_t generation) { return {generation, true, true, true, true, true}; }
}  // namespace

struct AAudioStreamBuilder {};
struct AAudioStream { std::int64_t frames_read = 0; };
int AAudio_createStreamBuilder(AAudioStreamBuilder** out) { *out = new AAudioStreamBuilder; return 0; }
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder*, int) {}
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder*, int) {}
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder*, int) {}
void AAudioStreamBuilder_setInputPreset(AAudioStreamBuilder*, int) {}
void AAudioStreamBuilder_setPrivacySensitive(AAudioStreamBuilder*, bool enabled) { privacy_sensitive.store(enabled); }
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder*, int) {}
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder*, int) {}
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder*, int) {}
int AAudioStreamBuilder_openStream(AAudioStreamBuilder*, AAudioStream** out) { *out = new AAudioStream; ++opened; return 0; }
int AAudioStreamBuilder_delete(AAudioStreamBuilder* builder) { delete builder; return 0; }
int AAudioStream_getSampleRate(AAudioStream*) { return mismatch_format ? 48000 : 16000; }
int AAudioStream_getChannelCount(AAudioStream*) { return 1; }
int AAudioStream_getFormat(AAudioStream*) { return AAUDIO_FORMAT_PCM_I16; }
std::int64_t AAudioStream_getFramesWritten(AAudioStream* stream) { return stream->frames_read + queued_keyword_frames.load(); }
std::int64_t AAudioStream_getFramesRead(AAudioStream* stream) { return stream->frames_read; }
int AAudioStream_requestStart(AAudioStream*) { return 0; }
int AAudioStream_requestStop(AAudioStream*) { return 0; }
int AAudioStream_close(AAudioStream* stream) { delete stream; ++closed; return 0; }
int AAudioStream_read(AAudioStream* stream, void* data, std::int32_t frames, std::int64_t) {
    std::this_thread::sleep_for(2ms);
    if (read_failure.exchange(false)) return -899;
    if (empty_capture) return 0;
    const auto queued = std::min<std::int64_t>(queued_keyword_frames.load(), frames);
    std::fill_n(static_cast<std::int16_t*>(data), frames, input_sample.load());
    if (queued > 0) {
        std::fill_n(static_cast<std::int16_t*>(data), static_cast<std::size_t>(queued), std::int16_t{777});
        queued_keyword_frames.fetch_sub(queued);
    }
    stream->frames_read += frames;
    return frames;
}

namespace hpvr::quest {
struct QuestVoiceDecoder::State { bool loaded = false; VoiceDecoderStats stats; };
QuestVoiceDecoder::QuestVoiceDecoder() : state_(std::make_unique<State>()) {}
QuestVoiceDecoder::~QuestVoiceDecoder() = default;
bool QuestVoiceDecoder::Load(const std::filesystem::path&, double) { ++model_loads; state_->loaded = true; return true; }
bool QuestVoiceDecoder::Begin() { ++decoder_begins; return state_->loaded; }
void QuestVoiceDecoder::End() {}
bool QuestVoiceDecoder::loaded() const noexcept { return state_->loaded; }
bool QuestVoiceDecoder::failed() const noexcept { return false; }
VoiceDecoderStats QuestVoiceDecoder::Stats() const noexcept { return state_->stats; }
bool QuestVoiceDecoder::Process(const std::int16_t* pcm, std::size_t count, float* seconds) {
    if (throw_decoder.exchange(false)) throw std::runtime_error("injected model runtime exception");
    ++decoded;
    state_->stats.steps += 2;
    if (emit_renewal.exchange(false)) ++state_->stats.stream_renewals;
    decoder_entered.store(true, std::memory_order_release);
    while (hold_decoder.load(std::memory_order_acquire)) hold_decoder.wait(true);
    *seconds = 0.8F;
    if (emit_short_keyword.exchange(false)) {
        ++state_->stats.keyword_hits; ++state_->stats.duration_rejects;
        state_->stats.last_keyword_seconds = 0.1F; return false;
    }
    const bool pretarget_word = std::find(pcm, pcm + count, 777) != pcm + count;
    if (pretarget_word) ++pretarget_keyword_decodes;
    const bool matched = emit_keyword.exchange(false, std::memory_order_acq_rel) || pretarget_word;
    if (matched) { ++state_->stats.keyword_hits; state_->stats.last_keyword_seconds = *seconds; }
    return matched;
}
}  // namespace hpvr::quest

int main() {
    hpvr::quest::QuestVoiceCast voice;
    try {
        Check(voice.Configure("fake-read-only-model"), "configure creates idle worker");
        std::this_thread::sleep_for(20ms);
        Check(opened == 0 && model_loads == 0 && decoded == 0, "disabled feature has no microphone/model/decoder work");
        Check(voice.Stats().capture_attempts == 0 && voice.Stats().total_samples == 0,
            "reading diagnostics cannot start input or fabricate counters");
        auto arm = Arm(1);
        arm.permission_granted = false;
        voice.SetListening(arm);
        Wait([&] { return voice.status() == VoiceCastStatus::NeedsPermission; }, "permission is explicit");
        Check(opened == 0 && model_loads == 0, "no capture or model activity before permission");
        arm.permission_granted = true; arm.focused = false;
        voice.SetListening(arm);
        Wait([&] { return voice.status() == VoiceCastStatus::Ready; }, "model loads asynchronously while capture stays disarmed");
        Check(opened == 0 && model_loads == 1, "model reads once, microphone remains closed without focus");
        arm.focused = true; arm.target_locked = false; voice.SetListening(arm);
        Wait([&] { return opened > 0 && voice.Stats().total_samples > 0; },
             "allowed gameplay warms input before targeting without recognition");
        Check(decoded == 0, "warm unarmed samples never reach the decoder");
        const auto warm_opens = opened.load();
        const auto warm_samples = voice.Stats().total_samples;
        input_sample.store(777);
        Wait([&] { return voice.Stats().total_samples > warm_samples + 1600; },
             "pretarget speech is drained while the microphone stays warm");
        Check(decoded == 0, "pretarget speech is not recognized or retained");
        input_sample.store(1);
        queued_keyword_frames.store(3360);
        arm.target_locked = true; voice.SetListening(arm);
        Wait([&] { return decoded > 0; }, "armed target starts input and acoustic processing");
        Check(opened == warm_opens && pretarget_keyword_decodes == 0,
              "first armed decode skips queued pretarget audio without restarting input");
        Check(privacy_sensitive, "capture requests Android privacy-sensitive input");
        VoiceCastEvent event;
        Check(!voice.PollEvent(arm, &event), "mere microphone samples cannot cast");
        emit_renewal.store(true);
        Wait([&] { return voice.Stats().stream_renewals == 1; },
             "numeric stream-renewal diagnostics are forwarded without a cast");
        Check(!voice.PollEvent(arm, &event), "stream renewal is not a recognition event");
        emit_keyword.store(true);
        bool accepted = false;
        Wait([&] { accepted |= voice.PollEvent(arm, &event); return accepted; }, "acoustic match reaches owning target");
        Check(event.generation == 1 && event.utterance_seconds == 0.8F, "event keeps target identity");
        Wait([&] { return voice.status() == VoiceCastStatus::AwaitingNewTarget; },
             "accepted keyword ends recognition while capture stays warm");
        Check(opened == warm_opens && opened == closed + 1, "acceptance does not restart healthy Android input");
        Check(!voice.PollEvent(arm, &event), "result is delivered only once");
        const auto prior_opens = opened.load();
        const auto prior_decoded = decoded.load();
        const auto accepted_samples = voice.Stats().total_samples;
        voice.SetListening(arm);
        input_sample.store(777);
        Wait([&] { return voice.Stats().total_samples > accepted_samples + 1600; },
             "Harry's own incantation is drained after a consumed attempt");
        Check(opened == prior_opens && decoded == prior_decoded,
              "held consumed lock keeps warm input but performs no recognition");
        input_sample.store(1);

        const auto prior_begins = decoder_begins.load();
        queued_keyword_frames.store(3360);
        arm = Arm(2); voice.SetListening(arm);
        Wait([&] { return decoder_begins > prior_begins && decoded > prior_decoded; },
             "new target lock permits fresh recognition on warm input");
        Check(opened == prior_opens && pretarget_keyword_decodes == 0 && !voice.PollEvent(arm, &event),
              "previous incantation queued before rearm cannot cast at the next target");
        arm.target_locked = false; voice.SetListening(arm);
        Wait([&] { return voice.status() == VoiceCastStatus::Ready; }, "target loss disarms recognition");
        const auto target_lost_decodes = decoded.load();
        std::this_thread::sleep_for(20ms);
        Check(opened == prior_opens && decoded == target_lost_decodes,
              "target loss neither closes the healthy mic nor decodes unarmed audio");
        arm.focused = false; voice.SetListening(arm);
        Wait([&] { return opened == closed; }, "headset focus loss closes microphone");
        Check(!voice.PollEvent(arm, &event), "unfocused target cannot receive speech result");
        arm.focused = true; arm.gameplay_allowed = false; voice.SetListening(arm);
        std::this_thread::sleep_for(20ms);
        Check(opened == closed, "menus cutscenes and disallowed gameplay cannot warm capture");

        hold_decoder.store(true); decoder_entered.store(false);
        arm = Arm(3); voice.SetListening(arm);
        Wait([&] { return decoder_entered.load(); }, "test pauses during inference");
        emit_keyword.store(true);
        arm.focused = false; voice.SetListening(arm);
        arm.focused = true; voice.SetListening(arm);
        hold_decoder.store(false); hold_decoder.notify_one();
        std::this_thread::sleep_for(30ms);
        Check(!voice.PollEvent(arm, &event), "disarm/rearm ABA discards inference that started before focus loss");
        emit_keyword.store(true);
        accepted = false;
        Wait([&] { accepted |= voice.PollEvent(arm, &event); return accepted; }, "fresh capture can recognize after focus recovery");

        arm.focused = false; voice.SetListening(arm);
        Wait([&] { return opened == closed; }, "focus loss closes the warm capture before new stream checks");
        mismatch_format.store(true);
        arm = Arm(4); voice.SetListening(arm);
        Wait([&] { return voice.status() == VoiceCastStatus::MicrophoneUnavailable; }, "unexpected input sample rate fails closed");
        Check(voice.microphone_error() == AAUDIO_ERROR_INVALID_FORMAT, "format error is available to UI");
        Check(opened == closed && !voice.PollEvent(arm, &event), "bad-rate microphone is closed without recognition");
        mismatch_format.store(false);
        Wait([&] { return voice.status() == VoiceCastStatus::Listening; },
            "same held lock automatically retries a recoverable microphone failure");
        const auto before_fifth = decoder_begins.load();
        arm = Arm(5); voice.SetListening(arm);
        Wait([&] { return decoder_begins > before_fifth && voice.status() == VoiceCastStatus::Listening; },
            "fresh lock starts its own decoder, not the previous generation's status");
        const auto continuous_opens = opened.load();
        const auto continuous_begins = decoder_begins.load();
        const auto continuous_decodes = decoded.load();
        Wait([&] { return decoded >= continuous_decodes + 900; },
            "healthy capture processes eighteen seconds of samples without a forced retry", 20000ms);
        Check(opened == continuous_opens && decoder_begins == continuous_begins,
            "healthy microphone and keyword state remain continuous across six-second boundaries");
        Check(!voice.PollEvent(arm, &event), "long healthy input does not itself produce a spell");
        emit_keyword.store(true); accepted = false;
        Wait([&] { accepted |= voice.PollEvent(arm, &event); return accepted; },
            "keyword after long continuous input works without changing the target");
        arm.focused = false; voice.SetListening(arm);
        Wait([&] { return opened == closed; }, "focus loss closes warm input after a held cast");
        empty_capture.store(true);
        const auto before_sixth = opened.load();
        arm = Arm(6); voice.SetListening(arm);
        Wait([&] { return opened > before_sixth && voice.status() == VoiceCastStatus::Listening; },
            "empty capture starts its own bounded attempt");
        const auto empty_opens = opened.load();
        Wait([&] { return opened > empty_opens; },
            "a stream returning no frames is recycled after six wall-clock seconds", 6500ms);
        Check(!voice.PollEvent(arm, &event), "empty capture never produces a spell");
        empty_capture.store(false);
        read_failure.store(true);
        Wait([&] { return voice.status() == VoiceCastStatus::MicrophoneUnavailable; }, "read errors close the disconnected stream");
        Check(opened == closed, "failed read has no live microphone during retry backoff");
        const auto failed_opens = opened.load();
        arm.focused = false; voice.SetListening(arm);
        std::this_thread::sleep_for(300ms);
        Check(opened == failed_opens && opened == closed, "focus loss suppresses automatic retry");
        arm.focused = true; voice.SetListening(arm);
        Wait([&] { return voice.status() == VoiceCastStatus::Listening; }, "focus recovery restarts the same target after stream loss");
        emit_keyword.store(true); accepted = false;
        Wait([&] { accepted |= voice.PollEvent(arm, &event); return accepted; }, "recovered input produces a fresh valid keyword");
        for (std::uint64_t generation = 7; generation <= 30; ++generation) {
            const auto before_next = decoder_begins.load();
            const auto repeated_opens = opened.load();
            arm = Arm(generation); voice.SetListening(arm);
            Wait([&] { return decoder_begins > before_next && voice.status() == VoiceCastStatus::Listening; },
                "new attempt can rearm while target and trigger stay held");
            Check(opened == repeated_opens, "repeated casts do not restart the healthy microphone");
            emit_keyword.store(true); accepted = false;
            Wait([&] { accepted |= voice.PollEvent(arm, &event); return accepted; },
                "repeated held-target attempt receives exactly its own keyword");
            Check(event.generation == generation, "repeated result keeps fresh generation");
        }
        input_sample.store(0); arm = Arm(31); voice.SetListening(arm);
        Wait([&] { const auto stats = voice.Stats(); return stats.generation == 31 &&
            stats.input_window_samples >= 4000 && stats.input_peak == 0; },
            "new capture publishes its own zero-input diagnostic window");
        auto stats = voice.Stats();
        Check(stats.total_samples > 0 && stats.decoder_steps > 0 && stats.input_nonzero == 0 && stats.input_rms == 0,
            "zero-valued PCM is distinguished from an unstarted decoder");
        Check(!voice.PollEvent(arm, &event), "input diagnostics never create a keyword");
        input_sample.store(32767);
        Wait([&] { const auto value = voice.Stats(); return value.input_window_samples >= 4000 &&
            value.input_clipped == value.input_window_samples && value.input_rms == 32767; },
            "clipping diagnostics cover a whole fresh window");
        const auto hits = voice.Stats().keyword_hits;
        emit_short_keyword.store(true);
        Wait([&] { const auto value = voice.Stats(); return value.keyword_hits > hits && value.duration_rejects > 0; },
            "keyword hit and duration rejection are separately visible");
        Check(!voice.PollEvent(arm, &event), "a duration-rejected keyword is not an accepted event");
        const auto rejection_opens = opened.load(), rejection_begins = decoder_begins.load();
        for (unsigned rejected = 1; rejected < 16; ++rejected) {
            const auto previous_rejects = voice.Stats().duration_rejects;
            emit_short_keyword.store(true);
            Wait([&] { return voice.Stats().duration_rejects > previous_rejects; },
                "test supplies a separate duration-rejected detection");
        }
        Wait([&] { return decoder_begins > rejection_begins; }, "rejected KWS history is bounded");
        Check(opened == rejection_opens, "bounding rejected history does not close healthy input");
        stats = voice.Stats();
        Check(stats.accepted_events > 20 && stats.stale_discards > 0 && stats.errors > 0 &&
            stats.retries >= 3 && stats.read_zeroes > 0 && stats.last_keyword_seconds == 0.1F,
            "numeric snapshot exposes acceptance, stale results, recovery and short-word failure");
        arm.permission_granted = false; voice.SetListening(arm);
        Wait([&] { return opened == closed && voice.status() == VoiceCastStatus::NeedsPermission; },
             "permission revocation closes even a warm microphone");
        const auto revoked_decodes = decoded.load();
        std::this_thread::sleep_for(20ms);
        Check(decoded == revoked_decodes, "revoked permission permits no background recognition");
        arm.permission_granted = true; arm.target_locked = false; voice.SetListening(arm);
        Wait([&] { return opened == closed + 1; }, "restored permission may warm input in allowed gameplay");
        arm.enabled = false; voice.SetListening(arm);
        Wait([&] { return opened == closed && voice.status() == VoiceCastStatus::Disabled; },
             "disabling voice casting closes warm input without waiting for a target");
        voice.Shutdown();
        Check(opened == closed && voice.status() == VoiceCastStatus::Disabled, "shutdown closes all native capture resources");
        Check(voice.Configure("fake-read-only-model"), "diagnostic lifecycle can be initialized again");
        stats = voice.Stats();
        Check(stats.generation == 0 && stats.capture_attempts == 0 && stats.total_samples == 0 &&
            stats.keyword_hits == 0 && stats.input_window_samples == 0 && stats.input_rms == 0,
            "Configure resets lifetime counters and previous input levels");
        throw_decoder.store(true);
        arm = Arm(32); voice.SetListening(arm);
        Wait([&] { return voice.status() == VoiceCastStatus::ModelUnavailable; },
            "model runtime exception is visible rather than terminating the application");
        Check(opened == closed && !voice.PollEvent(arm, &event),
            "model exception closes the microphone and cannot deliver a stale cast");
        Check(voice.Stats().decoder_errors == 1, "model exception increments numeric error evidence");
        voice.Shutdown();
        std::cout << "voice worker lifecycle: PASS (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& error) {
        hold_decoder.store(false); hold_decoder.notify_one(); voice.Shutdown();
        std::cerr << "voice worker lifecycle: FAIL: " << error.what() << '\n'; return 1;
    }
}
