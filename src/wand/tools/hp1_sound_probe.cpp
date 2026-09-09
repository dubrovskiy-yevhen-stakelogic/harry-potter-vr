#include "hpvr/hp1_gesture.h"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <fstream>

int main(int argc, char** argv) {
    if (argc == 5 && std::string(argv[2]) == "--export-mpeg") {
        const auto census = hpvr::wand::inspect_hp1_sound_assets(argv[1]);
        for (const auto& sound : census.sounds) {
            if (sound.object_name != argv[3]) continue;
            const auto mpeg = hpvr::wand::load_hp1_mpeg_sound(argv[1], sound.sound_reference);
            if (mpeg.status != hpvr::wand::Hp1ProfileStatus::ok) return 1;
            std::ofstream output(argv[4], std::ios::binary);
            output.write(reinterpret_cast<const char*>(mpeg.encoded_bytes.data()),
                         static_cast<std::streamsize>(mpeg.encoded_bytes.size()));
            std::cout << sound.object_name << " bytes=" << mpeg.encoded_bytes.size() << '\n';
            std::uint32_t hash = 2166136261U;
            for (const auto byte : mpeg.encoded_bytes) hash = (hash ^ byte) * 16777619U;
            std::cout << "cache_name=" << sound.object_name << '.' << std::hex
                      << std::setfill('0') << std::setw(8) << hash << ".s16\n";
            return output.good() ? 0 : 1;
        }
        return 1;
    }
    if (argc != 2) {
        std::cerr << "usage: hpvr_hp1_sound_probe <sound.uax>\n";
        return 2;
    }
    const auto census = hpvr::wand::inspect_hp1_sound_assets(
        std::filesystem::path(argv[1]));
    if (census.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "sound_census_status=error error=" << census.error << '\n';
        return 1;
    }
    std::cout << "sound_census_status=ok version=" << census.package_version
              << " sounds=" << census.sounds.size() << '\n';
    for (const auto& sound : census.sounds) {
        const auto pcm = hpvr::wand::load_hp1_pcm_sound(
            std::filesystem::path(argv[1]), sound.sound_reference);
        const auto mpeg = hpvr::wand::load_hp1_mpeg_sound(
            std::filesystem::path(argv[1]), sound.sound_reference);
        std::cout << "sound_ref=" << sound.sound_reference
                  << " object=" << sound.object_name
                  << " serialized_bytes=" << sound.serialized_bytes
                  << " wave_bytes=" << sound.wave_bytes
                  << " pcm_status="
                  << (pcm.status == hpvr::wand::Hp1ProfileStatus::ok ? "ok" : "error")
                  << " rate=" << pcm.sample_rate
                  << " channels=" << pcm.channel_count
                  << " samples=" << pcm.samples.size();
        std::cout << " mpeg_status="
                  << (mpeg.status == hpvr::wand::Hp1ProfileStatus::ok
                          ? "ok" : "error")
                  << " mpeg_bytes=" << mpeg.encoded_bytes.size()
                  << " mpeg_rate=" << mpeg.sample_rate
                  << " mpeg_channels=" << mpeg.channel_count;
        if (!pcm.error.empty()) std::cout << " error=" << pcm.error;
        std::cout << " prefix=";
        for (std::size_t index = 0; index < sound.payload_prefix_bytes;
             ++index) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<unsigned>(sound.payload_prefix[index]);
        }
        std::cout << std::dec;
        std::cout << '\n';
    }
    return 0;
}
