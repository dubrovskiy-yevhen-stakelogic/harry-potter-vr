#include "quest_audio.h"
#include "hpvr/quest_frontend.h"

#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace hpvr::quest {
namespace {

constexpr std::uint32_t kOutputRate = 48000;
constexpr float kAmbientGain = 0.16F;
constexpr float kWandTraceGain = 0.28F;
constexpr float kWandStartGain = 0.72F;
constexpr float kSpellCastGain = 0.60F;
constexpr float kIncantationGain = 0.92F;
constexpr float kSpellHitGain = 0.72F;
constexpr const char* kAudioLogTag = "HPVRQuest";

std::int16_t ClampSample(const float value) {
    return static_cast<std::int16_t>(std::clamp(
        value, static_cast<float>(std::numeric_limits<std::int16_t>::min()),
        static_cast<float>(std::numeric_limits<std::int16_t>::max())));
}

}  // namespace

QuestAudio::~QuestAudio() { Stop(); }

std::vector<std::int16_t> QuestAudio::ResampleMono48k(
    const wand::Hp1PcmSound& source) {
    if (source.status != wand::Hp1ProfileStatus::ok ||
        source.sample_rate == 0 || source.samples.empty() ||
        (source.channel_count != 1 && source.channel_count != 2) ||
        source.samples.size() % source.channel_count != 0) {
        return {};
    }
    const std::size_t source_frames =
        source.samples.size() / source.channel_count;
    const std::uint64_t output_frames_wide =
        static_cast<std::uint64_t>(source_frames) * kOutputRate /
        source.sample_rate;
    if (output_frames_wide == 0 ||
        output_frames_wide > std::numeric_limits<std::size_t>::max()) {
        return {};
    }
    std::vector<std::int16_t> output(
        static_cast<std::size_t>(output_frames_wide));
    const auto mono = [&source](const std::size_t frame) {
        const std::size_t base = frame * source.channel_count;
        if (source.channel_count == 1) return source.samples[base];
        return static_cast<std::int16_t>(
            (static_cast<std::int32_t>(source.samples[base]) +
             static_cast<std::int32_t>(source.samples[base + 1])) /
            2);
    };
    for (std::size_t index = 0; index < output.size(); ++index) {
        const double source_position =
            static_cast<double>(index) * source.sample_rate / kOutputRate;
        const std::size_t first = std::min(
            static_cast<std::size_t>(source_position), source_frames - 1);
        const std::size_t second = std::min(first + 1, source_frames - 1);
        const float fraction = static_cast<float>(
            source_position - static_cast<double>(first));
        output[index] = ClampSample(
            static_cast<float>(mono(first)) * (1.0F - fraction) +
            static_cast<float>(mono(second)) * fraction);
    }
    return output;
}

