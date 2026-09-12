#include "hpvr/quest_broom_flight.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace hpvr::quest::broom;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool Near(float a, float b, float tolerance = .0001F) { return std::abs(a - b) <= tolerance; }

void TestFlight() {
    FlightConfig config{4, 2, 8, 16, .05F};
    FlightInput input{1, 0, 0, {0, 0, -1}};
    FlightMotion motion;
    const auto first = AdvanceFlight(motion, input, config, .05F);
    Check(Near(motion.velocity[2], -.4F) && Near(first[2], -.01F),
          "flight ramps speed and integrates acceleration without an initial teleport");
    Check(first[0] == 0 && first[1] == 0, "forward flight does not change altitude or strafe");
    for (unsigned frame = 0; frame < 30; ++frame) (void)AdvanceFlight(motion, input, config, .05F);
    Check(Near(motion.velocity[2], -4), "flight reaches the caller's speed cap");
    const auto stopped = AdvanceFlight(motion, {}, config, .05F);
    Check(Near(motion.velocity[2], -3.2F) && Near(stopped[2], -.18F),
          "neutral input decelerates with the caller's braking rate");
    for (unsigned frame = 0; frame < 10; ++frame) (void)AdvanceFlight(motion, {}, config, .05F);
    Check(motion.velocity == Point{}, "braking settles exactly without reversing");

    motion.velocity = {0, 0, -.1F};
    const auto short_stop = AdvanceFlight(motion, {}, config, .05F);
    Check(Near(short_stop[2], -.0003125F, .000001F) && motion.velocity == Point{},
          "a within-frame stop travels only until velocity reaches zero");
    ResetFlight(motion);
    const auto stall = AdvanceFlight(motion, input, config, 10);
    Check(stall == first && Near(motion.velocity[2], -.4F),
          "a long frame is clamped instead of catching up through the map");
    const auto paused_velocity = motion.velocity;
    Check(AdvanceFlight(motion, input, config, 10, true) == Point{} &&
          motion.velocity == paused_velocity, "pause freezes movement and preserves velocity");

    config.acceleration = 1000;
    config.braking = 1000;
    ResetFlight(motion);
    input = {1, 1, 1, {0, 100, -5}};
    (void)AdvanceFlight(motion, input, config, .05F);
    Check(Near(std::hypot(motion.velocity[0], motion.velocity[2]), 4) &&
          Near(motion.velocity[1], 2), "diagonal input has no speed boost and pitch cannot steer altitude");
    Check(motion.velocity[0] > 0 && motion.velocity[2] < 0, "right strafe uses the planar heading");
    input = {1, 0, -.5F, {1, 5, 0}};
    (void)AdvanceFlight(motion, input, config, .05F);
    Check(Near(motion.velocity[0], 4) && Near(motion.velocity[1], -1) &&
          Near(motion.velocity[2], 0), "horizontal yaw and proportional altitude input stay independent");
    input = {1, 0, 0, {0, 1, 0}};
    (void)AdvanceFlight(motion, input, config, .05F);
    Check(Near(motion.velocity[2], -4), "a vertical or missing heading uses a finite planar fallback");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const float invalid : {-1.0F, nan, infinity}) {
        const auto velocity = motion.velocity;
        Check(AdvanceFlight(motion, input, config, invalid) == Point{} && motion.velocity == velocity,
              "invalid elapsed time cannot move the player or poison velocity");
    }
    input = {nan, infinity, nan, {nan, 0, infinity}};
    (void)AdvanceFlight(motion, input, config, .05F);
    Check(motion.velocity == Point{}, "invalid controller axes safely become neutral");
    motion.velocity[0] = nan;
    Check(AdvanceFlight(motion, input, config, .05F) == Point{} && motion.velocity == Point{},
          "invalid motion is reset before integration");
    config.speed = infinity;
    motion.velocity = {1, 2, 3};
    Check(AdvanceFlight(motion, input, config, .05F) == Point{} && motion.velocity == Point{},
          "invalid speed configuration fails closed");
    ResetFlight(motion);
    Check(motion.velocity == Point{}, "reset removes old map flight momentum");
}

