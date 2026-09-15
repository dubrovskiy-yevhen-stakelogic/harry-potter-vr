#include "hpvr/quest_charms_block.h"

#include <iostream>
#include <stdexcept>

namespace cb = hpvr::quest::charms_block;
namespace gm = hpvr::quest::grid_motion;
struct Triangle {
    std::array<cb::Vec, 3> vertices{};
    cb::Vec minimum{}, maximum{}, normal{};
};
Triangle Tri(cb::Vec a, cb::Vec b, cb::Vec c) {
    Triangle triangle{{a,b,c},a,a,{}};
    for (const auto& point : triangle.vertices)
        for (unsigned axis = 0; axis < 3; ++axis) {
            triangle.minimum[axis] = std::min(triangle.minimum[axis], point[axis]);
            triangle.maximum[axis] = std::max(triangle.maximum[axis], point[axis]);
        }
    const auto normal = gm::Cross(gm::Sub(b,a),gm::Sub(c,a));
    triangle.normal = gm::Scale(normal,1/std::sqrt(gm::Dot(normal,normal)));
    return triangle;
}
void Floor(std::vector<Triangle>& world, float y) {
    world.push_back(Tri({-10,y,-10},{10,y,-10},{10,y,10}));
    world.push_back(Tri({-10,y,-10},{10,y,10},{-10,y,10}));
}
void Wall(std::vector<Triangle>& world, float x) {
    world.push_back(Tri({x,-10,-10},{x,10,-10},{x,10,10}));
    world.push_back(Tri({x,-10,-10},{x,10,10},{x,-10,10}));
}
int main() { try {
    unsigned checks=0;
    const auto check=[&](bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);};
    constexpr auto none=std::numeric_limits<std::size_t>::max();
    std::vector<Triangle> world; Floor(world,0); Wall(world,-1); Wall(world,4);
    const auto static_count=world.size();
    cb::Bounds box{{-1,0,-1},{1,1.28F,1}};
    cb::AppendBox(world,box);
    check(world.size()==static_count+12,"collision adds one complete block with six solid faces");
    auto move=cb::Move(world,box,{6,0,0},static_count,12);
    check(std::abs(move[0]-3)<.0001F,"full block bounds stop before a wall, not after its center crosses");
    move=cb::Move(world,box,{1,0,.5F},static_count,12);
    check(move==cb::Vec{1,0,.5F},"own block and touching floor/rear wall do not lock free movement");
    move=cb::Move(world,box,{-1,0,.5F},static_count,12);
    check(std::abs(move[0])<.0001F&&std::abs(move[2]-.5F)<.0001F,"blocked x can still slide along z");
    cb::Motion motion;
    auto step=cb::Advance(motion,box,{0,2,0},true,.05F,15,world,static_count,static_count,12);
    check(step.offset[1]>.19F&&!step.released,"wand can lift out of its own collision range");
    const auto elevated=gm::Translate(box,step.offset);
    step=cb::Advance(motion,elevated,{},false,.05F,15,world,static_count,static_count,12);
    check(step.offset[1]<0,"release immediately enables gravity and ignores old self triangles");
    box=elevated;
    for(unsigned frame=0;frame<100;++frame){
        step=cb::Advance(motion,box,{},false,.05F,15,world,static_count,static_count,12);
        box=gm::Translate(box,step.offset);
    }
    check(motion.falling.grounded&&std::abs(box.minimum[1])<.0001F,"released block lands flush with the floor");
    std::vector<Triangle> empty;
    step=cb::Advance(motion,box,{},false,.05F,15,empty,0,none,0);
    check(step.offset==cb::Vec{},"idle static-grounded block performs no repeated world scan");
    motion={}; box={{-1,5,-1},{1,6.28F,1}};
    step=cb::Advance(motion,box,{},false,.05F,15,empty,0,none,0);
    check(step.offset[1]<0,"missing nearby floor cannot strand a block in mid-air");
    std::vector<Triangle> needle{Tri({1.1F,.3F,.2F},{1.1F,.5F,.2F},{1.1F,.4F,.4F})};
    move=cb::Move(needle,{{-1,0,-1},{1,1.28F,1}},{1,0,0},none,0);
    check(std::abs(move[0]-.1F)<.0001F,"thin wall fragments between old bean rays still block the full shape");
    motion={}; bool released=false;
    for(unsigned frame=0;frame<6;++frame){
        step=cb::Advance(motion,box,{0,5.64F,0},true,.05F,.25F,empty,0,none,0);
        if(step.released){released=true;break;}
    }
    check(released&&!motion.was_held&&step.offset[1]<0,"authored levitation timeout releases into gravity");
    box={{-1,0,-1},{1,1.28F,1}};
    check(cb::TouchesPlate(box,{.8F,.1F,0},.2F,.1F),"plate uses the complete horizontal and vertical block extent");
    check(!cb::TouchesPlate(box,{3,.1F,0},.2F,.1F),"nearby but disjoint plate remains untouched");
    check(!cb::TouchesPlate(box,{0,3,0},.2F,.1F),"flying over a low plate cannot trigger it");
    cb::Vec alignment;
    check(cb::SettleOnPlate(world,box,{.4F,.1F,0},static_count,12,&alignment)&&
          std::abs(alignment[0]-.4F)<.0001F&&std::abs(alignment[1])<.0001F,
          "plate centers x/z without burying the block below the support floor");
    check(!cb::SettleOnPlate(world,box,{-2,.1F,0},static_count,12,&alignment),
          "plate snapping never teleports the block through a wall");
    check(!cb::NeedsReset({0,-2,0},.02F)&&cb::NeedsReset({0,-2.01F,0},.02F),
          "reset uses the original strict 100-Unreal-unit drop threshold");
    check(!cb::NeedsReset({0,std::numeric_limits<float>::quiet_NaN(),0},.02F),"malformed pose is not a valid reset position");
    std::vector<Triangle> stacked;Floor(stacked,0);const auto floor_count=stacked.size();
    cb::AppendBox(stacked,{{-1,0,-1},{1,1.28F,1}});const auto upper_start=stacked.size();
    box={{-1,2,-1},{1,3.28F,1}};cb::AppendBox(stacked,box);motion={};
    for(unsigned frame=0;frame<100;++frame){
        step=cb::Advance(motion,box,{},false,.05F,15,stacked,floor_count,upper_start,12);
        box=gm::Translate(box,step.offset);
    }
    check(std::abs(box.minimum[1]-1.28F)<.0001F&&motion.falling.dynamic_support,
          "other blocks provide support while this block excludes only itself");
    for(const float yaw:{.3F,.85F,2.1F}){
        std::vector<Triangle> rotated;Floor(rotated,0);Wall(rotated,1.1F);
        for(auto& t:rotated)t=Tri(gm::Yaw(t.vertices[0],yaw),gm::Yaw(t.vertices[1],yaw),gm::Yaw(t.vertices[2],yaw));
        const cb::Bounds oriented{{-1,0,-1},{1,1.28F,1}};
        cb::Motion held;
        const auto lift=cb::Advance(held,oriented,{0,2,0},true,.05F,15,rotated,rotated.size(),none,0,yaw);
        check(std::abs(lift.offset[1]-.2F)<.001F,"rotated block can rise beside a ledge without an inflated world AABB");
        const auto stopped=cb::Move(rotated,oriented,{1,0,0},none,0,nullptr,yaw);
        check(std::abs(stopped[0]-.1F)<.001F,"oriented sweep retains the wall boundary");
        cb::Motion falling;auto falling_box=gm::Translate(oriented,{0,1,0});
        for(unsigned frame=0;frame<100;++frame){
            const auto drop=cb::Advance(falling,falling_box,{},false,.05F,15,rotated,rotated.size(),none,0,yaw);
            falling_box=gm::Translate(falling_box,drop.offset);
        }
        check(std::abs(falling_box.minimum[1])<.001F,"rotated released block still lands on its floor");
    }
    std::cout<<"CHARMS_BLOCK_TESTS=PASS checks="<<checks<<'\n';return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;} }
