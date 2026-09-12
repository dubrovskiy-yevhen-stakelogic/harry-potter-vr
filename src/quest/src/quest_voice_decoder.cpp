#include "hpvr/quest_voice_decoder.h"
#include "hpvr/quest_voice_cast.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <system_error>

namespace hpvr::quest {
struct QuestVoiceDecoder::State {
    const SherpaOnnxKeywordSpotter* spotter = nullptr;
    const SherpaOnnxOnlineStream* stream = nullptr;
    bool failed = false;
    VoiceDecoderStats stats{};
    // One fixed 20 ms analysis window makes the boundary independent of AAudio
    // read sizes. This is not a speech gate: every sample still reaches KWS.
    std::array<std::int16_t, 320> frame{};
    std::size_t frame_size = 0;
    // Eight 40 ms token-grid intervals; transient RAM only, never recorded.
    std::array<std::int16_t, 5120> history{};
    std::size_t history_size = 0, history_write = 0, quiet_samples = 0;
    bool renew_pending = false, accepted = false;
    void ClearAttempt() {
        frame.fill(0);
        history.fill(0);
        frame_size = history_size = history_write = quiet_samples = 0;
        renew_pending = accepted = false;
    }
    ~State() {
        if (stream != nullptr) SherpaOnnxDestroyOnlineStream(stream);
        if (spotter != nullptr) SherpaOnnxDestroyKeywordSpotter(spotter);
    }
};

QuestVoiceDecoder::QuestVoiceDecoder() : state_(std::make_unique<State>()) {}
QuestVoiceDecoder::~QuestVoiceDecoder() = default;

bool QuestVoiceDecoder::Load(const std::filesystem::path& directory, double threshold) {
    state_ = std::make_unique<State>();
    if (!std::isfinite(threshold) || threshold < 0.05 || threshold > 0.95) return false;
    for (const char* file : {"encoder.int8.onnx", "decoder.onnx", "joiner.int8.onnx",
                            "tokens.txt", "flipendo.keywords"}) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(directory / file, error) || error) return false;
    }
    const auto encoder = (directory / "encoder.int8.onnx").string();
    const auto decoder = (directory / "decoder.onnx").string();
    const auto joiner = (directory / "joiner.int8.onnx").string();
    const auto tokens = (directory / "tokens.txt").string();
    const auto keywords = (directory / "flipendo.keywords").string();
    SherpaOnnxKeywordSpotterConfig config{};
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer.encoder = encoder.c_str();
    config.model_config.transducer.decoder = decoder.c_str();
    config.model_config.transducer.joiner = joiner.c_str();
    config.model_config.tokens = tokens.c_str();
    config.model_config.provider = "cpu";
    config.model_config.num_threads = 1;
    config.model_config.debug = 0;
    config.max_active_paths = 16;
    config.num_trailing_blanks = 1;
    config.keywords_score = 1.0F;
    config.keywords_threshold = static_cast<float>(threshold);
    config.keywords_file = keywords.c_str();
    // Fixed keyword spotting, not a one-word forced transcription grammar.
    // A complete allowed pronunciation path must survive acoustic decoding.
    state_->spotter = SherpaOnnxCreateKeywordSpotter(&config);
    state_->failed = state_->spotter == nullptr;
    return !state_->failed;
}

bool QuestVoiceDecoder::Begin() {
    End();
    if (!loaded()) return false;
    state_->stream = SherpaOnnxCreateKeywordStream(state_->spotter);
    state_->failed = state_->stream == nullptr;
    return !state_->failed;
}

