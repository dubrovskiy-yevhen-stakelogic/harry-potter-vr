#include "quest_voice_cast_android.h"
#include "hpvr/quest_voice_decoder.h"
#if defined(HPVR_VOICE_DIAGNOSTICS) && HPVR_VOICE_DIAGNOSTICS
#include "hpvr/quest_voice_recording_diagnostic.h"
#include <android/log.h>
#endif

#include <aaudio/AAudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <thread>

namespace hpvr::quest {
namespace {
constexpr std::uint64_t kShutdown = std::uint64_t{1} << 63;
constexpr std::int32_t kSampleRate = 16000;
constexpr std::int32_t kBlockFrames = 320;
constexpr std::int64_t kReadTimeoutNs = 20'000'000;
constexpr std::uint32_t kLevelWindowSamples = kSampleRate / 4;
constexpr std::uint64_t kRejectedDetectionLimit = 16;
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);

struct MicrophoneDeleter {
    void operator()(AAudioStream* stream) const noexcept {
        if (stream != nullptr) {
            (void)AAudioStream_requestStop(stream);
            (void)AAudioStream_close(stream);
        }
    }
};

AAudioStream* OpenMicrophone(int* error) {
    AAudioStreamBuilder* builder = nullptr;
    *error = AAudio_createStreamBuilder(&builder);
    if (*error != AAUDIO_OK || builder == nullptr) {
        if (builder != nullptr) AAudioStreamBuilder_delete(builder);
        return nullptr;
    }
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
    AAudioStreamBuilder_setInputPreset(builder, AAUDIO_INPUT_PRESET_VOICE_RECOGNITION);
    AAudioStreamBuilder_setPrivacySensitive(builder, true);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setSampleRate(builder, kSampleRate);
    AAudioStream* stream = nullptr;
    *error = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (*error != AAUDIO_OK || stream == nullptr) {
        if (stream != nullptr) AAudioStream_close(stream);
        return nullptr;
    }
    // Fail closed rather than decode a mismatched rate or stereo as mono.
    // Shared AAudio may perform the requested conversion; verify its result.
    if (AAudioStream_getSampleRate(stream) != kSampleRate ||
        AAudioStream_getChannelCount(stream) != 1 ||
        AAudioStream_getFormat(stream) != AAUDIO_FORMAT_PCM_I16) {
        *error = AAUDIO_ERROR_INVALID_FORMAT;
        AAudioStream_close(stream);
        return nullptr;
    }
    *error = AAudioStream_requestStart(stream);
    if (*error != AAUDIO_OK) {
        AAudioStream_close(stream);
        return nullptr;
    }
    return stream;
}
}  // namespace

struct QuestVoiceCast::State {
    std::atomic<std::uint64_t> command{0};
    std::atomic<std::uint64_t> revision{0};
    std::atomic<std::uint64_t> event{0};
    std::atomic<std::uint64_t> event_revision{0};
    std::atomic<VoiceCastStatus> status{VoiceCastStatus::Disabled};
    std::atomic<int> microphone_error{0};
    std::atomic<std::uint64_t> generation{0}, capture_attempts{0}, total_samples{0};
    std::atomic<std::uint64_t> decoder_steps{0}, keyword_hits{0}, duration_rejects{0};
    std::atomic<std::uint64_t> stream_renewals{0};
    std::atomic<std::uint64_t> accepted_events{0}, stale_discards{0}, read_zeroes{0};
    std::atomic<std::uint64_t> errors{0}, decoder_errors{0}, retries{0};
    std::atomic<std::uint32_t> input_window_samples{0}, input_nonzero{0}, input_clipped{0}, input_peak{0};
    std::atomic<float> input_rms{0}, last_keyword_seconds{0};
    std::filesystem::path model_directory;
    std::thread worker;

