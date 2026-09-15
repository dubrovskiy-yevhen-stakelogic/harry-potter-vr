#include "hpvr/quest_frontend.h"
#include "hpvr/quest_performance.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <string_view>
int main(int argc,char**argv){
 unsigned map_id=hpvr::quest::kIntroductionMapId;
 bool valid=argc==3;
 if(argc==4&&std::string_view(argv[3])=="--challenge"){
  map_id=hpvr::quest::kFlipendoChallengeMapId;valid=true;
 }
 if(argc==5&&std::string_view(argv[3])=="--map"){
  for(const auto& map:hpvr::quest::kQuestMaps)if(std::to_string(map.id)==argv[4]){map_id=map.id;valid=true;break;}
 }
 if(!valid){
  std::cerr<<"usage: hpvr_quest_frontend_probe <owned-root> <output> [--challenge | --map <0|1|2>]\n";return 2;
 }
 hpvr::quest::QuestFrontEnd f;
 if(!hpvr::quest::LoadFrontAssets(argv[1],&f.assets,map_id)){std::cerr<<f.assets.error;return 3;}
 f.progress.map_id=map_id;
 const std::filesystem::path output(argv[2]);std::filesystem::create_directories(output);
 std::ofstream plan(output/"audio-plan.tsv");
 auto emit=[&](const auto& source,bool stereo){
  auto key=hpvr::quest::AudioCacheName(source,stereo);
  std::ofstream out(output/(key+".mp2"),std::ios::binary);
  out.write(reinterpret_cast<const char*>(source.encoded_bytes.data()),source.encoded_bytes.size());
  plan<<key<<'\t'<<(stereo?2:1)<<'\n';
 };
 for(const auto&p:f.assets.story)emit(p.voice,false);
 for(const auto&m:f.assets.music)emit(m,true);
 for(const auto&m:f.assets.gameplay_audio)emit(m,false);
 auto image=[&](const std::string& name,bool hud=false){
  std::vector<unsigned char> pixels(640*480*3,0);
  auto quads=hud?f.HudQuads(14,true):f.Quads();
  if(hud){
   const auto badge=f.HousePointQuads();quads.insert(quads.end(),badge.begin(),badge.end());
   unsigned value=165,place=0;
   do{const auto digit=f.HousePointQuads(static_cast<int>(value%10),place++,3);quads.insert(quads.end(),digit.begin(),digit.end());value/=10;}while(value);
  }
  if(hud&&map_id==hpvr::quest::kBroomstickTrainingMapId){
   const auto labels=f.BroomLabelQuads();quads.insert(quads.end(),labels.begin(),labels.end());
   constexpr std::array<unsigned,3> sample{27,150,2};
   for(unsigned field=0;field<sample.size();++field){
    const auto digits=f.BroomNumberQuads(sample[field],field);quads.insert(quads.end(),digits.begin(),digits.end());
   }
  }
  if(f.screen==hpvr::quest::FrontScreen::Vr&&!hud)for(bool scale:{false,true}){
   const auto values=f.VrValueQuads(scale?f.vr.render_scale:f.vr.ssr,scale);quads.insert(quads.end(),values.begin(),values.end());}
  if(f.screen==hpvr::quest::FrontScreen::Debug&&!hud){
   // Layout-only preview. N/A is deliberate: these are not headset measurements.
   const auto lines=hpvr::quest::PerformanceLines({});
   for(unsigned row=0;row<lines.size();++row){float x=40;
    for(unsigned char ch:lines[row]){if(ch!=' ')quads.push_back({x,float(150+row*26),9,12.6F,
      float(ch%16*16+2)/256,float(ch/16*16+2)/256,5.0F/256,7.0F/256,f.assets.font,0xffffff});x+=10.8F;}
   }
  }
  for(const auto&q:quads){
   const auto&t=f.assets.textures.at(q.texture);
   for(int y=std::max(0,int(q.y));y<std::min(480,int(std::ceil(q.y+q.h)));++y)
    for(int x=std::max(0,int(q.x));x<std::min(640,int(std::ceil(q.x+q.w)));++x){
     auto tx=std::clamp(int((q.u+(x-q.x)/q.w*q.uw)*256),0,255);
     auto ty=std::clamp(int((q.v+(y-q.y)/q.h*q.vh)*256),0,255);
     auto src=(ty*256+tx)*4;if(t.rgba[src+3]<128)continue;
     for(int c=0;c<3;++c)pixels[(y*640+x)*3+c]=static_cast<unsigned char>(t.rgba[src+c]*((q.tint>>(8*c))&255)/255);
    }
  }
  std::ofstream out(output/name,std::ios::binary);out<<"P6\n640 480\n255\n";
  out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
 };
 image("menu.ppm");f.BeginStory(0);image("story-first.ppm");
 f.BeginStory(13);image("story-last.ppm");
 f.screen=hpvr::quest::FrontScreen::Pause;f.selection=3;image("book.ppm");
 f.screen=hpvr::quest::FrontScreen::Cards;f.selection=0;image("cards.ppm");
 f.card_page=6;image("secret-card.ppm");
 f.screen=hpvr::quest::FrontScreen::Report;image("report.ppm");
 f.screen=hpvr::quest::FrontScreen::Objective;image("objective.ppm");
 image("hud.ppm",true);
 f.screen=hpvr::quest::FrontScreen::Vr;f.selection=1;image("vr-settings.ppm");
 f.progress.health=90;image("hud-damaged.ppm",true);
 f.screen=hpvr::quest::FrontScreen::Debug;f.selection=0;image("debugger.ppm");
 f.debug_pinned=true;image("debugger-pinned.ppm");
 f.ShowDemoNotice(false);image("welcome.ppm");
 f.ShowDemoNotice(true);image("demo-end.ppm");
 f.screen=hpvr::quest::FrontScreen::Vr;f.selection=3;f.vr.relaxed_lesson=false;image("difficulty-original.ppm");
 f.vr.relaxed_lesson=true;image("difficulty-relaxed.ppm");
 f.selection=4;f.vr.first_person_cutscenes=false;image("camera-theatrical.ppm");
 f.vr.first_person_cutscenes=true;image("camera-harry.ppm");
 f.screen=hpvr::quest::FrontScreen::Controls;
 for(unsigned page=0;page<hpvr::quest::kControlsPageCount;++page){f.controls_page=page;image("controls-"+std::to_string(page)+".ppm");}
 f.screen=hpvr::quest::FrontScreen::Levels;f.selection=map_id;image("levels.ppm");
 std::cout<<"FRONT_ASSETS=PASS pages="<<f.assets.story.size()<<" music="<<f.assets.music.size()
          <<" textures="<<f.assets.textures.size()<<" map="<<map_id<<" voices="<<f.assets.gameplay_audio.size()<<"\n";
 for(const auto&p:f.assets.story)std::cout<<p.dialogue_name<<"\n";
 return 0;
}
