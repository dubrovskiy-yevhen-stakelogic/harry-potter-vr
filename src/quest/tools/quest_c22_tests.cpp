#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
using namespace hpvr::quest;
int main(){
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    try{
        BasicCast cast;const std::array<float,3> tip{0,0,0},hit{0,0,-5},moved{4,0,-2};
        check(!cast.Observe(true,true,tip,hit,0.01F)&&!cast.charging,"menu trigger must release first");
        cast.Observe(true,false,tip,hit,0.01F);
        check(!cast.Observe(true,true,tip,hit,0.01F)&&cast.charging,"charge reticle");
        check(cast.Observe(true,false,tip,moved,0.01F)&&cast.destination==hit,"release uses displayed reticle");
        check(!cast.Observe(true,false,tip,moved,0.01F),"one cast per release");
        for(unsigned i=0;i<35;++i)cast.Observe(true,false,tip,hit,0.01F);
        check(!cast.flying,"short air effect expires");
        cast.Observe(true,true,tip,hit,0.01F);
        cast.Observe(false,true,tip,hit,0.01F);
        check(!cast.charging&&!cast.flying,"tracking loss cancels");
        check(!cast.Observe(true,false,tip,hit,0.01F),"tracking return cannot cast");
        CollisionTriangle near;near.vertices={{{-10,-10,-2},{10,-10,-2},{0,10,-2}}};
        CollisionTriangle far=near;for(auto& v:far.vertices)v[2]=-5;
        check(std::abs(BasicRayDistance({far,near},tip,{0,0,-1})-2)<1e-5F,"nearest wall occludes farther target");
        std::swap(near.vertices[1],near.vertices[2]);
        check(std::abs(BasicRayDistance({near},tip,{0,0,-1})-2)<1e-5F,"two sided world picking");
        check(BasicRayDistance({far},tip,{0,0,1})==24,"empty space is a valid basic cast");
        QuestFrontEnd front;front.assets.story.resize(14);
        for(unsigned i=0;i<49;++i)front.assets.textures.push_back({"test",std::vector<std::uint8_t>(256*256*4,255)});
        std::vector<GpuVertex> vertices;std::vector<std::uint8_t> textures;
        std::uint32_t layers=0,count=0;std::map<std::string,FrontDrawRange> ranges;
        check(AppendFrontGeometry(front,vertices,textures,layers,ranges,count),"UI baking");
        for(const auto& v:vertices)check(v.position[2]==0,"no microscopic depth offsets in UI");
        front.screen=FrontScreen::Pause;
        for(unsigned stage=0;stage<=20;++stage){front.progress.quest_stage=stage;
            check(ranges.contains(front.DrawKey()),"every saved quest objective has UI geometry");}
        for(unsigned n=0;n<=128;++n)check(ranges.contains("beans_"+std::to_string(n)),"bean count geometry");
        std::cout<<"C22_TESTS=PASS basic_input=PASS world_pick=PASS ui_layers=PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
