#include "hpvr/quest_broom_session.h"

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace hpvr::quest::broom;

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

bool Near(double a, double b) { return std::abs(a - b) < .0001; }

struct Fixture {
    std::array<std::vector<Hoop>, 5> routes;
    SessionConfig config;
    Fixture() {
        const std::array<unsigned, 5> counts{11, 11, 11, 24, 25};
        std::int32_t id = 1;
        for (std::size_t stage = 0; stage < counts.size(); ++stage) {
            for (unsigned index = 0; index < counts[stage]; ++index, ++id)
                routes[stage].push_back({id, {0, 0, float(id)}, {0, 0, 1}, 1});
            config.stages[stage] = routes[stage];
        }
        config.stage_time_bonuses = {100, 5, 5, 20, 15};
        config.minimum_hits = 30;
    }
};

void TestAssessment() {
    struct Expected { std::size_t hits; SessionGrade grade; unsigned points; };
    const std::array<Expected, 9> expected{{
        Expected{0, SessionGrade::Redo, 0}, {29, SessionGrade::Redo, 0},
        {30, SessionGrade::Pass, 5}, {46, SessionGrade::Pass, 5},
        {47, SessionGrade::Good, 10}, {63, SessionGrade::Good, 10},
        {64, SessionGrade::Excellent, 15}, {81, SessionGrade::Excellent, 15},
        {82, SessionGrade::Perfect, 20}}};
    for (const auto& item : expected) {
        const auto result = EvaluateSession(item.hits, 82, 30);
        Check(result.grade == item.grade && result.house_points == item.points,
              "assessment preserves the original integer score thresholds");
        Check(result.passed == (item.points != 0) && result.alternate_path == (item.hits == 82),
              "only a perfect trial requests the alternate bonus path");
    }
    Check(EvaluateSession(83, 82, 30).grade == SessionGrade::Invalid &&
          EvaluateSession(0, 0, 0).grade == SessionGrade::Invalid &&
          EvaluateSession(5, 5, 6).grade == SessionGrade::Invalid,
          "impossible score inputs cannot grant points");
    Check(std::string_view(SessionResultTag(SessionGrade::Good)) == "good" &&
          std::string_view(SessionResultTag(SessionGrade::None)).empty(),
          "result events map to the authored assessment tags");
}

void TestCountdownAndStages() {
    Fixture fixture;
    SessionState state;
    Check(BeginSession(state, fixture.config) && state.total_hoops == 82,
          "a complete five-stage route starts with the authored total");
    Check(Near(SessionTimeRemaining(state), 100), "initial countdown uses only the first stage allocation");
    RebaseSession(state, {0, 0, 0});
    const auto stage = AdvanceSession(state, fixture.config, {0, 0, 11}, 40);
    Check(stage.hits_added == 11 && stage.stages_advanced == 1 && state.stage == 1 &&
          Near(SessionTimeRemaining(state), 65), "finishing stage one with sixty seconds adds five seconds");
    Check(state.route.next_index == 0 && state.hits == 11, "stage-local index resets but trial hit count remains");
    const auto paused = AdvanceSession(state, fixture.config, {0, 0, 30}, 300, true);
    Check(paused.hits_added == 0 && Near(SessionTimeRemaining(state), 65),
          "opening a menu pauses both score and countdown");
    Check(AdvanceSession(state, fixture.config, {0, 0, 30}, 1).hits_added == 0 && state.hits == 11,
          "focus return rebases instead of sweeping across rings moved past while paused");
    const auto expired = AdvanceSession(state, fixture.config, {2, 0, 30}, 100);
    Check(expired.finished && state.status == SessionStatus::Finished &&
          state.result.grade == SessionGrade::Redo && SessionTimeRemaining(state) == 0,
          "countdown expiration below the minimum requests a retry");
    Check(!AdvanceSession(state, fixture.config, {0, 0, 82}, 1).finished,
          "a finished result emits only once");
    Check(RetrySession(state, fixture.config) && state.stage == 0 && state.hits == 0 &&
          !state.previous_valid && Near(SessionTimeRemaining(state), 100),
          "retry clears stale movement, score, stage and elapsed time");

    RebaseSession(state, {0, 0, 0});
    const auto thirty = AdvanceSession(state, fixture.config, {0, 0, 30}, 30);
    Check(thirty.hits_added == 30 && thirty.stages_advanced == 2 && !thirty.finished &&
          state.status == SessionStatus::Active && Near(SessionTimeRemaining(state), 80),
          "reaching the pass minimum keeps flying and preserves cumulative stage bonuses");
    const auto pass = AdvanceSession(state, fixture.config, {0, 0, 30}, 80);
    Check(pass.finished && state.result.grade == SessionGrade::Pass && state.result.house_points == 5,
          "timeout assesses the actual cumulative hit count rather than requiring all hoops");

    Check(RetrySession(state, fixture.config), "a passed route can be replayed explicitly");
    RebaseSession(state, {0, 0, 0});
    const auto perfect = AdvanceSession(state, fixture.config, {0, 0, 82}, 82);
    Check(perfect.finished && perfect.hits_added == 82 && perfect.stages_advanced == 4 &&
          state.result.grade == SessionGrade::Perfect && Near(SessionTimeRemaining(state), 63),
          "one swept segment handles stage boundaries in order with all time bonuses");
    ResetSession(state);
    Check(state.status == SessionStatus::Idle && state.total_hoops == 0 &&
          SessionTimeRemaining(state) == 0, "reset cannot retain flight progress in another level");
}

