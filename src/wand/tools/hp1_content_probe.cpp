#include "hpvr/hp1_gesture.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
int main(int argc,char** argv) {
 if(argc<2)return 2;
 if(argc==3 && std::string(argv[2])=="--navigation") {
  const auto navigation=hpvr::wand::inspect_hp1_navigation(argv[1]);
  if(navigation.status!=hpvr::wand::Hp1ProfileStatus::ok){std::cerr<<navigation.error;return 4;}
  for(std::size_t i=0;i<navigation.paths.size();++i){const auto& p=navigation.paths[i];
   std::cout<<i<<": "<<p.start<<" -> "<<p.end<<" distance="<<p.distance<<" radius="<<p.radius<<" height="<<p.height<<" flags="<<p.flags<<" pruned="<<p.pruned<<'\n';}
  return 0;
 }
 if(argc==3 && std::string(argv[2])=="--level-tail") {
  const auto level=hpvr::wand::inspect_hp1_level_handles(argv[1]);
  if(level.status!=hpvr::wand::Hp1ProfileStatus::ok){std::cerr<<level.error;return 4;}
  const auto payload=hpvr::wand::load_hp1_export_payload(argv[1],level.level_reference);
  if(payload.status!=hpvr::wand::Hp1ProfileStatus::ok)return 4;
  std::cout<<"offset="<<level.model_end_offset<<" size="<<payload.bytes.size()<<'\n';
  for(auto i=level.model_end_offset;i<std::min(payload.bytes.size(),level.model_end_offset+512);++i){
   if((i-level.model_end_offset)%16==0)std::cout<<'\n'<<std::dec<<i<<": ";
   std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(payload.bytes[i])<<' ';
  }
  return 0;
 }
 if(argc==4 && std::string(argv[2])=="--animation") {
  const auto animation=hpvr::wand::load_hp1_animation(argv[1],std::stoi(argv[3]));
  if(animation.status!=hpvr::wand::Hp1ProfileStatus::ok){std::cerr<<animation.error;return 4;}
  for(std::size_t i=0;i<animation.sequences.size();++i)
   std::cout<<"sequence="<<animation.sequences[i].name<<" duration="<<(i<animation.moves.size()?animation.moves[i].track_time:0)<<'\n';
  return 0;
 }
 if(argc==5 && std::string(argv[2])=="--payload") {
  auto p=hpvr::wand::load_hp1_export_payload(argv[1],std::stoi(argv[3]));
  if(p.status!=hpvr::wand::Hp1ProfileStatus::ok)return 4;
  std::ofstream out(argv[4],std::ios::binary);
  out.write(reinterpret_cast<const char*>(p.bytes.data()),p.bytes.size());
  return out.good()?0:5;
 }
 auto table=hpvr::wand::inspect_hp1_package_link_table(argv[1]);
 if(table.status!=hpvr::wand::Hp1ProfileStatus::ok){std::cerr<<table.error;return 3;}
 std::string filter=argc>2?argv[2]:"";
 for(const auto& e:table.exports) {
  std::string name;
  for(const auto& s:e.object_path){if(!name.empty())name+='.';name+=s;}
  auto lower=name;std::ranges::transform(lower,lower.begin(),[](unsigned char c){return std::tolower(c);});
  if(lower.find(filter)==std::string::npos)continue;
  std::cout<<"export="<<e.reference<<" class="<<e.qualified_class_name<<" name="<<name<<" bytes="<<e.serialized_bytes<<"\n";
  if(argc>3 && e.qualified_class_name=="Core.Class"){
   auto d=hpvr::wand::inspect_hp1_class_visual_defaults(argv[1],e.reference);
   std::cout<<" defaults_status="<<int(d.status)<<" error="<<d.error<<"\n";
   for(const auto& p:d.serialized_properties){
    std::cout<<"  "<<p.name<<"["<<p.array_index<<"] kind="<<int(p.kind)<<" text="<<p.text_value<<" ref="<<p.object_reference<<" path=";
    for(const auto& s:p.object_path)std::cout<<s<<".";
    std::cout<<" hex=";
    for(auto b:p.value)std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<int(b);
    std::cout<<std::dec<<"\n";
   }
  }
 }
 return 0;
}
