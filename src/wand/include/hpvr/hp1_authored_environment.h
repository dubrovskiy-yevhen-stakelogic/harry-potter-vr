#pragma once

#include "hpvr/hp1_gesture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>

namespace hpvr::wand {

inline bool Hp1EnvironmentNameEquals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    const auto fold=[](char c) { return c >= 'A' && c <= 'Z' ? char(c+'a'-'A') : c; };
    for (std::size_t i=0; i<a.size(); ++i) if (fold(a[i]) != fold(b[i])) return false;
    return true;
}

// UE1 stores saturation inversely: 255 is white, zero is fully saturated.
inline std::array<float,3> Hp1EnvironmentLightColor(std::uint8_t hue,
                                                  std::uint8_t saturation) noexcept {
    const float h=float(hue)*6.0F/255.0F, s=1.0F-float(saturation)/255.0F;
    const float x=s*(1.0F-std::abs(std::fmod(h,2.0F)-1.0F));
    std::array<float,3> result;
    if(h<1)result={s,x,0};else if(h<2)result={x,s,0};else if(h<3)result={0,s,x};
    else if(h<4)result={0,x,s};else if(h<5)result={x,0,s};else result={s,0,x};
    for(auto& value:result)value+=1.0F-s;
    return result;
}

inline std::optional<std::array<float,3>> Hp1SerializedZoneAmbient(
    const Hp1ActorVisual& actor) noexcept {
    std::uint8_t brightness=0, hue=0, saturation=255;
    bool has_brightness=false;
    for(const auto& property:actor.serialized_properties) {
        const bool b=Hp1EnvironmentNameEquals(property.name,"AmbientBrightness");
        const bool h=Hp1EnvironmentNameEquals(property.name,"AmbientHue");
        const bool s=Hp1EnvironmentNameEquals(property.name,"AmbientSaturation");
        if(!b&&!h&&!s)continue;
        if(property.kind!=1 || property.value.size()!=1 || property.array_index>=0)return std::nullopt;
        if(b){brightness=property.value[0];has_brightness=true;}
        if(h)hue=property.value[0];
        if(s)saturation=property.value[0];
    }
    if(!has_brightness)return std::nullopt;
    auto color=Hp1EnvironmentLightColor(hue,saturation);
    for(auto& value:color)value*=float(brightness)/255.0F;
    return color;
}

// Used only while cooking the outdoor lesson. Indoor cache lighting retains its
// separate, already-validated abyss/ambient policy.
class Hp1AuthoredZoneAmbient {
public:
    Hp1AuthoredZoneAmbient(const Hp1BspTopology& topology,
                          const Hp1ActorVisualCensus& actors, bool enabled)
        : topology_(topology) {
        if(!enabled || topology.status!=Hp1ProfileStatus::ok || actors.status!=Hp1ProfileStatus::ok ||
           topology.zone_count>ambient_.size() || topology.zone_actor_references.size()!=topology.zone_count)return;
        for(std::size_t zone=0;zone<topology.zone_count;++zone) {
            const auto actor=std::ranges::find_if(actors.actors,[&](const auto& a){
                return a.actor_reference==topology.zone_actor_references[zone];});
            if(actor!=actors.actors.end())ambient_[zone]=Hp1SerializedZoneAmbient(*actor);
        }
    }
    [[nodiscard]] std::optional<std::array<float,3>> Sample(
        Hp1BspVector point, Hp1BspVector visible_normal={}) const noexcept {
        point.x+=visible_normal.x*.25F;point.y+=visible_normal.y*.25F;point.z+=visible_normal.z*.25F;
        if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z))return std::nullopt;
        std::int32_t index=0;
        for(std::size_t step=0;step<=topology_.nodes.size()&&step<4096;++step) {
            if(index<0||std::size_t(index)>=topology_.nodes.size())return std::nullopt;
            const auto& node=topology_.nodes[std::size_t(index)];
            const double side=double(point.x)*node.plane[0]+double(point.y)*node.plane[1]+
                              double(point.z)*node.plane[2]-node.plane[3];
            if(!std::isfinite(side))return std::nullopt;
            const bool front=side>=0;
            index=front?node.front_node_index:node.back_node_index;
            if(index==-1) {
                const auto zone=node.zone_indices[front?1U:0U];
                return zone<ambient_.size()?ambient_[zone]:std::nullopt;
            }
        }
        return std::nullopt;
    }
private:
    const Hp1BspTopology& topology_;
    std::array<std::optional<std::array<float,3>>,64> ambient_{};
};

} // namespace hpvr::wand
