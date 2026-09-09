#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace hpvr::quest {
struct VrSettings {
    int render_scale=100; // Percent per axis, applied on the next frame.
    int ssr=30;           // Zero disables both sampling and history copies.
    std::uint64_t generation=0;
    bool relaxed_lesson=true;  // Comfortable demo default; saved original-mode choices remain respected.
    bool welcome_seen=false;   // App-wide, independent of the three game slots.
    bool first_person_cutscenes=true; // Live camera preference; saved theatre choices remain respected.
};
inline std::uint32_t VrSettingsChecksum(int scale,int ssr,std::uint64_t generation){
    std::uint32_t h=2166136261U;
    for(unsigned char c:std::to_string(scale)+":"+std::to_string(ssr)+":"+std::to_string(generation))h=(h^c)*16777619U;
    return h;
}
inline std::uint32_t VrSettingsChecksumV2(const VrSettings& v,std::uint64_t generation){
    std::uint32_t h=VrSettingsChecksum(v.render_scale,v.ssr,generation);
    for(unsigned char c:std::string(v.relaxed_lesson?":1":":0")+(v.welcome_seen?":1":":0"))h=(h^c)*16777619U;
    return h;
}
inline std::uint32_t VrSettingsChecksumV3(const VrSettings& v,std::uint64_t generation){
    std::uint32_t h=VrSettingsChecksumV2(v,generation);
    for(unsigned char c:std::string(v.first_person_cutscenes?":1":":0"))h=(h^c)*16777619U;
    return h;
}
inline VrSettings ReadVrSettings(const std::filesystem::path& root){
    VrSettings best;
    for(int bank=0;bank<2;++bank){
        const auto path=root/("vr-settings."+std::to_string(bank));std::error_code ec;
        if(std::filesystem::file_size(path,ec)>1024||ec)continue;
        std::ifstream f(path);std::string magic,extra;
        VrSettings v;std::uint32_t sum=0;
        if(!(f>>magic>>v.render_scale>>v.ssr>>v.generation))continue;
        int relaxed=0,welcome=0;
        if(magic=="HPVR_VR2"||magic=="HPVR_VR3"){
            if(!(f>>relaxed>>welcome)||relaxed<0||relaxed>1||welcome<0||welcome>1)continue;
            v.relaxed_lesson=relaxed!=0;v.welcome_seen=welcome!=0;
        }else if(magic!="HPVR_VR1")continue;
        if(magic=="HPVR_VR3"){
            int first_person=0;
            if(!(f>>first_person)||first_person<0||first_person>1)continue;
            v.first_person_cutscenes=first_person!=0;
        }
        const auto expected=magic=="HPVR_VR3"?VrSettingsChecksumV3(v,v.generation):
            magic=="HPVR_VR2"?VrSettingsChecksumV2(v,v.generation):VrSettingsChecksum(v.render_scale,v.ssr,v.generation);
        if(f>>sum && !(f>>extra) &&
           v.render_scale>=50&&v.render_scale<=175&&v.render_scale%5==0&&v.ssr>=0&&v.ssr<=100&&v.ssr%5==0&&
           sum==expected&&v.generation>best.generation&&v.generation<1000000000000ULL)best=v;
    }
    return best;
}
inline bool WriteVrSettings(const std::filesystem::path& root,VrSettings& v){
    v.render_scale=std::clamp(v.render_scale,50,175);v.render_scale-=v.render_scale%5;
    v.ssr=std::clamp(v.ssr,0,100);v.ssr-=v.ssr%5;
    const auto generation=std::max(v.generation,ReadVrSettings(root).generation)+1;
    std::error_code ec;std::filesystem::create_directories(root,ec);if(ec)return false;
    std::ofstream f(root/("vr-settings."+std::to_string(generation%2)),std::ios::trunc);
    f<<"HPVR_VR3 "<<v.render_scale<<' '<<v.ssr<<' '<<generation<<' '<<v.relaxed_lesson<<' '<<v.welcome_seen<<' '<<v.first_person_cutscenes<<' '<<VrSettingsChecksumV3(v,generation)<<'\n';
    f.flush();if(!f)return false;f.close();if(f.fail())return false;
    v.generation=generation;return true;
}
// The menu press is consumed until release, even if a grip is released first.
struct VrMenuChord {
    bool menu_down=false,consumed=false;
    bool Update(bool focused,bool menu,float left,float right){
        if(!focused||!menu){menu_down=false;consumed=false;return false;}
        const bool fire=!menu_down&&left>.75F&&right>.75F;
        menu_down=true;consumed=consumed||fire;return fire;
    }
};
inline unsigned VrRenderExtent(unsigned recommended,int scale,unsigned maximum){
    return std::max(1U,std::min(maximum,static_cast<unsigned>(std::uint64_t(recommended)*std::clamp(scale,50,175)/100)));
}
}