bool QuestVoiceDecoder::Process(const std::int16_t* samples,
                               std::size_t count, float* duration) {
    if (state_->stream == nullptr || samples == nullptr || duration == nullptr ||
        state_->failed || count == 0 || count > 16000) return false;
    bool matched = false;
    std::array<float, 320> normalized{};
    unsigned decode_calls = 0;
    for (std::size_t offset = 0; offset < count;) {
        const auto size = std::min(state_->frame.size() - state_->frame_size, count - offset);
        std::copy_n(samples + offset, size, state_->frame.data() + state_->frame_size);
        state_->frame_size += size;
        offset += size;
        if (state_->frame_size != state_->frame.size()) continue;
        state_->frame_size = 0;
        std::uint64_t squares = 0;
        int peak = 0;
        for (const auto sample : state_->frame) {
            const int value = sample;
            squares += static_cast<std::uint64_t>(static_cast<std::int64_t>(value) * value);
            peak = std::max(peak, std::abs(value));
        }
        const bool quiet = squares <= 160ULL * 160ULL * state_->frame.size() && peak <= 1200;
        if (state_->renew_pending && !quiet && !state_->accepted) {
            // Repeated unsuccessful speech must not inherit indefinitely old
            // hypotheses/encoder state. Renew only on a new onset after 600 ms
            // of stable quiet, never on a wall-clock timer or during a word.
            // A whole stream also resets its processed-frame offset; the
            // upstream partial Reset retains that offset and is not equivalent.
            const auto* fresh = SherpaOnnxCreateKeywordStream(state_->spotter);
            if (fresh == nullptr) { state_->failed = true; return false; }
            SherpaOnnxDestroyOnlineStream(state_->stream);
            state_->stream = fresh;
            const auto begin = (state_->history_write + state_->history.size() -
                                state_->history_size) % state_->history.size();
            for (std::size_t at = 0; at < state_->history_size; at += normalized.size()) {
                const auto replay = std::min(normalized.size(), state_->history_size - at);
                for (std::size_t i = 0; i < replay; ++i)
                    normalized[i] = static_cast<float>(state_->history[(begin + at + i) %
                                                     state_->history.size()]) / 32768.0F;
                SherpaOnnxOnlineStreamAcceptWaveform(state_->stream, 16000, normalized.data(),
                                                    static_cast<std::int32_t>(replay));
            }
            state_->renew_pending = false;
            ++state_->stats.stream_renewals;
        }
        for (std::size_t index = 0; index < state_->frame.size(); ++index)
            normalized[index] = static_cast<float>(state_->frame[index]) / 32768.0F;
        SherpaOnnxOnlineStreamAcceptWaveform(state_->stream, 16000, normalized.data(),
                                            static_cast<std::int32_t>(normalized.size()));
        normalized.fill(0);
        state_->quiet_samples = quiet ? std::min<std::size_t>(9600, state_->quiet_samples +
                                                             state_->frame.size()) : 0;
        if (state_->quiet_samples == 9600) state_->renew_pending = true;
        for (const auto sample : state_->frame) {
            state_->history[state_->history_write] = sample;
            state_->history_write = (state_->history_write + 1) % state_->history.size();
            state_->history_size = std::min(state_->history_size + 1, state_->history.size());
        }
        state_->frame.fill(0);
        while (SherpaOnnxIsKeywordStreamReady(state_->spotter, state_->stream)) {
            // A bounded API input must never cause an unbounded decoder loop.
            if (++decode_calls > 64) { state_->failed = true; return false; }
            SherpaOnnxDecodeKeywordStream(state_->spotter, state_->stream);
            ++state_->stats.steps;
            const auto* result = SherpaOnnxGetKeywordResult(state_->spotter, state_->stream);
            if (result == nullptr) { state_->failed = true; return false; }
            if (result->keyword != nullptr && result->keyword[0] != '\0') {
                // Do not retain recognized text or a waveform. Only a fixed
                // identity, duration and numeric counters cross this boundary.
                if (std::strcmp(result->keyword, "FLIPENDO") == 0 && result->count >= 2 &&
                    result->count <= 32 && result->timestamps != nullptr) {
                    ++state_->stats.keyword_hits;
                    bool timing_valid = true;
                    for (std::int32_t index = 0; index < result->count; ++index) {
                        const float time = result->timestamps[index];
                        if (!std::isfinite(time) || time < 0 ||
                            (index > 0 && time < result->timestamps[index - 1])) timing_valid = false;
                    }
                    // This pinned transducer emits tokens on a 40 ms grid.
                    const float seconds = result->timestamps[result->count - 1] -
                                          result->timestamps[0] + 0.04F;
                    state_->stats.last_keyword_seconds = timing_valid ? seconds : 0;
                    if (timing_valid && seconds >= kVoiceMinWordSeconds && seconds <= kVoiceMaxWordSeconds) {
                        *duration = seconds;
                        matched = true;
                        // The game closes capture on its first accepted word.
                        // Preserve normal repeated-word streaming for offline
                        // callers rather than renewing after successful speech.
                        state_->accepted = true;
                    } else ++state_->stats.duration_rejects;
                }
                // The pinned transducer clears matched hypotheses itself and
                // GetKeywordResult suppresses duplicate timestamps. An extra
                // Reset here discards encoder context while retaining the
                // stream's processed-frame count, losing later spoken words.
                // Begin starts a fresh stream for the next gameplay generation.
            }
            SherpaOnnxDestroyKeywordResult(result);
        }
    }
    return matched;
}

void QuestVoiceDecoder::End() {
    if (state_->stream != nullptr) {
        // Cancellation never flushes trailing audio into a late cast.
        SherpaOnnxDestroyOnlineStream(state_->stream);
        state_->stream = nullptr;
    }
    state_->ClearAttempt();
}
bool QuestVoiceDecoder::loaded() const noexcept { return state_->spotter != nullptr; }
bool QuestVoiceDecoder::failed() const noexcept { return state_->failed; }
VoiceDecoderStats QuestVoiceDecoder::Stats() const noexcept { return state_->stats; }
}  // namespace hpvr::quest
