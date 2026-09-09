#pragma once
#include "hpvr/hp1_gesture.h"
#include <array>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
namespace hpvr::quest {
// Reassemble the user's original legal/Warner startup artwork before loading
// world geometry. Nothing proprietary is embedded in the executable.
inline std::vector<std::uint8_t> LoadWarnerStartup(const std::filesystem::path& root){
    const auto package=root/"system/HPMenu.u";
    const auto table=wand::inspect_hp1_package_link_table(package);
    if(table.status!=wand::Hp1ProfileStatus::ok)return {};
    std::vector<std::uint8_t> pixels(640*480*4,255);
    for(unsigned tile=0;tile<6;++tile){
        const auto name="FELegalTexture"+std::to_string(tile+1);
        const auto e=std::ranges::find_if(table.exports,[&](const auto& item){
            return !item.object_path.empty()&&item.object_path.back()==name&&item.qualified_class_name=="Engine.Texture";});
        if(e==table.exports.end())return {};
        const auto texture=wand::load_hp1_p8_texture(package,e->reference,false);
        if(texture.status!=wand::Hp1ProfileStatus::ok||texture.mips.empty())return {};
        const auto& m=texture.mips.front();if(m.width!=256||m.height!=256)return {};
        for(unsigned y=0;y<256&&y+tile/3*256<480;++y)
            for(unsigned x=0;x<256&&x+tile%3*256<640;++x){
                const auto dst=((y+tile/3*256)*640+x+tile%3*256)*4;
                std::copy_n(texture.rgba8.begin()+(y*256+x)*4,4,pixels.begin()+dst);
                pixels[dst+3]=255;
            }
    }return pixels;
}
}