void TestHoopSweep() {
    const Hoop hoop{1, {0, 1, 0}, {0, 0, 4}, 2};
    float crossing = -1;
    Check(SweepHoop({0, 1, -10}, {0, 1, 10}, hoop, &crossing) && Near(crossing, .5F),
          "a fast segment through an oriented hoop cannot tunnel past it");
    Check(SweepHoop({0, 1, 10}, {0, 1, -10}, hoop), "hoops permit traversal from either side");
    Check(SweepHoop({2, 1, -1}, {2, 1, 1}, hoop), "the aperture includes its rim boundary");
    Check(!SweepHoop({2.01F, 1, -1}, {2.01F, 1, 1}, hoop), "crossing outside the radius is not a hit");
    Check(!SweepHoop({0, 1, -2}, {0, 1, -1}, hoop), "approaching a hoop without crossing does not count");
    Check(!SweepHoop({0, 1, 0}, {0, 1, 0}, hoop), "standing inside the aperture does not count");
    Check(!SweepHoop({0, 1, 0}, {0, 1, 1}, hoop), "starting on the plane does not award a free hit");
    Check(SweepHoop({0, 1, -1}, {0, 1, 0}, hoop), "ending exactly on the plane awards the crossing once");
    Check(!SweepHoop({-1, 1, 0}, {1, 1, 0}, hoop), "movement within the plane is not traversal");
    Check(!SweepHoop({0, 1, -2}, {3, 1, .1F}, hoop), "closest distance alone cannot award a near miss");
    Check(SweepHoop({-5, 0, -5}, {5, 0, 5}, {2, {0, 0, 0}, {1, 0, 1}, 1}),
          "a diagonal hoop uses its own plane rather than a world-axis approximation");
    Check(SweepHoop({0, -3, 0}, {0, 3, 0}, {3, {0, 0, 0}, {0, 1, 0}, 1}),
          "a horizontal hoop supports vertical flight");
    for (const float invalid : {0.0F, -1.0F, std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::quiet_NaN()}) {
        Hoop bad = hoop;
        bad.radius = invalid;
        Check(!SweepHoop({0, 1, -1}, {0, 1, 1}, bad), "invalid hoop radius fails closed");
    }
    Hoop bad = hoop;
    bad.normal = {};
    Check(!ValidHoop(bad), "a hoop without an orientation is invalid");
    Check(!SweepHoop({0, 1, std::numeric_limits<float>::quiet_NaN()}, {0, 1, 1}, hoop),
          "invalid tracking coordinates cannot award progress");
}

void TestRoute() {
    const std::array<Hoop, 3> route{{
        {10, {0, 0, 0}, {0, 0, 1}, 1},
        {20, {0, 0, 2}, {0, 0, 1}, 1},
        {30, {0, 0, 4}, {0, 0, 1}, 1}}};
    RouteState state;
    Check(BeginRoute(state, 4, route, 20), "a valid unique route starts");
    Check(state.stage_id == 4 && state.required_count == 3 && state.next_index == 0,
          "stage identity and whole-route default are retained");
    Check(AdvanceRoute(state, route, {0, 0, 1}, {0, 0, 3}, .5F).hits_added == 0,
          "a later hoop cannot be collected out of order");
    const auto first = AdvanceRoute(state, route, {0, 0, -1}, {0, 0, 1}, .5F);
    Check(first.hits_added == 1 && state.hit_count == 1 && state.next_index == 1,
          "the next hoop increments progress once");
    Check(AdvanceRoute(state, route, {0, 0, 1}, {0, 0, -1}, .5F).hits_added == 0,
          "revisiting a collected hoop cannot duplicate progress");
    const auto elapsed = state.elapsed_seconds;
    Check(AdvanceRoute(state, route, {0, 0, -1}, {0, 0, 5}, 100, true).hits_added == 0 &&
          state.elapsed_seconds == elapsed, "a paused menu freezes both the clock and route");
    const auto done = AdvanceRoute(state, route, {0, 0, 1}, {0, 0, 5}, .5F);
    Check(done.hits_added == 2 && done.completed && state.status == RouteStatus::Completed,
          "a fast frame can cross several correctly ordered hoops");
    Check(!AdvanceRoute(state, route, {0, 0, -1}, {0, 0, 5}, 1).completed,
          "completion is an edge event, not an event repeated every frame");

    RetryRoute(state);
    Check(state.status == RouteStatus::Active && state.hit_count == 0 && state.next_index == 0 &&
          state.elapsed_seconds == 0 && state.stage_id == 4, "retry resets progress and preserves stage policy");
    Check(BeginRoute(state, 5, route, 1, 2), "a caller can require fewer than all hoops");
    const auto timed = AdvanceRoute(state, route, {0, 0, -1}, {0, 0, 5}, 4);
    Check(timed.hits_added == 1 && timed.timed_out && !timed.completed &&
          state.elapsed_seconds == 1 && state.hit_count == 1,
          "a stalled frame counts only crossings before the authored deadline");
    Check(!AdvanceRoute(state, route, {0, 0, -1}, {0, 0, 5}, 1).timed_out,
          "timeout is reported once and does not auto-promote the lesson");
    RetryRoute(state);
    const auto passed = AdvanceRoute(state, route, {0, 0, -1}, {0, 0, 3}, 1);
    Check(passed.completed && state.hit_count == 2 && state.next_index == 2,
          "the caller's pass count can complete at the deadline without requiring another hoop");
    Check(BeginRoute(state, 6, route, 0), "zero authored timer allows an untimed route");
    Check(!AdvanceRoute(state, route, {2, 0, -1}, {2, 0, 5}, 10000).timed_out &&
          state.elapsed_seconds == 10000, "untimed flight does not invent a deadline");

    const std::array<Hoop, 3> folded{{route[0], route[2], route[1]}};
    Check(BeginRoute(state, 7, folded, 0), "a route may double back spatially");
    const auto fold = AdvanceRoute(state, folded, {0, 0, -1}, {0, 0, 5}, 1);
    Check(fold.hits_added == 2 && state.next_index == 2 && !fold.completed,
          "one sweep cannot credit a hoop that it crossed before the previous hoop");
    Check(AdvanceRoute(state, folded, {0, 0, 5}, {0, 0, 1}, 1).completed,
          "the remaining double-back hoop accepts its later reverse traversal");

    auto duplicate = route;
    duplicate[1].id = duplicate[0].id;
    Check(!BeginRoute(state, 0, duplicate, 0) && state.status == RouteStatus::Invalid,
          "duplicated actor IDs cannot create duplicate route rewards");
    Check(!BeginRoute(state, 0, {}, 0), "empty routes do not auto-complete a lesson");
    Check(!BeginRoute(state, 0, route, -1), "negative authored time is invalid");
    Check(!BeginRoute(state, 0, route, 1, 4), "an impossible required count is rejected");
    Check(BeginRoute(state, 0, route, 1), "valid route can replace invalid data");
    Check(AdvanceRoute(state, std::span(route).first(2), {0, 0, -1}, {0, 0, 1}, 1).hits_added == 0 &&
          state.status == RouteStatus::Invalid, "a route size change requires explicit reinitialization");
    RetryRoute(state);
    Check(state.status == RouteStatus::Invalid, "retry cannot make invalid route data active");
    ResetRoute(state);
    Check(state.status == RouteStatus::Idle && state.hit_count == 0 && state.route_size == 0,
          "reset removes old level and timer state");
    Check(AdvanceRoute(state, route, {0, 0, -1}, {0, 0, 5}, 1).hits_added == 0,
          "an inactive route cannot consume movement from walking or another map");
}

