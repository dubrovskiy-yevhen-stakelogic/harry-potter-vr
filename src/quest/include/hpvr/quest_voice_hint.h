#pragma once
#include "hpvr/quest_target_marker.h"

namespace hpvr::quest {
struct VoiceHintPlacement {
    bool valid=false;
    std::array<float,3> center{},right{},up{},normal{};
};

// The existing glyph strip is 10.5 UI pixels high at 0.004375 metres/pixel.
// Keep it above the marker on the same viewer-facing plane. A below-marker
// offset based on target width can bury the text underneath a low, wide prop.
[[nodiscard]] inline VoiceHintPlacement PlaceVoiceHint(
    const std::array<float,3>& minimum,const std::array<float,3>& maximum,
    const std::array<float,3>& viewer) noexcept {
    const auto marker=PlaceTargetMarker(minimum,maximum,viewer);
    if(!marker.valid)return {};
    VoiceHintPlacement result;
    result.up=marker.up;result.normal=marker.normal;
    result.right=target_marker_detail::Cross(result.up,result.normal);
    constexpr float glyph_half_height=.025F,gap=.075F;
    const float offset=marker.size*.5F+gap+glyph_half_height;
    for(unsigned axis=0;axis<3;++axis){
        result.center[axis]=marker.center[axis]+marker.up[axis]*offset;
        if(!std::isfinite(result.center[axis]))return {};
    }
    result.valid=true;
    return result;
}
} // namespace hpvr::quest
