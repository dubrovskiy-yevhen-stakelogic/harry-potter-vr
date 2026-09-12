#pragma once

#include "hpvr/hp1_gesture.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace hpvr::quest::broom {

using LessonProperties = std::vector<wand::Hp1ClassDefaultProperty>;
struct LessonDefaults {
    LessonProperties referee, player, hoop;
    std::array<LessonProperties, 5> stages;
};

struct LessonHoop {
    std::int32_t id{};
    unsigned path{}, stage{}, order{};
    std::array<float, 3> center_unreal{}, normal_unreal{};
    float collision_radius_unreal{}, collision_height_unreal{}, play_scale{};
    bool bobbing{};
    float bob_amount_unreal{};
};

struct LessonMetadata {
    bool valid{};
    std::string error;
    std::vector<LessonHoop> hoops;
    std::array<std::array<std::vector<std::size_t>, 5>, 2> stages;
    std::array<std::array<float, 5>, 2> stage_seconds{};
    unsigned minimum_hits{}, first_path{};
    std::int32_t player_reference{}, teacher_reference{}, referee_reference{};
    std::map<std::string, std::int32_t> scenes_by_tag;
    float normal_speed_unreal{}, boost_speed_unreal{}, pitch_up_degrees{}, pitch_down_degrees{};
};

namespace lesson_detail {
inline std::string Fold(std::string_view value) {
    std::string result(value);
    for (auto& c : result) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return result;
}
inline const wand::Hp1ClassDefaultProperty* Property(
    const LessonProperties& values, std::string_view name, unsigned index = 0) {
    for (const auto& value : values)
        if (Fold(value.name) == Fold(name) &&
            static_cast<unsigned>(std::max<std::int64_t>(0, value.array_index)) == index)
            return &value;
    return nullptr;
}
inline void Merge(LessonProperties& into, const LessonProperties& overrides) {
    for (const auto& value : overrides) {
        const auto index = static_cast<unsigned>(std::max<std::int64_t>(0, value.array_index));
        std::erase_if(into, [&](const auto& previous) {
            return Fold(previous.name) == Fold(value.name) &&
                static_cast<unsigned>(std::max<std::int64_t>(0, previous.array_index)) == index;
        });
        into.push_back(value);
    }
}
inline float Number(const LessonProperties& values, std::string_view name,
                    float fallback, unsigned index = 0) {
    const auto* value = Property(values, name, index);
    if (!value) return fallback;
    if (value->value.size() != 4 || (value->kind != 2 && value->kind != 4))
        throw std::runtime_error("Malformed broom numeric property: " + std::string(name));
    std::uint32_t bits = 0;
    for (unsigned byte = 0; byte < 4; ++byte) bits |= std::uint32_t(value->value[byte]) << (byte * 8);
    const float result = value->kind == 4 ? std::bit_cast<float>(bits)
        : static_cast<float>(std::bit_cast<std::int32_t>(bits));
    if (!std::isfinite(result)) throw std::runtime_error("Non-finite broom property: " + std::string(name));
    return result;
}
inline bool Boolean(const LessonProperties& values, std::string_view name) {
    const auto* value = Property(values, name);
    if (!value) return false;
    if (!value->boolean_value_serialized) throw std::runtime_error("Malformed broom boolean property");
    return value->boolean_value;
}
inline unsigned Integer(const LessonProperties& values, std::string_view name,
                        unsigned fallback, unsigned maximum) {
    const auto value = Number(values, name, static_cast<float>(fallback));
    if (value < 1 || value > static_cast<float>(maximum) || std::floor(value) != value)
        throw std::runtime_error("Invalid broom index: " + std::string(name));
    return static_cast<unsigned>(value);
}
inline LessonProperties Class(const std::filesystem::path& package,
                              const wand::Hp1PackageLinkTable& table, std::string_view name) {
    const auto found = std::ranges::find_if(table.exports, [&](const auto& item) {
        return item.qualified_class_name == "Core.Class" && item.object_path.size() == 1 &&
            Fold(item.object_path.front()) == Fold(name);
    });
    if (found == table.exports.end()) throw std::runtime_error("Missing broom class: " + std::string(name));
    const auto defaults = wand::inspect_hp1_class_visual_defaults(package, found->reference);
    if (defaults.status != wand::Hp1ProfileStatus::ok) throw std::runtime_error(defaults.error);
    return defaults.serialized_properties;
}
} // namespace lesson_detail

