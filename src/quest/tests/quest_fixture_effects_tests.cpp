#include "hpvr/quest_fixture_effects.h"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
struct Vertex{float position_m[3],texture_uv[2];std::uint32_t polygon_flags=2;};
void Check(bool value){if(!value)throw std::runtime_error("Fixture effect check failed");}
namespace fixtures=hpvr::quest::fixtures;
struct GpuVertex{
    float position[3]{},texture_uv[2]{};
    std::uint32_t polygon_flags=2,texture_layer=3,has_lightmap=0;
};
struct CollisionTriangle{
    std::array<std::array<float,3>,3> vertices{};
    std::array<float,3> minimum{},maximum{},normal{};
};
struct ChallengeProp{std::uint32_t first=0,count=0;std::array<float,3> minimum{},maximum{};};
struct FlameEmitter{std::array<float,3> position{};float phase=0,scale=.32F;};
struct PreparedGeometry{
    std::vector<GpuVertex> vertices;
    std::uint32_t map_vertices=0,fixture_vertices=0;
    std::vector<CollisionTriangle> collision,prop_aim;
    std::vector<ChallengeProp> challenge_props;
    std::vector<FlameEmitter> flames;
};
#include "../../../android/app/src/main/cpp/quest_candle_restore.inl"

std::vector<GpuVertex> TestCandle(float scale=1,float yaw=0){
    std::vector<GpuVertex> mesh(69);
    for(std::size_t i=0;i<mesh.size();++i){
        mesh[i].position[0]=.01F*float(i%3);mesh[i].position[1]=.05F*float(i%5);mesh[i].position[2]=.03F;
        mesh[i].texture_uv[0]=.1F;mesh[i].texture_uv[1]=.2F;
    }
    const std::array<float,3> wick{0,.57F,0},tip{0,.77F,0};
    const std::array<float,3> lf{-.034F,.67F,.03F},lb{-.034F,.67F,-.03F},rf{.034F,.67F,.03F},rb{.034F,.67F,-.03F};
    const auto face=[&](std::size_t index,std::array<std::array<float,3>,3> p,std::array<std::array<unsigned,2>,3> uv){
        for(unsigned k=0;k<3;++k){
            for(unsigned axis=0;axis<3;++axis)mesh[index*3+k].position[axis]=p[k][axis];
            for(unsigned axis=0;axis<2;++axis)mesh[index*3+k].texture_uv[axis]=float(uv[k][axis])/255;
        }
    };
    face(0,{lf,lb,wick},{{{164,203},{134,203},{149,252}}});
    face(2,{rf,tip,lf},{{{164,203},{149,167},{134,203}}});
    face(3,{lf,wick,rf},{{{134,203},{149,251},{164,203}}});
    face(4,{rb,wick,lb},{{{164,203},{149,251},{134,203}}});
    face(5,{lb,lf,tip},{{{134,203},{164,203},{150,166}}});
    face(6,{rb,rf,wick},{{{134,203},{164,203},{149,252}}});
    face(7,{rf,rb,tip},{{{164,203},{134,203},{150,166}}});
    face(10,{lb,tip,rb},{{{134,203},{149,167},{164,203}}});
    face(11,{{{-.2F,0,-.2F},{-.2F,0,.2F},{.2F,0,.2F}}},{{{128,124},{252,124},{252,3}}});
    face(12,{{{.2F,0,.2F},{.2F,0,-.2F},{-.2F,0,-.2F}}},{{{252,3},{128,3},{128,124}}});
    for(auto& v:mesh){
        const float x=v.position[0]*scale,z=v.position[2]*scale;
        v.position[0]=x*std::cos(yaw)-z*std::sin(yaw)+17;
        v.position[1]=v.position[1]*scale-3;
        v.position[2]=x*std::sin(yaw)+z*std::cos(yaw)-9;
    }
    return mesh;
}
void TestCandleRestoration(){
    const auto position=[](const GpuVertex& v)->const float(&)[3]{return v.position;};
    const auto writable=[](GpuVertex& v)->float(&)[3]{return v.position;};
    for(const float scale:{.3F,1.0F,2.0F})for(const float yaw:{-2.1F,0.0F,1.4F}){
        auto mesh=TestCandle(scale,yaw);const auto original=mesh;
        fixtures::PlainCandleMeshPatch patch;
        Check(fixtures::IdentifyPlainCandle(mesh.data(),mesh.size(),position,&patch));
        Check(std::abs(patch.wick[1]-(.57F*scale-3))<1e-6F);
        fixtures::ApplyPlainCandlePatch(mesh.data(),patch,writable);
        for(std::size_t i=0;i<mesh.size();++i)for(unsigned axis=0;axis<3;++axis){
            const float expected=(fixtures::IsPlainCandleFlameFace(i/3)?patch.wick[axis]:original[i].position[axis])+
                (axis==1?fixtures::kCandleSupportBias:0);
            Check(mesh[i].position[axis]==expected);
            if(axis<2)Check(mesh[i].texture_uv[axis]==original[i].texture_uv[axis]);
        }
        Check(!fixtures::IdentifyPlainCandle(mesh.data(),mesh.size(),position,&patch));
    }
    const auto valid=TestCandle();fixtures::PlainCandleMeshPatch patch;
    Check(!fixtures::IdentifyPlainCandle(valid.data(),valid.size()-1,position,&patch));
    Check(!fixtures::IdentifyPlainCandle(valid.data(),valid.size(),position,nullptr));
    auto bad=valid;bad[7].texture_uv[0]+=.01F;
    Check(!fixtures::IdentifyPlainCandle(bad.data(),bad.size(),position,&patch));
    bad=valid;bad[35].texture_uv[1]=.8F;
    Check(!fixtures::IdentifyPlainCandle(bad.data(),bad.size(),position,&patch));
    bad=valid;bad[0].polygon_flags=0;
    Check(!fixtures::IdentifyPlainCandle(bad.data(),bad.size(),position,&patch));
    bad=valid;bad[40].position[1]=std::numeric_limits<float>::quiet_NaN();
    Check(!fixtures::IdentifyPlainCandle(bad.data(),bad.size(),position,&patch));
    bad=valid;bad[7].position[0]+=.1F;
    Check(!fixtures::IdentifyPlainCandle(bad.data(),bad.size(),position,&patch));

    PreparedGeometry geometry;geometry.map_vertices=6;geometry.fixture_vertices=72;
    geometry.vertices.resize(6);geometry.vertices.insert(geometry.vertices.end(),valid.begin(),valid.end());
    geometry.vertices.resize(78);geometry.challenge_props.push_back({6,69});
    Check(fixtures::IdentifyPlainCandle(valid.data(),valid.size(),position,&patch));
    geometry.flames.push_back({patch.old_tip,1,.32F});
    geometry.flames.push_back({patch.old_tip,2,2.4F});
    auto nearby=patch.old_tip;nearby[0]+=.1F;geometry.flames.push_back({nearby,3,.32F});
    for(const auto& face:patch.original_triangles){
        CollisionTriangle t;t.vertices=face;t.minimum=t.maximum=face[0];t.normal={0,1,0};
        for(const auto& p:face)for(unsigned axis=0;axis<3;++axis){t.minimum[axis]=std::min(t.minimum[axis],p[axis]);t.maximum[axis]=std::max(t.maximum[axis],p[axis]);}
        geometry.collision.push_back(t);
    }
    geometry.collision.push_back({{{{100,100,100},{101,100,100},{100,100,101}}}});
    geometry.prop_aim=geometry.collision;const auto unrelated=geometry.collision.back();
    const auto result=RestorePreparedCandleFixtures(geometry);
    Check(result.fixtures==1&&result.removed_faces==8&&result.wick_anchors==1&&result.collision_faces==46);
    Check(geometry.vertices.size()==78&&geometry.map_vertices==6&&geometry.fixture_vertices==72);
    Check(geometry.challenge_props[0].first==6&&geometry.challenge_props[0].count==69);
    Check(geometry.challenge_props[0].maximum[1]<patch.old_tip[1]);
    Check(geometry.flames[0].position[1]==patch.wick[1]+fixtures::kCandleSupportBias);
    Check(geometry.flames[1].position==patch.old_tip&&geometry.flames[2].position==nearby);
    Check(geometry.collision.size()==16&&geometry.prop_aim.size()==16);
    Check(geometry.collision.back().vertices==unrelated.vertices);
    std::size_t remaining=0;
    for(std::size_t face=0;face<23;++face){
        if(fixtures::IsPlainCandleFlameFace(face))continue;
        const auto& t=geometry.collision[remaining++];
        Check(t.vertices[0][1]==patch.original_triangles[face][0][1]+fixtures::kCandleSupportBias);
        Check(t.normal==std::array<float,3>{0,1,0});
    }
    Check(RestorePreparedCandleFixtures(geometry).fixtures==0); // No cumulative ground bias.
    geometry.vertices=valid;geometry.map_vertices=0;geometry.fixture_vertices=69;
    geometry.challenge_props.clear();geometry.collision.clear();geometry.prop_aim.clear();
    Check(RestorePreparedCandleFixtures(geometry).fixtures==1); // Map0 has no prop records.
    geometry.vertices=valid;geometry.vertices[1].texture_layer=4;
    Check(RestorePreparedCandleFixtures(geometry).fixtures==0);
    geometry.vertices=valid;geometry.vertices[1].has_lightmap=1;
    Check(RestorePreparedCandleFixtures(geometry).fixtures==0);
    geometry.vertices=valid;geometry.fixture_vertices=999;
    Check(RestorePreparedCandleFixtures(geometry).fixtures==0);
}
int main(){
    TestCandleRestoration();
    using hpvr::quest::fixtures::ChandelierWickPositions;
    const std::vector<Vertex> panel{
        {{-1,1,2},{0,0}},{{-1,-1,2},{0,1}},{{1,-1,2},{1,1}},
        {{-1,1,2},{0,0}},{{1,-1,2},{1,1}},{{1,1,2},{1,0}}};
    auto points=ChandelierWickPositions(panel);Check(points.size()==1);
    Check(std::abs(points[0][0]-(2*93.0F/256-1))<1e-6F);
    Check(std::abs(points[0][1]-(1-2*160.0F/256))<1e-6F);
    Check(points[0][2]==2);
    auto doubled=panel;doubled.insert(doubled.end(),panel.rbegin(),panel.rend());
    Check(ChandelierWickPositions(doubled).size()==1);
    auto tiered=panel;for(auto v:panel){v.position_m[1]+=2;tiered.push_back(v);}
    Check(ChandelierWickPositions(tiered).size()==2);
    auto degenerate=panel;for(auto& v:degenerate){v.texture_uv[0]=0;v.texture_uv[1]=0;}
    Check(ChandelierWickPositions(degenerate).empty());
    auto opaque=panel;for(auto& v:opaque)v.polygon_flags=0;
    Check(ChandelierWickPositions(opaque).empty());
    auto invalid=panel;for(auto& v:invalid)v.position_m[0]=std::numeric_limits<float>::quiet_NaN();
    Check(ChandelierWickPositions(invalid).empty());
    auto support=panel;for(auto& v:support)v.texture_uv[1]*=.5F;
    Check(ChandelierWickPositions(support).empty());
    std::vector<Vertex> many;
    for(unsigned tier=0;tier<24;++tier)for(auto v:panel){v.position_m[1]+=tier*2;many.push_back(v);}
    Check(ChandelierWickPositions(many).size()==24);
    for(auto v:panel){v.position_m[1]+=48;many.push_back(v);}
    Check(ChandelierWickPositions(many).empty());
    std::cout<<"PASS: chandelier wick placement; strict candle flame removal, wax bias, anchors, collision, ranges and repeat safety\n";
}
