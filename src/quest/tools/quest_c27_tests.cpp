#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <stdexcept>
using namespace hpvr::quest;
namespace wand=hpvr::wand;
static void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
    wand::Hp1SkeletalSkin synthetic;synthetic.status=wand::Hp1ProfileStatus::ok;
    synthetic.bones.resize(1);synthetic.bones[0].name="root";synthetic.bones[0].orientation.w=1;synthetic.bones[0].position.z=2;
    synthetic.points.resize(1);synthetic.local_points.resize(1);synthetic.bone_weight_spans.push_back({0,1,0,0});synthetic.bone_weights.push_back({0,65535});
    wand::Hp1Animation sample;sample.status=wand::Hp1ProfileStatus::ok;sample.bones.resize(1);sample.bones[0].name="root";
    sample.sequences.resize(1);sample.moves.resize(1);auto& sm=sample.moves[0];sm.track_time=1;sm.bone_indices={0};sm.tracks.resize(1);
    sm.tracks[0].time_count=2;sm.tracks[0].time_scale=.5F;sm.tracks[0].position_count=2;sm.tracks[0].position_scale=10;
    sample.compressed_time_keys={0,1};sample.compressed_position_keys={{{0,0,0}},{{0,0,32767}}};
    auto endpoint=wand::sample_hp1_skeletal_animation(synthetic,sample,0,5,false);
    Check(endpoint.status==wand::Hp1ProfileStatus::ok&&std::abs(endpoint.points[0].z-10)<.001F,"one-shot holds last key without wrap");
    auto loop=wand::sample_hp1_skeletal_animation(synthetic,sample,0,1);
    Check(loop.status==wand::Hp1ProfileStatus::ok&&std::abs(loop.points[0].z)<.001F,"loop default unchanged");
    auto stable=wand::sample_hp1_skeletal_animation(synthetic,sample,0,.5F,true,true);
    Check(stable.status==wand::Hp1ProfileStatus::ok&&std::abs(stable.points[0].z-2)<.001F,"root height held while sampling limbs");
    std::cout<<"C27_SAMPLER=PASS one_shot=CLAMPED root_height=STABLE\n";
    if(argc==1)return 0;
    if(argc!=2)return 2;const std::filesystem::path root(argv[1]);
    for(int ref:{1341,494,558,623,152}){
        const auto package=root/(ref==152?"system/HProps.u":"system/HarryPotter.u");
        const auto skin=wand::load_hp1_skeletal_skin(package,ref);
        const auto anim=wand::load_hp1_animation(package,skin.census.animation_reference);
        Check(skin.status==wand::Hp1ProfileStatus::ok&&anim.status==wand::Hp1ProfileStatus::ok,"animation inputs");
        for(std::size_t i=0;i<anim.sequences.size();++i){
            auto name=AsciiFold(anim.sequences[i].name);
            if(name!="run"&&name!="walk"&&name!="trot"&&name!="idle2lookright"&&name!="lookright")continue;
            std::cout<<"CLIP mesh="<<ref<<" name="<<name<<" frames="<<anim.sequences[i].frame_count<<" duration="<<anim.moves[i].track_time<<'\n';
            const auto& move=anim.moves[i];
            for(unsigned b=0;b<3;++b){const auto ti=move.bone_indices[b];if(ti<0)continue;const auto& tr=move.tracks[ti];
                float low=1000,high=-1000;
                for(unsigned k=0;k<tr.position_count;++k){auto v=wand::decode_hp1_animation_position_key(anim.compressed_position_keys[tr.position_offset+k],tr.position_scale);low=std::min(low,v.z);high=std::max(high,v.z);}
                std::cout<<"BONE "<<skin.bones[b].name<<" z_range="<<low<<','<<high<<" bind_z="<<skin.bones[b].position.z<<'\n';}
            float oldlo=1000,oldhi=-1000,newlo=1000,newhi=-1000;
            for(unsigned f=0;f<=12;++f){auto pose=wand::sample_hp1_skeletal_animation(skin,anim,i,move.track_time*f/12);
                Check(pose.status==wand::Hp1ProfileStatus::ok,"sample");float lo=1000,hi=-1000;
                for(const auto& v:pose.points){lo=std::min(lo,v.z);hi=std::max(hi,v.z);}
                auto fixed=wand::sample_hp1_skeletal_animation(skin,anim,i,move.track_time*f/12,true,true);
                float top=-1000;for(const auto& v:fixed.points)top=std::max(top,v.z);
                oldlo=std::min(oldlo,hi);oldhi=std::max(oldhi,hi);newlo=std::min(newlo,top);newhi=std::max(newhi,top);}
            std::cout<<"VERTICAL mesh="<<ref<<" old_span_m="<<(oldhi-oldlo)*.02F<<" stabilized_span_m="<<(newhi-newlo)*.02F<<'\n';
            if(ref==1341||ref==494)Check(newhi-newlo<oldhi-oldlo,"run body oscillation not reduced");
        }
    }
    const auto map=root/"Maps/Lev_Tut1.unr";const auto census=wand::inspect_hp1_actor_visuals(map);
    hpvr_hp1_player_start_report start{};Check(hpvr_hp1_load_player_start_utf8(map.string().c_str(),kMetersPerUnrealUnit,0,&start)==0,"start");
    const float yaw=start.rotation_units[1]*kTau/65536.0F;
    for(const auto* name:{"cutscene52","cutscene54","cutscene55","cutscene56","cutscene1"}){IntroCutscene cut;Check(LoadIntroCutscene(census,start,yaw,&cut,name),"cutscene");
        std::cout<<"TRIGGER "<<name<<" radius="<<cut.trigger_radius<<" pos="<<cut.trigger_position[0]<<','<<cut.trigger_position[1]<<','<<cut.trigger_position[2]<<'\n';
        for(const auto& tr:cut.tracks)if(!tr.camera)std::cout<<"CAST "<<tr.alias<<" ref="<<tr.actor_reference<<" xyz="<<tr.position[0]<<','<<tr.position[1]<<','<<tr.position[2]<<'\n';}
    std::array<IntroCutscene,4> stories;
    unsigned index=0;for(const auto* name:{"cutscene56","cutscene1","cutscene58","cutscene59"})Check(LoadIntroCutscene(census,start,yaw,&stories[index++],name),"story metadata");
    const std::array<float,3> saved_head{58.66809F,25.17321F,-107.21909F};
    Check(SelectStoryEncounter(12,stories,saved_head)==1,"entrance triggers Draco without requiring Filch");
    Check(SelectStoryEncounter(14,stories,saved_head)==1,"Draco arrival zone");
    auto wrong_floor=saved_head;wrong_floor[1]-=5;
    Check(!SelectStoryEncounter(12,stories,wrong_floor),"no trigger through another floor");
    Check(!SelectStoryEncounter(16,stories,saved_head),"Draco must not replay");
    std::cout<<"C27_STORY=PASS old_save=DRACO no_replay=YES floor_gate=YES\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
