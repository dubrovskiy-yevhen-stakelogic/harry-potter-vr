#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"

#include <chrono>
#include <fstream>
#include <iostream>

namespace {
using namespace hpvr::quest;
unsigned checks=0;
void Check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}

struct FixtureDirectory {
    std::filesystem::path path;
    FixtureDirectory(){
        const auto stamp=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for(unsigned i=0;i<100;++i){auto candidate=std::filesystem::temp_directory_path()/
            ("hpvr-prepared-codec-"+stamp+"-"+std::to_string(i));
            if(std::filesystem::create_directory(candidate)){path=std::move(candidate);return;}}
        throw std::runtime_error("cannot create codec test directory");
    }
    ~FixtureDirectory(){if(!path.empty()){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
};

PreparedGeometry Fixture(){
    PreparedGeometry g;
    g.texture_layers=1;g.lightmap_width=1;g.lightmap_height=1;
    g.map_vertices=3;g.fixture_vertices=9;g.fixture_actors=2;
    g.character_frame_vertices=3;g.animation_frames=kCharacterAnimationFrameCount;
    g.decoded_lightmaps=1;g.decoded_textures=1;
    g.textures.resize(256*256*4,127);g.lightmaps={1,2,3,255};
    const std::array<GpuVertex,3> triangle{{
        {{0,0,0},{-4,3},{0,0},0,0,1,0x123456},
        {{1,0,0},{2,-1},{1,0},0,0,1,0xabcdef},
        {{0,0,1},{7,9},{0,1},0,0,1,0xffffff}}};
    for(unsigned i=0;i<10;++i)g.vertices.insert(g.vertices.end(),triangle.begin(),triangle.end());
    g.collision=BuildCollisionTriangles(g.vertices,3);g.prop_aim=g.collision;
    DoorDraw d;d.actor_reference=11;d.first_vertex=12;d.vertex_count=3;
    d.placement.pivot_scene={2,3,4};d.placement.base_rotation_units={1024,2048,3072};
    d.placement.player_yaw=.4F;d.motion.count=3;d.motion.keys[1].offset_unreal={64,20,8};
    d.motion.keys[2].rotation_units={0,65536,0};movers::Settle(d.motion,1);
    d.motion.chain=false;d.motion.direction=-1;d.challenge=true;d.collision_only=true;
    d.open_seconds=.7F;d.close_seconds=1.8F;d.initial_state="TriggerOpenTimed";
    d.grid=true;d.stay_open=2.3F;d.grid_increment=1.4F;d.grid_offset={.1F,.2F,.3F};
    d.grid_target={.3F,.1F,.2F};d.pivot={2,4,6};d.open_offset={0,2,0};
    d.open_yaw=.2F;d.duration=1.4F;d.tag="test-door";g.doors.push_back(d);
    CharacterDraw actor;actor.actor_reference=21;actor.vertex_count=3;actor.first_vertex=15;
    actor.player=true;actor.clips.emplace("breathe",CharacterClip{15,1,2});
    actor.clips.emplace("walk",CharacterClip{21,2,2});actor.object_name="test-actor";actor.class_name="test.character";
    actor.base_origin={1,2,3};actor.collision_center={1,2.5F,3};actor.base_yaw=.25F;
    actor.yaw=.25F;actor.desired_yaw=.25F;actor.visual_minimum={0,0,0};actor.visual_maximum={1,2,1};
    actor.collision_min_y=2;actor.collision_max_y=4;actor.staged=true;g.characters.push_back(actor);
    BeanDraw bean{41,27,3,{2,1,3},3,1,2,.25F,31,{2,3,3},1};g.beans.push_back(bean);
    g.knights.push_back({{1,0,2},.4F,.9F,1.4F,0,6,3,2,-1});
    g.challenge_props.push_back({31,"test-pot",3,3,{0,0,0},{1,1,1},true,true});
    g.flames.push_back({{1,2,3},.75F,.6F});g.glows.push_back({{2,3,4},.4F,.8F,.25F});
    g.targets.push_back({21,{0,0,0},{2,4,2},true});
    return g;
}

void Header(cache::Writer& w,const PreparedGeometry& g){
    w.U32(prepared_codec::kSchema);
    w.U32(g.texture_width);w.U32(g.texture_height);w.U32(g.texture_layers);
    w.U32(g.lightmap_width);w.U32(g.lightmap_height);
    w.U32(g.map_vertices);w.U32(g.fixture_vertices);w.U32(g.fixture_actors);
    w.U32(g.character_frame_vertices);w.U32(g.animation_frames);
    w.U32(g.decoded_lightmaps);w.U32(g.decoded_textures);w.U32(g.fallback_materials);
}
// Generate checksummed hostile fixtures without using the production writer's
// validation gate, so tests exercise decoding and validation independently.
void UnsafePayload(cache::Writer& w,const PreparedGeometry& g){
    using namespace prepared_codec;
    Header(w,g);RawArray(w,g.vertices);w.Bytes(g.textures);w.Bytes(g.lightmaps);
    RawArray(w,g.collision);RawArray(w,g.prop_aim);
    Array(w,g.doors,[](auto& a,const auto& b){Door(a,b);});
    Array(w,g.characters,[](auto& a,const auto& b){Character(a,b);});
    Array(w,g.beans,[](auto& a,const auto& b){Bean(a,b);});
    Array(w,g.knights,[](auto& a,const auto& b){Knight(a,b);});
    Array(w,g.challenge_props,[](auto& a,const auto& b){Prop(a,b);});
    Array(w,g.flames,[](auto& a,const auto& b){Flame(a,b);});
    Array(w,g.glows,[](auto& a,const auto& b){Glow(a,b);});
    Array(w,g.targets,[](auto& a,const auto& b){Target(a,b);});
}
bool Load(const std::filesystem::path& path,PreparedGeometry& destination,std::uint64_t frontend_reserve=0){
    try{cache::Reader reader(path,1,0x123456789abcULL);
        auto candidate=ReadPreparedGeometry(reader,frontend_reserve);reader.Finish();
        if(!ValidatePreparedGeometry(candidate))return false;
        destination=std::move(candidate);return true;
    }catch(const cache::CacheError&){return false;}
}
std::vector<char> FileBytes(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};
}
template<class F> void Reject(const std::filesystem::path& directory,const char* name,F modify){
    auto broken=Fixture();modify(broken);
    Check(!ValidatePreparedGeometry(broken),name);
    const auto path=directory/name;
    {cache::Writer writer(path,1,0x123456789abcULL);UnsafePayload(writer,broken);writer.Finish();}
    auto live=Fixture();live.characters.front().object_name="unchanged-live-scene";
    Check(!Load(path,live),"hostile candidate rejected");
    Check(live.characters.front().object_name=="unchanged-live-scene","failed load preserves live scene");
}
}

