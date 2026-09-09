#pragma once
#include <array>
#include <cmath>
#include "hpvr/quest_view.h"

namespace hpvr::quest {
inline constexpr const char* kDemoCommunityUrl="https://discord.com/channels/747967102895390741/1547254536203407390";
inline bool BuildDemoActorEye(const std::array<float,3>& feet,float yaw,float height,ViewPose* out){
    if(!out||!std::isfinite(yaw)||!std::isfinite(height)||height<=0)return false;
    for(float v:feet)if(!std::isfinite(v))return false;
    out->position={feet[0],feet[1]+height,feet[2]};
    const std::array<float,3> forward{feet[0]+std::sin(yaw),feet[1]+height,feet[2]+std::cos(yaw)};
    return BuildLookOrientation(out->position,forward,&out->orientation);
}
// Only actual horizontal locomotion, not a menu selection, tracking jump,
// save restoration, head rotation or cinematic placement, opens the welcome.
struct DemoFirstStep {
    std::array<float,3> previous{};
    bool valid=false;
    float distance=0;
    void Reset(){valid=false;distance=0;}
    bool Update(bool eligible,bool moving,const std::array<float,3>& position){
        if(!eligible||!moving){Reset();return false;}
        const float delta=std::hypot(position[0]-previous[0],position[2]-previous[2]);
        if(valid&&std::isfinite(delta)&&delta<.5F)distance+=delta;
        else distance=0;
        previous=position;valid=true;
        return distance>=.08F;
    }
};
struct DemoCameraReturn {
    bool valid=false;
    std::array<float,3> offset{};
    float yaw=0;
    bool Capture(const ViewPose& camera,const ViewPose& local,const ViewPose& reference){
        ViewPose world;Matrix4 wm{},lm{};
        valid=MapCinematicEye(local,local,camera,reference,&world)&&BuildRigidTransform(world,&wm)&&BuildRigidTransform(local,&lm);
        if(!valid)return false;
        for(unsigned i=0;i<3;++i)offset[i]=world.position[i]-camera.position[i];
        yaw=std::atan2(wm[8],wm[10])-std::atan2(lm[8],lm[10]);return true;
    }
};
}
