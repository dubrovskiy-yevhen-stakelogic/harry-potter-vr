#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string_view>

namespace hpvr::quest {
// Small per-level journal in app-private storage; survives logcat rotation.
class SceneLoadTrace {
    using Clock=std::chrono::steady_clock;
    // CPU preparation and GPU upload happen on different threads. Keep the
    // epoch until adoption so both phases use the same elapsed-time scale.
    static inline std::mutex epochs_mutex_;
    static inline std::map<std::filesystem::path,Clock::time_point> epochs_;
    std::ofstream stream_;
    std::filesystem::path path_;
    Clock::time_point start_=Clock::now();
    bool ready_=false;
    static bool Terminal(std::string_view status){
        return status=="ADOPTED"||status=="GPU_UPLOAD_FAILED"||
               status=="TRANSFER_CPU_FAILED"||status=="FAILED_OR_EXCEPTION";
    }
public:
    static std::filesystem::path Path(const std::filesystem::path& saves,unsigned map){
        return saves.parent_path()/("scene-load-"+std::to_string(map)+".log");
    }
    SceneLoadTrace(const std::filesystem::path& saves,unsigned map):path_(Path(saves,map)){
        {
            std::lock_guard lock(epochs_mutex_);
            // Only two maps are currently playable. Bound abandoned journals
            // as well, without retaining any game data between loads.
            if(!epochs_.contains(path_)&&epochs_.size()>=8){
                auto oldest=epochs_.begin();
                for(auto i=epochs_.begin();i!=epochs_.end();++i)
                    if(i->second<oldest->second)oldest=i;
                epochs_.erase(oldest);
            }
            epochs_[path_]=start_;
            stream_.open(path_,std::ios::trunc);
        }
        Stage("STARTED");
    }
    SceneLoadTrace(const SceneLoadTrace&)=delete;
    SceneLoadTrace& operator=(const SceneLoadTrace&)=delete;
    ~SceneLoadTrace(){if(!ready_)Stage("FAILED_OR_EXCEPTION");}
    void Stage(const char* name){
        std::lock_guard lock(epochs_mutex_);
        const auto epoch=epochs_.find(path_);
        if(epoch==epochs_.end()||epoch->second!=start_)return;
        stream_<<std::chrono::duration<double>(Clock::now()-start_).count()<<"s "<<name<<std::endl;
        if(Terminal(name))epochs_.erase(epoch);
    }
    void Ready(){Stage("CPU_READY");ready_=true;}
    static void Append(const std::filesystem::path& saves,unsigned map,const char* status){
        const auto path=Path(saves,map);
        std::lock_guard lock(epochs_mutex_);
        std::ofstream out(path,std::ios::app);
        const auto epoch=epochs_.find(path);
        if(epoch!=epochs_.end()){
            out<<std::chrono::duration<double>(Clock::now()-epoch->second).count()<<"s "<<status<<std::endl;
            if(Terminal(status))epochs_.erase(epoch);
        }else{
            // A process restart cannot recover a steady-clock epoch. Do not
            // print a misleading zero-second GPU/adoption duration.
            out<<status<<" elapsed=unavailable"<<std::endl;
        }
    }
};
}
