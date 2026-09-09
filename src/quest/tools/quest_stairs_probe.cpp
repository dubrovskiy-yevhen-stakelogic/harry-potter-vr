#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <stdexcept>
#include <queue>
using namespace hpvr::quest;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Quad(std::vector<GpuVertex>& v,std::array<float,3> a,std::array<float,3> b,
                 std::array<float,3> c,std::array<float,3> d){
    for(const auto& p:{a,b,c,a,c,d})v.push_back({{p[0],p[1],p[2]},{0,0},{0,0},0,0,0,0});
}
static void Walk(const std::vector<CollisionTriangle>& triangles,std::array<float,3>& p,
                 std::array<float,3> goal){
    for(int frame=0;frame<3000;++frame){
        auto d=SubtractVector(goal,p);d[1]=0;float length=std::hypot(d[0],d[2]);
        if(length<0.015F)return;
        LocomotionMove move;Check(ResolveCollisionMovement(triangles,p,ScaleVector(d,std::min(length,0.03F)/length),&move),"resolver failed");
        if(std::hypot(move.displacement[0],move.displacement[2])<0.00001F){
            std::cerr<<"blocked at="<<p[0]<<','<<p[1]<<','<<p[2]<<" goal="<<goal[0]<<','<<goal[1]<<','<<goal[2]<<'\n';
            throw std::runtime_error("stair route blocked");
        }
        p=AddVector(p,move.displacement);
        Check(!OverlapsCollisionWall(triangles,p),"movement penetrated solid surface");
    }
    throw std::runtime_error("route timeout");
}
static void Navigate(const std::vector<CollisionTriangle>& triangles,std::array<float,3>& start,
                     const std::array<float,3>& goal){
    struct Node {std::array<float,3> p;int x,z;float cost,score;};
    auto farther=[](const Node& a,const Node& b){return a.score>b.score;};
    std::priority_queue<Node,std::vector<Node>,decltype(farther)> todo(farther);
    std::map<std::pair<int,int>,float> costs;
    auto heuristic=[&](const auto& p){return std::hypot(goal[0]-p[0],goal[2]-p[2]);};
    todo.push({start,0,0,0,heuristic(start)});costs[{0,0}]=0;
    for(unsigned explored=0;!todo.empty()&&explored<30000;++explored){
        const auto n=todo.top();todo.pop();
        if(n.cost>costs[{n.x,n.z}]+0.001F)continue;
        if(heuristic(n.p)<0.25F){start=n.p;std::cout<<"NAV_NODES="<<explored<<'\n';return;}
        for(const auto& dir:std::array<std::array<int,2>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}){
            const int x=n.x+dir[0],z=n.z+dir[1];const auto key=std::make_pair(x,z);
            if(std::abs(x)>160||std::abs(z)>160)continue;
            const float cost=n.cost+0.2F;
            if(costs.contains(key)&&costs.at(key)<=cost)continue;
            LocomotionMove move;
            const std::array<float,3> delta{dir[0]*0.2F,0,dir[1]*0.2F};
            if(!ResolveCollisionMovement(triangles,n.p,delta,&move) ||
               std::hypot(move.displacement[0]-delta[0],move.displacement[2]-delta[2])>0.005F)continue;
            const auto p=AddVector(n.p,move.displacement);
            costs[key]=cost;todo.push({p,x,z,cost,cost+heuristic(p)});
        }
    }
    std::cerr<<"nav_failed start="<<start[0]<<','<<start[1]<<','<<start[2]
             <<" goal="<<goal[0]<<','<<goal[1]<<','<<goal[2]<<" visited="<<costs.size()<<'\n';
    throw std::runtime_error("no bounded walkable route to quest marker");
}
int main(int argc,char**argv){try{
    std::vector<GpuVertex> vertices;
    Quad(vertices,{-2,0,-3},{2,0,-3},{2,0,0},{-2,0,0});
    constexpr float rise=0.16F,run=0.36F;constexpr int steps=12;
    for(int i=0;i<steps;++i){float z=i*run,y=(i+1)*rise;
        Quad(vertices,{-2,y-rise,z},{2,y-rise,z},{2,y,z},{-2,y,z});
        Quad(vertices,{-2,y,z},{2,y,z},{2,y,z+run},{-2,y,z+run});}
    Quad(vertices,{-2,steps*rise,steps*run},{2,steps*rise,steps*run},{2,steps*rise,steps*run+3},{-2,steps*rise,steps*run+3});
    auto triangles=BuildCollisionTriangles(vertices,static_cast<std::uint32_t>(vertices.size()));
    std::array<float,3> p{0,kPlayerCapsuleHalfHeightMeters,-1};
    Walk(triangles,p,{0,0,steps*run+1});
    Check(std::abs(p[1]-steps*rise-kPlayerCapsuleHalfHeightMeters)<0.005F,"wrong top height");
    Walk(triangles,p,{0,0,-1});
    Check(std::abs(p[1]-kPlayerCapsuleHalfHeightMeters)<0.005F,"wrong bottom height");
    Walk(triangles,p,{0.7F,0,steps*run+1});
    Walk(triangles,p,{-0.7F,0,-1});
    auto push=[&](const std::vector<CollisionTriangle>& solid){
        std::array<float,3> q{0,kPlayerCapsuleHalfHeightMeters,-1};
        for(int n=0;n<100;++n){LocomotionMove m;Check(ResolveCollisionMovement(solid,q,{0,0,0.03F},&m),"barrier resolve");q=AddVector(q,m.displacement);}
        return q;
    };
    std::vector<GpuVertex> barrier;
    Quad(barrier,{-2,0,-3},{2,0,-3},{2,0,0},{-2,0,0});
    Quad(barrier,{-2,0,0},{2,0,0},{2,0.7F,0},{-2,0.7F,0});
    Quad(barrier,{-2,0.7F,0},{2,0.7F,0},{2,0.7F,3},{-2,0.7F,3});
    auto wall=BuildCollisionTriangles(barrier,static_cast<std::uint32_t>(barrier.size()));
    auto blocked=push(wall);Check(blocked[2]<0 && std::abs(blocked[1]-kPlayerCapsuleHalfHeightMeters)<0.005F,"tall obstacle climbed");
    ClimbMotion climb;
    Check(BeginClimb(wall,blocked,{0,0,0.03F},&climb),"bookcase mantle not found");
    auto climbed=blocked;
    for(int i=0;i<200&&climb.active;++i){LocomotionMove move;
        Check(StepClimb(wall,climb,climbed,0.03F,&move),"climb step");
        Check(std::sqrt(DotVector(move.displacement,move.displacement))<=0.0301F,"mantle teleported");
        climbed=AddVector(climbed,move.displacement);Check(!OverlapsCollisionWall(wall,climbed),"mantle penetrated");}
    Check(!climb.active&&climbed[2]>0&&std::abs(climbed[1]-1.54F)<0.01F,"mantle landing");
    auto covered=barrier;Quad(covered,{-2,1.8F,-2},{2,1.8F,-2},{2,1.8F,3},{-2,1.8F,3});
    Check(!BeginClimb(BuildCollisionTriangles(covered,static_cast<std::uint32_t>(covered.size())),blocked,{0,0,0.03F},&climb),"mantled through roof");
    std::cout<<"MANTLE=PASS swept=YES ceiling=BLOCKED smooth=YES\n";
    auto roof=vertices;
    Quad(roof,{-2,1.75F,-2},{2,1.75F,-2},{2,1.75F,1},{-2,1.75F,1});
    auto ceiling=BuildCollisionTriangles(roof,static_cast<std::uint32_t>(roof.size()));
    blocked=push(ceiling);Check(blocked[2]<0 && !OverlapsCollisionWall(ceiling,blocked),"stepped through low ceiling");
    barrier.clear();Quad(barrier,{-2,0,-3},{2,0,-3},{2,0,0},{-2,0,0});
    auto ledge=BuildCollisionTriangles(barrier,static_cast<std::uint32_t>(barrier.size()));
    blocked=push(ledge);Check(blocked[2]<=kPlayerCapsuleRadiusMeters,"walked into unsupported air");
    std::cout<<"BARRIERS=PASS tall_step=BLOCKED low_ceiling=BLOCKED ledge=BLOCKED diagonal=PASS\n";
    std::cout<<"SYNTHETIC_STAIRS=PASS steps=12 directions=UP_DOWN\n";
    if(argc==2){
        const std::filesystem::path root(argv[1]),map=root/"Maps/Lev_Tut1.unr";
        const auto scene=hpvr::wand::build_hp1_textured_bsp_scene(root,map,kMetersPerUnrealUnit,kMaximumTriangles);
        Check(scene.status==hpvr::wand::Hp1ProfileStatus::ok,"owned BSP load");
        hpvr_hp1_player_start_report start{};
        Check(hpvr_hp1_load_player_start_utf8(map.string().c_str(),kMetersPerUnrealUnit,0,&start)==HPVR_HP1_PROFILE_OK,"player start");
        const float yaw=start.rotation_units[1]*kTau/65536.0F;
        vertices.clear();for(const auto& v:scene.vertices){const auto q=RotateYaw({v.position_m.x-start.position_m[0],v.position_m.y-start.position_m[1],v.position_m.z-start.position_m[2]},yaw);
            vertices.push_back({{q[0],q[1],q[2]},{0,0},{0,0},0,v.polygon_flags,0,0});}
        triangles=BuildCollisionTriangles(vertices,static_cast<std::uint32_t>(vertices.size()));
        const auto census=hpvr::wand::inspect_hp1_actor_visuals(map);IntroCutscene intro;
        Check(LoadIntroCutscene(census,start,yaw,&intro),"intro route load");
        std::vector<std::array<float,3>> route;
        for(const auto& track:intro.tracks)if(track.actor_reference==1672){
            route.push_back(track.position);
            for(const auto& command:track.commands)if(AsciiFold(command).starts_with("moveto ")){
                const auto name=AsciiFold(command.substr(7));
                for(const auto& mark:intro.locations)if(AsciiFold(mark.alias)==name)route.push_back(mark.position);}}
        Check(route.size()>=3,"missing staircase route");
        auto make_start=[&](std::array<float,3> q){float floor;Check(FindPropGroundBelow(triangles,q,&floor),"route start floor");q[1]=floor+kPlayerCapsuleHalfHeightMeters;return q;};
        p=make_start(route.front());const float upper=p[1];
        for(std::size_t i=1;i<route.size();++i)Walk(triangles,p,route[i]);
        const float lower=p[1];
        for(std::size_t i=route.size()-1;i>0;--i)Walk(triangles,p,route[i-1]);
        Check(std::abs(p[1]-upper)<0.04F && upper-lower>2,"owned stair height");
        std::cout<<"OWNED_GRAND_STAIRS=PASS up_down_height="<<upper-lower<<" route_marks="<<route.size()<<'\n';
        IntroCutscene ron;Check(LoadIntroCutscene(census,start,yaw,&ron,"CutScene51"),"Ron encounter");
        std::cout<<"RON_TRIGGER="<<ron.trigger_position[0]<<','<<ron.trigger_position[1]<<','<<ron.trigger_position[2]
                 <<" radius="<<ron.trigger_radius<<'\n';
        Navigate(triangles,p,ron.trigger_position);
        Check(std::abs(p[1]-ron.trigger_position[1])<2.0F,"Ron trigger vertically reachable");
        auto mark=[&](const std::string& name){
            for(const auto& l:ron.locations)if(AsciiFold(l.alias)==name)return l.position;
            throw std::runtime_error("missing Ron path mark");
        };
        p=make_start(mark("hppath1"));
        Navigate(triangles,p,mark("allpath1"));
        Navigate(triangles,p,mark("ronwait"));
        std::cout<<"FIRST_QUEST_NAV=PASS stairs_to_trigger=YES harry_to_ron_wait=YES\n";
        IntroCutscene twins,next;
        Check(LoadIntroCutscene(census,start,yaw,&twins,"CutScene52")&&
              LoadIntroCutscene(census,start,yaw,&next,"CutScene54"),"twins sequence");
        Navigate(triangles,p,twins.trigger_position);
        auto beginning=std::ranges::find_if(twins.locations,[](const auto& l){return AsciiFold(l.alias)=="hploc2";});
        Check(beginning!=twins.locations.end(),"climb lesson start");p=make_start(beginning->position);
        std::vector<BeanDraw> beans;std::vector<GpuVertex> bean_vertices;std::vector<std::uint8_t> bean_pixels;std::uint32_t bean_layers=0;
        Check(LoadOwnedBeans(root,map,start,yaw,bean_vertices,bean_pixels,bean_layers,beans),"owned beans load");
        for(const auto& bean:beans)if(bean.actor_reference==2913||bean.actor_reference==2145||bean.actor_reference==2369||bean.actor_reference==3498)
            std::cout<<"LESSON_BEAN ref="<<bean.actor_reference<<" p="<<bean.position[0]<<','<<bean.position[1]<<','<<bean.position[2]<<'\n';
        std::cout<<"LESSON_START="<<p[0]<<','<<p[1]<<','<<p[2]<<" NEXT="<<next.trigger_position[0]<<','<<next.trigger_position[1]<<','<<next.trigger_position[2]<<'\n';
        auto first=std::ranges::find_if(beans,[](const auto& b){return b.actor_reference==2913;});
        Check(first!=beans.end(),"first bookcase bean");ClimbMotion mantle;unsigned climbs=0;
        for(const auto ref:{2913,2145}){
        first=std::ranges::find_if(beans,[&](const auto& b){return b.actor_reference==ref;});Check(first!=beans.end(),"route bean");
        for(int frame=0;frame<3000;++frame){
            auto delta=SubtractVector(first->position,p);delta[1]=0;const float length=std::hypot(delta[0],delta[2]);
            if(length<0.3F&&!mantle.active)break;
            const auto request=ScaleVector(delta,0.03F/std::max(length,0.001F));LocomotionMove move;
            if(mantle.active)Check(StepClimb(triangles,mantle,p,0.03F,&move),"owned mantle step");
            else {Check(ResolveCollisionMovement(triangles,p,request,&move),"owned bookcase walk");
                if(std::hypot(move.displacement[0],move.displacement[2])<0.001F){
                    std::cout<<"CLIMB_ATTEMPT="<<p[0]<<','<<p[1]<<','<<p[2]<<'\n';
                    Check(InClimbLesson(twins,next,p),"runtime climb region excludes bookcase");
                    Check(BeginClimb(triangles,p,request,&mantle),"owned mantle target not found");++climbs;
                    Check(StepClimb(triangles,mantle,p,0.03F,&move),"owned first mantle step");}}
            p=AddVector(p,move.displacement);
        }
        const auto distance=SubtractVector(p,first->position);
        Check(climbs>0&&std::hypot(distance[0],distance[2])<0.4F&&std::abs(distance[1])<0.9F,"bean reachable after mantle");
        Check(CanCollectBean(triangles,p,first->position),"actual bean pickup rejected");
        auto below=p;below[1]-=2;Check(!CanCollectBean(triangles,below,first->position),"bean collected from under bookcase");
        std::cout<<"OWNED_BOOKCASE=PASS climbs="<<climbs<<" bean="<<ref<<'\n';
        }
        Navigate(triangles,p,next.trigger_position);
        Check(std::abs(p[1]-next.trigger_position[1])<2.0F,"next room vertical reach");
        std::cout<<"TWINS_LESSON_NAV=PASS next_room=REACHABLE\n";
    }
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