wand::Hp1PcmSound QuestAudio::DecodeMpeg(
    const wand::Hp1MpegSound& source) {
    wand::Hp1PcmSound decoded;
    if (source.status != wand::Hp1ProfileStatus::ok ||
        source.encoded_bytes.size() < 4 || source.sample_rate == 0 ||
        source.channel_count == 0) {
        decoded.error = "invalid MPEG source";
        return decoded;
    }
    const char* mime = "audio/mpeg-L2";
    AMediaCodec* codec = AMediaCodec_createDecoderByType(mime);
    if (codec == nullptr) {
        mime = "audio/mpeg";
        codec = AMediaCodec_createDecoderByType(mime);
    }
    if (codec == nullptr) {
        decoded.error = "Quest has no MPEG Layer II decoder";
        return decoded;
    }
    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE,
                          static_cast<std::int32_t>(source.sample_rate));
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT,
                          static_cast<std::int32_t>(source.channel_count));
    const bool configured =
        AMediaCodec_configure(codec, format, nullptr, nullptr, 0) ==
            AMEDIA_OK &&
        AMediaCodec_start(codec) == AMEDIA_OK;
    AMediaFormat_delete(format);
    if (!configured) {
        AMediaCodec_delete(codec);
        decoded.error = "Quest MPEG Layer II decoder rejected stream";
        return decoded;
    }

    constexpr std::array<int, 16> kMpeg1Layer2Kbps{
        0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0};
    constexpr std::array<int, 16> kMpeg2Layer2Kbps{
        0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    std::size_t source_offset = 0;
    std::uint64_t queued_frames = 0;
    bool input_eos = false;
    bool output_eos = false;
    int idle_polls = 0;
    while (!output_eos && idle_polls < 250) {
        bool progressed = false;
        if (!input_eos) {
            const ssize_t input_index =
                AMediaCodec_dequeueInputBuffer(codec, 2000);
            if (input_index >= 0) {
                std::size_t capacity = 0;
                std::uint8_t* input = AMediaCodec_getInputBuffer(
                    codec, static_cast<std::size_t>(input_index), &capacity);
                if (input == nullptr) break;
                if (source_offset >= source.encoded_bytes.size()) {
                    AMediaCodec_queueInputBuffer(
                        codec, static_cast<std::size_t>(input_index), 0, 0,
                        static_cast<std::uint64_t>(queued_frames * 1152ULL *
                            1000000ULL / source.sample_rate),
                        AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    input_eos = true;
                } else {
                    if (source.encoded_bytes.size() - source_offset < 4) break;
                    const auto* frame = source.encoded_bytes.data() + source_offset;
                    const std::uint32_t header =
                        (static_cast<std::uint32_t>(frame[0]) << 24U) |
                        (static_cast<std::uint32_t>(frame[1]) << 16U) |
                        (static_cast<std::uint32_t>(frame[2]) << 8U) |
                        static_cast<std::uint32_t>(frame[3]);
                    const unsigned version = (header >> 19U) & 3U;
                    const unsigned bitrate_index = (header >> 12U) & 15U;
                    const unsigned padding = (header >> 9U) & 1U;
                    const int kbps = version == 3
                        ? kMpeg1Layer2Kbps[bitrate_index]
                        : kMpeg2Layer2Kbps[bitrate_index];
                    const std::size_t frame_bytes = static_cast<std::size_t>(
                        144000LL * kbps / source.sample_rate + padding);
                    if ((header & 0xFFE60000U) != 0xFFE40000U || version == 1 || kbps == 0 ||
                        frame_bytes < 4 || frame_bytes > capacity ||
                        frame_bytes > source.encoded_bytes.size() - source_offset) {
                        break;
                    }
                    std::memcpy(input, frame, frame_bytes);
                    AMediaCodec_queueInputBuffer(
                        codec, static_cast<std::size_t>(input_index), 0,
                        frame_bytes,
                        static_cast<std::uint64_t>(queued_frames * 1152ULL *
                            1000000ULL / source.sample_rate), 0);
                    source_offset += frame_bytes;
                    ++queued_frames;
                }
                progressed = true;
            }
        }
        AMediaCodecBufferInfo info{};
        const ssize_t output_index =
            AMediaCodec_dequeueOutputBuffer(codec, &info, 2000);
        if (output_index >= 0) {
            std::size_t capacity = 0;
            std::uint8_t* output = AMediaCodec_getOutputBuffer(
                codec, static_cast<std::size_t>(output_index), &capacity);
            if (output != nullptr && info.size > 0 && info.offset >= 0 &&
                static_cast<std::size_t>(info.offset) <= capacity &&
                static_cast<std::size_t>(info.size) <=
                    capacity - static_cast<std::size_t>(info.offset)) {
                const auto* samples = reinterpret_cast<const std::int16_t*>(
                    output + info.offset);
                decoded.samples.insert(decoded.samples.end(), samples,
                    samples + static_cast<std::size_t>(info.size) / 2U);
            }
            output_eos = (info.flags &
                          AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            AMediaCodec_releaseOutputBuffer(
                codec, static_cast<std::size_t>(output_index), false);
            progressed = true;
        }
        idle_polls = progressed ? 0 : idle_polls + 1;
    }
    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    if (decoded.samples.empty()) {
        decoded = {};
        decoded.error = "Quest MPEG Layer II decoder produced no PCM";
        return decoded;
    }
    decoded.status = wand::Hp1ProfileStatus::ok;
    decoded.object_name = source.object_name;
    decoded.sample_rate = source.sample_rate;
    decoded.channel_count = source.channel_count;
    return decoded;
}

bool QuestAudio::Configure(const wand::Hp1PcmSound& ambient,
                           const wand::Hp1MpegSound& wand_trace,
                           const wand::Hp1MpegSound& wand_start,
                           const wand::Hp1MpegSound& spell_cast,
                           const wand::Hp1MpegSound& incantation,
                           const wand::Hp1PcmSound& spell_hit,
                           const wand::Hp1MpegSound& basic_cast,
                           const std::vector<wand::Hp1MpegSound>& dialogue,
                           const std::filesystem::path& cache_directory) {
    if (stream_ != nullptr) return false;
    const auto decode = [&cache_directory](const wand::Hp1MpegSound& source) {
        // Private, owned-data PCM cache: the encoded-source checksum prevents
        // accidentally playing a clip from another language/package revision.
        std::uint32_t hash = 2166136261U;
        for (const auto byte : source.encoded_bytes) hash = (hash ^ byte) * 16777619U;
        std::ostringstream filename;
        filename << source.object_name << '.' << std::hex << std::setfill('0')
                 << std::setw(8) << hash << ".s16";
        const auto path = cache_directory / filename.str();
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (input) {
            const auto size = input.tellg();
            if (size > 960 && size <= 48000 * 2 * 180 && size % 2 == 0) {
                std::vector<std::int16_t> samples(static_cast<std::size_t>(size) / 2U);
                input.seekg(0);
                input.read(reinterpret_cast<char*>(samples.data()), size);
                if (input && std::ranges::any_of(samples, [](auto v) { return v != 0; })) {
                    __android_log_print(ANDROID_LOG_INFO, kAudioLogTag,
                        "[hpvr.quest.audio.cache] status=PCM_READY clip=%s samples=%zu rate=48000",
                        source.object_name.c_str(), samples.size());
                    return samples;
                }
            }
        }
        const auto decoded = DecodeMpeg(source);
        auto samples = ResampleMono48k(decoded);
        if (samples.empty()) __android_log_print(ANDROID_LOG_ERROR, kAudioLogTag,
            "[hpvr.quest.audio.cache] status=MISSING_PCM clip=%s decoder=%s",
            source.object_name.c_str(), decoded.error.c_str());
        return samples;
    };
    auto ambient_samples = ResampleMono48k(ambient);
    auto wand_trace_samples = decode(wand_trace);
    auto wand_start_samples = decode(wand_start);
    auto cast_samples = decode(spell_cast);
    auto basic_samples = decode(basic_cast);
    if(basic_samples.empty())return false;
    auto incantation_samples = decode(incantation);
    auto hit_samples = ResampleMono48k(spell_hit);
    std::vector<std::vector<std::int16_t>> dialogue_samples;
    dialogue_samples.reserve(dialogue.size());
    std::size_t dialogue_sample_count = 0;
    for (const auto& source : dialogue) {
        auto samples = decode(source);
        dialogue_sample_count += samples.size();
        dialogue_samples.push_back(std::move(samples));
    }
    __android_log_print(
        ANDROID_LOG_INFO, kAudioLogTag,
        "[hpvr.quest.audio.mpeg] trace=%zu ready=%zu cast=%zu voice=%zu",
        wand_trace_samples.size(), wand_start_samples.size(),
        cast_samples.size(), incantation_samples.size());
    __android_log_print(
        ANDROID_LOG_INFO, kAudioLogTag,
        "[hpvr.quest.audio.dialogue] clips=%zu samples=%zu rate=48000",
        dialogue_samples.size(), dialogue_sample_count);
    if (ambient_samples.empty() || hit_samples.empty() ||
        dialogue_samples.empty() ||
        std::ranges::any_of(dialogue_samples,[](const auto& clip){return clip.empty();})) return false;
    // Missing optional audio stays explicitly unavailable, never a successful
    // one-sample silent clip. Ambient rendering remains usable for diagnostics.
    ambient_ = std::move(ambient_samples);
    wand_trace_ = std::move(wand_trace_samples);
    wand_start_ = std::move(wand_start_samples);
    spell_cast_ = std::move(cast_samples);
    basic_cast_ = std::move(basic_samples);
    basic_cast_cursor_.store(kIdleCursor, std::memory_order_release);
    incantation_ = std::move(incantation_samples);
    spell_hit_ = std::move(hit_samples);
    dialogue_ = std::move(dialogue_samples);
    ambient_cursor_ = 0;
    wand_trace_cursor_ = 0;
    wand_start_cursor_.store(kIdleCursor, std::memory_order_release);
    spell_cast_cursor_.store(kIdleCursor, std::memory_order_release);
    incantation_cursor_.store(kIdleCursor, std::memory_order_release);
    spell_hit_cursor_.store(kIdleCursor, std::memory_order_release);
    dialogue_cursor_.store(kIdleCursor, std::memory_order_release);
    dialogue_index_.store(std::numeric_limits<std::uint32_t>::max(),
                          std::memory_order_release);
    wand_drawing_.store(false, std::memory_order_release);
    return true;
}

bool QuestAudio::ConfigureTutorialFrog(const wand::Hp1PcmSound& source){
    auto samples=ResampleMono48k(source);if(samples.empty())return false;
    dialogue_.push_back(std::move(samples));return true;
}
bool QuestAudio::ConfigureMusic(const std::vector<wand::Hp1MpegSound>& sources,
                               const std::filesystem::path& cache) {
    if(stream_)return false;
    music_.clear();
    for(const auto& source:sources){
        std::ifstream input(cache/AudioCacheName(source,true),std::ios::binary|std::ios::ate);
        if(!input)return false;
        const auto bytes=input.tellg();
        if(bytes<1920||bytes>48000*4*300||bytes%4!=0)return false;
        std::vector<std::int16_t> samples(static_cast<std::size_t>(bytes)/2);
        input.seekg(0);input.read(reinterpret_cast<char*>(samples.data()),bytes);
        if(!input||std::ranges::none_of(samples,[](auto v){return v!=0;}))return false;
        music_.push_back(std::move(samples));
    }
    __android_log_print(ANDROID_LOG_INFO,kAudioLogTag,
        "[hpvr.quest.music] status=READY tracks=%zu channels=2 rate=48000",music_.size());
    return music_.size()==4;
}
void QuestAudio::SelectMusic(unsigned index){music_requested_.store(index,std::memory_order_release);}
void QuestAudio::SetPresentationAudio(bool ambient,bool paused){
    ambient_enabled_.store(ambient,std::memory_order_release);
    narrative_paused_.store(paused,std::memory_order_release);
}
void QuestAudio::StopDialogue(){dialogue_cursor_.store(kIdleCursor,std::memory_order_release);}

bool QuestAudio::Start() {
    if (!IsConfigured()) return false;
    if (stream_ != nullptr) return true;
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK ||
        builder == nullptr) return false;
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(
        builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSampleRate(builder, kOutputRate);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setDataCallback(builder, &QuestAudio::DataCallback,
                                        this);
    const aaudio_result_t opened =
        AAudioStreamBuilder_openStream(builder, &stream_);
    AAudioStreamBuilder_delete(builder);
    if (opened != AAUDIO_OK || stream_ == nullptr) {
        stream_ = nullptr;
        return false;
    }
    if (AAudioStream_getFormat(stream_) != AAUDIO_FORMAT_PCM_I16 ||
        AAudioStream_getSampleRate(stream_) !=
            static_cast<std::int32_t>(kOutputRate) ||
        AAudioStream_getChannelCount(stream_) != 2) {
        AAudioStream_close(stream_);
        stream_ = nullptr;
        return false;
    }
    if (AAudioStream_requestStart(stream_) != AAUDIO_OK) {
        AAudioStream_close(stream_);
        stream_ = nullptr;
        return false;
    }
    return true;
}

void QuestAudio::Stop() {
    wand_drawing_.store(false, std::memory_order_release);
    dialogue_cursor_.store(kIdleCursor, std::memory_order_release);
    if (stream_ == nullptr) return;
    AAudioStream_requestStop(stream_);
    AAudioStream_close(stream_);
    stream_ = nullptr;
}

void QuestAudio::SetWandDrawing(const bool drawing) {
    const bool was_drawing =
        wand_drawing_.exchange(drawing, std::memory_order_acq_rel);
    if (drawing && !was_drawing) {
        wand_start_cursor_.store(0, std::memory_order_release);
    }
}

void QuestAudio::PlayBasicCast() {
    basic_cast_cursor_.store(0, std::memory_order_release);
}
void QuestAudio::PlaySpellCast() {
    spell_cast_cursor_.store(0, std::memory_order_release);
    incantation_cursor_.store(0, std::memory_order_release);
}

void QuestAudio::PlaySpellHit() {
    spell_hit_cursor_.store(0, std::memory_order_release);
}

bool QuestAudio::PlayWorldEffect(std::size_t index,float gain){
    if(index>=dialogue_.size()||effect_cursor_.load(std::memory_order_acquire)!=kIdleCursor)return false;
    effect_index_.store(static_cast<unsigned>(index),std::memory_order_relaxed);
    effect_gain_.store(std::clamp(gain,0.0F,1.0F),std::memory_order_relaxed);
    effect_cursor_.store(0,std::memory_order_release);return true;
}
bool QuestAudio::PlayDialogue(const std::size_t index) {
    if (index >= dialogue_.size() || dialogue_[index].empty() ||
        index > std::numeric_limits<std::uint32_t>::max()) return false;
    dialogue_index_.store(static_cast<std::uint32_t>(index),
                          std::memory_order_release);
    dialogue_cursor_.store(0, std::memory_order_release);
    return true;
}

float QuestAudio::DialogueDurationSeconds(const std::size_t index) const {
    if (index >= dialogue_.size()) return 0.0F;
    return static_cast<float>(dialogue_[index].size()) /
           static_cast<float>(kOutputRate);
}
bool QuestAudio::DialogueFinished(std::size_t index) const {
    return dialogue_index_.load(std::memory_order_acquire)!=index ||
        dialogue_cursor_.load(std::memory_order_acquire)==kIdleCursor;
}

std::size_t QuestAudio::DialogueClipCount() const noexcept {
    return dialogue_.size();
}

bool QuestAudio::IsConfigured() const noexcept {
    return !ambient_.empty() && !spell_hit_.empty() && !dialogue_.empty();
}

bool QuestAudio::IsRunning() const noexcept { return stream_ != nullptr; }

aaudio_data_callback_result_t QuestAudio::DataCallback(
    AAudioStream*, void* const user_data, void* const audio_data,
    const std::int32_t frame_count) {
    if (user_data == nullptr || audio_data == nullptr || frame_count < 0) {
        return AAUDIO_CALLBACK_RESULT_STOP;
    }
    return static_cast<QuestAudio*>(user_data)->Render(
        static_cast<std::int16_t*>(audio_data), frame_count);
}

aaudio_data_callback_result_t QuestAudio::Render(
    std::int16_t* const output, const std::int32_t frame_count) {
    for (std::int32_t frame = 0; frame < frame_count; ++frame) {
        const bool paused=narrative_paused_.load(std::memory_order_acquire);
        float mixed = ambient_enabled_.load(std::memory_order_acquire) && !paused
            ? static_cast<float>(ambient_[ambient_cursor_])*kAmbientGain : 0.0F;
        ambient_cursor_ = (ambient_cursor_ + 1) % ambient_.size();
        if (!wand_trace_.empty() && wand_drawing_.load(std::memory_order_acquire)) {
            mixed += static_cast<float>(wand_trace_[wand_trace_cursor_]) *
                     kWandTraceGain;
            wand_trace_cursor_ =
                (wand_trace_cursor_ + 1) % wand_trace_.size();
        } else {
            wand_trace_cursor_ = 0;
        }
        auto mix_one_shot = [&mixed](const std::vector<std::int16_t>& source,
                                     std::atomic<std::uint64_t>& cursor,
                                     const float gain) {
            auto position = cursor.load(std::memory_order_acquire);
            if (position == kIdleCursor) return;
            if (position >= source.size()) {
                cursor.store(kIdleCursor, std::memory_order_release);
                return;
            }
            mixed += static_cast<float>(source[static_cast<std::size_t>(position)]) *
                     gain;
            ++position;
            cursor.store(position < source.size() ? position : kIdleCursor,
                         std::memory_order_release);
        };
        mix_one_shot(wand_start_, wand_start_cursor_, kWandStartGain);
        mix_one_shot(spell_cast_, spell_cast_cursor_, kSpellCastGain);
        mix_one_shot(basic_cast_, basic_cast_cursor_, kSpellCastGain);
        mix_one_shot(incantation_, incantation_cursor_, kIncantationGain);
        mix_one_shot(spell_hit_, spell_hit_cursor_, kSpellHitGain);
        const auto effect_index=effect_index_.load(std::memory_order_acquire);
        if(!paused&&ambient_enabled_.load(std::memory_order_relaxed)&&effect_index<dialogue_.size())
            mix_one_shot(dialogue_[effect_index],effect_cursor_,effect_gain_.load(std::memory_order_relaxed));
        const auto dialogue_position =
            dialogue_cursor_.load(std::memory_order_acquire);
        if (dialogue_position != kIdleCursor && !paused) {
            const auto dialogue_index =
                dialogue_index_.load(std::memory_order_acquire);
            if (dialogue_index >= dialogue_.size() ||
                dialogue_position >= dialogue_[dialogue_index].size()) {
                dialogue_cursor_.store(kIdleCursor,
                                       std::memory_order_release);
            } else {
                mixed += static_cast<float>(
                    dialogue_[dialogue_index][dialogue_position]) * 0.92F;
                const auto next = dialogue_position + 1U;
                dialogue_cursor_.store(
                    next < dialogue_[dialogue_index].size()
                        ? next : kIdleCursor,
                    std::memory_order_release);
            }
        }
        const auto requested=music_requested_.load(std::memory_order_acquire);
        if(requested!=music_current_){music_current_=requested;music_cursor_=0;}
        float left=mixed,right=mixed;
        if(music_current_<music_.size() && !paused){
            const auto& music=music_[music_current_];
            if(music_cursor_+1>=music.size() && music_current_!=2)music_cursor_=0;
            const float gain=dialogue_position!=kIdleCursor?0.16F:0.32F;
            if(music_cursor_+1<music.size()){
                left+=music[music_cursor_]*gain;right+=music[music_cursor_+1]*gain;
                music_cursor_+=2;
            }
        }
        output[frame * 2] = ClampSample(left);
        output[frame * 2 + 1] = ClampSample(right);
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

}  // namespace hpvr::quest
