#include "hpvr/quest_death_transition.h"
#include <cassert>
#include <cmath>
#include <limits>

int main(){
    using namespace hpvr::quest::death;
    assert(std::abs(Duration(1.2F)-1.7F)<.0001F);
    assert(Duration(0)==.5F&&Duration(-1)==.5F);
    assert(Duration(std::numeric_limits<float>::quiet_NaN())==.5F);
    const float duration=Duration(1.2F);
    float time=0;for(unsigned i=0;i<24;++i)time=Advance(time,.05F,duration);
    assert(time<duration); // FinishAnim alone must not restore the level.
    assert(std::abs(FaintTime(time,duration)-1.2F)<.0001F);
    for(unsigned i=0;i<11;++i)time=Advance(time,.05F,duration);
    assert(time==duration&&std::abs(FaintTime(time,duration)-1.2F)<.0001F);
    assert(Advance(.2F,0,duration)==.2F);
    assert(Advance(.2F,std::numeric_limits<float>::quiet_NaN(),duration)==.2F);
    assert(Advance(0,10,duration)==.05F); // Focus-loss frame must not skip death.
}
