#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace hpvr::quest::award {

constexpr bool IsAwardScene(std::int32_t reference) noexcept {
    return reference == 3384 || reference == 5615 || reference == 2848;
}

constexpr bool IsHarryMark(std::string_view alias) noexcept {
    constexpr std::string_view expected = "hploc1";
    if (alias.size() != expected.size()) return false;
    for (std::size_t i = 0; i < alias.size(); ++i) {
        const char value = alias[i] >= 'A' && alias[i] <= 'Z'
            ? static_cast<char>(alias[i] - 'A' + 'a') : alias[i];
        if (value != expected[i]) return false;
    }
    return true;
}

// The three challenge award scripts move Harry to HPLoc1, then cue the
// professor's speech. They rely on his initial facing, without a Face command.
// Repair that initial pose only; retain the baked mesh's base_yaw.
template<class Locations, class Actors>
bool RestoreProfessorFacing(std::int32_t scene_reference,
                            const Locations& locations, Actors& actors) {
    if (!IsAwardScene(scene_reference) || locations.size() > 128 || actors.size() > 4096)
        return false;
    std::array<float, 3> destination{};
    bool found_mark = false;
    for (const auto& mark : locations) {
        if (!IsHarryMark(mark.alias)) continue;
        if (found_mark || mark.actor_reference != 1799) return false;
        destination = mark.position;
        found_mark = true;
    }
    if (!found_mark) return false;
    auto* professor = actors.data();
    professor = nullptr;
    for (auto& actor : actors) {
        if (actor.actor_reference != 2051) continue;
        if (professor != nullptr) return false;
        professor = &actor;
    }
    if (professor == nullptr) return false;
    std::array<float, 3> delta{};
    for (unsigned axis = 0; axis < 3; ++axis) {
        const float position = professor->base_origin[axis] + professor->cutscene_offset[axis];
        if (!std::isfinite(position) || !std::isfinite(destination[axis]) ||
            std::abs(position) > 10000 || std::abs(destination[axis]) > 10000)
            return false;
        delta[axis] = destination[axis] - position;
    }
    if (std::hypot(delta[0], delta[2]) < .0001F) return false;
    const float yaw = std::atan2(delta[0], delta[2]);
    professor->yaw = yaw;
    professor->desired_yaw = yaw;
    return true;
}

} // namespace hpvr::quest::award
