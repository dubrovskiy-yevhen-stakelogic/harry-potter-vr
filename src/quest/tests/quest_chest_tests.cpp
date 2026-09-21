#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"

#include <iostream>
#include <stdexcept>

using namespace hpvr::quest;

int main(int argc,char** argv){try{
    unsigned checks=0;
    const auto check=[&](bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);};
    check(chest::IsChest("hprops.bronzechest")&&chest::IsChest("hprops.ironchest")&&
          !chest::IsChest("hprops.bronzecauldron"),"only authored chest classes match");
    check(chest::RewardActor(2846,0)==536916448&&chest::CardId(536916448)==10&&
          chest::CardId(2918)==28&&chest::CardId(1)==0,"card rewards preserve original folio identities");
    ChallengeProp prop;prop.reference=2846;prop.name="hprops.bronzechest";prop.spell_target=true;
    prop.first=10;prop.count=12;prop.animation_first=30;prop.animation_frames=74;prop.animation_duration=74.F/30;
    prop.settled_first=1000;prop.settled_frames=6;prop.settled_duration=.2F;prop.activation_time=0;
    ProgressSave progress;std::vector<ChallengeProp> sources{prop};
    check(ChallengePropDrawRange(prop,false,0).first==10&&!ChallengeRewardsReady(sources,2846,progress),
          "closed chest shows no rewards");
    RememberChallengeEvent(progress,2846);
    check(ChallengePropDrawRange(prop,true,0).first==30&&!ChallengeRewardsReady(sources,2846,progress),
          "opening starts before rewards are exposed");
    prop.activation_time=prop.animation_duration;sources[0]=prop;
    check(ChallengePropDrawRange(prop,true,0).first==1000&&ChallengeRewardsReady(sources,2846,progress),
          "completed opening leaves original open chest and releases rewards");
    check(ChallengePropDrawRange(prop,true,.1F).first==1036,"open chest loops original end frames");
    if(argc==2){
        const std::filesystem::path root(argv[1]),map=root/"Maps/Lev_Tut3.unr";
        hpvr::wand::Hp1PackageReadScope read_cache;
        hpvr_hp1_player_start_report start{};
        check(hpvr_hp1_load_player_start_utf8(map.string().c_str(),kMetersPerUnrealUnit,0,&start)==0,"owned map player start");
        const float yaw=start.rotation_units[1]*kTau/65536;
        std::vector<GpuVertex> vertices;std::vector<std::uint8_t> pixels;std::uint32_t layers=0;
        std::vector<CollisionTriangle> collision;std::vector<ChallengeProp> props;
        std::vector<FlameEmitter> flames;std::vector<GlowEmitter> glows;
        check(LoadChallengeProps(root,map,start,yaw,{},vertices,pixels,layers,collision,props,flames,glows),"owned chest and prop meshes load");
        const auto chest_count=std::ranges::count_if(props,[](const auto& value){return chest::IsChest(value.name);});
        check(chest_count==11,"all ten bronze and one iron chest are present");
        for(const auto& value:props)if(chest::IsChest(value.name)){
            check(value.spell_target&&value.animation_frames==74&&value.settled_frames==6&&
                  std::abs(value.animation_duration-74.F/30)<.0001F,"owned original open and end animation recovered");
            bool changed=false;
            for(unsigned i=0;i<value.count;++i)for(unsigned axis=0;axis<3;++axis)
                changed=changed||std::abs(vertices[value.animation_first+i].position[axis]-
                                        vertices[value.settled_first+i].position[axis])>.001F;
            check(changed,"open chest lid differs from closed pose");
        }
        std::vector<BeanDraw> beans;
        // This fixture loads props only. Full-map preparation verifies pickup grounding against the BSP floor.
        check(LoadOwnedBeans(root,map,start,yaw,vertices,pixels,layers,beans),"owned chest rewards load");
        unsigned reward_count=0,card_count=0;
        for(const auto& bean:beans){
            if(std::ranges::none_of(props,[&](const auto& value){return value.reference==bean.source_actor&&chest::IsChest(value.name);}))continue;
            ++reward_count;if(bean.kind==4)++card_count;
            check(bean.actor_reference>=0x20000000&&bean.source_actor>0,"chest reward is tied to persistent source");
            if(bean.kind==4)check(bean.actor_reference==chest::RewardActor(2846,0),"Burdock comes only from authored card chest");
        }
        check(reward_count==50&&card_count==1,"owned chest contents are 49 beans and one Burdock card");
        std::cout<<"OWNED_CHESTS count="<<chest_count<<" rewards="<<reward_count<<" cards="<<card_count<<'\n';
    }
    std::cout<<"CHEST_TESTS=PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
