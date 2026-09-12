#include "hpvr/quest_award_facing.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Point = std::array<float, 3>;
constexpr float kTau = 6.28318530717958647692F;
struct Mark {
    std::int32_t actor_reference = 1799;
    std::string alias = "HPLoc1";
    Point position{2, 1, -3};
    bool operator==(const Mark&) const = default;
};
struct Actor {
    std::int32_t actor_reference = 2051;
    Point base_origin{1, 0, 0}, cutscene_offset{};
    float yaw = .4F, desired_yaw = .8F, base_yaw = .6F;
    bool operator==(const Actor&) const = default;
};
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
Point Rotate(const Point& p, float yaw) {
    return {std::cos(yaw) * p[0] + std::sin(yaw) * p[2], p[1],
            -std::sin(yaw) * p[0] + std::cos(yaw) * p[2]};
}
void Tests() {
    for (const std::int32_t scene : {3384, 5615, 2848}) {
        for (unsigned angle = 0; angle < 24; ++angle) {
            const float rotation = kTau * static_cast<float>(angle) / 24;
            const auto delta = Rotate({.7F, .5F, -1.2F}, rotation);
            const auto shift = Rotate({2, .1F, -3}, rotation);
            std::vector<Actor> actors(2);
            actors[0].actor_reference = 2403;
            actors[1].base_origin = {22, -5, 17};
            actors[1].cutscene_offset = shift;
            std::vector<Mark> marks(1);
            marks[0].alias = angle % 2 ? "hPlOc1" : "HPLoc1";
            for (unsigned axis = 0; axis < 3; ++axis)
                marks[0].position[axis] = actors[1].base_origin[axis] + shift[axis] + delta[axis];
            const auto before = actors;
            const auto before_marks = marks;
            Check(hpvr::quest::award::RestoreProfessorFacing(scene, marks, actors), "award facing rejected");
            const float expected = std::atan2(delta[0], delta[2]);
            Check(std::abs(std::remainder(actors[1].yaw - expected, kTau)) < .00001F,
                  "professor is not facing Harry's destination");
            Check(actors[1].desired_yaw == actors[1].yaw, "first frame would interpolate from wrong yaw");
            Check(actors[1].base_yaw == before[1].base_yaw &&
                  actors[1].base_origin == before[1].base_origin &&
                  actors[1].cutscene_offset == before[1].cutscene_offset,
                  "baked transform or placement changed");
            Check(actors[0] == before[0] && marks == before_marks, "unrelated data changed");
            const auto corrected = actors;
            Check(hpvr::quest::award::RestoreProfessorFacing(scene, marks, actors) && actors == corrected,
                  "repeated restore is not stable");
        }
    }
    const auto unchanged = [](std::int32_t scene, auto mutate) {
        std::vector<Mark> marks(1);
        std::vector<Actor> actors(1);
        mutate(marks, actors);
        std::vector<std::array<float, 3>> yaw;
        for (const auto& actor : actors) yaw.push_back({actor.yaw, actor.desired_yaw, actor.base_yaw});
        Check(!hpvr::quest::award::RestoreProfessorFacing(scene, marks, actors), "unrelated or invalid data accepted");
        for (std::size_t i = 0; i < actors.size(); ++i)
            Check(actors[i].yaw == yaw[i][0] && actors[i].desired_yaw == yaw[i][1] &&
                  actors[i].base_yaw == yaw[i][2], "rejected request overwrote orientation");
    };
    for (std::int32_t scene : {0, 3606, 4807, 5276, 5284, 2234})
        unchanged(scene, [](auto&, auto&) {});
    unchanged(3384, [](auto& marks, auto&) { marks.clear(); });
    unchanged(3384, [](auto&, auto& actors) { actors.clear(); });
    unchanged(3384, [](auto& marks, auto&) { marks[0].actor_reference = 1798; });
    unchanged(3384, [](auto& marks, auto&) { marks[0].alias = "HPLoc2"; });
    unchanged(3384, [](auto& marks, auto&) { marks.push_back(marks[0]); });
    unchanged(3384, [](auto&, auto& actors) { actors.push_back(actors[0]); });
    unchanged(3384, [](auto&, auto& actors) { actors[0].actor_reference = 2403; });
    unchanged(3384, [](auto& marks, auto& actors) { marks[0].position = actors[0].base_origin; });
    unchanged(3384, [](auto& marks, auto&) { marks[0].position[0] = std::numeric_limits<float>::quiet_NaN(); });
    unchanged(3384, [](auto&, auto& actors) { actors[0].cutscene_offset[2] = std::numeric_limits<float>::infinity(); });
    unchanged(3384, [](auto& marks, auto&) { marks[0].position[0] = 10001; });
    unchanged(3384, [](auto& marks, auto&) { marks.resize(129); });
    unchanged(3384, [](auto&, auto& actors) { actors.resize(4097); });
}
} // namespace

int main() {
    try {
        Tests();
        std::cout << "PASS: three award branches, 72 rotated-origin cases, first-frame pose, no unrelated writes, invalid and repeated restores\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
