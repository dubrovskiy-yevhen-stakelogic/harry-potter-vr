#include "hpvr/quest_broom_lesson.h"

#include <iostream>
#include <limits>

namespace {
using namespace hpvr;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
wand::Hp1ClassDefaultProperty Number(std::string name, float value, int index = -1) {
    wand::Hp1ClassDefaultProperty property;
    property.name = std::move(name); property.kind = 4; property.array_index = index;
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned i = 0; i < 4; ++i) property.value.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
    return property;
}
quest::broom::LessonDefaults Defaults() {
    quest::broom::LessonDefaults result;
    result.referee = {Number("MinHoopsToPass", 3), Number("PathToStartWith", 1),
        Number("TimeAddedEachStage", 100), Number("TimeAddedEachStage_Path2", 78)};
    result.player = {Number("AirSpeedNormal", 400), Number("AirSpeedBoost", 800),
        Number("PitchLimitUp", 60), Number("PitchLimitDown", 60)};
    result.hoop = {Number("CollisionRadius", 70), Number("CollisionHeight", 70),
        Number("PlayScale", 1), Number("fBobAmount", 24)};
    result.stages[0] = {Number("CollisionRadius", 90), Number("PlayScale", 1.333F)};
    return result;
}
wand::Hp1ActorVisualCensus Actors() {
    wand::Hp1ActorVisualCensus result; result.status = wand::Hp1ProfileStatus::ok;
    auto role = [&](const char* cls, int ref, const char* tag = "") {
        wand::Hp1ActorVisual a; a.actor_reference = ref; a.qualified_class_name = cls;
        a.tag = tag; a.tag_serialized = *tag != '\0'; result.actors.push_back(a);
    };
    role("HarryPotter.BroomHarry", 1); role("Tut2.BroomHooch", 2);
    role("Tut2.BroomPracticeReferee", 3);
    result.actors.back().serialized_properties = {Number("TimeAddedEachStage", 91), Number("TimeAddedEachStage", 5, 1)};
    role("HPBase.CutScene", 4, "Intro"); role("HPBase.CutScene", 5, "Redo"); role("HPBase.CutScene", 6, "Exit");
    for (unsigned path = 1; path <= 2; ++path) for (unsigned stage = 1; stage <= 5; ++stage) {
        wand::Hp1ActorVisual actor; actor.actor_reference = static_cast<std::int32_t>(100 + path * 10 + stage);
        actor.qualified_class_name = "HProps.BroomHoopStage" + std::to_string(stage);
        actor.location_serialized = true; actor.location_unreal = {100, 200, 300};
        actor.rotation_units[1] = 16384;
        actor.serialized_properties = {Number("PathNumber", static_cast<float>(path)), Number("CollisionRadius", 120)};
        result.actors.push_back(actor);
    }
    std::reverse(result.actors.begin(), result.actors.end());
    return result;
}
} // namespace

int main(int argc, char** argv) {
    try {
        using namespace hpvr::quest::broom;
        const auto actors = Actors(); const auto defaults = Defaults();
        const auto result = ParseLessonMetadata(actors, defaults);
        Check(result.valid, result.error.c_str());
        Check(result.hoops.size() == 10 && result.hoops.front().path == 1 && result.hoops.back().path == 2,
              "routes sort independently of actor slot order");
        Check(result.hoops.front().collision_radius_unreal == 120 && result.hoops.front().play_scale == 1.333F,
              "instance collision overrides stage while stage scale overrides base");
        Check(result.stage_seconds[0][0] == 91 && result.stage_seconds[0][1] == 5 && result.stage_seconds[1][0] == 78,
              "referee instance merges per-index stage times");
        Check(result.player_reference == 1 && result.scenes_by_tag.at("intro") == 4 && result.minimum_hits == 3,
              "authored roles and minimum result count");
        Check(std::abs(result.hoops.front().normal_unreal[0]) < .0001F &&
              std::abs(result.hoops.front().normal_unreal[1] - 1) < .0001F, "Unreal yaw maps to hoop plane normal");
        auto invalid = actors; invalid.actors.push_back(invalid.actors.front());
        Check(!ParseLessonMetadata(invalid, defaults).valid, "duplicate ordered hoop rejected");
        invalid = actors; invalid.actors.front().serialized_properties.push_back(Number("HoopNumber", 2));
        Check(!ParseLessonMetadata(invalid, defaults).valid, "missing first hoop rejected");
        invalid = actors; invalid.actors.front().serialized_properties[0] = Number("PathNumber", 3);
        Check(!ParseLessonMetadata(invalid, defaults).valid, "third path rejected");
        invalid = actors; invalid.actors.front().location_unreal.x = std::numeric_limits<float>::quiet_NaN();
        Check(!ParseLessonMetadata(invalid, defaults).valid, "non-finite location rejected");
        auto bad_defaults = defaults; bad_defaults.player.front().value.resize(1);
        Check(!ParseLessonMetadata(actors, bad_defaults).valid, "malformed numeric property rejected");
        invalid = actors; std::erase_if(invalid.actors, [](const auto& actor) { return actor.actor_reference == 1; });
        Check(!ParseLessonMetadata(invalid, defaults).valid, "missing player rejected");
        if (argc == 2) {
            const std::filesystem::path root = argv[1];
            const auto owned = LoadLessonMetadata(root, hpvr::wand::inspect_hp1_actor_visuals(root / "Maps/Lev_Tut2.unr"));
            Check(owned.valid, owned.error.c_str());
            Check(owned.hoops.size() == 164 && owned.player_reference == 75 && owned.teacher_reference == 73 &&
                  owned.referee_reference == 527 && owned.scenes_by_tag.at("intro") == 490, "owned lesson identities");
            for (unsigned path = 0; path < 2; ++path) for (unsigned stage = 0; stage < 5; ++stage)
                Check(owned.stages[path][stage].size() == std::array<unsigned, 5>{11,11,11,24,25}[stage], "owned two complete routes");
            Check(owned.stage_seconds[0] == std::array<float, 5>{100,5,5,20,15} &&
                  owned.stage_seconds[1] == std::array<float, 5>{78,4,4,15,11} && owned.minimum_hits == 30,
                  "owned lesson timing and pass threshold");
            std::cout << "OWNED_BROOM_METADATA=PASS hoops=" << owned.hoops.size() << " scenes=" << owned.scenes_by_tag.size() << '\n';
        }
        std::cout << "BROOM_METADATA_TESTS=PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "BROOM_METADATA_TESTS=FAIL " << error.what() << '\n'; return 1; }
}
