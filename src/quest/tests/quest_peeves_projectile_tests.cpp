#include "hpvr/quest_peeves_projectile.h"
#include "hpvr/quest_firecracker.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace gm=hpvr::quest::grid_motion;
namespace p=hpvr::quest::peeves;
struct Triangle { std::array<gm::Vec,3> vertices;gm::Vec minimum{},maximum{},normal{}; };
Triangle Tri(gm::Vec a,gm::Vec b,gm::Vec c){
    Triangle t{{a,b,c},a,a,{}};
    for(auto v:t.vertices)for(unsigned i=0;i<3;++i){t.minimum[i]=std::min(t.minimum[i],v[i]);t.maximum[i]=std::max(t.maximum[i],v[i]);}
    t.normal=gm::Cross(gm::Sub(b,a),gm::Sub(c,a));t.normal=gm::Scale(t.normal,1/std::sqrt(gm::Dot(t.normal,t.normal)));return t;
}
int main(){try{
    unsigned checks=0;const auto check=[&](bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);};
    const std::vector<Triangle> empty;
    const std::vector<Triangle> wall{Tri({1,-10,-10},{1,10,-10},{1,10,10}),Tri({1,-10,-10},{1,10,10},{1,-10,10})};
    const gm::Vec far{20,20,20};
    namespace c=hpvr::quest::cracker;
    c::Cracker candy;
    check(c::Spawn(candy,{0,1,0},{4,1,0},4.5F),"cracker spawns");
    check(std::abs(candy.velocity[0]-9.F)<.0001F,"cracker has usable horizontal speed");
    const auto launch_y=candy.velocity[1];
    c::Advance(candy,.1F,empty,far,far,.25F,.6F);
    check(std::abs(candy.position[0]-.9F)<.0001F&&std::abs(candy.position[1]-(1+launch_y*.1F-.055F))<.0001F,"cracker arc includes gravity");
    candy.phase=c::Phase::Ground;candy.fuse=.1F;
    check(c::PickUp(candy)&&!c::PickUp(candy),"ground cracker picked up once");
    for(unsigned i=0;i<100;++i)c::Advance(candy,.1F,empty,far,far,.25F,.6F);
    check(candy.phase==c::Phase::Carried&&candy.fuse==1,"carried fuse remains at one second");
    check(c::Release(candy,{0,1,0},{1,0,0})&&candy.returned&&candy.velocity[0]==11&&candy.velocity[1]==4.5F,"return throw spans the arena without a point-blank approach");
    candy.velocity={100,0,0};
    auto burst=c::Advance(candy,.02F,empty,far,{.5F,1,0},.25F,.6F);
    check(burst.boss_hit&&burst.exploded&&!c::Active(candy),"returned cracker hits boss without tunnelling");
    check(!c::Advance(candy,.02F,empty,far,{.5F,1,0},.25F,.6F).boss_hit,"one cracker cannot hit boss twice");
    candy={};c::Spawn(candy,{.8F,1,0},{4,1,0},.001F);candy.returned=true;
    burst=c::Advance(candy,.01F,wall,{1.2F,1,0},{1.2F,1,0},.25F,.6F);
    check(!burst.player_damage&&!burst.boss_hit,"wall occludes both cracker damage paths");
    candy={};c::Spawn(candy,{0,1,0},{4,1,0},4.5F);candy.velocity={100,0,0};
    burst=c::Advance(candy,.02F,empty,{.5F,1,0},far,.25F,.6F);
    check(burst.player_damage==14&&!burst.boss_hit,"enemy cracker deals original seven of fifty health");
    p::Apple apple;
    check(p::Throw(apple,{0,1,0},{4,1,0}),"valid throw");
    const auto apple_launch_y=apple.velocity[1];
    check(apple.velocity[0]==9,"apple horizontal speed");
    check(!p::Throw(apple,{0,1,0},{4,1,0}),"active apple cannot be overwritten");
    check(p::AdvanceApple(apple,.1F,empty,far,.25F,.6F,9.5F)==0,"remote player unharmed");
    check(std::abs(apple.position[0]-.9F)<.0001F&&std::abs(apple.position[1]-(1+apple_launch_y*.1F-.0475F))<.0001F,"ballistic arc");
    const auto unchanged=apple.position;
    check(p::AdvanceApple(apple,-1,empty,far,.25F,.6F,9.5F)==0&&apple.position==unchanged,"invalid time leaves motion unchanged");
    apple={};p::Throw(apple,{0,1,0},{4,1,0});
    p::AdvanceApple(apple,.25F,wall,far,.25F,.6F,0);
    check(apple.active&&apple.position[0]<1&&apple.velocity[0]<0,"thin wall bounces apple instead of tunnelling");
    check(std::abs(apple.velocity[0]+4.5F)<.0001F,"bounce retains half speed");
    apple={};p::Throw(apple,{0,1,0},{4,1,0});apple.fuse=.001F;
    check(p::AdvanceApple(apple,.01F,empty,{.5F,1,0},.25F,.6F,0)>0&&!apple.active,"expiry damages nearby player once");
    check(p::AdvanceApple(apple,.1F,empty,{.5F,1,0},.25F,.6F,0)==0,"expired apple cannot damage twice");
    apple={};p::Throw(apple,{.8F,1,0},{4,1,0});apple.fuse=.001F;
    check(p::AdvanceApple(apple,.01F,wall,{1.2F,1,0},.25F,.6F,0)==0,"wall blocks explosion damage");
    apple={};p::Throw(apple,{0,1,0},{4,1,0});apple.velocity={100,0,0};
    check(p::AdvanceApple(apple,.02F,empty,{.5F,1,0},.25F,.6F,0)==p::kAppleDamagePercent&&!apple.active,"swept direct hit cannot tunnel through player");
    apple={};p::Throw(apple,{0,1,0},{4,1,0});apple.velocity={100,0,0};
    check(p::AdvanceApple(apple,.02F,wall,{1.5F,1,0},.25F,.6F,0)==0&&apple.active,"earlier wall collision beats player contact");
    apple={};p::Throw(apple,{0,1,0},{4,1,0});apple.settled=true;apple.fuse=.1F;
    for(unsigned i=0;i<11;++i)p::AdvanceApple(apple,.01F,empty,far,.25F,.6F,0);
    check(!apple.active&&apple.position==gm::Vec{0,1,0},"settled apple expires without moving");
    apple={};check(!p::Throw(apple,{0,1,0},{0,1,0})&&!apple.active,"degenerate throw rejected");
    p::Throw(apple,{0,1,0},{4,1,0});apple.fuse=std::numeric_limits<float>::quiet_NaN();
    check(p::AdvanceApple(apple,.1F,empty,far,.25F,.6F,0)==0&&apple.position==gm::Vec{0,1,0},"invalid timer cannot enter simulation");
    for(float range:{3.F,6.F,10.F,14.F})for(float height:{1.F,3.F,5.F}){
        const gm::Vec target{range,1,0};
        apple={};check(p::Throw(apple,{0,height,0},target),"arena apple launch");
        unsigned damage=0;
        for(unsigned frame=0;frame<240&&apple.active;++frame)damage+=p::AdvanceApple(apple,.01F,empty,target,.25F,.6F,p::kAppleGravity);
        check(damage>0,"stationary player is hit across arena distances and elevations");
        candy={};check(c::Spawn(candy,{0,height,0},target,4.5F),"arena cracker launch");
        damage=0;
        for(unsigned frame=0;frame<240&&c::Active(candy);++frame)damage+=c::Advance(candy,.01F,empty,target,far,.25F,.6F).player_damage;
        check(damage>0,"Draco can hit a stationary player across the arena");
    }
    std::cout<<"PEEVES_PROJECTILE_TESTS=PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
