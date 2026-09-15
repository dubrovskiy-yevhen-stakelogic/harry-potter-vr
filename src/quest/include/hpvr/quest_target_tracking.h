#pragma once

#include <array>
#include <cmath>

namespace hpvr::quest {
using TargetVector = std::array<float,3>;

inline void FollowTargetOffset(TargetVector& point, TargetVector& minimum,
                               TargetVector& maximum, TargetVector& previous,
                               const TargetVector& current) {
    for (unsigned axis=0;axis<3;++axis) {
        const float movement=current[axis]-previous[axis];
        point[axis]+=movement;minimum[axis]+=movement;maximum[axis]+=movement;
    }
    previous=current;
}

// Keep the current projectile position and accumulated trail age while steering.
inline float RetargetProjectile(TargetVector& origin, TargetVector& direction,
                                float distance, const TargetVector& destination) {
    TargetVector current{}, delta{};
    float squared=0;
    for(unsigned axis=0;axis<3;++axis) {
        current[axis]=origin[axis]+direction[axis]*distance;
        delta[axis]=destination[axis]-current[axis];squared+=delta[axis]*delta[axis];
    }
    const float remaining=std::sqrt(squared);
    if(remaining>0.00001F)for(unsigned axis=0;axis<3;++axis)direction[axis]=delta[axis]/remaining;
    for(unsigned axis=0;axis<3;++axis)origin[axis]=current[axis]-direction[axis]*distance;
    return distance+remaining;
}
} // namespace hpvr::quest
