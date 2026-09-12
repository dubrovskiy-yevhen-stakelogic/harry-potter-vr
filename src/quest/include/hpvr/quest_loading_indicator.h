#pragma once

#include "hpvr/quest_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hpvr::quest {

constexpr unsigned kLoadingIconSize=64;
constexpr unsigned kLoadingIconFrames=8;
constexpr unsigned kLoadingAtlasWidth=kLoadingIconSize*kLoadingIconFrames;
constexpr std::int64_t kLoadingIconFrameNs=250000000;

// The compositor selects an atlas cell. Loading frames require no texture
// updates, command buffers, allocations, or elapsed-time percentage guesses.
inline unsigned LoadingIconFrame(std::int64_t display_time_ns){
    if(display_time_ns<=0)return 0;
    return static_cast<unsigned>((display_time_ns/kLoadingIconFrameNs)%kLoadingIconFrames);
}

inline bool BuildLoadingIconPose(const ViewPose& theater,ViewPose* output){
    Matrix4 transform{};
    if(!output||!BuildRigidTransform(theater,&transform))return false;
    ViewPose pose=theater;
    // Bottom-right corner of the existing 2.8 x 2.1 metre theater artwork.
    // This derives only from its retained world anchor, never the live head.
    constexpr std::array<float,3> offset{1.17F,-.83F,.008F};
    for(unsigned i=0;i<3;++i)
        pose.position[i]+=transform[i]*offset[0]+transform[4+i]*offset[1]+transform[8+i]*offset[2];
    *output=pose;return true;
}

inline std::vector<std::uint8_t> BuildLoadingIconAtlas(){
    std::vector<std::uint8_t> pixels(kLoadingAtlasWidth*kLoadingIconSize*4,0);
    const auto distance=[](float x,float y,float ax,float ay,float bx,float by){
        const float dx=bx-ax,dy=by-ay;
        const float t=std::clamp(((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy),0.0F,1.0F);
        return std::hypot(x-ax-t*dx,y-ay-t*dy);
    };
    for(unsigned frame=0;frame<kLoadingIconFrames;++frame){
        const float progress=static_cast<float>(std::min(frame,5U))/5.0F;
        const float angle=frame<6?0.0F:static_cast<float>(frame-5)*1.0471975512F;
        const float cs=std::cos(angle),sn=std::sin(angle);
        const float top=-3.0F-15.0F*std::sqrt(1.0F-progress);
        const float bottom=18.0F-15.0F*std::sqrt(progress);
        for(unsigned py=0;py<kLoadingIconSize;++py)for(unsigned px=0;px<kLoadingIconSize;++px){
            // Four subpixel samples keep the small graphic smooth in VR.
            unsigned covered=0;std::array<unsigned,3> rgb{};
            for(unsigned sample=0;sample<4;++sample){
                const float sx=static_cast<float>(px)-31.5F+(sample%2?.25F:-.25F);
                const float sy=static_cast<float>(py)-31.5F+(sample/2?.25F:-.25F);
                const float x=sx*cs+sy*sn,y=-sx*sn+sy*cs;
                const float ax=std::abs(x);
                const bool sand=(y>=top&&y<=-3&&ax<=(-y-2)*.78F)||
                    (y>=bottom&&y<=18&&ax<=std::min((y-2)*.78F,(y-bottom)*.92F))||
                    (frame>0&&frame<5&&ax<.85F&&y>-3&&y<bottom&&
                     (static_cast<int>(y+32)+static_cast<int>(frame))%5<3);
                const float line=std::min({distance(ax,y,14,-18,14,-14),distance(ax,y,14,-14,2,-2),
                    distance(ax,y,2,-2,2,2),distance(ax,y,2,2,14,14),distance(ax,y,14,14,14,18)});
                const bool rim=ax<=18&&((y>=-24&&y<=-20)||(y>=20&&y<=24));
                if(rim||line<=1.05F||sand){
                    const std::array<unsigned,3> color=(rim||sand)?std::array<unsigned,3>{255,202,92}:
                        std::array<unsigned,3>{255,248,222};
                    for(unsigned c=0;c<3;++c)rgb[c]+=color[c];
                    ++covered;
                }
            }
            if(covered){
                const auto at=(py*kLoadingAtlasWidth+frame*kLoadingIconSize+px)*4;
                for(unsigned c=0;c<3;++c)pixels[at+c]=static_cast<std::uint8_t>(rgb[c]/covered);
                pixels[at+3]=static_cast<std::uint8_t>(covered*255/4);
            }
        }
    }
    return pixels;
}

} // namespace hpvr::quest
