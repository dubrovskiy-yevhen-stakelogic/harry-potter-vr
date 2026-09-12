#include "hpvr/hp1_authored_environment.h"
#include "hpvr/hp1_package_linker.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace hpvr::wand;
void Check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
Hp1ClassDefaultProperty Byte(const char* name,std::uint8_t value) {
    Hp1ClassDefaultProperty property;property.name=name;property.kind=1;property.value={value};return property;
}
}

int main(int argc,char** argv){try{
    Hp1BspTopology top;top.status=Hp1ProfileStatus::ok;top.zone_count=3;top.zone_actor_references={0,6,168};
    Hp1BspNode node;node.plane={1,0,0,0};node.front_node_index=node.back_node_index=-1;node.zone_indices={2,1};top.nodes.push_back(node);
    Hp1ActorVisualCensus actors;actors.status=Hp1ProfileStatus::ok;
    Hp1ActorVisual courtyard;courtyard.actor_reference=6;courtyard.serialized_properties={
        Byte("AmbientBrightness",50),Byte("AmbientHue",160),Byte("AmbientSaturation",200)};
    actors.actors.push_back(courtyard);
    const auto expected=Hp1SerializedZoneAmbient(courtyard);
    Check(expected&&(*expected)[2]>(*expected)[0]&&(*expected)[0]>.15F,"authored courtyard ambient is cool, not warm fallback");
    const Hp1AuthoredZoneAmbient policy(top,actors,true);
    Check(policy.Sample({1,0,0})==expected,"visible-side courtyard receives authored ambient");
    Check(!policy.Sample({-1,0,0}),"sky zone without explicit ambient retains separate treatment");
    Check(policy.Sample({0,0,0},{1,0,0})==expected,"boundary texel samples visible side");
    Check(!policy.Sample({0,0,0},{-1,0,0}),"opposite side does not leak courtyard ambient");
    Check(!Hp1AuthoredZoneAmbient(top,actors,false).Sample({1,0,0}),"disabled policy leaves indoor maps unchanged");
    Check(!policy.Sample({std::numeric_limits<float>::infinity(),0,0}),"nonfinite sample rejected");
    courtyard.serialized_properties[0].value={0};
    Check(Hp1SerializedZoneAmbient(courtyard)==std::optional(std::array<float,3>{0,0,0}),"explicit zero is preserved");
    courtyard.serialized_properties[0].value.clear();
    Check(!Hp1SerializedZoneAmbient(courtyard),"malformed brightness rejected");
    top.nodes[0].front_node_index=0;
    Check(!Hp1AuthoredZoneAmbient(top,actors,true).Sample({1,0,0}),"cyclic BSP traversal bounded");
    if(argc>1) {
        Hp1PackageReadScope reads;const std::filesystem::path root=argv[1];
        const auto wet=load_hp1_p8_texture(root/"Textures/HP_Water.utx",4);
        Check(wet.status==Hp1ProfileStatus::ok&&!wet.rgba8.empty(),"owned fountain WetTexture resolves SourceTexture");
        const auto source1=load_hp1_p8_texture(root/"Textures/HP_Water.utx",2);
        const auto source2=load_hp1_p8_texture(root/"Textures/HP_Water.utx",3);
        Check(wet.rgba8==source1.rgba8||wet.rgba8==source2.rgba8,"water pixels come from an authored source, not substitute artwork");
        auto scene=build_hp1_textured_bsp_scene(root,root/"Maps/Lev_Tut2.unr",.02F,65536);
        if(scene.status!=Hp1ProfileStatus::ok)std::cerr<<scene.error<<'\n';
        Check(scene.status==Hp1ProfileStatus::ok&&scene.fallback_material_count==0&&scene.fallback_triangle_count==0,
              "all outdoor BSP materials resolve without magenta fallback");
        Check(scene.decoded_texture_count==23,"six authored sky faces and fountain water are present");
        std::cout<<"OWNED_BROOM_ENVIRONMENT=PASS textures="<<scene.decoded_texture_count<<" lightmaps="<<scene.decoded_lightmap_count<<'\n';
    }
    std::cout<<"AUTHORED_ENVIRONMENT_TESTS=PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
