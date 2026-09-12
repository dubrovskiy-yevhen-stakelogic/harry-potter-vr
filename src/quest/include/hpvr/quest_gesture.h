#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
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

struct GestureShapeMatch {
    bool valid = false;
    float score = 0.0F;
};

// A completed attempt only: bounded geometric diagnostics, never raw tracking
// history on disk. Serial changes for rejected and canceled attempts as well.
struct GestureDiagnostics {
    std::uint64_t serial = 0;
    std::uint32_t attempt = 0;
    const char* reason = "NONE";
    std::uint32_t sample_count = 0;
    float duration_seconds = 0;
    std::array<float, 2> projected_extent{};
    float depth_span_meters = 0;
    float path_length = 0;
    float score = 0;
    float threshold = 0;
    std::array<std::array<float, 2>, 32> projected_points{};
    std::uint32_t projected_point_count = 0;
};

// Quest-only completed-stroke scoring. Translation, in-plane rotation and
// drawing direction do not change the shape. Relaxed mode additionally fits
// bounded aspect/scale distortion; original mode retains the guide's physical
// size. Coverage retains short-tail and corner tolerance after alignment.
// This does not alter the original PC gesture-comparison ABI.
[[nodiscard]] GestureShapeMatch CompareGestureShape(
    std::span<const std::array<float, 2>> drawn,
    std::span<const std::array<float, 2>> pattern,
    float accuracy_radius, bool fit_scale);

// Gameplay Flipendo checks the single curling stroke's structure, not lesson
// tracing accuracy. Size, angle, drawing direction and handedness are free;
// lines, closed circles, backtracking scribbles and repeated loops fail.
[[nodiscard]] GestureShapeMatch CompareGameplayGestureShape(
    std::span<const std::array<float, 2>> drawn,
    std::span<const std::array<float, 2>> pattern);

class QuestGesture final {
public:
    QuestGesture();
    ~QuestGesture();
    QuestGesture(const QuestGesture&) = delete;
    QuestGesture& operator=(const QuestGesture&) = delete;

    [[nodiscard]] bool LoadFlipendoProfile(const std::filesystem::path& data_root);
    // Both modes accept any in-plane rotation and either stroke direction.
    // Original mode retains authored accuracy, physical size, four increasing
    // lesson marks and the deadline. Relaxed mode widens the radius and fits
    // bounded aspect distortion. Coverage, traversal and length checks reject
    // unrelated shapes; no mirrored candidate is created.
    // A policy change cancels an in-progress stroke; Reset preserves the policy.
    void SetLessonDifficulty(bool relaxed);
    // Kept separate from lesson difficulty so gameplay assistance cannot alter
    // authored lesson pass marks, drawing size or deadlines.
    void SetGameplayMode(bool gameplay);
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
    // Zero means unlimited (gameplay, relaxed lesson or no profile loaded).
    [[nodiscard]] float time_limit_seconds() const;
    [[nodiscard]] std::uint32_t attempt_count() const;
    [[nodiscard]] std::uint32_t accepted_count() const;
    [[nodiscard]] std::uint32_t rejected_count() const;
    [[nodiscard]] GestureDiagnostics diagnostics() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace hpvr::quest