int main(){
    try{
        FixtureDirectory directory;
        const auto source=Fixture();Check(ValidatePreparedGeometry(source),"synthetic complete scene validates");
        const auto original=directory.path/"original.cache",again=directory.path/"again.cache";
        {cache::Writer writer(original,1,0x123456789abcULL);WritePreparedGeometry(writer,source);writer.Finish();}
        PreparedGeometry decoded;Check(Load(original,decoded),"valid candidate loads");
        {cache::Writer writer(again,1,0x123456789abcULL);WritePreparedGeometry(writer,decoded);writer.Finish();}
        Check(FileBytes(original)==FileBytes(again),"byte-identical complete typed roundtrip");
        Check(decoded.doors[0].motion.keys[2].rotation_units[1]==65536&&
            decoded.doors[0].motion.current==1&&!decoded.doors[0].motion.chain&&
            decoded.doors[0].motion.direction==-1,"authored mover pose and behavior preserved");
        Check(decoded.characters[0].base_origin==source.characters[0].base_origin&&
            decoded.characters[0].clips.size()==2&&decoded.characters[0].staged,"character staging and clips preserved");
        Check(decoded.beans[0].source_actor==31&&decoded.beans[0].kind==3&&
            decoded.beans[0].emission==source.beans[0].emission,"pickup reward origin and kind preserved");
        Check(decoded.prop_aim.size()==1&&decoded.knights[0].frames==2&&
            decoded.targets[0].actor_reference==21,"all auxiliary draw and collision data preserved");

        for(const std::uint64_t reserve:std::array<std::uint64_t,3>{32,prepared_codec::kMaxFrontendVertexReserve,prepared_codec::kMaxRuntimeVertexReserve}){
            PreparedGeometry reserved;
            Check(Load(original,reserved,reserve),"reserved candidate loads and validates");
            Check(reserved.vertices.size()==source.vertices.size()&&
                reserved.textures.size()==source.textures.size(),"reserve does not change cooked element counts");
            Check(reserved.vertices.capacity()>=reserved.vertices.size()+reserve,
                "frontend vertices reserved before payload read");
            Check(reserved.textures.capacity()>=prepared_codec::kTextureCapacityBytes,
                "all supported texture layers reserved before payload read");
            const auto reserved_path=directory.path/("reserved-"+std::to_string(reserve));
            {cache::Writer writer(reserved_path,1,0x123456789abcULL);
                WritePreparedGeometry(writer,reserved);writer.Finish();}
            Check(FileBytes(original)==FileBytes(reserved_path),"reserved typed roundtrip has identical bytes");
            const auto* vertex_data=reserved.vertices.data();
            const auto* texture_data=reserved.textures.data();
            reserved.vertices.resize(reserved.vertices.size()+static_cast<std::size_t>(reserve));
            reserved.textures.resize(reserved.textures.size()+256*256*4);
            Check(reserved.vertices.data()==vertex_data&&reserved.textures.data()==texture_data,
                "frontend appends preserve large vertex and texture allocations");
        }
        for(const std::uint64_t reserve:std::array<std::uint64_t,2>{prepared_codec::kMaxRuntimeVertexReserve+1,
            std::numeric_limits<std::uint64_t>::max()}){
            cache::Reader reader(original,1,0x123456789abcULL);
            const auto remaining=reader.Remaining();bool rejected_reserve=false;
            try{(void)ReadPreparedGeometry(reader,reserve);}
            catch(const cache::CacheError&){rejected_reserve=true;}
            Check(rejected_reserve&&reader.Remaining()==remaining,
                "oversized frontend reserve rejected before reading or allocation");
        }

        Reject(directory.path,"nan-position",[](auto& g){g.vertices[0].position[0]=std::numeric_limits<float>::quiet_NaN();});
        Reject(directory.path,"infinite-uv",[](auto& g){g.vertices[0].texture_uv[1]=std::numeric_limits<float>::infinity();});
        Reject(directory.path,"invalid-layer",[](auto& g){g.vertices[0].texture_layer=1;});
        Reject(directory.path,"invalid-lightmap-flag",[](auto& g){g.vertices[0].has_lightmap=2;});
        Reject(directory.path,"short-texture",[](auto& g){g.textures.pop_back();});
        Reject(directory.path,"short-lightmap",[](auto& g){g.lightmaps.pop_back();});
        Reject(directory.path,"triangle-bounds",[](auto& g){g.collision[0].maximum[0]=3;});
        Reject(directory.path,"triangle-normal",[](auto& g){g.collision[0].normal[1]*=-1;});
        Reject(directory.path,"door-range",[](auto& g){g.doors[0].first_vertex=0xffffffffU;});
        Reject(directory.path,"door-motion-index",[](auto& g){g.doors[0].motion.current=16;});
        Reject(directory.path,"actor-missing-clip",[](auto& g){g.characters[0].active_clip="missing";});
        Reject(directory.path,"actor-frame-overflow",[](auto& g){g.characters[0].clips.at("walk").frame_count=0xffffffffU;});
        Reject(directory.path,"actor-overlap",[](auto& g){g.characters[0].clips.at("walk").first_vertex=15;});
        Reject(directory.path,"bean-kind",[](auto& g){g.beans[0].kind=5;});
        Reject(directory.path,"bean-frame-range",[](auto& g){g.beans[0].frames=4097;});
        Reject(directory.path,"knight-layer",[](auto& g){g.knights[0].layer=1;});
        Reject(directory.path,"prop-range",[](auto& g){g.challenge_props[0].count=300;});
        Reject(directory.path,"wrong-target",[](auto& g){g.targets[0].actor_reference=22;});
        Reject(directory.path,"unexpected-tail",[](auto& g){g.vertices.insert(g.vertices.end(),3,g.vertices[0]);});
        Reject(directory.path,"empty-characters",[](auto& g){g.characters.clear();});

        for(unsigned test=0;test<3;++test){const auto path=directory.path/("allocation-guard-"+std::to_string(test));
            {cache::Writer writer(path,1,0x123456789abcULL);
                auto g=Fixture();if(test==2)g.texture_layers=257;Header(writer,g);
                writer.U64(test==0?prepared_codec::kMaxVertices+1:prepared_codec::kMaxVertices);writer.Finish();}
            PreparedGeometry destination;Check(!Load(path,destination),"count/dimension guard before allocation");}
        const auto duplicate=directory.path/"duplicate-clips";
        {cache::Writer writer(duplicate,1,0x123456789abcULL);
            for(unsigned i=0;i<4;++i)writer.Bool(false);writer.U32(2);
            for(unsigned i=0;i<2;++i){writer.String("breathe");writer.U32(0);writer.F32(1);writer.U32(2);}
            writer.Finish();}
        bool rejected=false;
        try{cache::Reader reader(duplicate,1,0x123456789abcULL);(void)prepared_codec::Character(reader);}
        catch(const cache::CacheError&){rejected=true;}
        Check(rejected,"duplicate animation names rejected without replacement");
        auto invalid=source;invalid.vertices[0].texture_layer=7;
        rejected=false;try{cache::Writer writer(directory.path/"invalid-writer",1,0x123456789abcULL);
            WritePreparedGeometry(writer,invalid);writer.Finish();}catch(const cache::CacheError&){rejected=true;}
        Check(rejected&&!std::filesystem::exists(directory.path/"invalid-writer"),"invalid writer cannot publish cache");
        std::cout<<"PREPARED_CODEC=PASS checks="<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"PREPARED_CODEC=FAIL "<<error.what()<<'\n';return 1;}
}
