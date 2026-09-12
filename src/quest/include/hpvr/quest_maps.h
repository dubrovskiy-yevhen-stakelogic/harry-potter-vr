#pragma once
#include <array>
#include <string_view>

namespace hpvr::quest {
inline constexpr unsigned kIntroductionMapId=0;
inline constexpr unsigned kFlipendoChallengeMapId=1;
inline constexpr unsigned kBroomstickTrainingMapId=2;

struct QuestMapDescriptor {
    unsigned id;
    std::string_view package_path;
    std::string_view menu_title;
    std::string_view objective_key;
    unsigned maximum_quest_stage;
};

// IDs are persisted in saves and cooked-scene envelopes. Never reorder them.
inline constexpr std::array<QuestMapDescriptor,3> kQuestMaps{{
    {kIntroductionMapId,"Maps/Lev_Tut1.unr","HOGWARTS INTRODUCTION","objective_01=",23},
    {kFlipendoChallengeMapId,"Maps/Lev_Tut1b.unr","FLIPENDO CHALLENGE","objective_03=",64},
    {kBroomstickTrainingMapId,"Maps/Lev_Tut2.unr","BROOMSTICK TRAINING","objective_04=",64}
}};

constexpr const QuestMapDescriptor* FindQuestMap(unsigned id){
    for(const auto& map:kQuestMaps)if(map.id==id)return &map;
    return nullptr;
}
constexpr bool IsSupportedQuestMap(unsigned id){return FindQuestMap(id)!=nullptr;}
} // namespace hpvr::quest
