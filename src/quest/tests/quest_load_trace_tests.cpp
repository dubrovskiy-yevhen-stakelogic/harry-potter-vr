#include "../../../android/app/src/main/cpp/quest_load_trace.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Entry {double seconds;std::string stage;};
std::vector<Entry> Read(const std::filesystem::path& path){
    std::ifstream in(path);std::vector<Entry> entries;std::string line;
    while(std::getline(in,line)){
        std::istringstream fields(line);Entry entry{};char suffix=0;
        Check(bool(fields>>entry.seconds>>suffix>>entry.stage)&&suffix=='s',"timed journal entry");
        entries.push_back(std::move(entry));
    }
    return entries;
}
}
int main(){
    using hpvr::quest::SceneLoadTrace;
    const auto temporary=std::filesystem::temp_directory_path()/
        ("hpvr-load-trace-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        Check(std::filesystem::create_directory(temporary),"create isolated test directory");
        const auto saves=temporary/"saves";
        std::thread cpu([&]{SceneLoadTrace trace(saves,1);trace.Stage("BSP_READY");trace.Ready();});
        cpu.join();
        // The CPU trace has been destroyed, but GPU adoption must retain its
        // start time. A second map must not reset the first map's clock.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        {SceneLoadTrace trace(saves,0);trace.Ready();}
        std::thread gpu([&]{SceneLoadTrace::Append(saves,1,"GPU_UPLOAD_BEGIN");SceneLoadTrace::Append(saves,1,"ADOPTED");});
        gpu.join();
        const auto entries=Read(SceneLoadTrace::Path(saves,1));
        Check(entries.size()==5,"all CPU and GPU milestones retained");
        Check(entries[0].stage=="STARTED"&&entries[1].stage=="BSP_READY"&&
              entries[2].stage=="CPU_READY"&&entries[3].stage=="GPU_UPLOAD_BEGIN"&&
              entries[4].stage=="ADOPTED","existing milestone names preserved");
        for(std::size_t i=1;i<entries.size();++i)
            Check(entries[i].seconds>=entries[i-1].seconds,"monotonic CPU-to-GPU elapsed time");
        Check(entries[3].seconds>=entries[2].seconds+.004,"GPU timing includes post-CPU wait");
        const auto first=Read(SceneLoadTrace::Path(saves,0));
        Check(first.size()==2&&first.back().stage=="CPU_READY","map journals isolated");
        SceneLoadTrace::Append(saves,0,"GPU_UPLOAD_FAILED");
        Check(Read(SceneLoadTrace::Path(saves,0)).back().stage=="GPU_UPLOAD_FAILED","timed GPU failure");
        {SceneLoadTrace failed(saves,1);}
        const auto failed=Read(SceneLoadTrace::Path(saves,1));
        Check(failed.size()==2&&failed.back().stage=="FAILED_OR_EXCEPTION","CPU failure journal replaces prior load");
        SceneLoadTrace::Append(saves,2,"ADOPTED");
        std::ifstream unknown(SceneLoadTrace::Path(saves,2));std::string line;std::getline(unknown,line);
        Check(line=="ADOPTED elapsed=unavailable","missing epoch never invents elapsed time");
        unknown.close();
        SceneLoadTrace::Event(saves,3,"DEATH reason=MANTLE_INTERRUPTED");
        {SceneLoadTrace restart(saves,3);restart.Ready();}
        SceneLoadTrace::Event(saves,3,"CUTSCENE_START ref=1526");
        std::ifstream events(temporary/"scene-events-3.log");
        std::string all((std::istreambuf_iterator<char>(events)),{});events.close();
        Check(all.find("MANTLE_INTERRUPTED")!=std::string::npos&&all.find("ref=1526")!=std::string::npos,"event journal survives scene reload");
        {
            SceneLoadTrace trace(saves,4);trace.Stage("ASSET_CACHE_REJECTED_REBUILD");
            SceneLoadTrace::Note(saves,4,"SCENE CACHE REJECTED: checksum");
            SceneLoadTrace::Note(saves,4,std::string(400,'x'));
        }
        SceneLoadTrace::Append(saves,4,"TRANSFER_CPU_FAILED");
        Check(SceneLoadTrace::LastStage(saves,4)=="ASSET_CACHE_REJECTED_REBUILD","failure markers keep the failed milestone");
        const auto detail=SceneLoadTrace::Detail(saves,4);
        Check(detail.starts_with("SCENE CACHE REJECTED: checksum; x")&&detail.size()==SceneLoadTrace::kMaximumDetail,"first reasons kept within limit");
        {SceneLoadTrace retry(saves,4);SceneLoadTrace::Append(saves,4,"GPU_UPLOAD_BEGIN");}
        Check(SceneLoadTrace::LastStage(saves,4)=="GPU_UPLOAD_BEGIN"&&SceneLoadTrace::Detail(saves,4).empty(),"new attempt clears old diagnosis");
        Check(SceneLoadTrace::LastStage(saves,1).empty()==false&&SceneLoadTrace::Detail(saves,5).empty(),"diagnoses isolated by map");
        std::filesystem::remove_all(temporary);
        std::cout<<"LOAD_TRACE_TESTS=PASS\n";return 0;
    }catch(const std::exception& e){
        std::cerr<<e.what()<<'\n';
        std::error_code ignored;std::filesystem::remove_all(temporary,ignored);return 1;
    }
}
