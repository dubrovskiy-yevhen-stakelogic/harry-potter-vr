#include "hpvr/quest_ambient_effects.h"
#include "hpvr/quest_fixture_effects.h"
#include "hpvr/hp1_gesture.h"
#include "hpvr/hp1_gesture_c.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace wand=hpvr::wand;
constexpr float kMetersPerUnrealUnit=.02F,kTau=6.283185307179586F;
std::string AsciiFold(std::string_view text){
    std::string result(text);for(auto& c:result)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return result;
}
std::array<float,3> RotateYaw(const std::array<float,3>& p,float yaw){
    const float c=std::cos(yaw),s=std::sin(yaw);return {c*p[0]+s*p[2],p[1],-s*p[0]+c*p[2]};
}
std::array<float,3> ActorLocalPosition(const wand::Hp1ActorVisual& actor,const hpvr_hp1_player_start_report& start,float yaw){
    return RotateYaw({actor.location_unreal.y*kMetersPerUnrealUnit-start.position_m[0],
        actor.location_unreal.z*kMetersPerUnrealUnit-start.position_m[1],
        -actor.location_unreal.x*kMetersPerUnrealUnit-start.position_m[2]},yaw);
}
struct FlameEmitter{std::array<float,3> position{};float phase=0,scale=.32F;};
struct GlowEmitter{std::array<float,3> position{};float radius=.25F,intensity=.5F,phase=0;};
struct PreparedGeometry{std::vector<FlameEmitter> flames;std::vector<GlowEmitter> glows;};
#include "../../../android/app/src/main/cpp/quest_ambient_restore.inl"

