#include "hpvr/hp1_gesture.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc != 3 && argc != 5) {
        std::cerr << "usage: hpvr_hp1_profile_probe <HPBase.u> <lesson.unr> "
                     "[gesture-name spell-class-name]\n";
        return EXIT_FAILURE;
    }
    const std::string_view gesture = argc == 5 ? argv[3] : "FlipPattern";
    const std::string_view spell = argc == 5 ? argv[4] : "spellFlip";
    const auto profile = hpvr::wand::load_hp1_spell_profile(
        std::filesystem::path(argv[1]),
        std::filesystem::path(argv[2]),
        gesture,
        spell);
    if (profile.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "profile_status=" << static_cast<int>(profile.status)
                  << " error=" << profile.error << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "profile_status=ok"
              << " base_version=" << profile.base_package_version
              << " lesson_version=" << profile.lesson_package_version
              << " gesture=" << profile.gesture_name
              << " actor=" << profile.lesson_actor_name
              << " points=" << profile.template_points.size()
              << " segments=" << profile.segments.size()
              << " accuracy=" << profile.accuracy_radius
              << " very_good=" << profile.very_good_threshold
              << " very_bad=" << profile.very_bad_threshold
              << " default_draw_time=" << profile.default_draw_time_seconds
              << " lesson_draw_time=" << profile.draw_time_seconds
              << " sample_period_ns="
              << hpvr::wand::hp1_lesson_resampling_period_ns(
                     profile.draw_time_seconds)
              << " pass_marks=";
    for (std::size_t index = 0; index < profile.pass_mark_count; ++index) {
        if (index != 0) {
            std::cout << ',';
        }
        std::cout << profile.pass_marks[index];
    }
    std::cout << '\n';
    return EXIT_SUCCESS;
}
