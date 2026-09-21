#pragma once
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace hpvr::quest {
// Small per-level journal in app-private storage; survives logcat rotation.
class SceneLoadTrace {
    using Clock=std::chrono::steady_clock;
    // CPU preparation and GPU upload happen on different threads. Keep the
    // epoch until adoption so both phases use the same elapsed-time scale.
    static inline std::mutex epochs_mutex_;
    static inline std::map<std::filesystem::path,Clock::time_point> epochs_;
    // Last milestone and first reasons of each map's latest attempt. Release
    // builds cannot share private journals, so the menu shows this summary.
    struct Diagnosis {std::string stage,detail;};
    static inline std::map<std::filesystem::path,Diagnosis> diagnoses_;
    std::ofstream stream_;
    std::filesystem::path path_;
    Clock::time_point start_=Clock::now();
    bool ready_=false;
    static bool Terminal(std::string_view status){
        return status=="ADOPTED"||status=="GPU_UPLOAD_FAILED"||
               status=="TRANSFER_CPU_FAILED"||status=="FAILED_OR_EXCEPTION";
    }
    // Failure markers would hide the milestone that actually failed.
    static void RememberStage(const std::filesystem::path& path,std::string_view status){
        if(status!="ADOPTED"&&Terminal(status))return;
        diagnoses_[path].stage=status;
    }
public:
    static void Event(const std::filesystem::path& saves,unsigned map,const std::string& message){
        if(saves.empty())return;
        const auto path=saves.parent_path()/("scene-events-"+std::to_string(map)+".log");
        std::lock_guard lock(epochs_mutex_);std::error_code ec;
        const auto size=std::filesystem::file_size(path,ec);
        std::ofstream out(path,!ec&&size>65536?std::ios::trunc:std::ios::app);
        out<<std::chrono::duration<double>(Clock::now().time_since_epoch()).count()<<' '<<message<<'\n';
    }
    static std::filesystem::path Path(const std::filesystem::path& saves,unsigned map){
        return saves.parent_path()/("scene-load-"+std::to_string(map)+".log");
    }
    SceneLoadTrace(const std::filesystem::path& saves,unsigned map):path_(Path(saves,map)){
        {
            std::lock_guard lock(epochs_mutex_);
            // Bound abandoned journals without retaining game data between loads.
            if(!epochs_.contains(path_)&&epochs_.size()>=8){
                auto oldest=epochs_.begin();
                for(auto i=epochs_.begin();i!=epochs_.end();++i)
                    if(i->second<oldest->second)oldest=i;
                epochs_.erase(oldest);
            }
            epochs_[path_]=start_;
            diagnoses_[path_]={};
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
        RememberStage(path_,name);
        stream_<<std::chrono::duration<double>(Clock::now()-start_).count()<<"s "<<name<<std::endl;
        if(Terminal(name))epochs_.erase(epoch);
    }
    void Ready(){Stage("CPU_READY");ready_=true;}
    static void Append(const std::filesystem::path& saves,unsigned map,const char* status){
        const auto path=Path(saves,map);
        std::lock_guard lock(epochs_mutex_);
        std::ofstream out(path,std::ios::app);
        const auto epoch=epochs_.find(path);
        RememberStage(path,status);
        if(epoch!=epochs_.end()){
            out<<std::chrono::duration<double>(Clock::now()-epoch->second).count()<<"s "<<status<<std::endl;
            if(Terminal(status))epochs_.erase(epoch);
        }else{
            // A process restart cannot recover a steady-clock epoch. Do not
            // print a misleading zero-second GPU/adoption duration.
            out<<status<<" elapsed=unavailable"<<std::endl;
        }
    }
    // Adds a short reason to the latest attempt. The first reasons are kept:
    // later failures are usually consequences of the first one.
    static void Note(const std::filesystem::path& saves,unsigned map,std::string_view detail){
        if(saves.empty()||detail.empty())return;
        std::lock_guard lock(epochs_mutex_);
        auto& text=diagnoses_[Path(saves,map)].detail;
        if(text.size()>=kMaximumDetail)return;
        if(!text.empty())text+="; ";
        text.append(detail.substr(0,kMaximumDetail-std::min(text.size(),kMaximumDetail)));
    }
    static std::string LastStage(const std::filesystem::path& saves,unsigned map){
        std::lock_guard lock(epochs_mutex_);
        const auto found=diagnoses_.find(Path(saves,map));
        return found==diagnoses_.end()?std::string{}:found->second.stage;
    }
    static std::string Detail(const std::filesystem::path& saves,unsigned map){
        std::lock_guard lock(epochs_mutex_);
        const auto found=diagnoses_.find(Path(saves,map));
        return found==diagnoses_.end()?std::string{}:found->second.detail;
    }
    static constexpr std::size_t kMaximumDetail=240;
};
}
