#pragma once

#include "hpvr/hp1_gesture.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace hpvr::quest::charms {

enum class Spell : unsigned { alohomora, wingardium };
using Properties = std::vector<wand::Hp1ClassDefaultProperty>;

struct LessonMetadata {
    bool valid{};
    std::string error;
    Spell spell{};
    std::int32_t actor_reference{}, teacher_reference{};
    std::string spell_class, pattern_class, tag, event;
    float draw_seconds{}, accuracy{};
    std::array<float, 4> pass_marks{};
    std::array<unsigned, 4> house_points{};
    Properties properties;
};

namespace detail {
inline std::string Fold(std::string_view value) {
    std::string result(value);
    for (auto& c : result) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return result;
}
inline const wand::Hp1ClassDefaultProperty* Property(const Properties& values,
                                                   std::string_view name,
                                                   unsigned index = 0) {
    for (const auto& value : values)
        if (Fold(value.name) == Fold(name) &&
            static_cast<unsigned>(std::max<std::int64_t>(0, value.array_index)) == index)
            return &value;
    return nullptr;
}
inline void Merge(Properties& into, const Properties& overrides) {
    for (const auto& value : overrides) {
        const auto index = std::max<std::int64_t>(0, value.array_index);
        std::erase_if(into, [&](const auto& old) {
            return Fold(old.name) == Fold(value.name) &&
                std::max<std::int64_t>(0, old.array_index) == index;
        });
        into.push_back(value);
    }
}
inline float Number(const Properties& values, std::string_view name, unsigned index = 0) {
    const auto* value = Property(values, name, index);
    if (!value || value->value.size() != 4 || (value->kind != 2 && value->kind != 4))
        throw std::runtime_error("Missing or malformed lesson property: " + std::string(name));
    std::uint32_t bits{};
    for (unsigned byte = 0; byte < 4; ++byte)
        bits |= std::uint32_t(value->value[byte]) << (byte * 8);
    const float result = value->kind == 4 ? std::bit_cast<float>(bits)
        : static_cast<float>(std::bit_cast<std::int32_t>(bits));
    if (!std::isfinite(result)) throw std::runtime_error("Non-finite lesson property");
    return result;
}
inline std::string ReferenceName(const Properties& values, std::string_view name) {
    const auto* value = Property(values, name);
    if (!value || !value->object_reference_serialized || value->object_reference == 0 ||
        value->object_path.empty()) throw std::runtime_error("Missing lesson reference: " + std::string(name));
    return value->object_path.back();
}
inline Properties Class(const std::filesystem::path& package,
                        const wand::Hp1PackageLinkTable& table, std::string_view name) {
    const auto found = std::ranges::find_if(table.exports, [&](const auto& item) {
        return item.qualified_class_name == "Core.Class" && item.object_path.size() == 1 &&
            Fold(item.object_path.front()) == Fold(name);
    });
    if (found == table.exports.end()) throw std::runtime_error("Missing lesson class: " + std::string(name));
    const auto value = wand::inspect_hp1_class_visual_defaults(package, found->reference);
    if (value.status != wand::Hp1ProfileStatus::ok) throw std::runtime_error(value.error);
    return value.serialized_properties;
}
} // namespace detail

inline LessonMetadata ParseLesson(const wand::Hp1ActorVisual& actor,
                                 const Properties& defaults,
                                 const Properties& spell_defaults) {
    LessonMetadata result;
    try {
        using namespace detail;
        if (Fold(actor.qualified_class_name) != "hpbase.spelllearntrigger" || actor.actor_reference <= 0)
            throw std::runtime_error("Invalid charms lesson actor");
        result.actor_reference = actor.actor_reference;
        result.properties = defaults;
        Merge(result.properties, actor.serialized_properties);
        result.spell_class = ReferenceName(result.properties, "Spell");
        const auto spell = Fold(result.spell_class);
        if (spell == "spellaloho") result.spell = Spell::alohomora;
        else if (spell == "spelllev") result.spell = Spell::wingardium;
        else throw std::runtime_error("Unexpected charms lesson spell");
        result.pattern_class = ReferenceName(spell_defaults, "Gesture");
        const auto expected = result.spell == Spell::alohomora ? "alohopattern" : "levpattern";
        if (Fold(result.pattern_class) != expected) throw std::runtime_error("Mismatched charms lesson gesture");
        const auto* teacher = Property(result.properties, "Teacher");
        if (!teacher || !teacher->object_reference_serialized || teacher->object_reference <= 0)
            throw std::runtime_error("Missing charms lesson teacher");
        result.teacher_reference = teacher->object_reference;
        result.tag = actor.tag;
        result.event = actor.event;
        if (!actor.tag_serialized || !actor.event_serialized || result.tag.empty() || result.event.empty())
            throw std::runtime_error("Missing charms lesson event");
        result.draw_seconds = Number(result.properties, "DrawTime");
        result.accuracy = Number(result.properties, "fAccuracy");
        if (result.draw_seconds <= 0 || result.draw_seconds > 120 || result.accuracy <= 0 || result.accuracy > 1)
            throw std::runtime_error("Invalid charms lesson timing or accuracy");
        for (unsigned i = 0; i < result.pass_marks.size(); ++i) {
            result.pass_marks[i] = Number(result.properties, "PassMark", i);
            const float points = Number(result.properties, "iNumHousePoints", i);
            if (result.pass_marks[i] <= 0 || result.pass_marks[i] > 1 ||
                (i && result.pass_marks[i] < result.pass_marks[i - 1]) ||
                points < 0 || points > 1000 || std::floor(points) != points)
                throw std::runtime_error("Invalid charms lesson score schedule");
            result.house_points[i] = static_cast<unsigned>(points);
        }
        result.valid = true;
    } catch (const std::exception& error) { result.error = error.what(); }
    return result;
}