inline LessonMetadata ParseLessonMetadata(const wand::Hp1ActorVisualCensus& census,
                                         const LessonDefaults& defaults) {
    LessonMetadata result;
    try {
        using namespace lesson_detail;
        if (census.status != wand::Hp1ProfileStatus::ok) throw std::runtime_error(census.error);
        LessonProperties referee = defaults.referee, player = defaults.player;
        for (const auto& actor : census.actors) {
            const auto cls = Fold(actor.qualified_class_name);
            auto single = [&](std::int32_t& into) {
                if (into != 0 || actor.actor_reference <= 0) throw std::runtime_error("Duplicate broom lesson role");
                into = actor.actor_reference;
            };
            if (cls == "harrypotter.broomharry") {
                single(result.player_reference); Merge(player, actor.serialized_properties);
            } else if (cls == "tut2.broomhooch") single(result.teacher_reference);
            else if (cls == "tut2.broompracticereferee") {
                single(result.referee_reference); Merge(referee, actor.serialized_properties);
            } else if (cls == "hpbase.cutscene" && actor.tag_serialized) {
                if (!result.scenes_by_tag.emplace(Fold(actor.tag), actor.actor_reference).second)
                    throw std::runtime_error("Duplicate broom cutscene tag");
            }
            constexpr std::string_view prefix = "hprops.broomhoopstage";
            if (!cls.starts_with(prefix)) continue;
            if (cls.size() != prefix.size() + 1 || cls.back() < '1' || cls.back() > '5' ||
                actor.actor_reference <= 0 || !actor.location_serialized)
                throw std::runtime_error("Invalid broom hoop identity or location");
            LessonHoop hoop;
            hoop.id = actor.actor_reference; hoop.stage = static_cast<unsigned>(cls.back() - '0');
            auto properties = defaults.hoop;
            Merge(properties, defaults.stages[hoop.stage - 1]);
            Merge(properties, actor.serialized_properties);
            hoop.path = Integer(properties, "PathNumber", 1, 2);
            hoop.order = Integer(properties, "HoopNumber", 1, 25);
            hoop.center_unreal = {actor.location_unreal.x, actor.location_unreal.y, actor.location_unreal.z};
            for (const float v : hoop.center_unreal)
                if (!std::isfinite(v)) throw std::runtime_error("Non-finite broom hoop location");
            constexpr float radians = 6.283185307179586F / 65536.0F;
            const float pitch = static_cast<float>(actor.rotation_units[0]) * radians;
            const float yaw = static_cast<float>(actor.rotation_units[1]) * radians;
            hoop.normal_unreal = {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
            hoop.collision_radius_unreal = Number(properties, "CollisionRadius", 0);
            hoop.collision_height_unreal = Number(properties, "CollisionHeight", 0);
            hoop.play_scale = Number(properties, "PlayScale", 0);
            hoop.bobbing = Boolean(properties, "bBobbing");
            hoop.bob_amount_unreal = Number(properties, "fBobAmount", 0);
            if (hoop.collision_radius_unreal <= 0 || hoop.collision_height_unreal <= 0 ||
                hoop.play_scale <= 0 || hoop.bob_amount_unreal < 0)
                throw std::runtime_error("Invalid broom hoop dimensions");
            result.hoops.push_back(hoop);
        }
        if (!result.player_reference || !result.teacher_reference || !result.referee_reference ||
            !result.scenes_by_tag.contains("intro") || !result.scenes_by_tag.contains("redo") ||
            !result.scenes_by_tag.contains("exit")) throw std::runtime_error("Incomplete broom lesson roles");
        std::ranges::sort(result.hoops, [](const auto& a, const auto& b) {
            return std::array{a.path, a.stage, a.order} < std::array{b.path, b.stage, b.order};
        });
        for (std::size_t index = 0; index < result.hoops.size(); ++index) {
            const auto& hoop = result.hoops[index];
            auto& stage = result.stages[hoop.path - 1][hoop.stage - 1];
            if (hoop.order != stage.size() + 1) throw std::runtime_error("Missing or duplicate ordered broom hoop");
            stage.push_back(index);
        }
        for (unsigned path = 0; path < 2; ++path) {
            const auto property = path == 0 ? "TimeAddedEachStage" : "TimeAddedEachStage_Path2";
            for (unsigned stage = 0; stage < 5; ++stage) {
                if (result.stages[path][stage].empty()) throw std::runtime_error("Missing broom route stage");
                result.stage_seconds[path][stage] = Number(referee, property, 0, stage);
                if (result.stage_seconds[path][stage] < 0 ||
                    (stage == 0 && result.stage_seconds[path][stage] == 0))
                    throw std::runtime_error("Invalid broom stage time");
            }
        }
        result.minimum_hits = Integer(referee, "MinHoopsToPass", 0, 125);
        result.first_path = Integer(referee, "PathToStartWith", 1, 2);
        result.normal_speed_unreal = Number(player, "AirSpeedNormal", 0);
        result.boost_speed_unreal = Number(player, "AirSpeedBoost", 0);
        result.pitch_up_degrees = Number(player, "PitchLimitUp", 0);
        result.pitch_down_degrees = Number(player, "PitchLimitDown", 0);
        if (result.normal_speed_unreal <= 0 || result.boost_speed_unreal < result.normal_speed_unreal ||
            result.pitch_up_degrees <= 0 || result.pitch_up_degrees >= 90 ||
            result.pitch_down_degrees <= 0 || result.pitch_down_degrees >= 90)
            throw std::runtime_error("Invalid broom flight settings");
        result.valid = true;
    } catch (const std::exception& error) {
        result.error = error.what();
    }
    return result;
}

inline LessonMetadata LoadLessonMetadata(const std::filesystem::path& root,
                                        const wand::Hp1ActorVisualCensus& census) {
    try {
        using namespace lesson_detail;
        const auto props = root / "system/HProps.u", tut = root / "system/Tut2.u",
                   harry = root / "system/HarryPotter.u";
        const auto props_table = wand::inspect_hp1_package_link_table(props);
        const auto tut_table = wand::inspect_hp1_package_link_table(tut);
        const auto harry_table = wand::inspect_hp1_package_link_table(harry);
        if (props_table.status != wand::Hp1ProfileStatus::ok || tut_table.status != wand::Hp1ProfileStatus::ok ||
            harry_table.status != wand::Hp1ProfileStatus::ok) throw std::runtime_error("Broom lesson packages unavailable");
        LessonDefaults defaults;
        defaults.hoop = Class(props, props_table, "BroomHoop");
        for (unsigned i = 0; i < 5; ++i) defaults.stages[i] = Class(props, props_table, "BroomHoopStage" + std::to_string(i + 1));
        defaults.referee = Class(tut, tut_table, "BroomPracticeReferee");
        defaults.player = Class(harry, harry_table, "BroomHarry");
        return ParseLessonMetadata(census, defaults);
    } catch (const std::exception& error) {
        LessonMetadata result; result.error = error.what(); return result;
    }
}

} // namespace hpvr::quest::broom
