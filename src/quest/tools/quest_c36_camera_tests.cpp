#include "hpvr/quest_cinematic.h"
#include "hpvr/quest_demo.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace hpvr::quest;
namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Near(float a,float b){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<.0002F;}
void Same(const Matrix4& a,const Matrix4& b,const char* message){
    for(unsigned i=0;i<16;++i)Check(Near(a[i],b[i]),message);
}
ViewPose Pose(std::array<float,3> position,float yaw=0,float pitch=0){
    const float cy=std::cos(yaw*.5F),sy=std::sin(yaw*.5F);
    const float cp=std::cos(pitch*.5F),sp=std::sin(pitch*.5F);
    return {position,{cy*sp,sy*cp,-sy*sp,cy*cp}};
}
Matrix4 Panel(const ViewPose& head,float scale){
    ViewPose theater;Matrix4 result{};
    Check(BuildTheaterPose(head,&theater)&&BuildRigidTransform(theater,&result),"expected theater pose");
    for(unsigned i=0;i<12;++i)result[i]*=scale;
    return result;
}
float Distance(const std::array<float,3>& a,const std::array<float,3>& b){
    float squared=0;for(unsigned i=0;i<3;++i)squared+=(a[i]-b[i])*(a[i]-b[i]);
    return std::sqrt(squared);
}
void TestPanel(){
    constexpr float scale=.65F;
    CinematicPanelAnchor anchor;Matrix4 output{},first{};
    auto head=Pose({1,2,3},.3F,.2F);
    Check(anchor.Update(head,nullptr,false,false,scale,&output),"world panel captures");
    Same(output,Panel(head,scale),"world panel placed at rendered head");first=output;
    head=Pose({8,4,-3},-1.2F,-.4F);
    Check(anchor.Update(head,nullptr,false,false,scale,&output),"retained world panel");
    Same(output,first,"head movement cannot drag world panel");
    Check(anchor.Update(head,nullptr,false,true,scale,&output),"explicit world recapture");
    Same(output,Panel(head,scale),"recapture follows current rendered head");

    // Identity initial rig gives an independent expected local panel: a scaled
    // identity basis, 2.5 metres along local -Z. Then translate, yaw and pitch it.
    auto rig=Pose({5,2,8});head=rig;
    Check(anchor.Update(head,&rig,false,false,scale,&output),"enter scripted rig");
    Same(output,Panel(head,scale),"cinematic entry reanchors from gameplay");
    const auto relative=anchor.relative;
    rig=Pose({11,4,-3},1.2F,.45F);Matrix4 rig_matrix{};
    Check(BuildRigidTransform(rig,&rig_matrix),"moving rig transform");
    head=Pose({100,50,-70},-.8F,.7F);
    Check(anchor.Update(head,&rig,false,false,scale,&output),"moving scripted panel");
    for(unsigned axis=0;axis<3;++axis){
        Check(Near(output[12+axis],rig.position[axis]-2.5F*rig_matrix[8+axis]),"panel rides scripted translation yaw and pitch");
        for(unsigned column=0;column<3;++column)
            Check(Near(output[column*4+axis],scale*rig_matrix[column*4+axis]),"panel basis follows rig not head");
    }
    Same(anchor.relative,relative,"rig travel does not recapture tracked head");first=output;
    Check(anchor.Update(Pose({-100,-100,100},2,-1),&rig,false,false,scale,&output),"look and lean during scene");
    Same(output,first,"cinematic panel is not headlocked");

    head=Pose({9,5,-6},-.2F,.8F);
    Check(anchor.Update(head,&rig,true,false,scale,&output),"switch to Harry while menu open");
    Same(output,Panel(head,scale),"perspective switch reanchors immediately");
    head=Pose({-3,2,8},1.6F,-.2F);
    Check(anchor.Update(head,&rig,false,false,scale,&output),"switch back to theater live");
    Same(output,Panel(head,scale),"reverse perspective switch reanchors");
    // A nonidentity rig must cancel exactly when first captured, including its
    // translation and pitch; otherwise merely opening settings rotates the UI.
    Check(anchor.Update(head,&rig,false,true,scale,&output),"nonidentity rig recapture");
    Same(output,Panel(head,scale),"inverse scripted rig preserves initial world anchor");
    head=Pose({20,1.7F,4},.6F);
    Check(anchor.Update(head,nullptr,false,false,scale,&output),"scene ends with menu open");
    Same(output,Panel(head,scale),"return to gameplay discards cinematic anchor");
    Check(anchor.valid&&!anchor.cinematic&&!anchor.first_person,"anchor mode reflects gameplay return");

    const auto saved=anchor.relative;const auto saved_output=output;
    auto invalid=head;invalid.position[0]=std::numeric_limits<float>::quiet_NaN();
    Check(!anchor.Update(invalid,nullptr,false,true,scale,&output),"reject invalid rendered head on capture");
    Same(anchor.relative,saved,"failed capture preserves old anchor");
    Same(output,saved_output,"failed capture does not publish invalid transform");
    invalid=rig;invalid.orientation={0,0,0,0};
    Check(!anchor.Update(head,&invalid,false,false,scale,&output),"reject invalid rig quaternion");
    Check(!anchor.Update(head,nullptr,false,false,scale,nullptr),"reject null destination");
    for(float bad:{0.0F,-1.0F,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
        Check(!anchor.Update(head,nullptr,false,true,bad,&output),"reject nonpositive or nonfinite scale");
    Same(anchor.relative,saved,"invalid scale does not mutate retained anchor");
    Same(output,saved_output,"invalid scale does not publish transform");
}
void TestActorCamera(){
    constexpr float eye_height=1.655F,ipd=.064F;
    auto reference=Pose({0,1.6F,0},.4F);
    auto local=Pose({.2F,1.4F,-.1F},.7F,.3F);
    ViewPose camera,mapped_head,left=local,right=local,mapped_left,mapped_right;
    Check(BuildDemoActorEye({4,2,-7},1.1F,eye_height,&camera),"Harry eye pose");
    Check(Near(camera.position[1],2+eye_height),"eye height measured from actor feet");
    Matrix4 local_matrix{};Check(BuildRigidTransform(local,&local_matrix),"tracked head matrix");
    for(unsigned i=0;i<3;++i){left.position[i]-=local_matrix[i]*ipd*.5F;right.position[i]+=local_matrix[i]*ipd*.5F;}
    Check(MapCinematicEye(local,local,camera,reference,&mapped_head)&&
          MapCinematicEye(left,local,camera,reference,&mapped_left)&&
          MapCinematicEye(right,local,camera,reference,&mapped_right),"both firstperson eyes map");
    Check(Near(Distance(mapped_left.position,mapped_right.position),ipd),"firstperson preserves physical IPD");
    for(unsigned i=0;i<3;++i)Check(Near((mapped_left.position[i]+mapped_right.position[i])*.5F,mapped_head.position[i]),"eyes centered around tracked head");
    Check(Near(Distance(mapped_head.position,camera.position),Distance(local.position,reference.position)),"lean crouch and forward movement stay 6DOF");
    ViewPose turned=local,turned_world;turned.orientation=reference.orientation;
    Check(MapCinematicEye(turned,turned,camera,reference,&turned_world),"reference relative look");
    Matrix4 expected{},actual{};Check(BuildRigidTransform(camera,&expected)&&BuildRigidTransform(turned_world,&actual),"orientation matrices");
    for(unsigned i=0;i<12;++i)Check(Near(expected[i],actual[i]),"reference heading gives actor heading");
    Check(BuildRigidTransform(mapped_head,&actual),"turned mapped head matrix");
    Check(!Near(expected[9],actual[9]),"physical head pitch is not overwritten by actor yaw");

    ViewPose moved_camera,moved_head;
    Check(BuildDemoActorEye({8,3,-2},1.1F,eye_height,&moved_camera)&&
          MapCinematicEye(local,local,moved_camera,reference,&moved_head),"moving Harry camera");
    for(unsigned i=0;i<3;++i)Check(Near(moved_head.position[i]-mapped_head.position[i],moved_camera.position[i]-camera.position[i]),"camera follows actor motion not old seated body");
    Check(!BuildDemoActorEye({0,0,0},0,0,&camera),"reject zero eye height");
    Check(!BuildDemoActorEye({0,0,0},std::numeric_limits<float>::quiet_NaN(),eye_height,&camera),"reject invalid actor yaw");

    // Firstperson return uses the actual tracked offset and yaw. Physical pitch
    // and roll survive because locomotion transforms the original tracked pose.
    for(float actor_yaw:{-2.1F,0.0F,1.5F}){
        Check(BuildDemoActorEye({8,3,-2},actor_yaw,eye_height,&camera),"return actor heading");
        DemoCameraReturn ret;
        Check(ret.Capture(camera,local,reference)&&ret.valid,"capture actual firstperson return");
        Check(MapCinematicEye(local,local,camera,reference,&mapped_head),"displayed final firstperson head");
        auto position=camera.position;for(unsigned i=0;i<3;++i)position[i]+=ret.offset[i];
        LocomotionState locomotion;
        Check(locomotion.ObserveHead(local)&&locomotion.RestoreHead(position,ret.yaw),"restore player at actor endpoint");
        ViewPose resumed;Check(locomotion.MapPose(local,&resumed),"resumed gameplay head");
        Check(BuildRigidTransform(mapped_head,&expected)&&BuildRigidTransform(resumed,&actual),"compare end and resumed transforms");
        Same(actual,expected,"cutscene release preserves tracked view without old-body snap");
        auto bad=local;bad.orientation={0,0,0,0};
        Check(!ret.Capture(camera,bad,reference)&&!ret.valid,"invalid tracking cannot leave valid stale return");
    }
}
}
int main(){try{
    TestPanel();TestActorCamera();
    std::cout<<"C36_CAMERA=PASS panel=SCRIPTED_RIG_NOT_HEADLOCKED switching=LIVE_REANCHOR camera=ACTOR_6DOF_IPD return=CONTINUOUS\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"C36_CAMERA_FAIL="<<e.what()<<'\n';return 1;}}
