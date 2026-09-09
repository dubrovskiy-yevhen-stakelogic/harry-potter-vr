#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
using namespace hpvr::quest;
static void Check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
int main(int argc,char**argv){try{
 std::array<IntroCutscene,4> stories;
 for(unsigned i=0;i<4;++i){stories[i].trigger_position={float(i)*20,0,0};stories[i].trigger_radius=2;}
 Check(SelectStoryEncounter(12,stories,{20,kPlayerEyeHeightMeters,0})==1,"Draco first at entrance");
 Check(!SelectStoryEncounter(12,stories,{0,kPlayerEyeHeightMeters,0}),"Filch cannot preempt Draco");
 Check(SelectStoryEncounter(16,stories,{0,kPlayerEyeHeightMeters,0})==0,"Filch approach after Draco");
 Check(!SelectStoryEncounter(16,stories,{0,kPlayerEyeHeightMeters,0},true),"Filch cutscene once, barks separate");
 Check(SelectStoryEncounter(18,stories,{0,kPlayerEyeHeightMeters,0})==0,"Filch remains available after Hermione");
 auto wrong=std::array<float,3>{20,-5,0};Check(!SelectStoryEncounter(12,stories,wrong),"story floor gate");
 std::cout<<"C29_POLICY=PASS order=DRACO_THEN_OPTIONAL_FILCH\n";
 if(argc==1)return 0;if(argc!=2)return 2;const std::filesystem::path root(argv[1]);
 for(int ref:{1495,1556}){LoadedStaticMesh mesh;if(!LoadStaticMesh((root/"system/HProps.u").string(),ref,0,&mesh))return 3;
  std::map<std::pair<unsigned,unsigned>,std::array<float,2>> spans;
  for(const auto& v:mesh.vertices){auto key=std::make_pair(v.polygon_flags,v.texture_layer);if(!spans.contains(key))spans[key]={1000,-1000};
   auto& span=spans[key];span[0]=std::min(span[0],v.position_m[1]);span[1]=std::max(span[1],v.position_m[1]);}
  for(const auto& [key,span]:spans)std::cout<<"CANDLE mesh="<<ref<<" flags="<<key.first<<" layer="<<key.second<<" y="<<span[0]<<','<<span[1]<<'\n';
 }
 const auto skin=hpvr::wand::load_hp1_skeletal_skin(root/"system/HarryPotter.u",1794);
 const auto anim=hpvr::wand::load_hp1_animation(root/"system/HarryPotter.u",skin.census.animation_reference);
 for(const auto& s:anim.sequences)std::cout<<"PEEVES_CLIP="<<s.name<<'\n';
 const auto scene=hpvr::wand::build_hp1_textured_bsp_scene(root,root/"Maps/Lev_Tut1.unr",.02F,kMaximumTriangles);
 Check(scene.status==hpvr::wand::Hp1ProfileStatus::ok,"owned BSP decode");
 std::map<std::pair<unsigned,unsigned>,unsigned> materials;
 for(const auto& v:scene.vertices)if((v.polygon_flags&~(0x1000000U|0x400000U))!=0)++materials[{v.polygon_flags,v.texture_layer}];
 for(const auto& [key,count]:materials){unsigned black=0,alpha=0;const auto offset=std::size_t(key.second)*scene.texture_layer_width*scene.texture_layer_height*4;
  for(std::size_t i=offset;i<offset+std::size_t(scene.texture_layer_width)*scene.texture_layer_height*4;i+=4){if(scene.texture_rgba8[i]<8&&scene.texture_rgba8[i+1]<8&&scene.texture_rgba8[i+2]<8)++black;if(scene.texture_rgba8[i+3]<128)++alpha;}
  if(key.first&2U)Check(alpha>0,"masked material must decode palette alpha");
  std::cout<<"BSP flags="<<key.first<<" layer="<<key.second<<" vertices="<<count<<" black="<<black<<" alpha="<<alpha<<'\n';}
 const auto map=root/"Maps/Lev_Tut1.unr";const auto census=hpvr::wand::inspect_hp1_actor_visuals(map);
 hpvr_hp1_player_start_report start{};Check(hpvr_hp1_load_player_start_utf8(map.string().c_str(),.02F,0,&start)==0,"owned start");
 for(const auto name:{"cutscene5","cutscene6"}){IntroCutscene cut;Check(LoadIntroCutscene(census,start,start.rotation_units[1]*kTau/65536.0F,&cut,name),"missing twins scene");
  std::set<std::string> cues,waits;unsigned moves=0;
  for(const auto& t:cut.tracks)for(const auto& line:t.commands){auto cmd=AsciiFold(line);
   if(cmd.starts_with("cue "))cues.insert(cmd.substr(4));if(cmd.starts_with("waitfor "))waits.insert(cmd.substr(8));if(cmd.starts_with("moveto "))++moves;}
  for(const auto& wait:waits)Check(cues.contains(wait),"unresolved departure cue");Check(moves>=4,"twins paths absent");
  std::cout<<"C29_SCENE="<<name<<" moves="<<moves<<" cues=RESOLVED\n";
 }
 FrontAssets assets;Check(LoadFrontAssets(root,&assets),"owned audio");
 for(const auto name:{"111Peeves1","fred_george_new_135","fred_george_new_133"})Check(GameplayDialogueIndex(assets,name).has_value(),"Peeves/departure voice absent");
 std::cout<<"C29_OWNED=PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<"C29_FAIL="<<e.what()<<'\n';return 1;}}
