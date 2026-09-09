#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <stdexcept>
using namespace hpvr::quest;
namespace wand=hpvr::wand;
static void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
    if(argc!=2)return 2;const std::filesystem::path root(argv[1]);
    const auto map=root/"Maps/Lev_Tut1.unr";
    const auto data=wand::build_hp1_textured_bsp_scene(root,map,kMetersPerUnrealUnit,kMaximumTriangles);
    Check(data.status==wand::Hp1ProfileStatus::ok,"map");
    hpvr_hp1_player_start_report start{};Check(hpvr_hp1_load_player_start_utf8(map.string().c_str(),kMetersPerUnrealUnit,0,&start)==0,"start");
    const float yaw=start.rotation_units[1]*kTau/65536.0F;
    std::vector<GpuVertex> vertices;
    for(const auto& v:data.vertices){auto p=RotateYaw({v.position_m.x-start.position_m[0],v.position_m.y-start.position_m[1],v.position_m.z-start.position_m[2]},yaw);
        vertices.push_back({{p[0],p[1],p[2]},{0,0},{0,0},0,v.polygon_flags,0,0});}
    auto triangles=BuildCollisionTriangles(vertices,static_cast<std::uint32_t>(vertices.size()));
    for(int ref:{1341}){
        const auto mesh=wand::build_hp1_skeletal_triangle_mesh(root/"system/HarryPotter.u",ref,kMetersPerUnrealUnit);
        std::map<std::pair<unsigned,unsigned>,unsigned> flags;
        for(const auto& v:mesh.vertices)++flags[{v.material_index,v.polygon_flags}];
        for(const auto& [key,n]:flags)std::cout<<"HARRY_MATERIAL slot="<<key.first<<" flags="<<key.second<<" vertices="<<n<<'\n';
    }
    const auto census=wand::inspect_hp1_actor_visuals(map);
    std::map<std::string,unsigned> classes;
    for(const auto& actor:census.actors)if(AsciiFold(actor.qualified_class_name).starts_with("hprops."))++classes[actor.qualified_class_name];
    for(const auto& [name,n]:classes)std::cout<<"PROP_CLASS="<<name<<" count="<<n<<'\n';
    for(const auto& actor:census.actors)if(AsciiFold(actor.qualified_class_name).find("candlestick")!=std::string::npos){
        auto p=ActorLocalPosition(actor,start,yaw);float floor=0;const bool grounded=FindPropGroundBelow(triangles,p,&floor);
        std::cout<<"CANDLE actor="<<actor.actor_reference<<" scale="<<actor.draw_scale<<" class="<<actor.qualified_class_name<<" position="<<p[0]<<','<<p[1]<<','<<p[2]<<" floor="<<floor<<" found="<<grounded<<'\n';
        if(actor.actor_reference==2341)for(const auto& other:census.actors){
            if(!other.location_serialized)continue;auto q=ActorLocalPosition(other,start,yaw);
            if(std::hypot(p[0]-q[0],p[2]-q[2])<2.5F)
                std::cout<<"NEAR_CANDLE ref="<<other.actor_reference<<" class="<<other.qualified_class_name<<" mesh="<<other.mesh_reference<<" xyz="<<q[0]<<','<<q[1]<<','<<q[2]<<'\n';
        }
    }
    for(int ref:{1495,1556}){
        const auto mesh=wand::build_hp1_skeletal_triangle_mesh(root/"system/HProps.u",ref,kMetersPerUnrealUnit);
        std::cout<<"PROP ref="<<ref<<" bounds="<<mesh.bounds_min_m.y<<':'<<mesh.bounds_max_m.y<<'\n';
    }
    unsigned samples=0,missing=0;std::optional<std::array<float,3>> ron_previous;
    for(const auto name:{"CutScene51","CutScene52","CutScene54","CutScene55","CutScene56","CutScene1","CutScene58","CutScene59"}){
        IntroCutscene cut;Check(LoadIntroCutscene(census,start,yaw,&cut,name),"cutscene load");
        std::cout<<"SCENE "<<name<<" trigger="<<cut.trigger_position[0]<<','<<cut.trigger_position[1]<<','<<cut.trigger_position[2]<<'\n';
        if(std::string(name)=="CutScene54"||std::string(name)=="CutScene55")
            for(const auto& l:cut.locations)std::cout<<"MARK "<<l.alias<<'='<<l.position[0]<<','<<l.position[1]<<','<<l.position[2]<<'\n';
        unsigned talks=0;
        auto schedule=cut;
        for(unsigned tick=0;tick<1000;++tick){
            bool advanced=false;
            for(auto& t:schedule.tracks){
                if(t.next_command==t.commands.size())continue;
                auto line=AsciiFold(t.commands[t.next_command]);auto split=line.find(' ');
                auto op=line.substr(0,split),arg=split==std::string::npos?std::string{}:line.substr(split+1);
                if(op=="waitfor"&&!schedule.cues.contains(arg))continue;
                if(op=="cue")schedule.cues.insert(arg);
                if(op.starts_with("talk")||op=="say")++talks;
                ++t.next_command;advanced=true;
            }
            if(!advanced)break;
        }
        Check(std::ranges::none_of(schedule.tracks,[](const auto& t){return t.next_command!=t.commands.size();}),"cue deadlock");
        std::cout<<"TALK_COUNT="<<talks<<'\n';
        const std::map<std::string,unsigned> expected{{"CutScene51",1},{"CutScene52",6},{"CutScene54",5},{"CutScene55",6},
            {"CutScene56",2},{"CutScene1",6},{"CutScene58",2},{"CutScene59",1}};
        Check(talks==expected.at(name),"dialogue count");
        for(auto& t:cut.tracks){
            if(t.camera)continue;
            auto p=t.position;
            if(t.actor_reference==1348&&ron_previous)p=*ron_previous;
            Check(GroundScriptActor(triangles,p,p,false,&p),"initial actor unsupported");
            float floor=0;Check(FindPropGroundBelow(triangles,p,&floor)&&std::abs(p[1]-floor)<0.0001F,"initial feet offset");
            if(t.actor_reference!=1348)continue; // Ron's authored ascending route, not teleport/climb choreography.
            for(auto line:t.commands){
                line=AsciiFold(line);
                if(line.starts_with("teleport ")){
                    auto loc=std::ranges::find_if(cut.locations,[&](const auto& l){return AsciiFold(l.alias)==line.substr(9);});
                    Check(loc!=cut.locations.end()&&GroundScriptActor(triangles,p,loc->position,false,&p),"teleport floor");continue;
                }
                if(!line.starts_with("moveto "))continue;
                auto loc=std::ranges::find_if(cut.locations,[&](const auto& l){return AsciiFold(l.alias)==line.substr(7);});
                Check(loc!=cut.locations.end(),"route mark");
                const auto from=p;
                for(int i=1;i<=600;++i){
                    auto request=AddVector(from,ScaleVector(SubtractVector(loc->position,from),i/600.0F));
                    ++samples;
                    if(!GroundScriptActor(triangles,p,request,true,&p)){if(missing<5)std::cout<<"MISS scene="<<name<<" mark="<<loc->alias<<" previous="<<p[0]<<','<<p[1]<<','<<p[2]<<" request="<<request[0]<<','<<request[1]<<','<<request[2]<<'\n';++missing;continue;}
                    Check(FindPropGroundBelow(triangles,p,&floor)&&std::abs(p[1]-floor)<0.0001F,"Ron feet below stairs");
                }
            }
            ron_previous=p;
        }
        std::cout<<"CUES=PASS scene="<<name<<" talks="<<talks<<'\n';
    }
    Check(samples>0&&missing==0,"Ron route support missing");
    for(float x=47;x<53;x+=.2F){float y;
        std::cout<<"EDGE x="<<x<<" floor="<<(FindPropGroundBelow(triangles,{x,13,-59.23F},&y)?y:-999)<<'\n';}
    for(float speed:{3.2F,4.0F}){
        auto p=std::array<float,3>{47.9F,12.07531F+kPlayerCapsuleHalfHeightMeters,-59.23F};JumpMotion j;
        bool ok=StartJump(triangles,p,j);
        for(int frame=0;frame<144&&j.active;++frame){LocomotionMove m;
            StepJump(triangles,j,p,{speed/72,0,0},1.0F/72,&m);p=AddVector(p,m.displacement);}
        for(int frame=0;frame<35;++frame){LocomotionMove m;
            ResolveCollisionMovement(triangles,p,{speed/72,0,0},&m);p=AddVector(p,m.displacement);}
        Check(ok&&!j.active&&p[0]>50.7F&&std::abs(p[1]-12.91531F)<.02F,"owned jump platform unreachable");
        std::cout<<"OWNED_JUMP speed="<<speed<<" started="<<ok<<" end="<<p[0]<<','<<p[1]<<','<<p[2]<<'\n';
    }
    for(float z=-60;z>=-74;z-=2){
        std::cout<<"FLOOR z="<<z<<' ';
        for(float x=40;x<=76;x+=1){float floor;
            if(FindPropGroundBelow(triangles,{x,14,z},&floor))std::cout<<int(std::round((floor-12.0753F)*10))<<',';
            else std::cout<<"X,";
        }std::cout<<'\n';
    }
    std::cout<<"C25_GROUND=PASS samples="<<samples<<" misses="<<missing<<" cast_initial_floor=YES\n";
    WorldMetadata world;Check(LoadWorldMetadata(root,map,start,yaw,&world),"world metadata");
    auto pixels=data.texture_rgba8;auto layers=data.texture_layer_count;
    std::uint32_t fixture_count=0;std::size_t fixture_actors=0;
    std::vector<KnightDraw> knights;
    Check(LoadOwnedSceneProps(root,map,start,yaw,world.lights,triangles,&vertices,&pixels,&layers,
        &fixture_count,&fixture_actors,&world.glows,&world.flames,&knights),"fixtures");
    Check(knights.size()==6,"animated knights");
    for(const auto& knight:knights){
        Check(knight.frames==380&&knight.first+knight.frames*knight.count<=vertices.size(),"knight range");
        float motion=0,foot_span=0,max_step=0;float first_floor=1000;
        for(unsigned frame=0;frame<knight.frames;++frame){
            float floor=1000;
            for(unsigned i=0;i<knight.count;++i){
                const auto& a=vertices[knight.first+i];const auto& b=vertices[knight.first+frame*knight.count+i];
                const auto& previous=vertices[knight.first+((frame+knight.frames-1)%knight.frames)*knight.count+i];
                float d2=0;for(unsigned axis=0;axis<3;++axis)d2+=(b.position[axis]-previous.position[axis])*(b.position[axis]-previous.position[axis]);
                max_step=std::max(max_step,std::sqrt(d2));
                motion=std::max(motion,std::abs(a.position[0]-b.position[0])+std::abs(a.position[2]-b.position[2]));
                floor=std::min(floor,b.position[1]);
            }
            if(frame==0)first_floor=floor;foot_span=std::max(foot_span,std::abs(floor-first_floor));
        }
        Check(motion>.01F&&foot_span<.001F,"knight animation static or drifting");
        std::cout<<"KNIGHT_MAX_STEP="<<max_step<<'\n';
        Check(max_step<.08F,"knight one-shot wrapped back to first pose");
    }
    std::cout<<"C26_KNIGHTS=PASS actors=6 frames_each=380 animated=YES feet=STABLE\n";
    Check(fixture_actors==54,"fixture count");Check(world.flames.size()==86,"wick count");
    for(const auto& f:world.flames)Check(std::isfinite(f.position[1]),"wick invalid");
    std::cout<<"C25_FIXTURES=PASS actors=54 tables=8 candle_wicks=76 torch_emitters=10\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
