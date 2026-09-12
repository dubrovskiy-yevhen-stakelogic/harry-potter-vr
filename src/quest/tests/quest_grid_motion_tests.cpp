#include "hpvr/quest_grid_motion.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace gm=hpvr::quest::grid_motion;
struct Triangle { std::array<gm::Vec,3> vertices;gm::Vec minimum{},maximum{},normal{}; };
Triangle Tri(gm::Vec a,gm::Vec b,gm::Vec c){
    Triangle t{{a,b,c},a,a,{}};
    for(auto p:t.vertices)for(unsigned i=0;i<3;++i){t.minimum[i]=std::min(t.minimum[i],p[i]);t.maximum[i]=std::max(t.maximum[i],p[i]);}
    t.normal=gm::Cross(gm::Sub(b,a),gm::Sub(c,a));t.normal=gm::Scale(t.normal,1/std::sqrt(gm::Dot(t.normal,t.normal)));return t;
}
void Floor(std::vector<Triangle>& t,float y,float left=-10,float right=10){
    t.push_back(Tri({left,y,-10},{right,y,-10},{right,y,10}));t.push_back(Tri({left,y,-10},{right,y,10},{left,y,10}));
}
void Wall(std::vector<Triangle>& t,float x){
    t.push_back(Tri({x,0,-5},{x,10,-5},{x,10,5}));t.push_back(Tri({x,0,-5},{x,10,5},{x,0,5}));
}
int main(){try{
    unsigned checks=0;const auto check=[&](bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);};
    constexpr auto none=std::numeric_limits<std::size_t>::max();
    check(!gm::RestoredCompletionPending(false,{},{}),"unmoved spawn does not manufacture a completion");
    check(gm::RestoredCompletionPending(false,{0,-1,3.84F},{0,-1,3.84F}),"mid-fall save owes completion after horizontal target is reached");
    check(gm::RestoredCompletionPending(false,{},{0,0,1.92F}),"unfinished horizontal push resumes completion");
    check(!gm::RestoredCompletionPending(true,{0,-1,3.84F},{0,-1,3.84F}),"already completed saved grid never repeats completion");
    std::vector<Triangle> world;Floor(world,0);Wall(world,-1);Wall(world,4);
    gm::Bounds box{{-1,0,-1},{1,4,1}};
    auto hit=gm::Sweep(world,box,{1,0,0},none,0);check(!hit.found,"flush rear wall and floor must not prevent leaving a niche");
    hit=gm::Sweep(world,box,{-1,0,0},none,0);check(hit.found&&hit.fraction==0,"cannot move into rear wall");
    hit=gm::Sweep(world,box,{6,0,0},none,0);check(hit.found&&std::abs(hit.fraction-.5F)<1e-5F,"full solid bounds stop at opposite wall");
    gm::State motion;
    auto step=gm::Advance(motion,box,{.02F,0,0},1.0F/72,world,world.size(),none,0);
    check(motion.grounded&&std::abs(step.offset[1])<1e-5F&&step.offset[0]>.019F,"floor supports a horizontal grid move");
    std::vector<Triangle> empty;
    step=gm::Advance(motion,box,{},1.0F/72,empty,0,none,0);check(step.offset==gm::Vec{},"idle static-supported block does not scan the world again");
    std::vector<Triangle> pit;Floor(pit,0,-10,0);Floor(pit,-5);box={{-1,0,-1},{0,4,1}};motion={};
    step=gm::Advance(motion,box,{},1.0F/72,pit,pit.size(),none,0);check(motion.grounded,"initial floor support");
    bool left=false,landed=false;
    for(unsigned i=0;i<240;++i){const gm::Vec horizontal=left?gm::Vec{}:gm::Vec{.05F,0,0};
        step=gm::Advance(motion,box,horizontal,1.0F/72,pit,pit.size(),none,0);left|=step.left_support;landed|=left&&step.landed;box=gm::Translate(box,step.offset);}
    check(left&&landed&&motion.grounded,"unsupported pillar falls and lands instead of hovering over abyss");
    check(std::abs(box.minimum[1]+5)<1e-4F,"fall reaches lower floor height");
    motion={};box={{1,8,-1},{3,12,1}};
    for(unsigned i=0;i<100;++i){step=gm::Advance(motion,box,{},.05F,pit,pit.size(),none,0);box=gm::Translate(box,step.offset);}
    check(motion.grounded&&std::abs(box.minimum[1]+5)<1e-4F,"old floating save re-evaluates support and falls safely");
    std::vector<Triangle> dynamic;Floor(dynamic,-20);const auto static_count=dynamic.size();Floor(dynamic,0);
    box={{-1,3,-1},{1,7,1}};motion={};
    for(unsigned i=0;i<90;++i){step=gm::Advance(motion,box,{},1.0F/72,dynamic,static_count,none,0);box=gm::Translate(box,step.offset);}
    check(motion.grounded&&motion.dynamic_support&&std::abs(box.minimum[1])<1e-4F,"other mover top supports falling block");
    step=gm::Advance(motion,box,{},1.0F/72,dynamic,static_count,static_count,2);check(!motion.grounded&&step.offset[1]<0,"excluded own brush cannot falsely support itself");
    std::vector<Triangle> needle{Tri({1.1F,.5F,.2F},{1.1F,.7F,.2F},{1.1F,.6F,.4F})};
    hit=gm::Sweep(needle,{{-1,0,-1},{1,4,1}},{1,0,0},none,0);check(hit.found&&std::abs(hit.fraction-.1F)<1e-5F,"thin geometry between old ray samples blocks the full box");
    std::vector<Triangle> seams;Floor(seams,0);
    seams.push_back(Tri({-2,0,1},{2,0,1},{2,.32F,1.32F}));
    seams.push_back(Tri({-2,0,1},{2,.32F,1.32F},{-2,.32F,1.32F}));
    hit=gm::Sweep(seams,{{-1,0,-1},{1,2,1.00008F}},{.04F,0,0},none,0);
    check(!hit.found,"submillimeter cooked-brush contact cannot lock tangent movement");
    std::vector<Triangle> only_wall;Wall(only_wall,1);box={{-1,3,-1},{1.00008F,7,1}};motion={};
    step=gm::Advance(motion,box,{},1.F/72,only_wall,only_wall.size(),none,0);
    check(!motion.grounded&&step.offset[1]<0,"overlapping vertical wall is not floor support");
    for(unsigned i=0;i<1000;++i){step=gm::Advance(motion,box,{},1.F/72,only_wall,only_wall.size(),none,0);box=gm::Translate(box,step.offset);}
    check(motion.retired&&!motion.grounded&&std::abs(box.minimum[1]+6)<1e-4F,"below-map block retires below authored bounds instead of falling forever");
    const auto retired_box=box;
    for(unsigned i=0;i<1000;++i){step=gm::Advance(motion,box,{1,0,0},1.F/72,only_wall,only_wall.size(),none,0);box=gm::Translate(box,step.offset);}
    check(box.minimum==retired_box.minimum&&box.maximum==retired_box.maximum,"retired block remains finite and does no later simulation");
    motion={};step=gm::Advance(motion,box,{},1.F/72,only_wall,only_wall.size(),none,0);
    check(motion.retired&&step.offset==gm::Vec{},"restored below-map block retires without teleporting back into play");
    std::cout<<"GRID_MOTION_TESTS=PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
