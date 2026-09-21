#include "hpvr/quest_campaign_progress.h"
#include "hpvr/hp1_gesture.h"
#include "hpvr/quest_broom_lesson.h"

#include <cctype>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace {
unsigned checks=0;
void Check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
std::string Fold(std::string value){
    for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
unsigned Number(const hpvr::wand::Hp1ClassDefaultProperty& property){
    Check(property.value.size()>=4,"owned integer property has four bytes");
    return unsigned(property.value[0])|(unsigned(property.value[1])<<8U)|
        (unsigned(property.value[2])<<16U)|(unsigned(property.value[3])<<24U);
}
auto Defaults(const std::filesystem::path& package,const std::string& name){
    const auto table=hpvr::wand::inspect_hp1_package_link_table(package);
    Check(table.status==hpvr::wand::Hp1ProfileStatus::ok,"owned class table loads");
    const auto found=std::ranges::find_if(table.exports,[&](const auto& entry){
        return entry.qualified_class_name=="Core.Class"&&entry.object_path.size()==1&&Fold(entry.object_path[0])==Fold(name);
    });
    Check(found!=table.exports.end(),"owned class exists");
    const auto defaults=hpvr::wand::inspect_hp1_class_visual_defaults(package,found->reference);
    Check(defaults.status==hpvr::wand::Hp1ProfileStatus::ok,"owned class defaults load");
    return defaults.serialized_properties;
}
void OwnedData(const std::filesystem::path& root){
    using namespace hpvr::quest::campaign;
    const auto base=Defaults(root/"system/HPBase.u","baseHarry");
    std::array<unsigned,25> cards{};unsigned max_points=0;
    for(const auto& property:base){
        if(property.name=="WizardCards"){
            const auto slot=static_cast<unsigned>(std::max<std::int64_t>(0,property.array_index));
            Check(slot<cards.size(),"owned folio has twenty-five slots");cards[slot]=Number(property);
        }
        if(property.name=="maxPointsPerHouse")max_points=Number(property);
    }
    Check(cards==kCardIds&&max_points==400,"folio IDs and vial capacity match owned defaults");
    const auto lesson=Defaults(root/"system/HPBase.u","SpellLearnTrigger");
    std::array<unsigned,4> rounds{};
    for(const auto& property:lesson)if(property.name=="iNumHousePoints"){
        const auto round=static_cast<unsigned>(std::max<std::int64_t>(0,property.array_index));
        if(round<rounds.size())rounds[round]=Number(property);
    }
    Check(rounds==std::array<unsigned,4>{5,10,15,20},"four authored lesson round rewards total fifty");
    std::array<unsigned,3> beans{};
    for(unsigned map=0;map<3;++map){
        const auto filename=std::array<const char*,3>{"Lev_Tut1.unr","Lev_Tut1b.unr","Lev_Tut2.unr"}[map];
        const auto census=hpvr::wand::inspect_hp1_actor_visuals(root/"Maps"/filename);
        Check(census.status==hpvr::wand::Hp1ProfileStatus::ok,"owned prior-map actor census loads");
        for(const auto& actor:census.actors){
            const auto cls=Fold(actor.qualified_class_name);
            if(cls.starts_with("hprops.")&&cls.ends_with("bean")&&!actor.hidden&&actor.location_serialized)++beans[map];
            if(map==0&&actor.actor_reference==1617)
                Check(std::ranges::any_of(actor.serialized_properties,[](const auto& property){
                    return property.name=="SpawnClass"&&!property.object_path.empty()&&property.object_path.back()=="WCDumbldore";
                }),"first map spawns Dumbledore's card");
            if(map==2&&actor.actor_reference==265)Check(cls=="hprops.wcmerlin","flight secret owns Merlin's card");
            if(map==1&&cls=="hpbase.starstrigger"){
                unsigned average=0,winner=0;
                for(const auto& property:actor.serialized_properties){
                    if(property.name=="avgStarCount")average=Number(property);
                    if(property.name=="winnerStarCount")winner=Number(property);
                }
                Check(average==6&&winner==8,"challenge result thresholds match owned map");
            }
            const bool cauldron=cls=="hprops.bronzecauldron";
            if(map!=1||(!cauldron&&!cls.starts_with("hprops.flipendovase")))continue;
            auto properties=Defaults(root/"system/HProps.u",cls.substr(7));
            properties.insert(properties.end(),actor.serialized_properties.begin(),actor.serialized_properties.end());
            unsigned count=1;std::map<unsigned,bool> contents;
            for(const auto& property:properties){
                const auto key=Fold(property.name);
                if(cauldron&&key=="inumberofbeans")count=Number(property);
                if((cauldron&&key=="ejectedobjects")||(!cauldron&&key=="transforminto")){
                    const auto index=cauldron?static_cast<unsigned>(std::max<std::int64_t>(0,property.array_index)):0U;
                    contents[index]=!property.object_path.empty()&&Fold(property.object_path.back()).ends_with("bean");
                }
            }
            for(const auto& [index,is_bean]:contents)if(index<count&&is_bean)++beans[map];
        }
        if(map==2){
            const auto flying=hpvr::quest::broom::LoadLessonMetadata(root,census);
            Check(flying.valid,"owned flight lesson metadata loads");
            unsigned total=0;for(const auto& route:flying.stages[0])total+=static_cast<unsigned>(route.size());
            Check(total==82&&flying.minimum_hits==30,"story flight has eighty-two hoops");
        }
    }
    Check(beans==std::array<unsigned,3>{29,37,4},"perfect prior bean bank includes authored container contents");
    std::cout<<"OWNED_CAMPAIGN_PROGRESS=PASS cards=25 prior_beans=29,37,4 lesson_rounds=5,10,15,20\n";
}
}

