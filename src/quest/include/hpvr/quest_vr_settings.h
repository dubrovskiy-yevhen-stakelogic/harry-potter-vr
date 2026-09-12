#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace hpvr::quest {
enum class CastingMode { Classic=0, VisibleGesture=1, Gesture=2 };
enum class TurningMode { Snap=0, Smooth=1 };
inline bool ValidTurningMode(TurningMode mode){return mode==TurningMode::Snap||mode==TurningMode::Smooth;}
inline bool ValidSmoothTurnSpeed(int degrees){return degrees>=30&&degrees<=180&&degrees%30==0;}
inline bool ValidCastingMode(CastingMode mode){
    return mode==CastingMode::Classic||mode==CastingMode::VisibleGesture||mode==CastingMode::Gesture;
}
inline bool UsesGestureCasting(CastingMode mode){return mode==CastingMode::VisibleGesture||mode==CastingMode::Gesture;}
inline bool ShowsGestureTrace(CastingMode mode){return mode==CastingMode::VisibleGesture;}
struct VrSettings {
    int render_scale=100; // Percent per axis, applied on the next frame.
    int ssr=30;           // Zero disables both sampling and history copies.
    std::uint64_t generation=0;
    bool relaxed_lesson=true;  // Comfortable demo default; saved original-mode choices remain respected.
    bool welcome_seen=false;   // App-wide, independent of the three game slots.
    bool first_person_cutscenes=true; // Live camera preference; saved theatre choices remain respected.
    CastingMode casting_mode=CastingMode::Classic; // Optional freehand modes outside the lesson.
    bool voice_cast=false; // Independent of the selected wand-input mode; opt-in.
    bool voice_hints=false; // Optional casting prompts; old settings also default to hidden.
    int refresh_rate=90;
    TurningMode turning_mode=TurningMode::Snap;
    int smooth_turn_speed=90; // Degrees per second at full stick deflection.
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
inline bool ValidRefreshRate(int hz){return hz==0||hz==72||hz==80||hz==90||hz==120;}
inline std::uint32_t VrSettingsChecksumV4(const VrSettings& v,std::uint64_t generation,bool legacy_manual){
    auto h=VrSettingsChecksumV3(v,generation);
    for(unsigned char c:std::string(legacy_manual?":1:":":0:")+std::to_string(v.refresh_rate))h=(h^c)*16777619U;
    return h;
}
inline std::uint32_t VrSettingsChecksumV5(const VrSettings& v,std::uint64_t generation){
    auto h=VrSettingsChecksumV3(v,generation);
    for(unsigned char c:":"+std::to_string(static_cast<int>(v.casting_mode))+":"+std::to_string(v.refresh_rate)+(v.voice_cast?":1":":0"))h=(h^c)*16777619U;
    return h;
}
inline std::uint32_t VrSettingsChecksumV6(const VrSettings& v,std::uint64_t generation){
    auto h=VrSettingsChecksumV5(v,generation);
    for(unsigned char c:std::string(v.voice_hints?":1":":0"))h=(h^c)*16777619U;
    return h;
}
inline std::uint32_t VrSettingsChecksumV7(const VrSettings& v,std::uint64_t generation){
    auto h=VrSettingsChecksumV6(v,generation);
    for(unsigned char c:":"+std::to_string(static_cast<int>(v.turning_mode))+":"+std::to_string(v.smooth_turn_speed))h=(h^c)*16777619U;
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
        int relaxed=0,welcome=0,legacy_manual=0;
        if(magic=="HPVR_VR2"||magic=="HPVR_VR3"||magic=="HPVR_VR4"||magic=="HPVR_VR5"||magic=="HPVR_VR6"||magic=="HPVR_VR7"){
            if(!(f>>relaxed>>welcome)||relaxed<0||relaxed>1||welcome<0||welcome>1)continue;
            v.relaxed_lesson=relaxed!=0;v.welcome_seen=welcome!=0;
        }else if(magic!="HPVR_VR1")continue;
        if(magic=="HPVR_VR3"||magic=="HPVR_VR4"||magic=="HPVR_VR5"||magic=="HPVR_VR6"||magic=="HPVR_VR7"){
            int first_person=0;
            if(!(f>>first_person)||first_person<0||first_person>1)continue;
            v.first_person_cutscenes=first_person!=0;
        }
        if(magic=="HPVR_VR4"){
            if(!(f>>legacy_manual>>v.refresh_rate)||legacy_manual<0||legacy_manual>1||!ValidRefreshRate(v.refresh_rate))continue;
            v.casting_mode=legacy_manual?CastingMode::Gesture:CastingMode::Classic;
        }
        if(magic=="HPVR_VR5"||magic=="HPVR_VR6"||magic=="HPVR_VR7"){
            int mode=0,voice=0;
            if(!(f>>mode>>v.refresh_rate>>voice)||mode<0||mode>2||voice<0||voice>1||!ValidRefreshRate(v.refresh_rate))continue;
            v.casting_mode=static_cast<CastingMode>(mode);v.voice_cast=voice!=0;
        }
        if(magic=="HPVR_VR6"||magic=="HPVR_VR7"){
            int hints=0;
            if(!(f>>hints)||hints<0||hints>1)continue;
            v.voice_hints=hints!=0;
        }
        if(magic=="HPVR_VR7"){
            int turning=0;
            if(!(f>>turning>>v.smooth_turn_speed)||turning<0||turning>1||!ValidSmoothTurnSpeed(v.smooth_turn_speed))continue;
            v.turning_mode=static_cast<TurningMode>(turning);
        }
        const auto expected=magic=="HPVR_VR7"?VrSettingsChecksumV7(v,v.generation):magic=="HPVR_VR6"?VrSettingsChecksumV6(v,v.generation):magic=="HPVR_VR5"?VrSettingsChecksumV5(v,v.generation):magic=="HPVR_VR4"?VrSettingsChecksumV4(v,v.generation,legacy_manual!=0):magic=="HPVR_VR3"?VrSettingsChecksumV3(v,v.generation):
            magic=="HPVR_VR2"?VrSettingsChecksumV2(v,v.generation):VrSettingsChecksum(v.render_scale,v.ssr,v.generation);
        if(f>>sum && !(f>>extra) &&
           v.render_scale>=50&&v.render_scale<=175&&v.render_scale%5==0&&v.ssr>=0&&v.ssr<=100&&v.ssr%5==0&&
           sum==expected&&v.generation>best.generation&&v.generation<1000000000000ULL)best=v;
    }
    return best;
}
inline bool WriteVrSettings(const std::filesystem::path& root,VrSettings& v){
    if(!ValidRefreshRate(v.refresh_rate)||!ValidCastingMode(v.casting_mode)||
       !ValidTurningMode(v.turning_mode)||!ValidSmoothTurnSpeed(v.smooth_turn_speed))return false;
    v.render_scale=std::clamp(v.render_scale,50,175);v.render_scale-=v.render_scale%5;
    v.ssr=std::clamp(v.ssr,0,100);v.ssr-=v.ssr%5;
    const auto generation=std::max(v.generation,ReadVrSettings(root).generation)+1;
    std::error_code ec;std::filesystem::create_directories(root,ec);if(ec)return false;
    std::ofstream f(root/("vr-settings."+std::to_string(generation%2)),std::ios::trunc);
    f<<"HPVR_VR7 "<<v.render_scale<<' '<<v.ssr<<' '<<generation<<' '<<v.relaxed_lesson<<' '<<v.welcome_seen<<' '<<v.first_person_cutscenes<<' '<<static_cast<int>(v.casting_mode)<<' '<<v.refresh_rate<<' '<<v.voice_cast<<' '<<v.voice_hints<<' '<<static_cast<int>(v.turning_mode)<<' '<<v.smooth_turn_speed<<' '<<VrSettingsChecksumV7(v,generation)<<'\n';
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
