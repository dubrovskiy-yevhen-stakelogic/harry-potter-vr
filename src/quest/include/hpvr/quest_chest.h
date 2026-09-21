#pragma once

#include <cstdint>
#include <string_view>

namespace hpvr::quest::chest {

constexpr bool IsChest(std::string_view name){
    return name=="hprops.bronzechest"||name=="hprops.ironchest"||name=="hprops.woodchest";
}
constexpr std::int32_t RewardActor(std::int32_t source,unsigned index){
    return source>0&&source<1000000&&index<8?0x20000000+source*16+static_cast<std::int32_t>(index):0;
}
constexpr unsigned RewardCardId(std::string_view name){
    return name=="hprops.wcmuldoon"?10U:name=="hprops.wcwaffling"?24U:
        name=="hprops.wcshimpling"?8U:name=="hprops.wcoddball"?18U:0U;
}
constexpr unsigned CardId(std::int32_t actor){
    return actor==2918?28U:actor==RewardActor(2846,0)?10U:0U;
}

} // namespace hpvr::quest::chest
