#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace hpvr::quest::fixtures {
// Chndaler_128's candle-top center. Mesh bounds include transparent space
// above the wax, so a bound-derived flame would float above the candle.
inline constexpr std::array<float,2> kChandelierWickUv{93.0F/256.0F,160.0F/256.0F};

inline constexpr std::size_t kPlainCandleVertexCount=69;
inline constexpr float kCandleSupportBias=.002F;
inline constexpr std::array<std::size_t,8> kPlainCandleFlameFaces{0,2,3,4,5,6,7,10};
struct PlainCandleMeshPatch {
    std::array<float,3> old_tip{},wick{};
    std::array<std::array<std::array<float,3>,3>,23> original_triangles{};
};

// Identify only PlainCandle's eight masked flame faces, not its masked wax.
// UV topology plus the independent wax-base/vertical geometry checks fail
// closed for other fixtures and for already repaired prepared geometry.
template<class Vertex,class Position>
bool IdentifyPlainCandle(const Vertex* vertices,std::size_t count,Position position,
                        PlainCandleMeshPatch* output){
    if(!vertices||!output||count!=kPlainCandleVertexCount)return false;
    using Uv=std::array<unsigned,2>;
    constexpr std::array<std::array<Uv,3>,8> flame_uv{{
        {{{164,203},{134,203},{149,252}}},
        {{{164,203},{149,167},{134,203}}},
        {{{134,203},{149,251},{164,203}}},
        {{{164,203},{149,251},{134,203}}},
        {{{134,203},{164,203},{150,166}}},
        {{{134,203},{164,203},{149,252}}},
        {{{164,203},{134,203},{150,166}}},
        {{{134,203},{149,167},{164,203}}}}};
    const auto uv_matches=[&](std::size_t index,Uv expected){
        const auto& v=vertices[index];
        return (v.polygon_flags&2U)!=0&&std::isfinite(v.texture_uv[0])&&std::isfinite(v.texture_uv[1])&&
            std::abs(v.texture_uv[0]-float(expected[0])/255)<.00002F&&
            std::abs(v.texture_uv[1]-float(expected[1])/255)<.00002F;
    };
    for(std::size_t face=0;face<kPlainCandleFlameFaces.size();++face)
        for(std::size_t corner=0;corner<3;++corner)
            if(!uv_matches(kPlainCandleFlameFaces[face]*3+corner,flame_uv[face][corner]))return false;
    constexpr std::array<Uv,6> wax_uv{{{128,124},{252,124},{252,3},{252,3},{128,3},{128,124}}};
    for(std::size_t corner=0;corner<6;++corner)if(!uv_matches(33+corner,wax_uv[corner]))return false;
    PlainCandleMeshPatch patch;
    for(std::size_t i=0;i<count;++i){
        const auto p=position(vertices[i]);
        for(unsigned axis=0;axis<3;++axis){
            if(!std::isfinite(p[axis]))return false;
            patch.original_triangles[i/3][i%3][axis]=p[axis];
        }
    }
    patch.wick=patch.original_triangles[0][2];
    patch.old_tip=patch.original_triangles[2][1];
    const float height=patch.old_tip[1]-patch.wick[1];
    if(height<.015F||height>2.0F)return false;
    const float tolerance=height*.005F;
    if(std::abs(patch.old_tip[0]-patch.wick[0])>tolerance||
       std::abs(patch.old_tip[2]-patch.wick[2])>tolerance)return false;
    for(const auto face:kPlainCandleFlameFaces)for(std::size_t corner=0;corner<3;++corner){
        const auto& p=patch.original_triangles[face][corner];
        const float v=vertices[face*3+corner].texture_uv[1];
        const auto& expected=v>.98F?patch.wick:patch.old_tip;
        if(v>.98F||v<.66F){
            for(unsigned axis=0;axis<3;++axis)if(std::abs(p[axis]-expected[axis])>tolerance)return false;
        }else if(std::abs(p[1]-(patch.old_tip[1]+patch.wick[1])*.5F)>tolerance)return false;
    }
    const float bottom=patch.original_triangles[11][0][1];
    const float wax_height=(patch.wick[1]-bottom)/height;
    if(wax_height<2.80F||wax_height>2.90F)return false;
    for(std::size_t face=11;face<=12;++face)for(const auto& p:patch.original_triangles[face])
        if(std::abs(p[1]-bottom)>tolerance)return false;
    *output=patch;return true;
}

inline bool IsPlainCandleFlameFace(std::size_t face){
    return std::find(kPlainCandleFlameFaces.begin(),kPlainCandleFlameFaces.end(),face)!=kPlainCandleFlameFaces.end();
}

// Position returns a writable float[3] or array<float,3> reference.
template<class Vertex,class Position>
void ApplyPlainCandlePatch(Vertex* vertices,const PlainCandleMeshPatch& patch,Position position){
    for(std::size_t i=0;i<kPlainCandleVertexCount;++i){
        auto& p=position(vertices[i]);
        const auto& source=IsPlainCandleFlameFace(i/3)?patch.wick:patch.original_triangles[i/3][i%3];
        for(unsigned axis=0;axis<3;++axis)p[axis]=source[axis]+(axis==1?kCandleSupportBias:0);
    }
}

template<class Vertex>
std::vector<std::array<float,3>> ChandelierWickPositions(const std::vector<Vertex>& vertices){
    std::vector<std::array<float,3>> result;
    for(std::size_t index=0;index+2<vertices.size();index+=3){
        const auto& a=vertices[index];const auto& b=vertices[index+1];const auto& c=vertices[index+2];
        if((a.polygon_flags&2U)==0)continue;
        const float ux=b.texture_uv[0]-a.texture_uv[0],uy=b.texture_uv[1]-a.texture_uv[1];
        const float vx=c.texture_uv[0]-a.texture_uv[0],vy=c.texture_uv[1]-a.texture_uv[1];
        const float determinant=ux*vy-uy*vx;
        if(!std::isfinite(determinant)||std::abs(determinant)<1.0e-8F)continue;
        const float du=kChandelierWickUv[0]-a.texture_uv[0],dv=kChandelierWickUv[1]-a.texture_uv[1];
        const float wb=(du*vy-dv*vx)/determinant,wc=(ux*dv-uy*du)/determinant,wa=1.0F-wb-wc;
        if(!std::isfinite(wa)||!std::isfinite(wb)||!std::isfinite(wc)||std::min({wa,wb,wc})<-.0001F)continue;
        std::array<float,3> point{};
        for(unsigned axis=0;axis<3;++axis)point[axis]=a.position_m[axis]*wa+b.position_m[axis]*wb+c.position_m[axis]*wc;
        if(!std::all_of(point.begin(),point.end(),[](float value){return std::isfinite(value);}))continue;
        // Both sides of each masked candle panel contain the same wick.
        if(std::any_of(result.begin(),result.end(),[&](const auto& other){
            float squared=0;for(unsigned axis=0;axis<3;++axis){const float d=point[axis]-other[axis];squared+=d*d;}
            return squared<.0001F;
        }))continue;
        if(result.size()==24)return {}; // At most three authored eight-candle tiers.
        result.push_back(point);
    }
    return result;
}
} // namespace hpvr::quest::fixtures
