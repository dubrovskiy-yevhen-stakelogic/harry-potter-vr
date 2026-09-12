#pragma once

#include "hpvr/hp1_gesture.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hpvr::quest {

using QuestMusicTracks = std::vector<std::vector<std::int16_t>>;

// Load every requested 48 kHz stereo PCM track, preserving authored indices.
// Empty/malformed libraries fail without leaving a partially loaded output.
bool LoadQuestMusicCache(const std::vector<wand::Hp1MpegSound>& sources,
                        const std::filesystem::path& cache,
                        QuestMusicTracks* out, std::string* error);

}  // namespace hpvr::quest
