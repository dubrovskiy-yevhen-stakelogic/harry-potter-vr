// Installer-only CPU preparation. Input game files are always read-only.
#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <iomanip>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
using namespace hpvr::quest;
namespace fs=std::filesystem;
bool Within(const fs::path& child,const fs::path& parent){
    auto c=child.begin();
    for(auto p=parent.begin();p!=parent.end();++p,++c)
        if(c==child.end()||AsciiFold(c->generic_string())!=AsciiFold(p->generic_string()))return false;
    return true;
}
void RejectRedirects(const fs::path& path){
    fs::path current;
    for(const auto& part:fs::absolute(path).lexically_normal()){
        current/=part;
#ifdef _WIN32
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("preparation paths must not contain junctions or symbolic links");
#else
        if(fs::is_symlink(fs::symlink_status(current)))
            throw std::runtime_error("preparation paths must not contain symbolic links");
#endif
    }
}
int Run(const std::vector<fs::path>& args){
    if(args.size()!=6&&args.size()!=7){
        std::cerr<<"usage: hpvr_quest_prepare_assets <owned-root> --output <private-output-outside-root> --map <0|1> [--verify]\n";
        return 2;
    }
    try{
        if(args[2]!="--output"||args[4]!="--map"||
            (args[5]!="0"&&args[5]!="1")||(args.size()==7&&args[6]!="--verify"))
            throw std::runtime_error("invalid preparation arguments");
        RejectRedirects(args[1]);RejectRedirects(args[3]);
        const auto root=fs::canonical(args[1]);
        const auto output=fs::weakly_canonical(fs::absolute(args[3]));
        if(!fs::is_directory(root)||output==output.root_path()||Within(output,root)||Within(root,output))
            throw std::runtime_error("output must be a dedicated private folder outside the input game tree");
        const unsigned map_id=args[5]=="0"?0:1;
        const auto path=output/("map-"+std::to_string(map_id)+".hpvc");
        RejectRedirects(path);
        const bool verify=args.size()==7;
        if(!verify&&fs::exists(path))throw std::runtime_error("prepared file already exists; use a new output folder");
        const auto began=std::chrono::steady_clock::now();
        const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();};
        hpvr::wand::Hp1PackageReadScope package_reads;
        const auto fingerprint=cache::ComputeSourceFingerprint(root,map_id);
        const auto fingerprint_seconds=elapsed();
        if(!verify){
            const auto map=root/(map_id==0?"Maps/Lev_Tut1.unr":"Maps/Lev_Tut1b.unr");
            hpvr_hp1_player_start_report start{};
            if(hpvr_hp1_load_player_start_utf8(map.string().c_str(),kMetersPerUnrealUnit,0,&start)!=HPVR_HP1_PROFILE_OK||
                start.status!=HPVR_HP1_PROFILE_OK||start.abi_version!=HPVR_HP1_PLAYER_START_ABI_VERSION||
                !start.location_serialized||start.rotation_units[0]!=0||start.rotation_units[2]!=0)
                throw std::runtime_error("owned player start is invalid");
            const float yaw=static_cast<float>(start.rotation_units[1])*kTau/65536.0F;
            WorldMetadata world;
            if(!LoadWorldMetadata(root,map,start,yaw,&world))throw std::runtime_error("owned scene metadata could not be loaded");
            PreparedGeometry geometry;
            if(!PrepareGeometryFromOwnedData(root,map_id,start,yaw,world,geometry,[&](const char* stage){
                std::cout<<"PREPARE_STAGE="<<stage<<" map="<<map_id<<" seconds="<<elapsed()<<std::endl;
            }))throw std::runtime_error("owned scene preparation failed");
            if(!ValidatePreparedGeometry(geometry))throw std::runtime_error("prepared scene layout is invalid");
            if(cache::ComputeSourceFingerprint(root,map_id)!=fingerprint)
                throw std::runtime_error("input game files changed during preparation");
            const auto prepared_at=elapsed();
            cache::Writer writer(path,map_id,fingerprint);
            WritePreparedGeometry(writer,geometry);writer.Finish();
            std::cout<<"PREPARE_WRITE=PASS map="<<map_id<<" vertices="<<geometry.vertices.size()
                <<" layers="<<geometry.texture_layers<<" prepare_seconds="<<prepared_at
                <<" write_seconds="<<elapsed()-prepared_at<<std::endl;
        }
        // Free preparation arrays before the independent streamed read-back.
        const auto read_at=elapsed();
        cache::Reader reader(path,map_id,fingerprint);
        auto candidate=ReadPreparedGeometry(reader,verify?prepared_codec::kMaxFrontendVertexReserve:0);reader.Finish();
        if(!ValidatePreparedGeometry(candidate))throw std::runtime_error("prepared read-back layout is invalid");
        const auto read_seconds=elapsed()-read_at;
        if(verify){
            const auto* vertex_address=candidate.vertices.data();
            const auto* texture_address=candidate.textures.data();
            QuestFrontEnd front;
            if(!LoadFrontAssets(root,&front.assets,map_id))throw std::runtime_error("prepared scene frontend assets are incomplete");
            // Reserve the same two extra layers used by fire and the target
            // marker. This check does not render or alter the cooked file.
            if(candidate.texture_layers+2+front.assets.textures.size()>kMaximumCombinedTextureLayers)
                throw std::runtime_error("prepared scene leaves insufficient frontend texture layers");
            candidate.texture_layers+=2;
            candidate.textures.resize(std::uint64_t(candidate.texture_layers)*256*256*4);
            std::map<std::string,FrontDrawRange> ranges;std::uint32_t front_vertices=0;
            if(!AppendFrontGeometry(front,candidate.vertices,candidate.textures,candidate.texture_layers,ranges,front_vertices)||
                candidate.vertices.data()!=vertex_address||candidate.textures.data()!=texture_address)
                throw std::runtime_error("frontend exceeded prepared-scene allocation headroom");
            std::cout<<"PREPARED_FRONTEND=PASS map="<<map_id<<" vertices="<<front_vertices
                <<" layers="<<candidate.texture_layers<<" scene_buffer_reallocated=NO\n";
        }
        std::cout<<std::fixed<<std::setprecision(3)<<"PREPARED_SCENE=PASS map="<<map_id
            <<" schema="<<cache::kSchema<<" cook="<<cache::kCookRevision<<" bytes="<<fs::file_size(path)
            <<" vertices="<<candidate.vertices.size()<<" fingerprint_seconds="<<fingerprint_seconds
            <<" read_seconds="<<read_seconds<<" total_seconds="<<elapsed()<<" mode="<<(verify?"VERIFY":"PREPARE")<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<"PREPARED_SCENE=FAILED reason="<<e.what()<<'\n';return 1;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return Run(args);}
#else
int main(int argc,char** argv){std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return Run(args);}
#endif
