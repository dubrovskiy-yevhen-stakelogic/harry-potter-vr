#include "hpvr/quest_grid_push.h"
#include "../../../android/app/src/main/cpp/quest_challenge_movers.h"
#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
    using namespace hpvr::quest;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    try {
        grid_push::PendingHits pending;
        const grid_push::Position object{-2992,-4800,1104},player{-2992,-5000,1104};
        // Exact seventh-star block: the player may stand south while striking
        // its east face. East/west slides are open; north/south has stone curbs.
        const grid_push::Position east_impact{-2944,-4800,1104};
        grid_push::RememberImpact(pending,4511,east_impact);
        check(movers::GridStepUnreal(object,grid_push::ConsumeOrigin(pending,4511,player),96)==
              grid_push::Position{-96,0,0},"projectile contact face determines the cardinal push");
        check(pending.empty(),"impact is consumed exactly once");
        check(movers::GridStepUnreal(object,grid_push::ConsumeOrigin(pending,4511,player),96)==
              grid_push::Position{0,96,0},"physical bump still uses the player's position");
        const grid_push::Position north_impact{-2992,-4752,1104};
        grid_push::RememberImpact(pending,4511,north_impact);
        check(movers::GridStepUnreal(object,grid_push::ConsumeOrigin(pending,4511,player),96)==
              grid_push::Position{0,-96,0},"north face keeps original direction; collision is not bypassed");
        grid_push::RememberImpact(pending,4511,east_impact);
        (void)grid_push::ConsumeOrigin(pending,4511,player); // Already-moving mover rejects this event.
        check(grid_push::ConsumeOrigin(pending,4511,player)==player,"busy rejection cannot retain a stale projectile origin");
        grid_push::RememberImpact(pending,4511,{std::numeric_limits<float>::quiet_NaN(),0,0});
        grid_push::RememberImpact(pending,0,east_impact);
        check(pending.empty(),"invalid impact identity and nonfinite coordinates are rejected");
        grid_push::RememberImpact(pending,4511,east_impact);
        check(grid_push::ConsumeOrigin(pending,42,player)==player && pending.size()==1,
              "one mover cannot consume another mover's projectile");
        pending.clear();
        check(grid_push::ConsumeOrigin(pending,4511,player)==player,"undelivered batch does not leak into a future bump");
        std::cout<<"GRID_PUSH_TESTS=PASS\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
