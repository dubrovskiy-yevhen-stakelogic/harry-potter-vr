#include "hpvr/hp1_gesture.h"
#include "hpvr/hp1_gesture_c.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace wand=hpvr::wand;
constexpr float kTau=6.28318530717958647692F;
constexpr std::uint32_t kPolyNotSolid=8;
using Point=std::array<float,3>;
Point SubtractVector(Point a,Point b){for(unsigned i=0;i<3;++i)a[i]-=b[i];return a;}
Point CrossVector(Point a,Point b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
bool NormalizeVector(Point p,Point* out){const float n=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);if(!std::isfinite(n)||n<1e-5F)return false;for(auto& x:p)x/=n;*out=p;return true;}
Point RotateYaw(Point p,float yaw){const float c=std::cos(yaw),s=std::sin(yaw);return {c*p[0]+s*p[2],p[1],-s*p[0]+c*p[2]};}
std::string AsciiFold(std::string text){for(auto& c:text)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return text;}
Point ActorLocalPosition(const wand::Hp1ActorVisual& a,const hpvr_hp1_player_start_report& start,float yaw){return RotateYaw({a.location_unreal.y*.02F-start.position_m[0],a.location_unreal.z*.02F-start.position_m[1],-a.location_unreal.x*.02F-start.position_m[2]},yaw);}
struct GpuVertex{float position[3]{},texture_uv[2]{},lightmap_uv[2]{};std::uint32_t texture_layer=2,polygon_flags=0,has_lightmap=0,packed_light=0x234567;};
struct CollisionTriangle{std::array<Point,3> vertices{};Point minimum{},maximum{},normal{};};
struct ChallengeProp{
 std::int32_t reference=0;std::string name;std::uint32_t first=0,count=3;Point minimum{},maximum{};
 bool breakable=false,cauldron=false;std::uint32_t animation_first=0,animation_frames=0,settled_first=0,settled_frames=0,broken_first=0,broken_count=0;
 bool cauldron_tip_valid=false;Point cauldron_mouth{},cauldron_direction{};
};
struct BeanDraw {std::int32_t source_actor=0;Point emission{},position{};unsigned emission_points=0;};
struct PreparedGeometry{
 bool prop_orientation_restored=false;
 std::vector<GpuVertex> vertices;std::vector<CollisionTriangle> collision,prop_aim;std::vector<ChallengeProp> challenge_props;
 std::vector<BeanDraw> beans;
 std::uint32_t map_vertices=0,fixture_vertices=0;
};
#include "../../../android/app/src/main/cpp/quest_prop_orientation_restore.inl"
void Check(bool b,const char* text){if(!b)throw std::runtime_error(text);}
void Near(Point a,Point b){for(unsigned i=0;i<3;++i)Check(std::abs(a[i]-b[i])<.00003F,"position mismatch");}
struct Fixture{PreparedGeometry g;wand::Hp1ActorVisualCensus census;hpvr_hp1_player_start_report start{};float yaw=0;std::vector<Point> expected;};
Fixture Make(float yaw,std::int32_t actor_yaw){
 Fixture f;f.yaw=yaw;f.start.position_m[0]=2;f.start.position_m[1]=-3;f.start.position_m[2]=5;
 f.census.status=wand::Hp1ProfileStatus::ok;
 const auto actor=[&](int ref,const char* name,Point location){wand::Hp1ActorVisual a;a.actor_reference=ref;a.qualified_class_name=name;a.location_serialized=true;a.location_unreal={location[0],location[1],location[2]};a.rotation_units[1]=actor_yaw;f.census.actors.push_back(a);};
 actor(1,"HProps.bronzecauldron",{700,800,200});actor(2,"HProps.FlipendoVaseBronze",{-900,-500,100});
 const auto triangle=[&](unsigned a,float shift,float extra){
  const auto origin=ActorLocalPosition(f.census.actors[a],f.start,yaw);const float angle=yaw+float(actor_yaw)*kTau/65536+extra;
  for(Point p:std::array<Point,3>{{{.2F,.1F,.3F},{.8F,.1F,.3F},{.2F,.8F,.9F}}}){
   p[0]+=shift;const auto current=RotateYaw(p,angle);p[2]=-p[2];
   p[0]=-p[0];
   const auto corrected=RotateYaw(p,yaw-float(actor_yaw)*kTau/65536-extra);
   GpuVertex v;for(unsigned k=0;k<3;++k){v.position[k]=origin[k]+current[k];p[k]=origin[k]+corrected[k];}v.texture_uv[0]=.17F;v.texture_uv[1]=.23F;
   f.g.vertices.push_back(v);f.expected.push_back(p);
  }
 };
 // Identical authored world/prop faces must not lead to BSP mutation.
 triangle(0,0,0);f.g.map_vertices=3;for(unsigned i=0;i<3;++i)f.expected[i]=prop_orientation_restore::Position(f.g.vertices[i]);
 ChallengeProp pot;pot.reference=1;pot.name="hprops.bronzecauldron";pot.cauldron=true;pot.first=3;
 triangle(0,0,0);pot.animation_first=6;pot.animation_frames=2;triangle(0,.1F,0);triangle(0,.2F,0);
 pot.settled_first=12;pot.settled_frames=1;triangle(0,.3F,0);f.g.challenge_props.push_back(pot);
 ChallengeProp vase;vase.reference=2;vase.name="hprops.flipendovasebronze";vase.breakable=true;vase.first=15;
 triangle(1,0,0);vase.broken_first=18;vase.broken_count=3;triangle(1,.2F,32000*kTau/65536);f.g.challenge_props.push_back(vase);
 // Nearby but different triangle is not selected by bounds or distance.
 triangle(0,.0002F,0);for(unsigned i=21;i<24;++i)f.expected[i]=prop_orientation_restore::Position(f.g.vertices[i]);
 f.g.fixture_vertices=21;
 const auto origin=ActorLocalPosition(f.census.actors[0],f.start,yaw);
 const auto legacy=[&](Point p){const auto d=RotateYaw(p,yaw+(float(actor_yaw)+5000)*kTau/65536);return Point{origin[0]+d[0],origin[1]+d[1],origin[2]+d[2]};};
 f.g.beans.push_back({1,legacy({0,-.4F,-1.28F}),legacy({.56F,-.1F,-2.5F}),0});
 const auto vase_origin=ActorLocalPosition(f.census.actors[1],f.start,yaw);
 const auto vase_legacy=[&](Point p){const auto d=RotateYaw(p,yaw+float(actor_yaw)*kTau/65536);return Point{vase_origin[0]+d[0],vase_origin[1]+d[1],vase_origin[2]+d[2]};};
 f.g.beans.push_back({2,vase_legacy({0,0,-.4F}),vase_legacy({0,0,-1.52F}),0});
 f.g.beans.push_back({99,{1,2,3},{4,5,6},0});
 for(std::size_t first=0;first<f.g.vertices.size();first+=3){CollisionTriangle t;t.vertices=prop_orientation_restore::Key(f.g.vertices,first);prop_orientation_restore::Refresh(t);f.g.collision.push_back(t);if(first>=3)f.g.prop_aim.push_back(t);}
 return f;
}
void Tests(){
 for(float yaw:{-2.9F,-.5F,0.F,1.7F})for(std::int32_t actor_yaw:{-32576,-16672,0,9584,16704,65535}){
  auto f=Make(yaw,actor_yaw);const auto before=f.g;
  const auto result=RestorePreparedPropOrientations(f.g,f.census,f.start,f.yaw);
  Check(result.valid,result.error);Check(result.props==2&&result.vertices==18,"wrong correction extent");
  Check(result.collision_faces==6&&result.aim_faces==6,"wrong collision correction extent");
  Check(result.beans==2,"wrong reward correction extent");
  const auto origin=ActorLocalPosition(f.census.actors[0],f.start,f.yaw);
  const auto original_vector=[&](Point converted){
   const float angle=(float(actor_yaw)+5000)*kTau/65536,c=std::cos(angle),s=std::sin(angle);
   const float ux=-converted[2],uy=converted[0];
   const auto world=RotateYaw({s*ux+c*uy,converted[1],-(c*ux-s*uy)},f.yaw);
   return Point{origin[0]+world[0],origin[1]+world[1],origin[2]+world[2]};};
  Near(f.g.beans[0].emission,original_vector({0,-.4F,-1.28F}));Near(f.g.beans[0].position,original_vector({.56F,-.1F,-2.5F}));
  const auto vase_origin=ActorLocalPosition(f.census.actors[1],f.start,f.yaw);
  const float va=float(actor_yaw)*kTau/65536;
  const auto vew=RotateYaw({.4F*std::sin(va),0,-.4F*std::cos(va)},f.yaw);
  Near(f.g.beans[1].emission,{vase_origin[0]+vew[0],vase_origin[1],vase_origin[2]+vew[2]});
  Check(f.g.beans[2].emission==before.beans[2].emission&&f.g.beans[2].position==before.beans[2].position,"unrelated reward changed");
  Check(f.g.vertices.size()==before.vertices.size()&&f.g.fixture_vertices==before.fixture_vertices,"ranges changed");
  for(std::size_t i=0;i<f.g.vertices.size();++i){Near(prop_orientation_restore::Position(f.g.vertices[i]),f.expected[i]);auto v=f.g.vertices[i];for(unsigned k=0;k<3;++k)v.position[k]=before.vertices[i].position[k];Check(std::memcmp(&v,&before.vertices[i],sizeof(v))==0,"vertex attributes changed");}
  Check(f.g.collision.front().vertices==before.collision.front().vertices,"BSP changed");
  Check(f.g.collision.back().vertices==before.collision.back().vertices,"nearby face changed");
  for(std::size_t i=1;i<7;++i)for(unsigned k=0;k<3;++k)Near(f.g.collision[i].vertices[k],f.expected[i*3+k]);
  for(const auto& prop:f.g.challenge_props)for(unsigned k=0;k<3;++k){const auto p=f.expected[prop.first+k];for(unsigned axis=0;axis<3;++axis)Check(p[axis]>=prop.minimum[axis]-.00003F&&p[axis]<=prop.maximum[axis]+.00003F,"bounds stale");}
  const auto corrected=f.g.vertices;const auto again=RestorePreparedPropOrientations(f.g,f.census,f.start,f.yaw);
  Check(again.valid&&again.already_applied,"repeat guard missing");Check(std::memcmp(corrected.data(),f.g.vertices.data(),corrected.size()*sizeof(GpuVertex))==0,"repeat changed geometry");
  Near(f.g.beans[0].emission,original_vector({0,-.4F,-1.28F}));
 }
 const auto reject=[](auto mutate){auto f=Make(.3F,9584);mutate(f);const auto before=f.g;const auto result=RestorePreparedPropOrientations(f.g,f.census,f.start,f.yaw);Check(!result.valid,"invalid input accepted");Check(!f.g.prop_orientation_restored,"failed correction marked done");Check(std::memcmp(before.vertices.data(),f.g.vertices.data(),before.vertices.size()*sizeof(GpuVertex))==0,"failed correction mutated vertices");Check(before.collision[1].vertices==f.g.collision[1].vertices,"failed correction mutated collision");};
 reject([](auto& f){f.census.actors.clear();});
 reject([](auto& f){f.census.actors.push_back(f.census.actors[0]);});
 reject([](auto& f){f.g.challenge_props[0].animation_frames=257;});
 reject([](auto& f){f.g.challenge_props[1].broken_first=4;});
 reject([](auto& f){f.g.challenge_props[1].broken_first=6;});
 reject([](auto& f){f.g.challenge_props[0].first=0;});
 reject([](auto& f){f.g.fixture_vertices=999999;});
 reject([](auto& f){f.g.vertices[6].position[0]=std::numeric_limits<float>::quiet_NaN();});
 reject([](auto& f){f.census.actors[0].location_unreal.x=std::numeric_limits<float>::infinity();});
 reject([](auto& f){f.g.collision[0].vertices[0][0]+=.1F;});
 reject([](auto& f){f.g.beans[0].position[0]=std::numeric_limits<float>::infinity();});
 reject([](auto& f){f.g.beans[0].emission_points=17;});
 auto empty=Make(0,0);empty.g.challenge_props.clear();const auto result=RestorePreparedPropOrientations(empty.g,empty.census,empty.start,0);Check(result.valid&&result.props==0,"unaffected map rejected");
}
void TipTopologyTests(){
 PreparedGeometry geometry;geometry.prop_orientation_restored=true;
 ChallengeProp prop;prop.cauldron=true;prop.first=0;prop.count=6;prop.settled_first=6;prop.settled_frames=1;
 for(bool posed:{false,true})for(unsigned band=0;band<2;++band)for(Point p:std::array<Point,3>{{{-.3F,0,0},{.3F,0,0},{0,0,.3F}}}){
  p[1]=band?0.F:1.F;if(posed){p[0]+=band?0.F:2.F;p[1]=.5F;}
  GpuVertex v;std::copy(p.begin(),p.end(),v.position);geometry.vertices.push_back(v);
 }
 const auto tip=PreparedCauldronTip(geometry,prop);Check(tip.valid,"actual rim topology not found");Near(tip.mouth,{2,.5F,.1F});Near(tip.direction,{1,0,0});
 prop.settled_first=10;Check(!PreparedCauldronTip(geometry,prop).valid,"out-of-bounds settled topology accepted");prop.settled_first=6;
 geometry.vertices[6].position[0]=std::numeric_limits<float>::quiet_NaN();Check(!PreparedCauldronTip(geometry,prop).valid,"nonfinite rim accepted");
}
int main(){try{Tests();TipTopologyTests();std::cout<<"PASS: scoped prop orientation, 24 yaw combinations, cauldron/vase source rewards, actual rim topology, all baked ranges, exact collision/aim copies, BSP isolation, bounds, repeat guard, fail-closed inputs\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
