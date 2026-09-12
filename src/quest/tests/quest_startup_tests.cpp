#include "hpvr/quest_loading_indicator.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace hpvr::quest;
namespace {
unsigned checks=0;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
bool Near(float a,float b){return std::isfinite(a)&&std::abs(a-b)<.0001F;}

void TestAtlas(){
    const auto pixels=BuildLoadingIconAtlas();
    Check(pixels.size()==512*64*4,"single 128 KiB atlas");
    Check(pixels==BuildLoadingIconAtlas(),"deterministic frame artwork");
    std::array<std::uint64_t,kLoadingIconFrames> hashes{};
    std::array<unsigned,kLoadingIconFrames> top{},bottom{};
    for(unsigned frame=0;frame<kLoadingIconFrames;++frame){
        unsigned visible=0,partial=0;std::uint64_t hash=14695981039346656037ULL;
        for(unsigned y=0;y<kLoadingIconSize;++y)for(unsigned x=0;x<kLoadingIconSize;++x){
            const auto at=(y*kLoadingAtlasWidth+frame*kLoadingIconSize+x)*4;
            for(unsigned c=0;c<4;++c){hash^=pixels[at+c];hash*=1099511628211ULL;}
            const auto alpha=pixels[at+3];
            if(alpha){++visible;if(alpha<255)++partial;}
            if(x<2||y<2||x+2>=kLoadingIconSize||y+2>=kLoadingIconSize)
                Check(alpha==0,"transparent atlas gutters prevent edge bleed");
            if(alpha==0)Check(pixels[at]==0&&pixels[at+1]==0&&pixels[at+2]==0,"transparent background has no black quad");
            const bool sand=pixels[at]==255&&pixels[at+1]==202&&pixels[at+2]==92;
            if(sand&&x>18&&x<45&&y>14&&y<49){
                if(y<31)top[frame]+=alpha;else if(y>32)bottom[frame]+=alpha;
            }
        }
        Check(visible>350&&visible<2200,"recognizable bounded icon in every frame");
        Check(partial>30,"antialiased icon edges");hashes[frame]=hash;
        for(unsigned previous=0;previous<frame;++previous)Check(hash!=hashes[previous],"all eight animation frames differ");
    }
    Check(top[0]>top[5]+10000,"upper chamber visibly empties");
    Check(bottom[5]>bottom[0]+10000,"lower chamber visibly fills");
}

void TestTiming(){
    Check(LoadingIconFrame(-1)==0&&LoadingIconFrame(0)==0,"invalid startup time is safe");
    for(std::int64_t cycle=0;cycle<4;++cycle)for(unsigned frame=0;frame<kLoadingIconFrames;++frame){
        const auto start=(cycle*kLoadingIconFrames+frame)*kLoadingIconFrameNs;
        Check(LoadingIconFrame(start)==frame,"250 ms frame selection");
        Check(LoadingIconFrame(start+kLoadingIconFrameNs-1)==frame,"stable frame until next boundary");
    }
    Check(LoadingIconFrame(std::numeric_limits<std::int64_t>::max())<kLoadingIconFrames,"long sessions cannot overflow atlas");
}

void TestAnchor(){
    ViewPose theater{{2,1.6F,-2.5F},{0,0,0,1}},icon{};
    Check(BuildLoadingIconPose(theater,&icon),"icon from theater anchor");
    Check(Near(icon.position[0],3.17F)&&Near(icon.position[1],.77F)&&Near(icon.position[2],-2.492F),"inside lower right artwork corner");
    Check(icon.orientation==theater.orientation,"same facing as theater");
    ViewPose again{};Check(BuildLoadingIconPose(theater,&again)&&again.position==icon.position,"anchor has no live-head dependency");
    constexpr float q=.70710678118F;
    theater.orientation={0,q,0,q};
    Check(BuildLoadingIconPose(theater,&icon),"yawed loading theater");
    Check(Near(icon.position[0],2.008F)&&Near(icon.position[1],.77F)&&Near(icon.position[2],-3.67F),"offset rotates with theater instead of world axes");
    const auto saved=icon;theater.orientation={0,0,0,0};
    Check(!BuildLoadingIconPose(theater,&icon)&&icon.position==saved.position,"invalid pose preserves output");
    Check(!BuildLoadingIconPose(saved,nullptr),"null output is rejected");
}
}

int main(){
    try{TestAtlas();TestTiming();TestAnchor();std::cout<<"PASS startup indicator "<<checks<<" checks\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
