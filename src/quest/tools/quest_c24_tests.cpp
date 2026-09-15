#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <stdexcept>
using namespace hpvr::quest;
static void Check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static void Quad(std::vector<GpuVertex>& v,float y,float x0,float x1,float z0,float z1){
    for(const auto& p:std::array<std::array<float,3>,6>{{{x0,y,z0},{x1,y,z0},{x1,y,z1},{x0,y,z0},{x1,y,z1},{x0,y,z1}}})
        v.push_back({{p[0],p[1],p[2]},{0,0},{0,0},0,0,0,0});
}
int main(int argc,char** argv){try{
    std::vector<GpuVertex> v;Quad(v,0,-4,4,-4,4);
    auto triangles=BuildCollisionTriangles(v,static_cast<std::uint32_t>(v.size()));
    for(float dt:{1.0F/72,1.0F/90,1.0F/120,0.05F}){
        JumpMotion jump;std::array<float,3> p{0,kPlayerCapsuleHalfHeightMeters,0};
        Check(StartJump(triangles,p,jump),"grounded jump rejected");
        Check(!StartJump(triangles,p,jump),"double jump accepted");float peak=p[1];
        for(unsigned i=0;i<300&&jump.active;++i){LocomotionMove move;
            Check(StepJump(triangles,jump,p,{},dt,&move),"jump step failed");
            p=AddVector(p,move.displacement);peak=std::max(peak,p[1]);
            Check(!OverlapsCollisionWall(triangles,p),"jump penetrates solid");}
        Check(!jump.active&&std::abs(p[1]-kPlayerCapsuleHalfHeightMeters)<0.002F,"jump landing failed");
        Check(peak-kPlayerCapsuleHalfHeightMeters>0.93F&&peak-kPlayerCapsuleHalfHeightMeters<0.96F,"jump apex changes with frame rate");
    }
    Quad(v,1.9F,-4,4,-4,4);triangles=BuildCollisionTriangles(v,static_cast<std::uint32_t>(v.size()));
    JumpMotion jump;std::array<float,3> p{0,kPlayerCapsuleHalfHeightMeters,0};
    Check(StartJump(triangles,p,jump),"low roof jump failed");float peak=p[1];
    for(unsigned i=0;i<200&&jump.active;++i){LocomotionMove move;StepJump(triangles,jump,p,{},1.0F/72,&move);
        p=AddVector(p,move.displacement);peak=std::max(peak,p[1]);Check(!OverlapsCollisionWall(triangles,p),"jump crosses roof");}
    Check(!jump.active&&peak<1.1F,"roof failed to stop jump");
    p[1]=4;Check(!StartJump(triangles,p,jump),"air jump accepted");
    v.clear();Quad(v,0,-3,3,-4,0);Quad(v,0,-3,3,1,4);
    triangles=BuildCollisionTriangles(v,static_cast<std::uint32_t>(v.size()));
    p={0,kPlayerCapsuleHalfHeightMeters,-0.2F};Check(StartJump(triangles,p,jump),"gap takeoff");
    const auto safe=SafeCheckpointHead({0,4,0},ClimbMotion{},jump);
    Check(std::abs(safe[1]-kPlayerCapsuleHalfHeightMeters-kPlayerEyeHeightMeters)<0.001F&&safe[2]==-0.2F,"mid-air checkpoint unsafe");
    for(unsigned i=0;i<150&&jump.active;++i){LocomotionMove move;
        StepJump(triangles,jump,p,{0,0,2.8F/72},1.0F/72,&move);p=AddVector(p,move.displacement);}
    Check(!jump.active&&p[2]>1.0F&&std::abs(p[1]-kPlayerCapsuleHalfHeightMeters)<0.002F,"forward gap jump failed");
    std::cout<<"C24_JUMP=PASS neutral=YES double_jump=BLOCKED roof=BLOCKED forward_gap=YES safe_checkpoint=YES frame_rates=20,72,90,120\n";
    LocomotionState locomotion;ViewPose local{{0,0,0},{0,0,0,1}};
    Check(locomotion.ObserveHead(local),"head observation");
    ViewPose before,after;Check(locomotion.MapPose(local,&before),"initial pose");
    unsigned ticks=0;auto physics=[](void* ctx,const std::array<float,3>&,const std::array<float,3>& request,LocomotionMove* move){
        ++*static_cast<unsigned*>(ctx);Check(request==std::array<float,3>{},"inactive stick moved during physics");
        *move={};move->displacement[1]=0.02F;return true;};
    LocomotionInput input;input.physics_active=true;input.move_x=1;
    Check(locomotion.Tick(input,1.0F/72,physics,&ticks)&&ticks==1,"neutral physics not ticked");
    Check(locomotion.MapPose(local,&after)&&std::abs(after.position[1]-before.position[1]-0.02F)<0.0001F,"jump did not move camera");
    QuestFrontEnd front;front.assets.story.resize(14);front.BeginGame();
    front.Input(0,false,true);Check(front.screen==FrontScreen::Pause,"menu button did not pause");
    front.Input(0,false,false);front.selection=3;front.Input(0,true,false);
    Check(front.screen==FrontScreen::Cards,"card album inaccessible");
    front.Input(0,false,false,1);Check(front.card_page==1,"right page input");
    front.Input(0,false,false,1);Check(front.card_page==1,"held stick repeats pages");
    front.Input(0,false,true);Check(front.screen==FrontScreen::Pause,"album back exited game book");
    front.progress.house_points={78,46,113,90};
    Check(front.ReportValue(2)==90&&front.ReportValue(3)==78&&!front.Quads().empty(),"pause displays the live original house report");
    front.Input(0,false,false);front.selection=4;front.Input(0,true,false);
    Check(front.screen==FrontScreen::Vr&&front.vr_return==FrontScreen::Pause,"original Options orb opens settings");
    front.Input(0,false,false);front.Input(0,false,true);front.Input(0,false,false);
    Check(front.Input(0,false,true)==FrontAction::Resume&&!front.Visible(),"book close failed");
    const auto health=front.HudQuads(0,false),beans=front.HudQuads(14,true);
    Check(health.size()==2&&beans.size()>health.size(),"empty bar plus clipped health and pickup HUD missing");
    std::cout<<"C24_UI_INPUT=PASS book=YES cards=7_pages report=YES neutral_jump_camera=YES\n";
    IntroCutscene release;release.playing=true;release.tracks.resize(2);release.tracks[1].moving=true;
    Check(!ReleaseCutsceneControlIfReady(release),"control released before script Harry release");
    release.harry_released=true;release.camera_active=true;
    Check(!ReleaseCutsceneControlIfReady(release),"control released while camera captured");
    release.camera_active=false;
    Check(ReleaseCutsceneControlIfReady(release)&&release.control_released&&release.playing&&release.tracks[1].moving,
        "NPC exit track either blocked control or was killed");
    Check(!ReleaseCutsceneControlIfReady(release),"control release repeated");
    std::cout<<"C24_CUTSCENE_RELEASE=PASS actor_tail=CONTINUES control=ONCE_AFTER_HARRY_AND_CAMERA\n";
    if(argc==2){
        const std::filesystem::path root(argv[1]);
        const auto scene=hpvr::wand::build_hp1_textured_bsp_scene(root,root/"Maps/Lev_Tut1.unr",kMetersPerUnrealUnit,kMaximumTriangles);
        Check(scene.status==hpvr::wand::Hp1ProfileStatus::ok,"owned BSP load failed");
        hpvr_hp1_player_start_report start{};
        Check(hpvr_hp1_load_player_start_utf8((root/"Maps/Lev_Tut1.unr").string().c_str(),kMetersPerUnrealUnit,0,&start)==HPVR_HP1_PROFILE_OK,"start load");
        const float yaw=start.rotation_units[1]*kTau/65536.0F;
        const auto census=hpvr::wand::inspect_hp1_actor_visuals(root/"Maps/Lev_Tut1.unr");IntroCutscene intro;
        Check(LoadIntroCutscene(census,start,yaw,&intro),"opening load");
        std::vector<std::array<float,3>> route;
        for(const auto& t:intro.tracks)if(t.actor_reference==1672){
            route.push_back(t.position);
            for(const auto& command:t.commands)if(AsciiFold(command).starts_with("moveto "))
                for(const auto& loc:intro.locations)if(AsciiFold(loc.alias)==AsciiFold(command.substr(7)))route.push_back(loc.position);
        }
        Check(route.size()>=3,"main stair route missing");
        std::set<int> tread_heights;unsigned ramps=0;
        auto near_route=[&](const std::array<float,3>& p){
            for(std::size_t i=1;i<route.size();++i){
                auto d=SubtractVector(route[i],route[i-1]);d[1]=0;
                const float length=DotVector(d,d);if(length<0.0001F)continue;
                auto to=SubtractVector(p,route[i-1]);to[1]=0;
                const auto residual=SubtractVector(to,ScaleVector(d,std::clamp(DotVector(to,d)/length,0.0F,1.0F)));
                if(DotVector(residual,residual)<0.7F*0.7F)return true;
            }return false;
        };
        std::map<std::uint32_t,unsigned> flags;
        for(std::size_t i=0;i+2<scene.vertices.size();i+=3){const auto& a=scene.vertices[i];++flags[a.polygon_flags];
            std::array<std::array<float,3>,3> points;
            for(unsigned j=0;j<3;++j){const auto& q=scene.vertices[i+j].position_m;
                points[j]=RotateYaw({q.x-start.position_m[0],q.y-start.position_m[1],q.z-start.position_m[2]},yaw);}
            auto center=ScaleVector(AddVector(AddVector(points[0],points[1]),points[2]),1.0F/3);
            if(!near_route(center))continue;
            const auto d=SubtractVector(points[1],points[0]),e=SubtractVector(points[2],points[0]);
            std::array<float,3> n{};if(!NormalizeVector({d[1]*e[2]-d[2]*e[1],d[2]*e[0]-d[0]*e[2],d[0]*e[1]-d[1]*e[0]},&n))continue;
            if(!(a.polygon_flags&1)&&std::abs(n[1])>.999F)tread_heights.insert(static_cast<int>(std::lround(center[1]*1000)));
            if((a.polygon_flags&1)&&std::abs(n[1])>.4F&&std::abs(n[1])<.99F)++ramps;
        }
        Check(ramps>0&&tread_heights.size()>8,"authored stairs/ramp proof missing");
        std::cout<<"C24_OWNED_STAIRS=PASS invisible_ramp_triangles="<<ramps<<" visible_tread_levels="<<tread_heights.size()<<'\n';
        for(const auto& [flag,count]:flags)std::cout<<"BSP_FLAGS="<<flag<<" triangles="<<count<<'\n';
    }
    return 0;
}catch(const std::exception& e){std::cerr<<"C24_FAILED="<<e.what()<<'\n';return 1;}}