    void Run() {
        QuestVoiceDecoder decoder;
        VoiceCastGate gate;
        std::array<std::int16_t, kBlockFrames> pcm{};
        VoiceInputMeter input_meter;
#if defined(HPVR_VOICE_DIAGNOSTICS) && HPVR_VOICE_DIAGNOSTICS
        VoiceRecordingDiagnostic diagnostic;
        bool diagnostic_requested = false;
        try {
            std::error_code diagnostic_path_error;
            const auto files_root = std::filesystem::canonical(
                model_directory.parent_path().parent_path(), diagnostic_path_error);
            if (!diagnostic_path_error) diagnostic_requested = diagnostic.Configure(files_root);
        } catch (...) { /* Optional diagnostics must never disable voice casting. */ }
        __android_log_print(ANDROID_LOG_INFO, "HPVR.Quest",
            "[hpvr.quest.voice.diagnostic] build=HPVR_LOCAL_VOICE_RECORDING_DIAGNOSTIC consent=%s max_seconds=30 network=NONE",
            diagnostic_requested ? "ONE_SHOT" : "ABSENT");
        bool diagnostic_reported = false;
        const auto report_diagnostic = [&](const char* finish_reason) {
            if (!diagnostic_reported && (diagnostic.finished() || diagnostic.failed())) {
                diagnostic_reported = true;
                __android_log_print(ANDROID_LOG_INFO, "HPVR.Quest",
                    "[hpvr.quest.voice.diagnostic] status=%s samples=%zu rate=16000 channels=1 finish_reason=%s network=NONE",
                    diagnostic.failed() ? "FAILED" : "SAVED", diagnostic.samples(),
                    diagnostic.failed() ? "DIAGNOSTIC_ERROR" : finish_reason);
            }
        };
        const auto finish_diagnostic = [&](const char* finish_reason) {
            // Startup may briefly be disabled before the first armed sample.
            // Do not consume that still-empty diagnostic session here.
            if (diagnostic.samples() != 0 && !diagnostic.finished()) (void)diagnostic.Finish();
            report_diagnostic(finish_reason);
        };
#endif
        // Close input even if the model runtime throws while decoding.
        std::unique_ptr<AAudioStream, MicrophoneDeleter> microphone;
        std::uint64_t active_generation = 0, retry_generation = 0;
        std::uint64_t rejected_at_start = 0;
        std::int64_t attempt_first_frame = 0;
        auto input_progress_deadline = std::chrono::steady_clock::time_point{};
        auto retry_after = std::chrono::steady_clock::time_point{};
        unsigned microphone_failures = 0;
        bool model_attempted = false;
        const auto publish_levels = [&] {
            input_window_samples.store(input_meter.samples, std::memory_order_relaxed);
            input_nonzero.store(input_meter.nonzero, std::memory_order_relaxed);
            input_clipped.store(input_meter.clipped, std::memory_order_relaxed);
            input_peak.store(input_meter.peak, std::memory_order_relaxed);
            input_rms.store(input_meter.Rms(), std::memory_order_relaxed);
            input_meter.Reset();
        };
        const auto reset_attempt = [&] {
            decoder.End();
#if defined(HPVR_VOICE_DIAGNOSTICS) && HPVR_VOICE_DIAGNOSTICS
            diagnostic.Boundary();
#endif
            // No recognition history crosses an attempt, focus change or disable.
            pcm.fill(0);
            active_generation = 0;
            attempt_first_frame = 0;
        };
        const auto close_capture = [&] {
            microphone.reset();
            reset_attempt();
            // An opted-in diagnostic pauses at this boundary. A transient
            // tracking/focus/gameplay loss must not end its total sample budget.
            if (input_meter.samples != 0) publish_levels();
        };
        const auto retry_capture = [&](const VoiceCastArm& arm, bool failure) {
            close_capture();
            retries.fetch_add(1, std::memory_order_relaxed);
            if (failure) errors.fetch_add(1, std::memory_order_relaxed);
            retry_generation = arm.generation;
            // Keep retrying while a valid lock is held. Neither a timeout nor
            // a transient Android input error permanently consumes the lock.
            const auto delay = failure ? 250U << std::min(microphone_failures++, 3U) : 100U;
            retry_after = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
            status.store(failure ? VoiceCastStatus::MicrophoneUnavailable : VoiceCastStatus::Ready,
                         std::memory_order_release);
        };
        for (;;) {
            const auto observed_revision = revision.load(std::memory_order_acquire);
            if ((observed_revision & 1) != 0) { std::this_thread::yield(); continue; }
            const auto observed = command.load(std::memory_order_acquire);
            if (revision.load(std::memory_order_acquire) != observed_revision) continue;
            if ((observed & kShutdown) != 0) break;
            const auto arm = UnpackVoiceArm(observed);
            if (!arm.enabled || !arm.permission_granted) {
                close_capture();
#if defined(HPVR_VOICE_DIAGNOSTICS) && HPVR_VOICE_DIAGNOSTICS
                finish_diagnostic(arm.enabled ? "PERMISSION_REVOKED" : "VOICE_DISABLED");
#endif
                status.store(arm.enabled ? VoiceCastStatus::NeedsPermission :
                             VoiceCastStatus::Disabled, std::memory_order_release);
                command.wait(observed, std::memory_order_acquire);
                continue;
            }
            if (!model_attempted) {
                status.store(VoiceCastStatus::LoadingModel, std::memory_order_release);
                model_attempted = true;
                (void)decoder.Load(model_directory);
                // A focus/permission/target change during model IO must be
                // observed before opening the microphone.
                continue;
            }
            if (!decoder.loaded()) {
                close_capture();
#if defined(HPVR_VOICE_DIAGNOSTICS) && HPVR_VOICE_DIAGNOSTICS
                finish_diagnostic("MODEL_UNAVAILABLE");
#endif
                status.store(VoiceCastStatus::ModelUnavailable, std::memory_order_release);
                command.wait(observed, std::memory_order_acquire);
                continue;
            }
            if (!VoiceMayCapture(arm)) {
                close_capture();
                status.store(VoiceCastStatus::Ready, std::memory_order_release);
                command.wait(observed, std::memory_order_acquire);
                continue;
            }
            if (arm.generation != retry_generation) {
                retry_after = {};
                microphone_failures = 0;
                retry_generation = arm.generation;
            }
            if (std::chrono::steady_clock::now() < retry_after) {
                // Only the worker sleeps, with a short cancellation bound.
                // There is no mutex or wait on the render thread.
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if (microphone == nullptr) {
                capture_attempts.fetch_add(1, std::memory_order_relaxed);
                publish_levels();  // Clear the previous capture's level window.
                int error = 0;
                microphone.reset(OpenMicrophone(&error));
                microphone_error.store(error, std::memory_order_release);
                if (microphone == nullptr) {
                    retry_capture(arm, true);
                    continue;
                }
                input_progress_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
                if (command.load(std::memory_order_acquire) != observed ||
                    revision.load(std::memory_order_acquire) != observed_revision) continue;
            }
            const bool listening = gate.CanListen(arm);
            if (!listening) {
                if (active_generation != 0) reset_attempt();
                status.store(VoiceMayListen(arm) ? VoiceCastStatus::AwaitingNewTarget :
                             VoiceCastStatus::Ready, std::memory_order_release);
            } else if (active_generation != arm.generation) {
                reset_attempt();
                generation.store(arm.generation, std::memory_order_relaxed);
                input_meter.Reset();
                publish_levels();  // A level window belongs to this attempt.
                // AAudio may still have queued input from the previous target
                // or Harry's own incantation. Do not give it to the new stream.
                attempt_first_frame = AAudioStream_getFramesWritten(microphone.get());
                if (!decoder.Begin()) {
                    decoder_errors.fetch_add(1, std::memory_order_relaxed);
                    retry_capture(arm, true); continue;
                }
                active_generation = arm.generation;
                rejected_at_start = decoder.Stats().duration_rejects;
                status.store(VoiceCastStatus::Listening, std::memory_order_release);
            }
            if (std::chrono::steady_clock::now() >= input_progress_deadline) {
                retry_capture(arm, false);
                continue;
            }
            const auto read_first_frame = AAudioStream_getFramesRead(microphone.get());
            const auto frames = AAudioStream_read(microphone.get(), pcm.data(),
                                                  kBlockFrames, kReadTimeoutNs);
            if (frames > 0) {
                input_progress_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
                total_samples.fetch_add(static_cast<std::uint64_t>(frames), std::memory_order_relaxed);
                input_meter.Add(pcm.data(), static_cast<std::size_t>(frames));
                if (input_meter.samples >= kLevelWindowSamples) publish_levels();
            } else if (frames == 0) read_zeroes.fetch_add(1, std::memory_order_relaxed);
            if (frames < 0) {
                microphone_error.store(frames, std::memory_order_release);
                retry_capture(arm, true);
                continue;
            }
            if (command.load(std::memory_order_acquire) != observed ||
                revision.load(std::memory_order_acquire) != observed_revision) {
                stale_discards.fetch_add(1, std::memory_order_relaxed);
                reset_attempt();
                continue;
            }
            if (frames == 0) continue;
            microphone_failures = 0;
            if (!listening) {
                // Drain while warm. Never feed unarmed audio to recognition.
                pcm.fill(0);
                continue;
            }
            const auto skipped = static_cast<std::size_t>(std::clamp<std::int64_t>(
                attempt_first_frame - read_first_frame, 0, frames));
            if (skipped == static_cast<std::size_t>(frames)) {
                pcm.fill(0);
                continue;
            }
            float duration = 0;
#if defined(HPVR_VOICE_DIAGNOSTICS) && HPVR_VOICE_DIAGNOSTICS
            diagnostic.Observe(active_generation, pcm.data() + skipped,
                static_cast<std::size_t>(frames) - skipped);
#endif
            const bool matched = decoder.Process(pcm.data() + skipped,
                static_cast<std::size_t>(frames) - skipped, &duration);
#if defined(HPVR_VOICE_DIAGNOSTICS) && HPVR_VOICE_DIAGNOSTICS
            if (matched) diagnostic.MarkMatch();
            report_diagnostic(diagnostic.samples() >= VoiceRecordingDiagnostic::kMaxSamples ?
                "SAMPLE_LIMIT" : "SEGMENT_LIMIT");
#endif
            const auto decoded = decoder.Stats();
            decoder_steps.store(decoded.steps, std::memory_order_relaxed);
            keyword_hits.store(decoded.keyword_hits, std::memory_order_relaxed);
            duration_rejects.store(decoded.duration_rejects, std::memory_order_relaxed);
            stream_renewals.store(decoded.stream_renewals, std::memory_order_relaxed);
            last_keyword_seconds.store(decoded.last_keyword_seconds, std::memory_order_relaxed);
            pcm.fill(0);
            if (command.load(std::memory_order_acquire) != observed ||
                revision.load(std::memory_order_acquire) != observed_revision) {
                stale_discards.fetch_add(1, std::memory_order_relaxed);
                reset_attempt();
                continue;
            }
            if (decoder.failed()) {
                decoder_errors.fetch_add(1, std::memory_order_relaxed);
                retry_capture(arm, true); continue;
            }
            VoiceCastEvent accepted;
            if (matched && command.load(std::memory_order_acquire) == observed &&
                revision.load(std::memory_order_acquire) == observed_revision &&
                gate.Accept(arm, active_generation, "flipendo", duration, &accepted)) {
                accepted_events.fetch_add(1, std::memory_order_relaxed);
                const auto centiseconds = static_cast<std::uint64_t>(std::lround(duration * 100));
                event_revision.store(observed_revision, std::memory_order_release);
                event.store((accepted.generation << 8) | centiseconds, std::memory_order_release);
                reset_attempt();
            } else if (decoded.duration_rejects - rejected_at_start >= kRejectedDetectionLimit) {
                // KWS keeps a list of non-overlapping detections. Bound rejected
                // entries without periodically closing healthy microphone input
                // or cutting ordinary words at an arbitrary six-second mark.
#if defined(HPVR_VOICE_DIAGNOSTICS) && HPVR_VOICE_DIAGNOSTICS
                diagnostic.Boundary();
#endif
                if (!decoder.Begin()) {
                    decoder_errors.fetch_add(1, std::memory_order_relaxed);
                    retry_capture(arm, true);
                } else rejected_at_start = decoder.Stats().duration_rejects;
            }
        }
        close_capture();
#if defined(HPVR_VOICE_DIAGNOSTICS) && HPVR_VOICE_DIAGNOSTICS
        finish_diagnostic("SHUTDOWN");
#endif
        event.store(0, std::memory_order_release);
        status.store(VoiceCastStatus::Disabled, std::memory_order_release);
    }
};

QuestVoiceCast::QuestVoiceCast() : state_(std::make_unique<State>()) {}
QuestVoiceCast::~QuestVoiceCast() { Shutdown(); }

bool QuestVoiceCast::Configure(const std::filesystem::path& directory) {
    if (state_->worker.joinable()) return directory == state_->model_directory;
    if (directory.empty()) return false;
    state_->model_directory = directory;
    state_->command.store(0, std::memory_order_release);
    state_->event.store(0, std::memory_order_release);
    for (auto* count : {&state_->generation, &state_->capture_attempts, &state_->total_samples,
            &state_->decoder_steps, &state_->keyword_hits, &state_->duration_rejects,
            &state_->accepted_events, &state_->stale_discards, &state_->read_zeroes,
            &state_->errors, &state_->decoder_errors, &state_->retries, &state_->stream_renewals})
        count->store(0, std::memory_order_relaxed);
    state_->input_window_samples.store(0, std::memory_order_relaxed);
    state_->input_nonzero.store(0, std::memory_order_relaxed);
    state_->input_clipped.store(0, std::memory_order_relaxed);
    state_->input_peak.store(0, std::memory_order_relaxed);
    state_->input_rms.store(0, std::memory_order_relaxed);
    state_->last_keyword_seconds.store(0, std::memory_order_relaxed);
    state_->microphone_error.store(0, std::memory_order_relaxed);
    try {
        state_->worker = std::thread([state = state_.get()] {
            try { state->Run(); }
            catch (const std::exception&) {
                state->errors.fetch_add(1, std::memory_order_relaxed);
                state->decoder_errors.fetch_add(1, std::memory_order_relaxed);
                state->event.store(0, std::memory_order_release);
                state->status.store(VoiceCastStatus::ModelUnavailable, std::memory_order_release);
            }
        });
    } catch (const std::exception&) { return false; }
    return true;
}

void QuestVoiceCast::SetListening(const VoiceCastArm& arm) noexcept {
    if (!state_->worker.joinable()) return;
    const auto next = PackVoiceArm(arm);
    if (state_->command.load(std::memory_order_acquire) == next) return;
    // Single producer: the render/lifecycle owner calls SetListening. Revision
    // also rejects a quick disarm/rearm with the same target generation (ABA).
    state_->revision.fetch_add(1, std::memory_order_acq_rel);
    state_->command.store(next, std::memory_order_release);
    state_->event.store(0, std::memory_order_release);
    state_->revision.fetch_add(1, std::memory_order_release);
    state_->command.notify_one();
}

bool QuestVoiceCast::PollEvent(const VoiceCastArm& current, VoiceCastEvent* result) noexcept {
    if (result == nullptr) return false;
    const auto word = state_->event.exchange(0, std::memory_order_acq_rel);
    const VoiceCastEvent event{word >> 8, static_cast<float>(word & 255) / 100.0F};
    const auto revision = state_->revision.load(std::memory_order_acquire);
    if (word == 0 || (revision & 1) != 0 ||
        state_->event_revision.load(std::memory_order_acquire) != revision ||
        state_->command.load(std::memory_order_acquire) != PackVoiceArm(current) ||
        !VoiceEventIsCurrent(current, event)) return false;
    *result = event;
    return true;
}

VoiceCastStatus QuestVoiceCast::status() const noexcept {
    const auto arm = UnpackVoiceArm(state_->command.load(std::memory_order_acquire));
    if (!arm.enabled) return VoiceCastStatus::Disabled;
    if (!arm.permission_granted) return VoiceCastStatus::NeedsPermission;
    const auto status = state_->status.load(std::memory_order_acquire);
    if (!VoiceMayListen(arm) && status == VoiceCastStatus::Listening)
        return VoiceCastStatus::Ready;
    return status;
}
int QuestVoiceCast::microphone_error() const noexcept {
    return state_->microphone_error.load(std::memory_order_acquire);
}
VoiceCastStats QuestVoiceCast::Stats() const noexcept {
    const auto& s = *state_;
    return {s.generation.load(std::memory_order_relaxed),
        s.capture_attempts.load(std::memory_order_relaxed), s.total_samples.load(std::memory_order_relaxed),
        s.decoder_steps.load(std::memory_order_relaxed), s.keyword_hits.load(std::memory_order_relaxed),
        s.duration_rejects.load(std::memory_order_relaxed), s.accepted_events.load(std::memory_order_relaxed),
        s.stale_discards.load(std::memory_order_relaxed), s.read_zeroes.load(std::memory_order_relaxed),
        s.errors.load(std::memory_order_relaxed), s.decoder_errors.load(std::memory_order_relaxed),
        s.retries.load(std::memory_order_relaxed), s.input_window_samples.load(std::memory_order_relaxed),
        s.input_nonzero.load(std::memory_order_relaxed), s.input_clipped.load(std::memory_order_relaxed),
        s.input_peak.load(std::memory_order_relaxed), s.input_rms.load(std::memory_order_relaxed),
        s.last_keyword_seconds.load(std::memory_order_relaxed), s.microphone_error.load(std::memory_order_relaxed),
        s.stream_renewals.load(std::memory_order_relaxed)};
}
void QuestVoiceCast::Shutdown() {
    if (!state_->worker.joinable()) return;
    state_->command.store(kShutdown, std::memory_order_release);
    state_->command.notify_one();
    state_->worker.join();
    state_->command.store(0, std::memory_order_release);
}
}  // namespace hpvr::quest