inline std::array<LessonMetadata, 2> LoadLessons(const std::filesystem::path& root,
                                              const wand::Hp1ActorVisualCensus& census) {
    std::array<LessonMetadata, 2> result;
    try {
        using namespace detail;
        if (census.status != wand::Hp1ProfileStatus::ok) throw std::runtime_error(census.error);
        const auto package = root / "system/HPBase.u";
        const auto table = wand::inspect_hp1_package_link_table(package);
        if (table.status != wand::Hp1ProfileStatus::ok) throw std::runtime_error(table.error);
        const auto defaults = Class(package, table, "SpellLearnTrigger");
        for (const auto& actor : census.actors) {
            if (Fold(actor.qualified_class_name) != "hpbase.spelllearntrigger") continue;
            const auto spell = ReferenceName(actor.serialized_properties, "Spell");
            const auto name = Fold(spell);
            if (name != "spellaloho" && name != "spelllev") continue;
            const unsigned index = name == "spellaloho" ? 0U : 1U;
            if (result[index].actor_reference) throw std::runtime_error("Duplicate charms lesson");
            result[index] = ParseLesson(actor, defaults, Class(package, table, spell));
            if (!result[index].valid) throw std::runtime_error(result[index].error);
            if (!std::ranges::any_of(census.actors, [&](const auto& candidate) {
                    return candidate.actor_reference == result[index].teacher_reference;
                })) throw std::runtime_error("Charms lesson teacher is not in the map");
        }
        for (const auto& lesson : result)
            if (!lesson.valid) throw std::runtime_error("Missing charms lesson");
    } catch (const std::exception& error) {
        for (auto& lesson : result) { lesson.valid = false; lesson.error = error.what(); }
    }
    return result;
}

enum class AttemptResult { ignored, retry, passed, complete };
struct LessonSession {
    unsigned round{}, points{};
    bool started{}, finished{}, learned{};
    void Begin() { *this = {}; started = true; }
    [[nodiscard]] AttemptResult Submit(const LessonMetadata& lesson, float score) {
        if (!started || finished || !lesson.valid || round >= 4 || !std::isfinite(score) || score < 0 || score > 1)
            return AttemptResult::ignored;
        if (score < lesson.pass_marks[round]) {
            if (round == 0) return AttemptResult::retry;
            finished = learned = true;
            return AttemptResult::complete;
        }
        points += lesson.house_points[round++];
        if (round == 4) {
            finished = learned = true;
            return AttemptResult::complete;
        }
        return AttemptResult::passed;
    }
};

inline bool ValidSession(const std::array<LessonMetadata,2>& lessons,const LessonSession& session,
                         int active_lesson,bool finish_pending) {
    if(active_lesson< -1||active_lesson>1||session.round>4||session.finished!=session.learned)return false;
    if(!session.started)return active_lesson== -1&&!finish_pending&&!session.finished&&session.round==0&&session.points==0;
    if(session.finished&&session.round==0)return false;
    if(session.round==4&&!session.finished)return false;
    if(active_lesson<0&&(!session.finished||finish_pending))return false;
    if(active_lesson>=0&&session.finished!=finish_pending)return false;
    const auto& metadata=lessons[static_cast<unsigned>(std::max(0,active_lesson))];
    if(!metadata.valid)return false;
    unsigned expected_points=0;
    for(unsigned round=0;round<session.round;++round)expected_points+=metadata.house_points[round];
    return session.points==expected_points;
}

} // namespace hpvr::quest::charms
