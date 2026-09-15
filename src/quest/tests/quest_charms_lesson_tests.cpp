#include "hpvr/quest_charms_lesson.h"

#include <iostream>
#include <limits>

namespace {
using namespace hpvr;
void Check(bool value, const char* label) { if (!value) throw std::runtime_error(label); }
wand::Hp1ClassDefaultProperty Number(std::string name, float number, int index = -1) {
    wand::Hp1ClassDefaultProperty result;
    result.name = std::move(name); result.kind = 4; result.array_index = index;
    const auto bits = std::bit_cast<std::uint32_t>(number);
    for (unsigned i = 0; i < 4; ++i) result.value.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
    return result;
}
wand::Hp1ClassDefaultProperty Reference(std::string name, int ref, std::string target) {
    wand::Hp1ClassDefaultProperty result;
    result.name = std::move(name); result.kind = 5; result.object_reference_serialized = true;
    result.object_reference = ref; result.object_path = {std::move(target)};
    return result;
}
quest::charms::Properties Defaults() {
    quest::charms::Properties result = {Number("DrawTime", 6), Number("fAccuracy", .03F)};
    const std::array<float, 4> marks{.5F, .65F, .8F, .95F};
    for (unsigned i = 0; i < 4; ++i) {
        result.push_back(Number("PassMark", marks[i], static_cast<int>(i)));
        result.push_back(Number("iNumHousePoints", float(5 * (i + 1)), static_cast<int>(i)));
    }
    return result;
}
wand::Hp1ActorVisual Actor(bool levitate) {
    wand::Hp1ActorVisual result;
    result.actor_reference = levitate ? 3237 : 1200;
    result.qualified_class_name = "HPBase.SpellLearnTrigger";
    result.tag_serialized = result.event_serialized = true;
    result.tag = levitate ? "CharmsStart" : "AlohomoraStart";
    result.event = levitate ? "CutScriptII11" : "Aloholessondispatcher";
    result.serialized_properties = {Reference("Spell", -1, levitate ? "SPELLLEV" : "spellAloho"),
        Reference("Teacher", levitate ? 565 : 533, "Teacher"), Number("DrawTime", 12)};
    return result;
}
}

int main(int argc, char** argv) {
    try {
        using namespace hpvr::quest::charms;
        const auto defaults = Defaults();
        const Properties aloho{Reference("Gesture", 1137, "AlohoPattern")};
        const Properties wing{Reference("Gesture", 1509, "LevPattern")};
        auto metadata = ParseLesson(Actor(false), defaults, aloho);
        Check(metadata.valid && metadata.draw_seconds == 12 && metadata.house_points[3] == 20,
              "Map timing overrides class default while preserving score schedule");
        Check(ParseLesson(Actor(true), defaults, wing).spell == Spell::wingardium, "Wingardium identity");
        Check(!ParseLesson(Actor(true), defaults, aloho).valid, "Mismatched profile rejected");
        auto bad = defaults; bad.push_back(Number("PassMark", std::numeric_limits<float>::quiet_NaN(), 2));
        bad.erase(bad.begin() + 6);
        Check(!ParseLesson(Actor(false), bad, aloho).valid, "Non-finite pass mark rejected");
        auto actor = Actor(false); actor.event_serialized = false;
        Check(!ParseLesson(actor, defaults, aloho).valid, "Missing transition rejected");
        LessonSession session;
        Check(session.Submit(metadata, 1) == AttemptResult::ignored, "Unstarted session ignored");
        session.Begin();
        Check(session.Submit(metadata, .49F) == AttemptResult::retry && session.round == 0 && !session.learned,
              "First attempt failure retries");
        for (unsigned i = 0; i < 4; ++i) {
            const auto result = session.Submit(metadata, metadata.pass_marks[i]);
            Check(result == (i == 3 ? AttemptResult::complete : AttemptResult::passed), "Threshold boundary accepted");
        }
        Check(session.points == 50 && session.finished && session.learned, "Perfect lesson gives fifty points");
        Check(session.Submit(metadata, 1) == AttemptResult::ignored && session.points == 50, "No duplicate reward");
        session.Begin(); (void)session.Submit(metadata, 1);
        Check(session.Submit(metadata, .64F) == AttemptResult::complete && session.learned && session.points == 5,
              "Later failure ends lesson with earned points");
        session.Begin();
        Check(session.Submit(metadata, std::numeric_limits<float>::quiet_NaN()) == AttemptResult::ignored,
              "Non-finite scores ignored");
        Check(session.Submit(metadata, 2) == AttemptResult::ignored, "Out-of-range scores ignored");
        const std::array<LessonMetadata,2> session_lessons{metadata,ParseLesson(Actor(true),defaults,wing)};
        Check(ValidSession(session_lessons,{},-1,false),"Fresh unstarted checkpoint is valid");
        Check(ValidSession(session_lessons,session,0,false),"Active first lesson round is valid");
        auto invalid_session=session;invalid_session.points=50;
        Check(!ValidSession(session_lessons,invalid_session,0,false),"Impossible points before any completed round rejected");
        Check(!ValidSession(session_lessons,session,-1,false),"Unfinished lesson cannot lose its active teacher");
        Check(!ValidSession(session_lessons,session,0,true),"Finish pending requires a completed lesson");
        (void)session.Submit(metadata,1);
        Check(ValidSession(session_lessons,session,1,false),"Award sum validates an active later round");
        invalid_session=session;invalid_session.points=4;
        Check(!ValidSession(session_lessons,invalid_session,1,false),"Partial award totals rejected");
        (void)session.Submit(metadata,0);
        Check(ValidSession(session_lessons,session,1,true)&&ValidSession(session_lessons,session,-1,false),
              "Finished early lesson validates before and after its exit event");
        invalid_session=session;invalid_session.learned=false;
        Check(!ValidSession(session_lessons,invalid_session,-1,false),"Finished and learned flags must agree");
        Check(!ValidSession(session_lessons,session,1,false),"Active finished lesson must retain pending exit event");
        session.Begin();
        for(unsigned round=0;round<4;++round)(void)session.Submit(metadata,1);
        Check(ValidSession(session_lessons,session,-1,false),"Perfect completed checkpoint validates");
        invalid_session=session;invalid_session.finished=invalid_session.learned=false;
        Check(!ValidSession(session_lessons,invalid_session,0,false),"Fourth round must be terminal");
        if (argc == 2) {
            const std::filesystem::path root(argv[1]);
            const auto lessons = LoadLessons(root, wand::inspect_hp1_actor_visuals(root / "Maps/Lev_Tut3.unr"));
            for (const auto& lesson : lessons) {
                Check(lesson.valid, lesson.error.c_str());
                Check(lesson.draw_seconds == 12 && lesson.house_points == std::array<unsigned, 4>{5,10,15,20},
                      "Owned lessons preserve map time and original awards");
            }
            Check(lessons[0].actor_reference == 1200 && lessons[0].teacher_reference == 533 &&
                  lessons[1].actor_reference == 3237 && lessons[1].teacher_reference == 565,
                  "Owned lesson roles match authored map");
            std::cout << "OWNED_CHARMS_LESSONS=PASS lessons=2 rounds=4 max_points_each=50\n";
        }
        std::cout << "CHARMS_LESSONS=PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
