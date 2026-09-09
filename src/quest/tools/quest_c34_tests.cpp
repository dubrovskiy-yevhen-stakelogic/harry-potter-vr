#include "hpvr/quest_frontend.h"
#include "hpvr/quest_reflection_math.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace hpvr::quest;
void Check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);}
std::array<float,4> Transform(const Matrix4& m,std::array<float,4> p){
    std::array<float,4> q{};for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)q[r]+=m[c*4+r]*p[c];return q;
}
int main(){try{
    QuestFrontEnd f;
    Check(f.PausesWorld()&&!f.PausesAudio(),"main music is not paused");
    f.screen=FrontScreen::Story;Check(f.PausesWorld()&&!f.PausesAudio(),"storybook narration continues");
    f.BeginGame();f.ToggleVrMenu();
    Check(f.Visible()&&f.WorldVisible()&&!f.PausesWorld()&&!f.PausesAudio(),"live settings keep simulation/audio");
    f.screen=FrontScreen::Debug;
    Check(!f.PausesWorld()&&!f.PausesAudio(),"live debugger keeps simulation/audio");
    f.ToggleVrMenu();f.screen=FrontScreen::Pause;f.ToggleVrMenu();
    Check(f.PausesWorld()&&f.PausesAudio(),"opening VR settings over book preserves explicit pause");
    Check(!DelayReflectionOverlay(false,0)&&!DelayReflectionOverlay(false,1),"SSR off never adds overlay resume");
    Check(DelayReflectionOverlay(true,0)&&!DelayReflectionOverlay(true,1),"clean only captured eye");
    for(unsigned n:{1U,100U,175U,2047U,2048U,3585U}){
        Check(ReflectionExtent(n,1)==n,"copy fallback keeps full extent");
        Check(ReflectionExtent(n,2)==(n+1)/2,"half history extent rounds up");
    }
    unsigned cases=0;float max_error=0;
    for(float yaw:{-.9F,0.0F,1.7F})for(float x:{-2.0F,0.0F,1.0F})for(float z:{-2.0F,-7.0F,-19.0F}){
        ViewPose eye;eye.position={3,2,-5};eye.orientation={0,std::sin(yaw/2),0,std::cos(yaw/2)};
        Matrix4 vp{},inv{},world{};Check(BuildRigidTransform(eye,&world),"rigid");
        Check(BuildViewProjection(eye,{-.8F,.7F,-.75F,.8F},0,.05F,200,&vp)&&InvertReflectionMatrix(vp,inv),"projection");
        const auto p=Transform(world,{x,.3F,z,1});const auto clip=Transform(vp,p);
        const float depth=ReflectionCameraDepth(inv,clip[0]/clip[3],clip[1]/clip[3],clip[2]/clip[3]);
        max_error=std::max(max_error,std::abs(depth-clip[3]));
        Check(std::abs(depth-clip[3])<.015F,"projected depth matches full inverse");
        const auto direction=Transform(vp,{.1F,.2F,-.5F,0});
        const auto moved=Transform(vp,{p[0]+.7F*.1F,p[1]+.7F*.2F,p[2]-.7F*.5F,1});
        for(unsigned i=0;i<4;++i)Check(std::abs(moved[i]-(clip[i]+.7F*direction[i]))<.0001F,"project-once ray is exact");
        ++cases;
    }
    std::cout<<"C34_POLICY=PASS live_menu=YES clean_left_capture=YES no_extra_world_draw=YES half_history=YES depth_cases="<<cases<<" max_error="<<max_error<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
