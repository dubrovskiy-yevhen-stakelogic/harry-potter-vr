#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <stdexcept>

namespace q=hpvr::quest;
namespace restore=hpvr::quest::grid_visual_restore;
using Point=std::array<float,3>;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Fixture {q::PreparedGeometry geometry;q::DoorDraw door;hpvr::wand::Hp1ActorVisual actor;hpvr::wand::Hp1TexturedBspScene model;};
Fixture Make(float angle){
    Fixture f;f.geometry.texture_layers=1;f.geometry.textures.assign(restore::kLayerBytes,15);
    f.geometry.vertices.resize(3);f.geometry.map_vertices=3;
    f.door.first_vertex=3;f.door.vertex_count=36;f.door.pivot={9,3,17};
    f.door.placement.player_yaw=angle;f.door.placement.base_rotation_units={1234,11100,578};
    f.model.status=hpvr::wand::Hp1ProfileStatus::ok;f.model.decoded_texture_count=3;
    f.model.texture_layer_count=4;f.model.texture_layer_width=2;f.model.texture_layer_height=2;
    f.model.texture_rgba8.resize(64);
    for(std::size_t i=0;i<64;++i)f.model.texture_rgba8[i]=static_cast<std::uint8_t>(30+i);
    hpvr::wand::Hp1ClassDefaultProperty pre;pre.name="PrePivot";pre.value.resize(12);
    const Point offset{3,4,5};std::memcpy(pre.value.data(),offset.data(),12);f.actor.serialized_properties.push_back(pre);
    const Point prepivot{.08F,.10F,-.06F};
    const std::array<std::array<Point,4>,6> faces{{
        {{{-1,-2,-1},{-1,2,-1},{1,2,-1},{1,-2,-1}}},
        {{{1,-2,-1},{1,2,-1},{1,2,1},{1,-2,1}}},
        {{{1,-2,1},{1,2,1},{-1,2,1},{-1,-2,1}}},
        {{{-1,-2,1},{-1,2,1},{-1,2,-1},{-1,-2,-1}}},
        {{{-1,2,-1},{-1,2,1},{1,2,1},{1,2,-1}}},
        {{{-1,-2,1},{-1,-2,-1},{1,-2,-1},{1,-2,1}}}
    }};
    const std::array<std::array<float,2>,4> uv{{{0,0},{0,1},{1,1},{1,0}}};
    for(unsigned face=0;face<6;++face)for(const unsigned index:{0U,1U,2U,0U,2U,3U}){
        hpvr::wand::Hp1TexturedBspVertex v;const auto& point=faces[face][index];
        v.position_m={point[0],point[1],point[2]};v.texture_uv={uv[index][0],uv[index][1]};
        v.texture_layer=face<4?1:face==4?2:3;f.model.vertices.push_back(v);
    }
    // Old BSP: opposite diagonal, reversed face order and unrelated attributes.
    for(unsigned face=6;face-->0;)for(const unsigned index:{1U,2U,3U,1U,3U,0U}){
        const auto point=q::AddVector(f.door.pivot,q::movers::RotateBrushLocal(q::SubtractVector(faces[face][index],prepivot),
            f.door.placement.base_rotation_units,angle));
        q::GpuVertex v;for(unsigned axis=0;axis<3;++axis)v.position[axis]=point[axis];
        v.texture_uv[0]=uv[index][0]*2;v.texture_uv[1]=uv[index][1]*2;v.texture_layer=0;
        v.polygon_flags=0x400000;v.has_lightmap=0;v.lightmap_uv[0]=.23F;v.lightmap_uv[1]=.57F;v.packed_light=0x765432;
        f.geometry.vertices.push_back(v);
    }
    return f;
}
void Apply(q::PreparedGeometry& geometry,const std::vector<std::vector<std::uint8_t>>& added,const std::vector<restore::Patch>& patches){
    for(const auto& tile:added)geometry.textures.insert(geometry.textures.end(),tile.begin(),tile.end());
    geometry.texture_layers+=static_cast<std::uint32_t>(added.size());
    for(const auto& patch:patches){auto& v=geometry.vertices[patch.index];v.texture_layer=patch.layer;
        v.texture_uv[0]=patch.uv[0];v.texture_uv[1]=patch.uv[1];}
}
void RotationAndPreservation(){
    for(unsigned turn=0;turn<24;++turn){auto f=Make(float(turn)*q::kTau/24);const auto original=f.geometry.vertices;
        std::vector<std::vector<std::uint8_t>> added;std::vector<restore::Patch> patches;
        Check(restore::StageModel(f.geometry,f.door,f.actor,f.model,added,patches),"valid rotated Polys rejected");
        Check(added.size()==3&&patches.size()==36,"bounded material/patch count");
        Check(std::memcmp(original.data(),f.geometry.vertices.data(),original.size()*sizeof(q::GpuVertex))==0,"staging mutated vertices");
        Apply(f.geometry,added,patches);Check(f.geometry.texture_layers==4,"new material count");
        Check(std::memcmp(original.data(),f.geometry.vertices.data(),3*sizeof(q::GpuVertex))==0,"BSP prefix changed");
        for(std::size_t i=3;i<original.size();++i){auto expected=original[i];const auto& actual=f.geometry.vertices[i];
            expected.texture_uv[0]=actual.texture_uv[0];expected.texture_uv[1]=actual.texture_uv[1];expected.texture_layer=actual.texture_layer;
            Check(std::memcmp(&expected,&actual,sizeof(actual))==0,"non-material vertex attribute changed");
            Check(actual.texture_uv[0]>=0&&actual.texture_uv[0]<=1&&actual.texture_uv[1]>=0&&actual.texture_uv[1]<=1,"repeated tall symbol");
            const auto reverse_face=(i-3)/6;Check(actual.texture_layer==(reverse_face==0?3U:reverse_face==1?2U:1U),"adjacent plane borrowed material");
        }
        const auto repaired=f.geometry.vertices;const auto texture_bytes=f.geometry.textures;
        added.clear();patches.clear();Check(restore::StageModel(f.geometry,f.door,f.actor,f.model,added,patches),"repeat rejected");
        Check(added.empty(),"repeat added duplicate materials");Apply(f.geometry,added,patches);
        Check(f.geometry.textures==texture_bytes&&std::memcmp(repaired.data(),f.geometry.vertices.data(),repaired.size()*sizeof(q::GpuVertex))==0,"repeat is not idempotent");
    }
}
void RepeatedAtlas(){
    auto f=Make(.7F);std::vector<std::vector<std::uint8_t>> expected_tiles;std::vector<restore::Patch> expected;
    Check(restore::StageModel(f.geometry,f.door,f.actor,f.model,expected_tiles,expected),"original atlas rejected");
    const auto original=f.model.texture_rgba8;
    f.model.texture_layer_width=4;f.model.texture_layer_height=8;f.model.texture_rgba8.resize(4*4*8*4);
    for(std::size_t layer=0;layer<4;++layer)for(std::size_t y=0;y<8;++y)for(std::size_t x=0;x<4;++x)
        std::copy_n(original.begin()+(layer*4+(y%2)*2+x%2)*4,4,f.model.texture_rgba8.begin()+((layer*8+y)*4+x)*4);
    for(auto& vertex:f.model.vertices){vertex.texture_uv[0]/=2;vertex.texture_uv[1]/=4;}
    std::vector<std::vector<std::uint8_t>> actual_tiles;std::vector<restore::Patch> actual;
    Check(restore::StageModel(f.geometry,f.door,f.actor,f.model,actual_tiles,actual),"repeating atlas rejected");
    Check(actual_tiles==expected_tiles&&actual.size()==expected.size(),"atlas dimensions duplicated textures");
    for(std::size_t i=0;i<actual.size();++i)Check(actual[i].index==expected[i].index&&actual[i].layer==expected[i].layer&&
        actual[i].uv==expected[i].uv,"atlas period changed the original material mapping");
}
void Rejection(){
    auto f=Make(.3F);const auto original=f.geometry.vertices;
    const auto fails=[&](){std::vector<std::vector<std::uint8_t>> added;std::vector<restore::Patch> patches;
        Check(!restore::StageModel(f.geometry,f.door,f.actor,f.model,added,patches),"unsafe model accepted");
        Check(std::memcmp(original.data(),f.geometry.vertices.data(),original.size()*sizeof(q::GpuVertex))==0,"failure changed geometry");};
    f.model.vertices[0].position_m.x+=.05F;fails();f=Make(.3F);
    f.model.vertices[3].texture_uv[0]=.2F;fails();f=Make(.3F);
    f.model.vertices[0].texture_layer=0;fails();f=Make(.3F);
    f.model.vertices[0].texture_uv[0]=std::numeric_limits<float>::quiet_NaN();fails();f=Make(.3F);
    f.model.vertices.pop_back();fails();f=Make(.3F);
    f.model.texture_rgba8.pop_back();fails();f=Make(.3F);
    f.door.first_vertex=0;fails();f=Make(.3F);
    f.door.first_vertex=std::numeric_limits<std::uint32_t>::max();fails();f=Make(.3F);
    f.door.placement.player_yaw=std::numeric_limits<float>::infinity();fails();f=Make(.3F);
    f.actor.serialized_properties.push_back(f.actor.serialized_properties.front());fails();
    auto empty=Make(0).geometry;std::vector<std::vector<std::uint8_t>> added;std::uint32_t layer=0;
    for(unsigned i=0;i<3;++i)Check(restore::StageLayer(empty,std::vector<std::uint8_t>(restore::kLayerBytes,static_cast<std::uint8_t>(i)),added,layer),"three tiles rejected");
    Check(!restore::StageLayer(empty,std::vector<std::uint8_t>(restore::kLayerBytes,9),added,layer),"unbounded new tiles");
    Check(!restore::StageLayer(empty,{},added,layer),"empty tile accepted");
    q::GridVisualRestoreStats stats{3,36,3};hpvr::wand::Hp1ActorVisualCensus census;
    Check(q::RestorePreparedGridVisuals(empty,"unopened","Lev_Tut1.unr",census,&stats)&&stats.movers==0,"map0 is not no-op");
}
int main(){try{RotationAndPreservation();RepeatedAtlas();Rejection();std::cout<<"PASS grid visual restoration\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
