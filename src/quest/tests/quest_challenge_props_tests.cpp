#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <stdexcept>

using namespace hpvr::quest;
namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
CollisionTriangle Triangle(std::array<float,3> a,std::array<float,3> b,std::array<float,3> c){
    CollisionTriangle result;result.vertices={a,b,c};result.minimum=result.maximum=a;
    for(const auto& point:result.vertices)for(unsigned axis=0;axis<3;++axis){
        result.minimum[axis]=std::min(result.minimum[axis],point[axis]);result.maximum[axis]=std::max(result.maximum[axis],point[axis]);
    }
    NormalizeVector(CrossVector(SubtractVector(b,a),SubtractVector(c,a)),&result.normal);
    return result;
}
}
int main(){try{
    ChallengeProp vase;vase.reference=12;vase.first=10;vase.count=30;vase.breakable=true;vase.broken_first=100;vase.broken_count=18;
    Check(ChallengePropDrawRange(vase,false,0)==std::pair<std::uint32_t,std::uint32_t>{10,30},"unbroken vase uses intact mesh");
    Check(ChallengePropDrawRange(vase,true,0)==std::pair<std::uint32_t,std::uint32_t>{100,18},"broken vase leaves original remaining model");
    ChallengeProp cauldron;cauldron.reference=14;cauldron.first=20;cauldron.count=60;cauldron.cauldron=true;
    cauldron.animation_first=200;cauldron.animation_frames=68;cauldron.animation_duration=68.0F/30;
    cauldron.settled_first=5000;cauldron.settled_frames=5;cauldron.settled_duration=5.0F/30;
    cauldron.activation_time=0;
    ProgressSave progress;std::vector<ChallengeProp> sources{cauldron};
    Check(!ChallengeRewardsReady(sources,14,progress),"unhit cauldron has no rewards");
    RememberChallengeEvent(progress,14);
    Check(!ChallengeRewardsReady(sources,14,progress),"reward waits for cauldron finish animation");
    Check(ChallengePropDrawRange(cauldron,true,0).first==200,"cauldron begins at first tip frame");
    cauldron.activation_time=cauldron.animation_duration*.5F;
    Check(ChallengePropDrawRange(cauldron,true,0).first==200+34*60,"cauldron tips through authored frames");
    cauldron.activation_time=cauldron.animation_duration;sources[0]=cauldron;
    Check(ChallengeRewardsReady(sources,14,progress),"beans release only after tipover completes");
    Check(ChallengePropDrawRange(cauldron,true,0).first==5000,"completed cauldron stays tipped");
    Check(ChallengePropDrawRange(cauldron,true,1.0F/30).first==5060,"settled cauldron loops authored tipped clip");
    ChallengeProp book;book.reference=16;book.savebook=true;book.first=90;book.count=30;
    Check(ChallengePropDrawRange(book,false,0).second==30&&ChallengePropDrawRange(book,true,0).second==0,"touched book is consumed in completed checkpoint state");
    Check(std::abs(ChallengePropOffset(book,0)[1]-.10F)<.00001F,"savebook preserves authored bob baseline");
    Check(std::abs(ChallengePropOffset(book,3.1415926535F/16)[1]-.13F)<.00001F,"savebook uses original bob amplitude and speed");

    ChallengeProp source;source.minimum={-.5F,0,-.5F};source.maximum={.5F,1,.5F};
    std::vector<CollisionTriangle> collision{
        Triangle({.4F,0,-.4F},{.4F,1,-.4F},{.4F,0,.4F}), // Source's own shell.
        Triangle({1,-5,-5},{1,5,-5},{1,-5,5}),Triangle({1,5,-5},{1,5,5},{1,-5,5})};
    Check(ChallengeBeanSweepFraction(collision,source,{0,.5F,0},{.7F,.5F,0})==1,"reward escapes its source collider");
    const auto fraction=ChallengeBeanSweepFraction(collision,source,{0,.5F,0},{2,.5F,0});
    Check(fraction>0&&fraction<.5F,"pickup-radius sweep stops before the world wall");
    BeanDraw bean;bean.source_actor=20;bean.emission={0,.5F,0};bean.position={2,.18F,0};
    PrepareChallengeBeanEmission(bean,source,collision);
    Check(bean.emission_points==17&&bean.position[0]<1,"reward's destination remains on reachable side");
    for(unsigned step=0;step<=100;++step){bean.emission_time=float(step)/100;Check(BeanWorldPosition(bean)[0]<1,"rendered reward path never crosses wall");}
    std::cout<<"CHALLENGE_PROPS_TESTS=PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
