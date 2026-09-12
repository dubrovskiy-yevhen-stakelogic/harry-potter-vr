#include "hpvr/quest_voice_decoder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
std::vector<std::int16_t> ReadRaw(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    Check(static_cast<bool>(input), "PCM test fixture opens");
    const auto bytes = input.tellg();
    Check(bytes > 0 && bytes < 16000 * 2 * 120 && bytes % 2 == 0, "fixture is bounded raw mono16k PCM16");
    std::vector<std::int16_t> data(static_cast<std::size_t>(bytes) / 2);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(bytes));
    Check(static_cast<bool>(input), "complete fixture read");
    return data;
}
bool Decode(hpvr::quest::QuestVoiceDecoder& decoder, const std::vector<std::int16_t>& pcm,
            unsigned leading_silence_blocks = 15) {
    Check(decoder.Begin(), "start independent microphone attempt");
    // Include a short leading/trailing silence as a real armed capture does.
    std::array<std::int16_t, 320> silence{};
    float seconds = 0;
    bool detected = false;
    for (unsigned i = 0; i < leading_silence_blocks; ++i) {
        Check(!decoder.Process(silence.data(), silence.size(), &seconds), "waiting silence never replays a keyword");
        Check(!decoder.failed(), "long waiting input remains healthy");
    }
    for (std::size_t at = 0; at < pcm.size(); at += 320) {
        const auto count = std::min<std::size_t>(320, pcm.size() - at);
        detected |= decoder.Process(pcm.data() + at, count, &seconds);
        Check(!decoder.failed(), "acoustic decoder processing succeeds");
    }
    for (unsigned i = 0; i < 30; ++i) detected |= decoder.Process(silence.data(), silence.size(), &seconds);
    decoder.End();
    if (detected) std::cout << "keyword duration: " << seconds << " s\n";
    return detected;
}
std::vector<float> DecodeChunks(hpvr::quest::QuestVoiceDecoder& decoder,
                               const std::vector<std::int16_t>& speech, std::size_t chunk) {
    std::vector<std::int16_t> pcm(16000 * 2);
    pcm.insert(pcm.end(), speech.begin(), speech.end());
    pcm.resize(pcm.size() + 9600);
    Check(decoder.Begin(), "start chunk-boundary comparison");
    const auto renewals = decoder.Stats().stream_renewals;
    std::vector<float> events;
    for (std::size_t at = 0; at < pcm.size(); at += chunk) {
        float duration = 0;
        if (decoder.Process(pcm.data() + at, std::min(chunk, pcm.size() - at), &duration))
            events.push_back(duration);
        Check(!decoder.failed(), "fragmented capture remains healthy");
    }
    Check(decoder.Stats().stream_renewals > renewals, "quiet-to-speech onset renews the complete stream");
    const auto after_match = decoder.Stats().stream_renewals;
    // An already accepted attempt must retain standard continuous recognition;
    // gameplay closes capture here, but offline callers may keep supplying PCM.
    if (!events.empty()) {
        std::array<std::int16_t, 320> silence{};
        float ignored = 0;
        for (unsigned i = 0; i < 50; ++i) (void)decoder.Process(silence.data(), silence.size(), &ignored);
        (void)decoder.Process(speech.data(), std::min<std::size_t>(320, speech.size()), &ignored);
        Check(decoder.Stats().stream_renewals == after_match, "successful stream is not renewed again");
    }
    decoder.End();
    return events;
}
void CheckRepeatedNegativePrefix(hpvr::quest::QuestVoiceDecoder& decoder,
                                 const std::vector<std::int16_t>& negative,
                                 const std::vector<std::int16_t>& positive) {
    Check(decoder.Begin(), "start held attempt with repeated unrelated words");
    const auto feed = [&](const std::vector<std::int16_t>& pcm) {
        bool matched = false;
        for (std::size_t at = 0; at < pcm.size(); at += 320) {
            float duration = 0;
            matched |= decoder.Process(pcm.data() + at,
                                       std::min<std::size_t>(320, pcm.size() - at), &duration);
            Check(!decoder.failed(), "repeated-prefix processing remains healthy");
        }
        return matched;
    };
    const std::vector<std::int16_t> silence(16000);
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        Check(!feed(negative), "wider search rejects repeated unrelated speech");
        Check(!feed(silence), "negative-word tail never produces a delayed cast");
    }
    bool matched = feed(positive);
    matched |= feed(silence);
    Check(matched, "correct word is recognized after repeated unrelated speech in the same attempt");
    decoder.End();
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("usage: voice_decoder_tests MODEL_DIRECTORY [UNRELATED_PCM16K [FLIPENDO_PCM16K [KEYWORD_THRESHOLD]]]");
        hpvr::quest::QuestVoiceDecoder decoder;
        Check(!decoder.loaded() && !decoder.Begin(), "unconfigured backend cannot report a keyword");
        Check(!decoder.Load(argv[1], -1) && !decoder.loaded(), "invalid threshold cannot load a model");
        const auto begin = std::chrono::steady_clock::now();
        const double threshold = argc >= 5 ? std::stod(argv[4]) : hpvr::quest::kVoiceKeywordThreshold;
        Check(decoder.Load(argv[1], threshold), "licensed neural model and fixed keyword load");
        Check(decoder.Stats().steps == 0 && decoder.Stats().keyword_hits == 0,
              "new acoustic model starts numeric counters at zero");
        Check(!Decode(decoder, std::vector<std::int16_t>(6 * 16000)), "silence never casts");
        std::vector<std::int16_t> noise(6 * 16000);
        std::uint32_t random = 0x78192413;
        for (auto& sample : noise) { random = random * 1664525U + 1013904223U; sample = static_cast<std::int16_t>(static_cast<int>(random >> 20) - 2048); }
        Check(!Decode(decoder, noise), "non-speech noise never casts");
        for (std::size_t i = 0; i < noise.size(); ++i) noise[i] = i % 16000 < 40 ? 30000 : 0;
        Check(!Decode(decoder, noise), "loud impulses do not substitute for keyword recognition");
        const bool spoken_negative = argc >= 3 && argv[2][0] != '\0';
        const auto negative = spoken_negative ? ReadRaw(argv[2]) : noise;
        Check(!Decode(decoder, negative), "unrelated spoken words do not cast Flipendo");
        Check(decoder.Stats().steps > 0, "real acoustic processing advances numeric decode-step counter");
        if (argc >= 4) {
            const auto positive = ReadRaw(argv[3]);
            for (unsigned cycle = 0; cycle < 24; ++cycle) {
                Check(Decode(decoder, positive), "repeated positive Flipendo test clip recognized");
                Check(!Decode(decoder, negative), "negative speech between repeated casts is rejected");
            }
            Check(Decode(decoder, positive, 350), "positive recognized after seven seconds in the SAME capture stream");
            Check(Decode(decoder, positive, 1500), "positive recognized after thirty seconds in the SAME capture stream");
            const auto reference = DecodeChunks(decoder, positive, 320);
            Check(reference.size() == 1, "single qualification word recognized after stable silence");
            for (const auto chunk : {1U, 137U, 1000U, 16000U})
                Check(DecodeChunks(decoder, positive, chunk) == reference,
                      "capture fragmentation preserves accepted keyword count and duration");
            std::vector<std::int16_t> retry = negative;
            retry.resize(retry.size() + 16000);
            retry.insert(retry.end(), positive.begin(), positive.end());
            Check(Decode(decoder, retry), "unmatched spoken prefix does not poison the next word in the same attempt");
            CheckRepeatedNegativePrefix(decoder, negative, positive);
            const auto stats = decoder.Stats();
            Check(stats.keyword_hits >= 25 && stats.last_keyword_seconds >= 0.25F &&
                  stats.last_keyword_seconds <= 1.8F, "real decoder reports accepted word timing without storing speech");
        }
        std::array<std::int16_t, 320> cancelled{};
        float cancelled_duration = 0;
        Check(!decoder.Process(cancelled.data(), cancelled.size(), &cancelled_duration),
            "cancelled capture cannot flush a late keyword");
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        std::cout << "voice acoustic tests: PASS (" << checks << " checks, host elapsed " << seconds << " s)\n";
        if (!spoken_negative) std::cout << "Unrelated spoken-word qualification was not supplied.\n";
        if (argc < 4) std::cout << "Positive Flipendo/Quest microphone acceptance remains untested.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "voice acoustic tests: FAIL: " << error.what() << '\n'; return 1;
    }
}
