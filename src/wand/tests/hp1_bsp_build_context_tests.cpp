#include "hpvr/hp1_package_linker.h"
#include <algorithm>
#include <bit>
#include <cctype>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <tuple>

namespace {
using namespace hpvr::wand;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
auto VectorKey(const Hp1BspVector& value){return std::tie(value.x,value.y,value.z);}
std::string Fold(std::string value){
    std::ranges::transform(value,value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return value;
}
struct RollingHash {
    std::uint64_t value=14695981039346656037ULL;
    void Byte(std::uint8_t byte){value^=byte;value*=1099511628211ULL;}
    void Integer(std::uint64_t integer){for(unsigned i=0;i<8;++i){Byte(static_cast<std::uint8_t>(integer&255U));integer>>=8U;}}
    void Float(float scalar){Integer(std::bit_cast<std::uint32_t>(scalar));}
    void Vector(const Hp1BspVector& vector){Float(vector.x);Float(vector.y);Float(vector.z);}
    void Bytes(const std::vector<std::uint8_t>& bytes){Integer(bytes.size());for(const auto byte:bytes)Byte(byte);}
    void String(const std::string& text){Integer(text.size());for(const auto c:text)Byte(static_cast<std::uint8_t>(c));}
    void Scene(const Hp1TexturedBspScene& scene){
        Integer(static_cast<std::uint64_t>(scene.status));String(scene.error);Integer(scene.vertices.size());
        for(const auto& vertex:scene.vertices){
            Vector(vertex.position_m);Vector(vertex.normal);
            for(const auto uv:vertex.texture_uv)Float(uv);
            for(const auto uv:vertex.lightmap_uv)Float(uv);
            Integer(vertex.texture_layer);Integer(vertex.polygon_flags);Integer(vertex.has_lightmap);
            Integer(vertex.node_index);Integer(vertex.surface_index);
        }
        Integer(scene.texture_layer_width);Integer(scene.texture_layer_height);Integer(scene.texture_layer_count);
        Integer(scene.texture_layer_names.size());for(const auto& name:scene.texture_layer_names)String(name);
        Bytes(scene.texture_rgba8);Integer(scene.lightmap_width);Integer(scene.lightmap_height);Bytes(scene.lightmap_rgba8);
        Integer(scene.decoded_lightmap_count);Integer(scene.lightmap_texel_count);Integer(scene.lightmap_light_count);
        Integer(scene.available_triangle_count);Integer(scene.selected_triangle_count);Integer(scene.omitted_triangle_count);
        Integer(scene.decoded_texture_count);Integer(scene.fallback_material_count);Integer(scene.fallback_triangle_count);
    }
};
void SameScene(const Hp1TexturedBspScene& a,const Hp1TexturedBspScene& b){
    Check(a.status==Hp1ProfileStatus::ok&&b.status==a.status,"both scene builders succeed");
    Check(a.vertices.size()==b.vertices.size(),"vertex count preserved");
    for(std::size_t i=0;i<a.vertices.size();++i){
        const auto& x=a.vertices[i];const auto& y=b.vertices[i];
        Check(VectorKey(x.position_m)==VectorKey(y.position_m)&&VectorKey(x.normal)==VectorKey(y.normal)&&
              std::tie(x.texture_uv,x.lightmap_uv,x.texture_layer,x.polygon_flags,x.has_lightmap,x.node_index,x.surface_index)==
              std::tie(y.texture_uv,y.lightmap_uv,y.texture_layer,y.polygon_flags,y.has_lightmap,y.node_index,y.surface_index),
              "every vertex attribute preserved exactly");
    }
    Check(std::tie(a.texture_layer_width,a.texture_layer_height,a.texture_layer_count,a.texture_layer_names,
                   a.texture_rgba8,a.lightmap_width,a.lightmap_height,a.lightmap_rgba8,a.decoded_lightmap_count,
                   a.lightmap_texel_count,a.lightmap_light_count,a.available_triangle_count,a.selected_triangle_count,
                   a.omitted_triangle_count,a.decoded_texture_count,a.fallback_material_count,a.fallback_triangle_count)==
          std::tie(b.texture_layer_width,b.texture_layer_height,b.texture_layer_count,b.texture_layer_names,
                   b.texture_rgba8,b.lightmap_width,b.lightmap_height,b.lightmap_rgba8,b.decoded_lightmap_count,
                   b.lightmap_texel_count,b.lightmap_light_count,b.available_triangle_count,b.selected_triangle_count,
                   b.omitted_triangle_count,b.decoded_texture_count,b.fallback_material_count,b.fallback_triangle_count),
          "all textures lightmaps flags and scene counters preserved exactly");
}
}

int main(int argc,char** argv){try{
    using namespace hpvr::wand;
    const Hp1BspBuildContext empty;
    Check(empty.status()!=Hp1ProfileStatus::ok&&!empty.error().empty(),"empty context rejected");
    const auto missing=std::filesystem::temp_directory_path()/
        ("hpvr-no-context-data-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto invalid=prepare_hp1_bsp_build_context(missing,missing/"no-map.unr");
    Check(invalid.status()!=Hp1ProfileStatus::ok&&!invalid.error().empty(),"missing dependency graph rejected");
    const auto invalid_build=build_hp1_textured_bsp_scene(missing,missing/"no-map.unr",.02F,4096,1,&empty);
    Check(invalid_build.status!=Hp1ProfileStatus::ok&&invalid_build.vertices.empty(),"empty context cannot build geometry");
    Check(build_hp1_textured_bsp_scene(missing,missing/"no-map.unr",.02F,0,1,&empty).error==
          "textured BSP triangle limit is zero","triangle limit validation retained");
    if(argc>1){
        const bool benchmark=argc>2&&std::string_view(argv[2])=="--benchmark-movers";
        Check(argc<=2||benchmark,"optional argument must be --benchmark-movers");
        const std::filesystem::path root=argv[1],map=root/"Maps/Lev_Tut1b.unr";
        Hp1PackageReadScope reads;
        const auto context_start=std::chrono::steady_clock::now();
        auto context=prepare_hp1_bsp_build_context(root,map);
        const double context_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-context_start).count();
        Check(context.status()==Hp1ProfileStatus::ok,"owned BSP context prepared");
        auto retained=context;context={};
        Check(retained.status()==Hp1ProfileStatus::ok,"immutable shared context lifetime");
        Check(build_hp1_textured_bsp_scene(root,root/"Maps/Lev_Tut1.unr",.02F,4096,0,&retained).error==
              "BSP build context belongs to a different data root or map","different map context rejected");
        Check(build_hp1_textured_bsp_scene(root/"Maps",map,.02F,4096,0,&retained).error==
              "BSP build context belongs to a different data root or map","different root context rejected");
        const auto census=inspect_hp1_actor_visuals(map);
        Check(census.status==Hp1ProfileStatus::ok,"owned actor census");
        std::vector<std::int32_t> brushes;
        for(const auto& actor:census.actors){
            const auto cls=Fold(actor.qualified_class_name);
            if(!cls.starts_with("engine.")||!cls.ends_with("mover"))continue;
            for(const auto& property:actor.serialized_properties)
                if(Fold(property.name)=="brush"&&property.object_reference_serialized&&property.object_reference>0)
                    brushes.push_back(property.object_reference);
        }
        Check(brushes.size()>=3,"owned mover brush samples available");
        const std::vector<std::int32_t> samples=benchmark?brushes:
            std::vector<std::int32_t>{0,brushes.front(),brushes[brushes.size()/2],brushes.back()};
        double independent_seconds=0,batch_seconds=0;
        RollingHash independent_hash,batch_hash;
        std::size_t completed=0;
        for(const auto brush:samples){
            const auto limit=benchmark?4096U:65536U;
            const auto start=std::chrono::steady_clock::now();
            const auto independent=build_hp1_textured_bsp_scene(root,map,.02F,limit,brush);
            const auto split=std::chrono::steady_clock::now();
            const auto batched=build_hp1_textured_bsp_scene(root,map,.02F,limit,brush,&retained);
            const auto end=std::chrono::steady_clock::now();
            independent_seconds+=std::chrono::duration<double>(split-start).count();
            batch_seconds+=std::chrono::duration<double>(end-split).count();
            SameScene(independent,batched);
            independent_hash.Integer(static_cast<std::uint64_t>(brush));independent_hash.Scene(independent);
            batch_hash.Integer(static_cast<std::uint64_t>(brush));batch_hash.Scene(batched);
            ++completed;
            if(benchmark&&completed%16==0)std::cerr<<"Mover pairs verified: "<<completed<<'/'<<samples.size()<<std::endl;
        }
        Check(independent_hash.value==batch_hash.value,"rolling hash of all exact scene data preserved");
        std::cout<<(benchmark?"OWNED_MOVER_BENCHMARK=PASS movers=":"OWNED_BSP_PARITY=PASS samples=")
                 <<samples.size()<<" independent_seconds="<<independent_seconds<<" shared_context_seconds="<<context_seconds
                 <<" shared_build_seconds="<<batch_seconds<<" shared_total_seconds="<<batch_seconds+context_seconds
                 <<" speedup="<<independent_seconds/(batch_seconds+context_seconds)
                 <<" independent_hash="<<std::hex<<independent_hash.value<<" shared_hash="<<batch_hash.value<<std::dec<<'\n';
    }
    std::cout<<"BSP_BUILD_CONTEXT_TESTS=PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