void Check(bool value){if(!value)throw std::runtime_error("Ambient particle test failed");}
wand::Hp1ClassDefaultProperty Range(std::string name,float base,float range){
    wand::Hp1ClassDefaultProperty p;p.name=name;p.structure_name="FloatParams";p.value.resize(8);
    std::memcpy(p.value.data(),&base,4);std::memcpy(p.value.data()+4,&range,4);return p;
}
wand::Hp1ActorVisual Actor(std::int32_t reference,bool glow){
    wand::Hp1ActorVisual actor;actor.actor_reference=reference;actor.location_serialized=true;
    actor.qualified_class_name="HPParticle.Fire01";actor.rotation_units={16368,0,0};
    actor.serialized_properties={Range("SourceWidth",150,0),Range("SourceHeight",200,0),
        Range("Speed",5,5),Range("Lifetime",1,1),Range("SizeWidth",4,10),
        Range("SizeEndScale",-1,4),Range("ParticlesPerSec",20,5),Range("AlphaStart",1,.5F)};
    wand::Hp1ClassDefaultProperty color;color.name="ColorStart";color.structure_name="ColorParams";
    color.value={105,90,241,0,0,0,0,0};actor.serialized_properties.push_back(color);
    color.name="ColorEnd";color.value={169,74,255,0,0,0,0,0};actor.serialized_properties.push_back(color);
    if(glow){wand::Hp1ClassDefaultProperty p;p.name="Textures";p.object_reference_serialized=true;p.object_reference=-122;
        p.object_path={"HPParticle","particle_fx","glow00"};actor.serialized_properties.push_back(p);}
    return actor;
}
void AddLegacyEffects(PreparedGeometry& g,const wand::Hp1ActorVisual& actor,const hpvr_hp1_player_start_report& start,float yaw){
    const auto p=ActorLocalPosition(actor,start,yaw);
    g.flames.push_back({p,float(actor.actor_reference&255)*.071F,2.4F});
    g.glows.push_back({p,.34F,.55F,float(actor.actor_reference&255)*.043F});
}
void UnitTests(){
    wand::Hp1ActorVisualCensus census;census.status=wand::Hp1ProfileStatus::ok;
    census.actors={Actor(17,true),Actor(29,false)};
    hpvr_hp1_player_start_report start{};PreparedGeometry geometry;
    for(const auto& actor:census.actors)AddLegacyEffects(geometry,actor,start,0);
    geometry.flames.push_back({{0,0,0},17*.071F,.32F});
    geometry.glows.push_back({{0,0,0},.36F,.62F,17*.043F});
    const auto emitters=RestorePreparedAmbientParticles(geometry,census,start,0);
    Check(emitters.size()==1&&geometry.flames.size()==2&&geometry.glows.size()==2);
    const auto& e=emitters[0];Check(e.actor_reference==17&&e.source_width_m==3&&e.source_height_m==4);
    Check(std::abs(e.speed_mps-.1F)<1e-6F&&e.lifetime==1&&e.lifetime_range==1);
    Check(e.direction[1]>.999F&&std::abs(e.source_up[1])<.002F);
    Check(e.color_start[2]>.94F&&e.color_start[0]<.42F&&e.color_end[2]==1);
    Check(RestorePreparedAmbientParticles(geometry,census,start,0).size()==1&&geometry.flames.size()==2);
    std::array<AmbientParticle,64> a{},b{};
    for(const float time:{0.0F,.99F,1.0F,6.0F,12.0F,60.0F,1000000.0F}){
        const auto count=BuildAmbientParticles(emitters,time,{0,0,0},a.data(),a.size());
        Check(count==16&&BuildAmbientParticles(emitters,time,{0,0,0},b.data(),b.size())==count);
        for(std::size_t i=0;i<count;++i){
            Check(a[i].position==b[i].position&&a[i].color==b[i].color&&a[i].size_m==b[i].size_m);
            Check(a[i].size_m>0&&a[i].size_m<=1&&a[i].color[3]>=0&&a[i].color[3]<=1);
            Check(std::abs(a[i].position[0])<=1.501F&&std::abs(a[i].position[2])<=2.001F);
            for(float value:a[i].position)Check(std::isfinite(value));
            Check(a[i].color[2]>a[i].color[0]&&a[i].color[2]>a[i].color[1]);
        }
    }
    Check(BuildAmbientParticles(emitters,1,{0,0,0},a.data(),3)==3);
    Check(BuildAmbientParticles(emitters,1,{100,0,0},a.data(),a.size())==0);
    Check(BuildAmbientParticles(emitters,-1,{0,0,0},a.data(),a.size())==0);
    Check(BuildAmbientParticles(emitters,std::numeric_limits<float>::quiet_NaN(),{0,0,0},a.data(),a.size())==0);
    Check(BuildAmbientParticles(emitters,1,{0,0,0},nullptr,64)==0);
    auto invalid=emitters;invalid[0].lifetime=std::numeric_limits<float>::infinity();
    Check(BuildAmbientParticles(invalid,1,{0,0,0},a.data(),a.size())==0);
    invalid=emitters;invalid[0].source_right[0]=std::numeric_limits<float>::quiet_NaN();
    Check(BuildAmbientParticles(invalid,1,{0,0,0},a.data(),a.size())==0);
    census.actors[0].hidden=true;
    Check(RestorePreparedAmbientParticles(geometry,census,start,0).empty());
    census.actors[0].hidden=false;census.actors[0].serialized_properties.back().array_index=1;
    Check(RestorePreparedAmbientParticles(geometry,census,start,0).empty());
    census.actors[0].serialized_properties.back().array_index=-1;
    census.actors[0].serialized_properties.back().object_path={"HPParticle","particle_fx","other"};
    Check(RestorePreparedAmbientParticles(geometry,census,start,0).empty());
    census.actors={Actor(17,true)};census.actors[0].serialized_properties[0]=Range("SourceWidth",std::numeric_limits<float>::quiet_NaN(),0);
    Check(RestorePreparedAmbientParticles(geometry,census,start,0)[0].source_width_m==0);
    census.status=wand::Hp1ProfileStatus::invalid_profile;
    Check(RestorePreparedAmbientParticles(geometry,census,start,0).empty());
}
int main(int argc,char** argv){
    UnitTests();
    // Optional read-only check against a locally owned installation. No asset
    // output is produced and automated tests do not require retail files.
    if(argc==2){
        const std::filesystem::path root=argv[1];
        const auto census=wand::inspect_hp1_actor_visuals(root/"Maps/Lev_Tut1b.unr");
        Check(census.status==wand::Hp1ProfileStatus::ok);
        PreparedGeometry geometry;hpvr_hp1_player_start_report start{};
        for(const auto& actor:census.actors)
            if(AsciiFold(actor.qualified_class_name)=="hpparticle.fire01"&&actor.location_serialized)AddLegacyEffects(geometry,actor,start,0);
        Check(geometry.flames.size()==15);
        const auto emitters=RestorePreparedAmbientParticles(geometry,census,start,0);
        Check(emitters.size()==13&&geometry.flames.size()==2&&geometry.glows.size()==2);
        const auto candle=wand::build_hp1_skeletal_triangle_mesh(root/"System/HProps.u",1374,kMetersPerUnrealUnit);
        Check(candle.status==wand::Hp1ProfileStatus::ok);
        hpvr::quest::fixtures::PlainCandleMeshPatch patch;
        Check(hpvr::quest::fixtures::IdentifyPlainCandle(candle.vertices.data(),candle.vertices.size(),[](const auto& v){
            return std::array<float,3>{v.position_m.x,v.position_m.y,v.position_m.z};
        },&patch));
        std::cout<<"OWNED_READ_ONLY: ambient=13 genuine_fire=2 PlainCandle_faces=8 wick_y="<<patch.wick[1]<<"\n";
    }
    std::cout<<"PASS: authored ambient descriptors, exact fire filtering, deterministic bounded particles, color and invalid inputs\n";
}
