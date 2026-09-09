#include "hpvr/hp1_gesture.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
int main(int argc,char** argv) {
 if(argc<2)return 2;
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
