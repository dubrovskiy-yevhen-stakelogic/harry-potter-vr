#include "hpvr/quest_music_cache.h"
#include "hpvr/quest_frontend.h"

#include <algorithm>
#include <fstream>
#include <utility>

namespace hpvr::quest {

bool LoadQuestMusicCache(const std::vector<wand::Hp1MpegSound>& sources,
                        const std::filesystem::path& cache,
                        QuestMusicTracks* out, std::string* error) {
    if (error) error->clear();
    const auto reject = [error](const std::string& reason) {
        if (error) *error = reason;
        return false;
    };
    if (!out) return reject("Missing music output");
    out->clear();
    if (sources.empty()) return reject("Empty music library");

    QuestMusicTracks loaded;
    loaded.reserve(sources.size());
    for (const auto& source : sources) {
        const auto name = AudioCacheName(source, true);
        std::ifstream input(cache / name, std::ios::binary | std::ios::ate);
        if (!input) return reject("Missing music PCM: " + name);
        const auto bytes = static_cast<std::streamoff>(input.tellg());
        constexpr std::streamoff maximum_bytes = 48000 * 4 * 300;
        if (bytes < 1920 || bytes > maximum_bytes || bytes % 4 != 0)
            return reject("Invalid stereo PCM size: " + name);
        std::vector<std::int16_t> samples(static_cast<std::size_t>(bytes) / 2);
        input.seekg(0);
        input.read(reinterpret_cast<char*>(samples.data()),
                   static_cast<std::streamsize>(bytes));
        if (!input) return reject("Incomplete music PCM read: " + name);
        if (std::ranges::none_of(samples, [](auto value) { return value != 0; }))
            return reject("Silent music PCM: " + name);
        loaded.push_back(std::move(samples));
    }
    // Different maps append different music cues after the frontend tracks.
    // Success means all requested files loaded, not exactly four menu tracks.
    *out = std::move(loaded);
    return true;
}

}  // namespace hpvr::quest
