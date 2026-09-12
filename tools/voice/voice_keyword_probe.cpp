// Offline file-fed qualification only; never linked into the game.
#include "hpvr/quest_voice_decoder.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::ifstream input(argv[2], std::ios::binary | std::ios::ate);
    if (!input) return 3;
    const auto bytes = input.tellg();
    if (bytes <= 0 || bytes > 16000 * 2 * 120 || bytes % 2 != 0) return 4;
    std::vector<std::int16_t> pcm(static_cast<std::size_t>(bytes) / 2 + 8000);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(pcm.data()), bytes);
    if (!input) return 5;
    hpvr::quest::QuestVoiceDecoder decoder;
    const double threshold = argc > 3 ? std::stod(argv[3]) : hpvr::quest::kVoiceKeywordThreshold;
    if (!decoder.Load(argv[1], threshold) || !decoder.Begin()) return 6;
    const auto start = std::chrono::steady_clock::now();
    unsigned hits = 0;
    for (std::size_t at = 0; at < pcm.size(); at += 320) {
        const auto count = std::min<std::size_t>(320, pcm.size() - at);
        float seconds = 0;
        if (decoder.Process(pcm.data() + at, count, &seconds)) {
            ++hits;
            std::cout << "keyword=FLIPENDO duration_s=" << seconds
                      << " input_time_s=" << static_cast<double>(at + count) / 16000 << '\n';
        }
        if (decoder.failed()) return 7;
    }
    decoder.End();
    const auto stats = decoder.Stats();
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "hits=" << hits << " rejected=" << stats.duration_rejects
              << " decode_steps=" << stats.steps << " host_seconds=" << elapsed << '\n';
    return 0;
}
