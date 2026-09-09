#include "../../../android/app/src/main/cpp/shaders/reflection_hit_policy.h"
#include <iostream>
#include <stdexcept>

using namespace hpvr::quest;
namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}

// Mirrors the bounded bracket search, executing the exact policy included by
// the production shader. The callback is analytic history depth, not an asset.
template<class Gap> bool TraceBracket(Gap sample,bool old_policy){
    float lo=0.0f,hi=1.0f,lo_gap=sample(lo),hi_gap=sample(hi);
    if(!(lo_gap<0.0f&&hi_gap>=0.0f))return false;
    for(int n=0;n<4;++n){const float mid=(lo+hi)*.5f,gap=sample(mid);
        if(gap>=0.0f){hi=mid;hi_gap=gap;}else{lo=mid;lo_gap=gap;}}
    return old_policy?hi_gap<=.23f:ReflectionHitBracketValid(lo_gap,hi_gap,(hi-lo)*.4f);
}
}

int main(){try{
    unsigned cases=0;
    // True geometric intersections converge from both sides, even with the
    // existing four-refinement budget. Preserve sloping walls and table legs.
    for(float crossing:{.05f,.2f,.5f,.75f,.94f})for(float slope:{.1f,.3f,.6f}){
        Check(TraceBracket([=](float t){return(t-crossing)*slope;},false),"true surface was rejected");++cases;
    }
    // Analytic bean silhouette: the ray changes from the distant background to
    // the foreground bean's depth, but never intersects either surface there.
    for(float jump:{.3f,1.0f,3.0f})for(float behind:{.01f,.05f,.15f,.22f}){
        const auto silhouette=[=](float t){return t<.48f?-jump:behind;};
        Check(TraceBracket(silhouette,true),"regression must reproduce old false hit");
        Check(!TraceBracket(silhouette,false),"silhouette extrusion was accepted");++cases;
    }
    Check(!ReflectionHitBracketValid(-.01f,.15f,.005f),"23 cm foreground thickness remains");
    Check(!ReflectionHitBracketValid(.01f,.01f,.005f),"invalid front bracket accepted");
    Check(!ReflectionHitBracketValid(-.01f,-.01f,.005f),"invalid behind bracket accepted");
    Check(ReflectionSameDepthLayer(5.0f,5.02f),"same bean color texel rejected");
    Check(!ReflectionSameDepthLayer(5.0f,8.0f),"half-res background color leaked into bean");
    Check(!ReflectionSameDepthLayer(5.0f,0.0f),"invalid color depth accepted");
    Check(ReflectionHitTolerance(100.0f)<=.09f,"thickness grew without bound");
    std::cout<<"C35_SSR_POLICY=PASS analytic_brackets="<<cases
             <<" silhouette_extrusion=REJECTED color_depth_alignment=YES trace_budget=16+4\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
