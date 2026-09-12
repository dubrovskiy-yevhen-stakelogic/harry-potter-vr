#pragma once
#include <cmath>
#include <cstdint>
#include <map>

namespace hpvr::quest {
// Prepared files keep their original strict layout. At runtime, recovered
// animation clips may follow the pickups; all ranges must still cover the
// same contiguous interval without gaps, overlaps or out-of-bounds vertices.
template<class Actors, class Pickups>
bool ValidateAnimatedVertexLayout(const Actors& actors, const Pickups& pickups,
                                 std::uint64_t begin, std::uint64_t end) {
    if(begin>end)return false;
    std::map<std::uint64_t,std::uint64_t> ranges;
    const auto add=[&](std::uint64_t first,std::uint64_t count){
        return first>=begin&&first<=end&&count&&count<=end-first&&ranges.emplace(first,count).second;
    };
    for(const auto& actor:actors){
        if(!actor.clips.contains(actor.active_clip))return false;
        for(const auto& [name,clip]:actor.clips){
            (void)name;
            if(!std::isfinite(clip.duration)||clip.duration<=0||
               !add(clip.first_vertex,std::uint64_t(actor.vertex_count)*clip.frame_count))return false;
        }
    }
    for(const auto& pickup:pickups)
        if(!add(pickup.first,std::uint64_t(pickup.count)*pickup.frames))return false;
    for(const auto& [first,count]:ranges){if(first!=begin)return false;begin+=count;}
    return begin==end;
}
} // namespace hpvr::quest