void TestDiscontinuities() {
    Fixture fixture;
    SessionState state;
    Check(BeginSession(state, fixture.config), "test session starts");
    Check(AdvanceSession(state, fixture.config, {0, 0, 40}, 1).hits_added == 0,
          "a first tracking sample is not movement from world origin");
    Check(AdvanceSession(state, fixture.config, {0, 0, -5}, 1, false, true).hits_added == 0,
          "an explicit recenter or teleport cannot earn hoops on the way");
    Check(state.hits == 0 && Near(SessionTimeRemaining(state), 98),
          "rebasing position does not reset or pause the authored timer");
    RebaseSession(state, {0, 0, 0});
    Check(AdvanceSession(state, fixture.config, {0, 0, 0}, 1).hits_added == 0,
          "collision-blocked flight leaves the center still and cannot collect a hoop");
    Check(AdvanceSession(state, fixture.config, {0, 0, 1.5F}, 1).hits_added == 1,
          "normal resolved movement works immediately after explicit rebasing");
    const auto remaining = SessionTimeRemaining(state);
    Check(AdvanceSession(state, fixture.config, {0, 0, 20},
                         std::numeric_limits<float>::quiet_NaN()).hits_added == 0 &&
          SessionTimeRemaining(state) == remaining,
          "invalid delta cannot award rings or poison the timer");
    Check(AdvanceSession(state, fixture.config, {0, 0, 20}, 1).hits_added == 0,
          "movement across an invalid tracking interval is not replayed later");
    Check(AdvanceSession(state, fixture.config, {0, 0, std::numeric_limits<float>::infinity()}, 1).hits_added == 0,
          "invalid tracking coordinates cannot award rings");
    Check(AdvanceSession(state, fixture.config, {0, 0, -5}, 1).hits_added == 0,
          "returning from invalid tracking creates a fresh baseline");
}

void TestInvalidConfiguration() {
    Fixture fixture;
    SessionState state;
    auto bad = fixture.config;
    bad.stage_time_bonuses[0] = 0;
    Check(!BeginSession(state, bad), "a timed lesson cannot accidentally start untimed");
    bad = fixture.config;
    bad.hoop_assist_radius = std::numeric_limits<float>::quiet_NaN();
    Check(!BeginSession(state,bad), "invalid VR assistance cannot start a trial");
    bad = fixture.config;
    bad.stage_time_bonuses[4] = -1;
    Check(!BeginSession(state, bad), "negative bonus time is invalid");
    bad = fixture.config;
    bad.stages[3] = {};
    Check(!BeginSession(state, bad), "missing stage data cannot skip required gameplay");
    bad = fixture.config;
    bad.minimum_hits = 83;
    Check(!BeginSession(state, bad), "a pass score exceeding the route is rejected");
    bad = fixture.config;
    bad.stages[1] = bad.stages[0];
    Check(!BeginSession(state, bad), "one actor cannot grant progress in two stages");
    Check(state.status == SessionStatus::Invalid && SessionTimeRemaining(state) == 0,
          "invalid configuration fails closed");
}

void TestVrAllowanceAcrossStages() {
    Fixture fixture;
    fixture.config.hoop_assist_radius=.18F;
    SessionState state;
    Check(BeginSession(state,fixture.config), "VR-assisted session starts");
    RebaseSession(state,{1.14F,0,0});
    const auto passed=AdvanceSession(state,fixture.config,{1.14F,0,82},82);
    Check(passed.finished&&passed.hits_added==82&&passed.stages_advanced==4,
          "VR allowance survives every stage transition without changing the authored route");
    Check(BeginSession(state,fixture.config), "grazing test starts");
    RebaseSession(state,{1.29F,0,0});
    Check(AdvanceSession(state,fixture.config,{1.29F,0,82},82).hits_added==0&&state.hits==0,
          "a capsule merely grazing the route earns no rings");
    Check(BeginSession(state,fixture.config), "out-of-order assisted test starts");
    RebaseSession(state,{1.14F,0,1.5F});
    Check(AdvanceSession(state,fixture.config,{1.14F,0,4},1).hits_added==0,
          "VR assistance never bypasses ring order");
}
} // namespace

int main() {
    try {
        TestAssessment();
        TestCountdownAndStages();
        TestDiscontinuities();
        TestInvalidConfiguration();
        TestVrAllowanceAcrossStages();
        std::cout << "BROOM_SESSION_TESTS=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
