#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <chrono>
#include <iostream>
using namespace hpvr::quest;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    const auto dir=std::filesystem::temp_directory_path()/("hpvr-c32-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    VrSettings v;Check(ReadVrSettings(dir).render_scale==100,"settings default");
    v.render_scale=125;v.ssr=0;Check(WriteVrSettings(dir,v),"write settings");
    auto r=ReadVrSettings(dir);Check(r.render_scale==125&&r.ssr==0,"round trip");
    v.render_scale=75;v.ssr=50;Check(WriteVrSettings(dir,v),"second bank");
    {std::ofstream f(dir/"vr-settings.0");f<<"torn write";}
    r=ReadVrSettings(dir);Check(r.render_scale==125&&r.ssr==0,"recover older bank");
    VrMenuChord chord;
    Check(!chord.Update(true,true,0,1)&&!chord.consumed,"menu alone remains game menu");
    Check(!chord.Update(true,true,1,1),"adding grips to held menu never reopens");
    chord.Update(true,false,1,1);Check(chord.Update(true,true,1,1),"two grips plus menu opens once");
    Check(!chord.Update(true,true,0,0)&&chord.consumed,"release grips cannot leak game menu");
    Check(!chord.Update(false,true,1,1)&&!chord.consumed,"focus loss clears chord");
    QuestFrontEnd front;front.saves=dir/"SaveGames";front.screen=FrontScreen::Game;front.progress.quest_stage=10;
    const int initial_ssr=front.vr.ssr;
    front.ToggleVrMenu();Check(front.Visible()&&front.screen==FrontScreen::Vr,"VR menu pauses gameplay");
    front.Input(0,false,false,0);front.Input(0,false,false,1);Check(front.vr.render_scale==105,"scale increases once");
    front.Input(0,false,false,1);Check(front.vr.render_scale==105,"held stick never repeats");
    front.Input(0,false,false,0);front.Input(-1,false,false,0);front.Input(0,false,false,0);
    front.Input(0,false,false,-1);Check(front.vr.ssr==initial_ssr-5&&front.vr.render_scale==105,"SSR decreases one step independently of render scale");
    front.Input(0,false,true);Check(front.screen==FrontScreen::Game&&front.progress.quest_stage==10,"close restores game without save mutation");
    Check(!std::filesystem::exists(front.saves),"graphics settings do not create save slot");
    Check(VrRenderExtent(2000,50,2500)==1000&&VrRenderExtent(2000,125,2400)==2400,"bounded live extent");
    Check(IsReflectiveWoodFloor("woodredfloor_3",1)&&!IsReflectiveWoodFloor("woodredfloor_3",0)&&!IsReflectiveWoodFloor("rug_01_b",1),"material and plane gating");
    Matrix4 vp{},inv{};ViewPose pose;pose.position={3,2,-5};pose.orientation={0,.258819F,0,.965926F};
    Check(BuildViewProjection(pose,{-.8F,.7F,-.75F,.8F},0,.05F,200,&vp)&&InvertReflectionMatrix(vp,inv),"inverse VR projection");
    const auto identity=MultiplyMatrices(vp,inv);
    for(int i=0;i<16;++i)Check(std::abs(identity[i]-(i%5==0?1:0))<.001F,"projection inverse roundtrip");
    for(int i=0;i<3;++i)Check(std::abs(inv[8+i]/inv[11]-pose.position[i])<.001F,"history eye recovered exactly");
    Check(!InvertReflectionMatrix({},inv),"reject singular history");
    ProgressSave progress;Check(ApplyFirstPeevesContact(progress)&&progress.health==90,"five out of fifty damage");
    Check(!ApplyFirstPeevesContact(progress)&&progress.health==90,"first strike idempotent");
    front.progress.health=90;Check(front.HudQuads(0,false).size()==2,"health uses only owned lightning art");
    std::filesystem::remove(dir/"vr-settings.0");std::filesystem::remove(dir/"vr-settings.1");std::filesystem::remove(dir);
    std::cout<<"C32_POLICY=PASS menu_chord=YES live_scale=YES settings_recovery=YES reflection_math=YES damage_percent=10\n";
    if(argc==1)return 0;if(argc!=2)return 2;
    const std::filesystem::path root(argv[1]),map=root/"Maps/Lev_Tut1.unr";
    const auto census=hpvr::wand::inspect_hp1_actor_visuals(map);
    unsigned desks=0;for(const auto& a:census.actors)if(AsciiFold(a.qualified_class_name)=="hprops.transtrestletable"){++desks;Check(a.actor_reference==3513,"authored teacher table");}
    Check(desks==1,"one teacher table in owned map");
    Check(LoadFrontAssets(root,&front.assets),"owned HUD");
    const auto& full=front.assets.textures.at(front.assets.health_full).rgba;
    front.progress.health=90;const auto quad=front.HudQuads(0,false).at(1);
    unsigned changed=0;for(unsigned y=0;y<static_cast<unsigned>(quad.v*256);++y)for(unsigned x=0;x<256;++x)changed+=full[(y*256+x)*4+3]>=128;
    Check(changed>0,"ten percent damage removes visible bolt pixels");
    std::cout<<"C32_OWNED=PASS teacher_tables="<<desks<<" damaged_bolt_texels="<<changed<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
