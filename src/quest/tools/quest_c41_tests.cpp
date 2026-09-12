#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <stdexcept>
#include <chrono>

using namespace hpvr::quest;
void Check41(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    const auto temporary=std::filesystem::temp_directory_path()/("hpvr-c41-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(temporary);
    VrSettings settings;Check41(settings.casting_mode==CastingMode::Classic&&!settings.voice_cast&&settings.refresh_rate==90,"classic casting default");
    settings.casting_mode=CastingMode::Gesture;settings.refresh_rate=120;
    Check41(WriteVrSettings(temporary,settings),"VR5 settings write");
    auto loaded=ReadVrSettings(temporary);
    Check41(loaded.casting_mode==CastingMode::Gesture&&!loaded.voice_cast&&loaded.refresh_rate==120,"VR5 settings round trip");
    settings.refresh_rate=75;Check41(!WriteVrSettings(temporary,settings),"reject invalid refresh rate");
    QuestFrontEnd front;front.saves=temporary/"saves";front.refresh_rates={0,72,90,120};
    front.screen=FrontScreen::Vr;front.paused=FrontScreen::Game;front.selection=5;
    front.Input(0,false,false);front.Input(0,true,false);
    Check41(front.vr.casting_mode==CastingMode::VisibleGesture,"visible gesture option");
    front.Input(0,false,false);front.Input(0,true,false);
    Check41(front.vr.casting_mode==CastingMode::Gesture,"gesture option");
    front.Input(0,false,false);front.Input(0,true,false);
    Check41(front.vr.casting_mode==CastingMode::Classic&&!front.vr.voice_cast,"classic option and independent voice default");
    front.selection=6;front.Input(0,false,false);front.Input(0,true,false);
    Check41(front.vr.refresh_rate==120,"cycle supported refresh modes");
    front.screen=FrontScreen::Main;front.selection=4;
    front.Input(0,false,false);front.Input(0,true,false);Check41(front.screen==FrontScreen::Levels,"level menu");
    front.selection=1;front.Input(0,false,false);front.Input(0,true,false);
    Check41(front.selected_map==1&&front.screen==FrontScreen::LevelSlots,"fresh second level slot selection");
    front.selection=2;front.Input(0,false,false);front.Input(0,true,false);
    Check41(front.slot==2&&front.screen==FrontScreen::LevelStart&&front.selection==1,"level reset requires explicit confirmation");
    front.selection=0;front.Input(0,false,false);
    Check41(front.Input(0,true,false)==FrontAction::StartSelectedLevel,"confirmed level reload action");
    BeanDraw bean;bean.source_actor=100;bean.emission={1,2,3};bean.position={3,1,3};bean.emission_time=0;
    Check41(BeanWorldPosition(bean)==bean.emission,"reward starts at pot");
    bean.emission_time=.5F;Check41(BeanWorldPosition(bean)[1]>1.5F,"reward ejects in an arc");
    bean.emission_time=1;Check41(BeanWorldPosition(bean)==bean.position,"reward settles at pickup position");
    std::vector<CharacterDraw> teachers(2);teachers[0].actor_reference=2047;teachers[1].actor_reference=2386;
    SetBridgeProfessor(teachers,false);Check41(teachers[0].enabled&&!teachers[1].enabled,"replacement teacher hidden before swap");
    SetBridgeProfessor(teachers,true);Check41(!teachers[0].enabled&&teachers[1].enabled,"only replacement remains after authored swap");
    ProgressSave progress;progress.map_id=1;progress.collected_beans={0x20000000+4658*16};RememberChallengeEvent(progress,4658);
    Check41(WriteProgress(temporary,0,&progress),"pot reward save");
    ProgressSave restored;Check41(ReadProgress(temporary,0,&restored)&&restored.collected_beans==progress.collected_beans&&ChallengeActivated(restored,4658),"pot rewards survive checkpoint");
    std::filesystem::remove_all(temporary);
    if(argc>1){
        const std::filesystem::path root=argv[1],map=root/"Maps/Lev_Tut1b.unr";
        hpvr::wand::Hp1PackageReadScope cache;
        const auto actors=hpvr::wand::inspect_hp1_actor_visuals(map);
        const auto manifest=hpvr::wand::build_hp1_character_manifest(root,map,0,{},true);
        for(const auto& actor:manifest.actors)if(AsciiFold(actor.qualified_class_name)=="harrypotter.savepoint")
            std::cout<<"SAVEBOOK ref="<<actor.actor_reference<<" mesh="<<actor.mesh_reference<<" package="<<actor.mesh_package.string()<<" scale="<<actor.draw_scale<<"\n";
        const auto table=hpvr::wand::inspect_hp1_package_link_table(root/"system/HProps.u");
        for(const auto& e:table.exports)if(!e.object_path.empty()&&e.qualified_class_name=="Core.Class"&&
            (AsciiFold(e.object_path.back()).find("vase")!=std::string::npos||AsciiFold(e.object_path.back())=="bronzecauldron")){
            auto d=hpvr::wand::inspect_hp1_class_visual_defaults(root/"system/HProps.u",e.reference);
            std::cout<<"CLASS "<<e.object_path.back()<<"\n";
                for(const auto& p:d.serialized_properties){std::cout<<p.name<<"="<<p.text_value<<" bytes=";
                for(auto b:p.value)std::cout<<unsigned(b)<<",";std::cout<<"\n";}
        }
        for(const auto& a:actors.actors)if(a.qualified_class_name=="Engine.Mover"){
            std::int32_t brush=0;for(const auto& p:a.serialized_properties)if(AsciiFold(p.name)=="brush")brush=p.object_reference;
            if(!brush)continue;
            auto m=hpvr::wand::build_hp1_textured_bsp_scene(root,map,.02F,4096,brush);
            for(std::size_t i=0;i<m.texture_layer_names.size();++i){const auto& name=m.texture_layer_names[i];
                if(name.find("grat")==std::string::npos&&name.find("gate")==std::string::npos&&name.find("bar")==std::string::npos)continue;
                std::set<unsigned> flags;unsigned transparent=0;
                for(const auto& v:m.vertices)if(v.texture_layer==i)flags.insert(v.polygon_flags);
                const auto size=std::size_t(m.texture_layer_width)*m.texture_layer_height;
                for(std::size_t j=0;j<size;++j)transparent+=m.texture_rgba8[(i*size+j)*4+3]==0;
                std::cout<<"GATE actor="<<a.object_name<<" texture="<<name<<" alpha0="<<transparent<<" flags=";
                if(transparent)Check41(std::ranges::all_of(flags,[](auto f){return (f&2U)!=0;}),"all faces of masked gate preserve holes");
                for(auto f:flags)std::cout<<f<<",";std::cout<<"\n";
            }
        }
    }
    std::cout<<"C41_TESTS=PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