void TestVrHoopAllowance() {
    const Hoop hoop{1,{0,0,0},{0,0,1},1.44F};
    Check(SweepHoop({1.60F,0,-1},{1.60F,0,1},hoop,nullptr,.18F),
          "small off-center VR passage receives at most eighteen centimeters of assistance");
    Check(!SweepHoop({1.60F,0,-1},{1.60F,0,1},hoop),
          "unassisted hoop behavior remains unchanged");
    Check(!SweepHoop({1.63F,0,-1},{1.63F,0,1},hoop,nullptr,.18F),
          "crossing beyond the small allowance is still a miss");
    Check(!SweepHoop({1.73F,0,-1},{1.73F,0,1},hoop,nullptr,.18F),
          "grazing the hoop with the outer edge of a thirty-centimeter player capsule is not a pass");
    Check(!SweepHoop({1.63F,0,-1},{1.63F,0,1},hoop,nullptr,100),
          "even an excessive requested allowance remains capped");
    Check(!SweepHoop({.59F,0,-1},{.59F,0,1},{2,{0,0,0},{0,0,1},.5F},nullptr,.18F),
          "small hoops retain their challenge with a fifteen-percent proportional cap");
    Check(!SweepHoop({0,0,-1},{0,0,-.01F},hoop,nullptr,.18F)&&
          !SweepHoop({0,0,0},{0,0,1},hoop,nullptr,.18F),
          "VR assistance never credits approach-only motion or starting on the plane");
    Check(SweepHoop({0,1.60F,1},{0,1.60F,-1},hoop,nullptr,.18F),
          "the allowance applies equally to height and reverse traversal");
    for(float invalid:{-1.0F,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        Check(!SweepHoop({0,0,-1},{0,0,1},hoop,nullptr,invalid),
              "invalid assistance fails closed rather than awarding arbitrary hits");
}
} // namespace

int main() {
    try {
        TestFlight();
        TestHoopSweep();
        TestRoute();
        TestVrHoopAllowance();
        std::cout << "BROOM_FLIGHT_TESTS=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