int main(int argc,char** argv){
    try{
        using namespace hpvr::quest::campaign;
        for(unsigned i=0;i<kCardIds.size();++i){
            Check(CardMask(kCardIds[i])==(1U<<i),"owned card IDs map to original folio slots");
            Check(CardCount(CardMask(kCardIds[i]))==1,"one owned card is counted once");
        }
        Check(CardMask(0)==0&&CardMask(25)==0&&CardMask(102)==0,"unknown card IDs are not unlocked");
        Check(CardCount(~0U)==25,"counter excludes out-of-range bits");
        Check(CardMask(101)==1&&CardMask(1)==2&&CardMask(28)==4,"first three story cards retain authored order");
        for(unsigned passes=0;passes<=4;++passes)
            Check(LessonPoints(passes)==std::array<unsigned,5>{0,5,15,30,50}[passes],"lesson awards accumulate each passed round");
        Check(LessonPoints(5)==0,"invalid round count cannot award points");
        for(unsigned stars=0;stars<=8;++stars)
            Check(ChallengePoints(stars)==(stars==8?20U:stars>=6?10U:5U),"challenge awards use authored star thresholds");
        Check(ChallengePoints(9)==0,"invalid star count does not award maximum");
        for(unsigned map=0;map<=3;++map){
            const auto prior=PerfectPriorProgress(map);
            Check(prior.has_value(),"supported selected map has a complete preset");
            const auto& p=*prior;
            Check(p.completed_maps==((1U<<map)-1U),"only earlier maps are completed");
            Check(p.house_points[kGryffindor]==std::array<unsigned,4>{0,50,70,90}[map],"perfect single-story score excludes replay farming");
            Check(p.banked_beans==std::array<unsigned,4>{0,29,66,70}[map],"only earlier-map beans enter the bank");
            Check(p.earned_cards==(map==0?0U:map<3?CardMask(101):CardMask(101)|CardMask(1)),"selected-map cards remain collectible");
            Check(p.lesson_passes==(map?4U:0U)&&p.card_awarded==bool(map)&&p.card_taken==bool(map),"legacy tutorial fields match prior completion");
            Check(p.lesson_best[kFlipendoLesson]==(map?100U:0U)&&p.lesson_points[kFlipendoLesson]==(map?50U:0U),"prior Flipendo lesson is perfect");
            Check(p.lesson_best[kAlohomoraLesson]==0&&p.lesson_best[kWingardiumLesson]==0,"next-map lessons are not pre-completed");
            Check(p.lesson_best[kBroomLesson]==(map==3?100U:0U)&&p.lesson_points[kBroomLesson]==(map==3?20U:0U),"only prior story flight is perfect");
            Check(p.lesson_points[kBroomLesson+1]==0,"optional replay route is not added to story maximum");
            Check(PerfectPriorProgress(map)->house_points==p.house_points,"level-selection house values are stable");
        }
        const auto returning=PerfectPriorProgress(4);
        Check(returning&&returning->completed_maps==15&&returning->earned_cards==15&&returning->banked_beans==153&&
            returning->house_points[kGryffindor]==210&&returning->lesson_best[kAlohomoraLesson]==100&&
            returning->lesson_best[kWingardiumLesson]==100&&returning->lesson_points[kAlohomoraLesson]==50&&
            returning->lesson_points[kWingardiumLesson]==50,"return map starts after a perfect Charms clear");
        Check(!PerfectPriorProgress(5)&&!PerfectPriorProgress(std::numeric_limits<unsigned>::max()),"unsupported map fails without shift overflow");
        std::array<unsigned,8> awards{};std::array<unsigned,4> houses{};
        for(unsigned pass=1;pass<=4;++pass){
            Check(RaiseLessonPoints(awards,houses,kFlipendoLesson,LessonPoints(pass),pass)==pass*5U,"new passed round awards its own points");
            const auto saved=houses;
            Check(RaiseLessonPoints(awards,houses,kFlipendoLesson,LessonPoints(pass),pass+20)==0&&houses==saved,"restored or replayed result cannot double award");
        }
        Check(houses[kGryffindor]==50,"actual perfect lesson totals fifty points");
        Check(RaiseLessonPoints(awards,houses,kBroomLesson,5,91)==5&&RaiseLessonPoints(awards,houses,kBroomLesson,20,92)==15,"improved flight result awards only the improvement");
        Check(RaiseLessonPoints(awards,houses,8,50,0)==0&&RaiseLessonPoints(awards,houses,0,51,0)==0&&RaiseLessonPoints(awards,houses,5,21,0)==0,"invalid award cannot alter totals");
        for(unsigned seed=0;seed<1000;++seed){
            std::array<unsigned,4> generated{};AddHousePoints(generated,90,seed);
            Check(generated[0]>=63&&generated[0]<=80&&generated[1]>=45&&generated[1]<=62&&generated[2]>90&&generated[2]<=148&&generated[3]==90,"other houses stay inside original random ranges");
            const auto previous=generated;AddHousePoints(generated,5,seed+1);
            for(unsigned house=0;house<4;++house)Check(generated[house]>=previous[house],"house totals never decrease after an award");
        }
        std::array<unsigned,4> limited{kPointLimit,kPointLimit,kPointLimit,kPointLimit};
        AddHousePoints(limited,std::numeric_limits<unsigned>::max(),17);
        for(auto score:limited)Check(score==kPointLimit,"large award saturates saved score bounds");
        if(argc==2)OwnedData(argv[1]);else Check(argc==1,"usage: test [owned-data-root]");
        std::cout<<"CAMPAIGN_PROGRESS_TESTS=PASS checks="<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"CAMPAIGN_PROGRESS_TESTS=FAIL "<<error.what()<<'\n';return 1;}
}
