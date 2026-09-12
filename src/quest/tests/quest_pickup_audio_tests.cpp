#include "hpvr/quest_pickup_audio.h"
#include "hpvr/quest_frontend.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    try {
        hpvr::quest::PickupAudioCue cue;
        const std::array<std::int16_t, 4> pcm{100, 200, -300, 400};
        cue.BeginBlock();
        Check(cue.NextSample(pcm) == 0, "idle cue is silent");
        cue.Request(); cue.BeginBlock();
        Check(cue.NextSample(pcm) == 100, "pickup starts at first sample");
        Check(cue.NextSample(pcm) == 200, "cue advances");
        cue.Request();
        Check(cue.NextSample(pcm) == -300, "mid-block request never races callback position");
        cue.BeginBlock();
        Check(cue.NextSample(pcm) == 100, "request during playback restarts next block");
        cue.BeginBlock();
        Check(cue.NextSample(pcm) == 200, "consumed request does not restart again");
        Check(cue.NextSample(pcm) == -300 && cue.NextSample(pcm) == 400,
              "original samples preserved without pitch or timing changes");
        Check(cue.NextSample(pcm) == 0, "finished cue is silent");
        cue.BeginBlock(); Check(cue.NextSample(pcm) == 0, "finished cue does not loop");
        for (int i = 0; i < 50; ++i) cue.Request();
        cue.BeginBlock();
        for (auto sample : pcm) Check(cue.NextSample(pcm) == sample, "same-block pickups coalesce");
        Check(cue.NextSample(pcm) == 0, "no delayed pickup backlog");
        cue.Request(); cue.BeginBlock();
        Check(cue.NextSample({}) == 0, "empty clip is safe");
        cue.Request(); cue.Reset(); cue.BeginBlock();
        Check(cue.NextSample(pcm) == 0, "reset clears pending pickup");
        hpvr::quest::PickupAudioCue independent;
        independent.Request(); independent.BeginBlock();
        Check(cue.NextSample(pcm) == 0 && independent.NextSample(pcm) == 100,
              "cues share no cursor or busy state");
        if (argc == 2) {
            const std::filesystem::path root(argv[1]);
            const auto original = hpvr::wand::load_hp1_mpeg_sound(root/"system/HPSounds.u", 333);
            Check(original.status == hpvr::wand::Hp1ProfileStatus::ok && original.object_name == "pickup11",
                  "owned Jellybean killbean sound exists");
            for (unsigned map = 0; map < 2; ++map) {
                hpvr::quest::FrontAssets assets;
                Check(hpvr::quest::LoadFrontAssets(root, &assets, map), "owned frontend loads");
                unsigned matches = 0;
                for (const auto& sound : assets.gameplay_audio) if (sound.object_name == "pickup11") {
                    ++matches;
                    Check(sound.encoded_bytes == original.encoded_bytes,
                          "imported cue exactly matches original Jellybean sound");
                    Check(hpvr::quest::AudioCacheName(sound, false) == "pickup11.d54bfbdc.s16",
                          "existing prepared bean PCM stays compatible");
                }
                Check(matches == 1, "each map binds exactly one original bean cue by name");
            }
        }
        std::cout << "PICKUP_AUDIO_PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
