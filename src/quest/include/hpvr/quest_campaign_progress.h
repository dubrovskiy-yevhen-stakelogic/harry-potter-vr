#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <optional>

namespace hpvr::quest::campaign {

inline constexpr std::array<unsigned,25> kCardIds{
    101,1,28,10,24,18,8,2,19,47,35,41,17,69,48,37,62,57,49,96,72,82,83,11,100};
inline constexpr std::uint32_t kCardBits=(1U<<kCardIds.size())-1U;
inline constexpr unsigned kGryffindor=3;
inline constexpr unsigned kFlipendoLesson=0;
inline constexpr unsigned kAlohomoraLesson=1;
inline constexpr unsigned kWingardiumLesson=2;
inline constexpr unsigned kBroomLesson=5;
inline constexpr unsigned kPointLimit=1000000;

constexpr std::uint32_t CardMask(unsigned owned_id){
    for(unsigned i=0;i<kCardIds.size();++i)if(kCardIds[i]==owned_id)return 1U<<i;
    return 0;
}
constexpr unsigned CardCount(std::uint32_t cards){return std::popcount(cards&kCardBits);}
constexpr unsigned LessonPoints(unsigned passes){
    return passes<=4?5U*passes*(passes+1U)/2U:0U;
}
constexpr unsigned ChallengePoints(unsigned stars){
    return stars>8?0U:stars==8?20U:stars>=6?10U:5U;
}

// The original houses rise alongside Harry with bounded random variation.
// A caller-supplied seed makes a selected-level preset reproducible.
inline void AddHousePoints(std::array<unsigned,4>& houses,unsigned award,std::uint32_t seed){
    if(!award)return;
    auto random=[&](){seed=seed*1664525U+1013904223U;return seed;};
    const auto gryffindor=std::min(kPointLimit,houses[kGryffindor]+std::min(award,kPointLimit));
    houses[kGryffindor]=gryffindor;
    const auto lead=std::min(gryffindor,58U);
    houses[2]=std::min(kPointLimit,std::max(houses[2],gryffindor+1U+random()%std::max(lead,1U)));
    const auto hufflepuff=static_cast<unsigned>((std::uint64_t(gryffindor)*(5000U+random()%2000U))/10000U);
    const auto ravenclaw=static_cast<unsigned>((std::uint64_t(gryffindor)*(7000U+random()%2000U))/10000U);
    houses[1]=std::max(houses[1],hufflepuff);
    houses[0]=std::max(houses[0],ravenclaw);
}

inline unsigned RaiseLessonPoints(std::array<unsigned,8>& awarded,std::array<unsigned,4>& houses,
                                 unsigned lesson,unsigned points,std::uint32_t seed){
    if(lesson>=awarded.size())return 0;
    const unsigned maximum=lesson<5?50U:lesson<7?20U:0U;
    if(points>maximum||points<=awarded[lesson])return 0;
    const auto delta=points-awarded[lesson];awarded[lesson]=points;
    AddHousePoints(houses,delta,seed);
    return delta;
}

struct LevelStart {
    std::uint32_t earned_cards=0,completed_maps=0;
    std::array<unsigned,4> house_points{};
    std::array<unsigned,8> lesson_best{},lesson_points{};
    unsigned banked_beans=0,lesson_passes=0;
    bool card_awarded=false,card_taken=false;
};

// A single perfect story clear of earlier maps, without repeatable menu farming.
// Map-local actor IDs, stars and event state belong to the fresh selected map.
inline std::optional<LevelStart> PerfectPriorProgress(unsigned selected_map){
    if(selected_map>4)return std::nullopt;
    LevelStart result;
    result.completed_maps=(1U<<selected_map)-1U;
    if(selected_map>0){
        result.earned_cards|=CardMask(101);
        result.card_awarded=result.card_taken=true;result.lesson_passes=4;
        result.lesson_best[kFlipendoLesson]=100;
        result.lesson_points[kFlipendoLesson]=LessonPoints(4);
        result.banked_beans=29;
        for(unsigned round=1;round<=4;++round)AddHousePoints(result.house_points,round*5U,0x48505652U+round);
    }
    if(selected_map>1){
        result.banked_beans+=37;
        AddHousePoints(result.house_points,ChallengePoints(8),0x48505657U);
    }
    if(selected_map>2){
        result.earned_cards|=CardMask(1);
        result.banked_beans+=4;
        result.lesson_best[kBroomLesson]=100;result.lesson_points[kBroomLesson]=20;
        AddHousePoints(result.house_points,20,0x48505658U);
    }
    if(selected_map>3){
        result.earned_cards|=CardMask(28)|CardMask(10);result.banked_beans+=83;
        for(const auto lesson:{kAlohomoraLesson,kWingardiumLesson}){
            result.lesson_best[lesson]=100;result.lesson_points[lesson]=LessonPoints(4);
            AddHousePoints(result.house_points,LessonPoints(4),0x48505660U+lesson);
        }
        AddHousePoints(result.house_points,20,0x48505664U);
    }
    return result;
}

} // namespace hpvr::quest::campaign
