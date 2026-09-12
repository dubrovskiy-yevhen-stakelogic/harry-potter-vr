#include "hpvr/quest_frontend.h"
#include "hpvr/quest_music_cache.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace hpvr::quest;
using hpvr::wand::Hp1MpegSound;
unsigned checks = 0;

void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

class FixtureDirectory {
public:
    FixtureDirectory() {
        const auto parent = std::filesystem::temp_directory_path();
        const auto stamp = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = parent /
                ("hpvr-music-cache-test-" + stamp + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) {
                path = candidate;
                return;
            }
        }
        throw std::runtime_error("Could not create a private test directory");
    }
    ~FixtureDirectory() {
        if (!path.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    }
    FixtureDirectory(const FixtureDirectory&) = delete;
    FixtureDirectory& operator=(const FixtureDirectory&) = delete;
    std::filesystem::path path;
};

std::vector<Hp1MpegSound> Sources(unsigned count) {
    std::vector<Hp1MpegSound> sources;
    for (unsigned i = 0; i < count; ++i) {
        Hp1MpegSound source;
        source.status = hpvr::wand::Hp1ProfileStatus::ok;
        source.object_name = "generated_music_" + std::to_string(i);
        source.sample_rate = 48000;
        source.channel_count = 2;
        source.encoded_bytes = {0x12, 0x34, static_cast<std::uint8_t>(i), 0x56};
        sources.push_back(std::move(source));
    }
    return sources;
}

std::vector<std::int16_t> Samples(unsigned track) {
    std::vector<std::int16_t> samples(960);
    for (std::size_t i = 0; i < samples.size(); i += 2) {
        samples[i] = static_cast<std::int16_t>(100 + track);
        samples[i + 1] = static_cast<std::int16_t>(-200 - static_cast<int>(track));
    }
    return samples;
}

void WriteSamples(const std::filesystem::path& path,
                  const std::vector<std::int16_t>& samples) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(samples.data()),
        static_cast<std::streamsize>(samples.size() * sizeof(samples[0])));
    if (!file) throw std::runtime_error("Cannot write a generated PCM fixture");
}

void WriteBank(const std::filesystem::path& cache,
               const std::vector<Hp1MpegSound>& sources) {
    for (std::size_t i = 0; i < sources.size(); ++i) {
        WriteSamples(cache / AudioCacheName(sources[i], true),
                     Samples(static_cast<unsigned>(i)));
    }
}

void ExpectRejected(const std::vector<Hp1MpegSound>& sources,
                    const std::filesystem::path& cache,
                    const char* message) {
    QuestMusicTracks loaded{{42, -42}};
    std::string error;
    Check(!LoadQuestMusicCache(sources, cache, &loaded, &error), message);
    Check(loaded.empty(), "failed loading must clear partial or previous output");
    Check(!error.empty(), "failed loading must provide a diagnostic");
}

void SyntheticTests() {
    FixtureDirectory fixture;
    const auto four = Sources(4);
    const auto ten = Sources(10);
    WriteBank(fixture.path, ten);
    QuestMusicTracks loaded;
    std::string error;

    Check(LoadQuestMusicCache(four, fixture.path, &loaded, &error),
          "the four-track tutorial bank must load");
    Check(loaded.size() == 4, "tutorial track count must be exact");
    Check(loaded.front() == Samples(0) && loaded.back() == Samples(3),
          "tutorial PCM and stereo channel order must be preserved");
    Check(LoadQuestMusicCache(ten, fixture.path, &loaded, &error),
          "a ten-track challenge bank must not be rejected as non-tutorial");
    Check(loaded.size() == 10, "challenge must preserve every track");
    for (unsigned i = 0; i < 10; ++i) {
        Check(loaded[i] == Samples(i), "all challenge music indices must retain their PCM");
    }
    Check(LoadQuestMusicCache(four, fixture.path, &loaded, &error) && loaded.size() == 4,
          "repeated configuration must replace rather than append tracks");
    const auto one = Sources(1);
    Check(LoadQuestMusicCache(one, fixture.path, &loaded, &error) && loaded.size() == 1,
          "valid banks are not constrained to a particular map track count");
    ExpectRejected({}, fixture.path, "an empty source bank must be rejected");

    const auto middle = fixture.path / AudioCacheName(ten[5], true);
    Check(std::filesystem::remove(middle), "generated middle track exists");
    ExpectRejected(ten, fixture.path, "a missing middle track must reject the entire bank");
    WriteBank(fixture.path, ten);

    for (const std::uintmax_t bytes : {std::uintmax_t{1921}, std::uintmax_t{1922},
                                     std::uintmax_t{1916}, std::uintmax_t{0}}) {
        std::filesystem::resize_file(middle, bytes);
        ExpectRejected(ten, fixture.path,
                       "odd, non-frame-aligned, short and empty PCM must be rejected");
        WriteBank(fixture.path, ten);
    }
    // Extend on disk to exercise the size guard without allocating a PCM vector.
    std::filesystem::resize_file(middle, std::uintmax_t{48000} * 4 * 300 + 4);
    ExpectRejected(ten, fixture.path, "oversized PCM must fail before sample allocation");
    WriteBank(fixture.path, ten);

    WriteSamples(middle, std::vector<std::int16_t>(960, 0));
    ExpectRejected(ten, fixture.path, "a silent all-zero placeholder is not a valid track");
    WriteBank(fixture.path, ten);
    Check(LoadQuestMusicCache(ten, fixture.path, &loaded, &error) &&
          loaded.size() == 10 && loaded.back() == Samples(9),
          "a repaired cache must load fully after failed attempts");
}

void OwnedTests(const std::filesystem::path& root,
                const std::filesystem::path& cache) {
    std::size_t tutorial_tracks = 0;
    for (unsigned map = 0; map < 2; ++map) {
        FrontAssets assets;
        Check(LoadFrontAssets(root, &assets, map), "owned frontend music metadata must load");
        QuestMusicTracks tracks;
        std::string error;
        if (!LoadQuestMusicCache(assets.music, cache, &tracks, &error))
            throw std::runtime_error("Owned music cache failed: " + error);
        Check(tracks.size() == assets.music.size(), "all owned music must reach the runtime bank");
        if (map == 0) tutorial_tracks = tracks.size();
        else Check(tracks.size() > tutorial_tracks,
                   "challenge music beyond the tutorial prefix must be admitted");
        for (const auto& cue : assets.music_cues) {
            Check(cue.music_index < 0 || static_cast<std::size_t>(cue.music_index) < tracks.size(),
                  "authored music cues must index the loaded bank");
        }
        std::cout << "OWNED_MUSIC_CACHE=PASS map=" << map << " tracks=" << tracks.size() << '\n';
    }
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 1 && argc != 3) throw std::runtime_error("Usage: quest_music_cache_tests [owned-root pcm-cache]");
        SyntheticTests();
        if (argc == 3) OwnedTests(argv[1], argv[2]);
        std::cout << "QUEST_MUSIC_CACHE_TESTS=PASS checks=" << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "QUEST_MUSIC_CACHE_TESTS=FAIL " << error.what() << '\n';
        return 1;
    }
}
