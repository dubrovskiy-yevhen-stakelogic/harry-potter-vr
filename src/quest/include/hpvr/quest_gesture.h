#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace hpvr::quest {

enum class GestureVisualState { Idle, Recording, Accepted, Rejected, Canceled };

struct GestureSample {
    std::int64_t predicted_display_time_ns = 0;
    std::array<float, 3> tip{};
    std::array<float, 3> aim_direction{0.0F, 0.0F, -1.0F};
    bool tracked = false;
    bool cast_held = false;
};

struct FlipendoEvent {
    std::uint64_t serial = 0;
    std::int64_t predicted_display_time_ns = 0;
    std::array<float, 3> release_tip{};
    std::array<float, 3> locked_origin{};
    std::array<float, 3> locked_direction{};
    float score = 0.0F;
    float threshold = 0.0F;
};

struct GestureGuide {
    std::vector<std::array<float, 3>> template_points;
    std::vector<std::array<float, 3>> trail_points;
    std::array<float, 3> plane_normal{0.0F, 0.0F, -1.0F};
    GestureVisualState trail_state = GestureVisualState::Idle;
    bool visible = false;
};

class QuestGesture final {
public:
    QuestGesture();
    ~QuestGesture();
    QuestGesture(const QuestGesture&) = delete;
    QuestGesture& operator=(const QuestGesture&) = delete;

    [[nodiscard]] bool LoadFlipendoProfile(const std::filesystem::path& data_root);
    // Original mode scores the frozen guide directly with authored accuracy,
    // four increasing lesson marks, and the authored drawing deadline. Relaxed
    // mode retains the earlier demo's widened radius and position/scale assist.
    // A policy change cancels an in-progress stroke; Reset preserves the policy.
    void SetLessonDifficulty(bool relaxed);
    void SetLessonRound(std::uint32_t zero_based_round);
    void Reset();
    void Advance(float delta_seconds);
    [[nodiscard]] bool Observe(const GestureSample& sample);
    [[nodiscard]] bool ConsumeEvent(FlipendoEvent* output);
    [[nodiscard]] bool BuildGuide(GestureGuide* output) const;

    [[nodiscard]] bool IsLoaded() const;
    [[nodiscard]] GestureVisualState visual_state() const;
    [[nodiscard]] float last_score() const;
    [[nodiscard]] float threshold() const;
    [[nodiscard]] float authored_accuracy() const;
    [[nodiscard]] float effective_accuracy() const;
    [[nodiscard]] bool relaxed_difficulty() const;
    [[nodiscard]] std::uint32_t lesson_round() const;
    // Zero means unlimited (relaxed mode or no profile loaded).
    [[nodiscard]] float time_limit_seconds() const;
    [[nodiscard]] std::uint32_t attempt_count() const;
    [[nodiscard]] std::uint32_t accepted_count() const;
    [[nodiscard]] std::uint32_t rejected_count() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace hpvr::quest
