#pragma once
#include <array>
#include <string_view>

namespace hpvr::quest {
inline constexpr unsigned kIntroductionMapId=0;
inline constexpr unsigned kFlipendoChallengeMapId=1;
inline constexpr unsigned kBroomstickTrainingMapId=2;
inline constexpr unsigned kCharmsTrainingMapId=3;

struct QuestMapDescriptor {
    unsigned id;
    std::string_view package_path;
    std::string_view menu_title;
    std::string_view objective_key;
    unsigned maximum_quest_stage;
};

// IDs are persisted in saves and cooked-scene envelopes. Never reorder them.
inline constexpr std::array<QuestMapDescriptor,4> kQuestMaps{{
    {kIntroductionMapId,"Maps/Lev_Tut1.unr","HOGWARTS INTRODUCTION","objective_01=",23},
    {kFlipendoChallengeMapId,"Maps/Lev_Tut1b.unr","FLIPENDO CHALLENGE","objective_03=",64},
    {kBroomstickTrainingMapId,"Maps/Lev_Tut2.unr","BROOMSTICK TRAINING","objective_04=",64},
    {kCharmsTrainingMapId,"Maps/Lev_Tut3.unr","CHARMS TRAINING","objective_05=",64}
}};

constexpr const QuestMapDescriptor* FindQuestMap(unsigned id){
    for(const auto& map:kQuestMaps)if(map.id==id)return &map;
    return nullptr;
}
constexpr bool IsSupportedQuestMap(unsigned id){return FindQuestMap(id)!=nullptr;}
constexpr bool IsWalkingChallenge(unsigned id){return id==kFlipendoChallengeMapId||id==kCharmsTrainingMapId;}
} // namespace hpvr::quest
