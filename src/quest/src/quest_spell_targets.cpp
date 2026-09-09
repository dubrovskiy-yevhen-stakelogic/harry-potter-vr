#include "hpvr/quest_spell_targets.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hpvr::quest {
namespace {

constexpr float kMaximumDistanceMeters = 18.0F;
constexpr float kMinimumDistanceMeters = 0.15F;
constexpr float kReactionDurationSeconds = 1.20F;
constexpr float kReactionPushMeters = 0.65F;
constexpr float kReactionLiftMeters = 0.24F;

bool FiniteVector(const std::array<float, 3>& value) {
    return std::all_of(value.begin(), value.end(),
                       [](const float component) {
                           return std::isfinite(component);
                       });
}

bool Normalize(const std::array<float, 3>& input,
               std::array<float, 3>* const output) {
    if (output == nullptr || !FiniteVector(input)) return false;
    const float length = std::sqrt(input[0] * input[0] +
                                   input[1] * input[1] +
                                   input[2] * input[2]);
    if (!std::isfinite(length) || length < 1.0e-5F) return false;
    *output = {input[0] / length, input[1] / length, input[2] / length};
    return true;
}

bool RayAabbDistance(const std::array<float, 3>& origin,
                     const std::array<float, 3>& direction,
                     const std::array<float, 3>& minimum,
                     const std::array<float, 3>& maximum,
                     float* const output_distance) {
    float near_distance = 0.0F;
    float far_distance = kMaximumDistanceMeters;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) <= 1.0e-7F) {
            if (origin[axis] < minimum[axis] ||
                origin[axis] > maximum[axis]) return false;
            continue;
        }
        const float inverse = 1.0F / direction[axis];
        float first = (minimum[axis] - origin[axis]) * inverse;
        float second = (maximum[axis] - origin[axis]) * inverse;
        if (first > second) std::swap(first, second);
        near_distance = std::max(near_distance, first);
        far_distance = std::min(far_distance, second);
        if (near_distance > far_distance) return false;
    }
    const float distance = std::max(near_distance, 0.0F);
    if (far_distance < 0.0F || distance < kMinimumDistanceMeters ||
        distance > kMaximumDistanceMeters) return false;
    if (output_distance != nullptr) *output_distance = distance;
    return true;
}

}  // namespace

bool QuestSpellTargets::SetTargets(
    const std::vector<SpellTargetDescriptor>& targets) {
    if (targets.empty()) return false;
    for (const auto& target : targets) {
        if (target.actor_reference <= 0 || !FiniteVector(target.bounds_min) ||
            !FiniteVector(target.bounds_max)) return false;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (target.bounds_min[axis] > target.bounds_max[axis]) return false;
        }
    }
    targets_ = targets;
    reactions_.assign(targets.size(), Reaction{});
    scene_offsets_.assign(targets.size(), std::array<float, 3>{});
    last_serial_ = 0;
    last_applied_serial_ = 0;
    hit_count_ = 0;
    miss_count_ = 0;
    return true;
}

void QuestSpellTargets::Reset() {
    for (auto& reaction : reactions_) reaction = Reaction{};
    last_serial_ = 0;
    last_applied_serial_ = 0;
}

void QuestSpellTargets::Advance(const float delta_seconds) {
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0F) return;
    for (auto& reaction : reactions_) {
        if (!reaction.active) continue;
        reaction.elapsed_seconds += delta_seconds;
        if (reaction.elapsed_seconds >= kReactionDurationSeconds) {
            reaction = Reaction{};
        }
    }
}

bool QuestSpellTargets::Consume(const FlipendoEvent& event,
                                SpellTargetResult* const output) {
    if (output == nullptr || event.serial == 0 || event.serial <= last_serial_ ||
        !std::isfinite(event.score) || !std::isfinite(event.threshold) ||
        event.score < event.threshold || !FiniteVector(event.locked_origin)) {
        return false;
    }
    std::array<float, 3> direction{};
    if (!Normalize(event.locked_direction, &direction)) return false;
    last_serial_ = event.serial;
    *output = SpellTargetResult{};
    output->serial = event.serial;

    float nearest = std::numeric_limits<float>::infinity();
    std::size_t selected = 0;
    bool found = false;
    for (std::size_t index = 0; index < targets_.size(); ++index) {
        if (!targets_[index].enabled) continue;
        auto offset = ReactionOffset(index);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            offset[axis] += scene_offsets_[index][axis];
        }
        std::array<float, 3> minimum{};
        std::array<float, 3> maximum{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            minimum[axis] = targets_[index].bounds_min[axis] + offset[axis];
            maximum[axis] = targets_[index].bounds_max[axis] + offset[axis];
        }
        float distance = 0.0F;
        if (RayAabbDistance(event.locked_origin, direction, minimum, maximum,
                            &distance) && distance < nearest) {
            nearest = distance;
            selected = index;
            found = true;
        }
    }
    if (!found) {
        ++miss_count_;
        return true;
    }

    output->target_index = selected;
    output->actor_reference = targets_[selected].actor_reference;
    output->distance_m = nearest;
    output->hit = true;
    return true;
}

bool QuestSpellTargets::SetSceneOffset(
    const std::size_t target_index,
    const std::array<float, 3>& offset) {
    if (target_index >= scene_offsets_.size() || !FiniteVector(offset)) {
        return false;
    }
    scene_offsets_[target_index] = offset;
    return true;
}

bool QuestSpellTargets::ApplyHit(
    const SpellTargetResult& result,
    const std::array<float, 3>& direction) {
    if (!result.hit || result.serial == 0 ||
        result.serial != last_serial_ ||
        result.serial <= last_applied_serial_ ||
        result.target_index >= targets_.size() ||
        result.actor_reference !=
            targets_[result.target_index].actor_reference) {
        return false;
    }
    std::array<float, 3> normalized{};
    if (!Normalize(direction, &normalized)) return false;
    const float horizontal_length =
        std::sqrt(normalized[0] * normalized[0] +
                  normalized[2] * normalized[2]);
    auto& reaction = reactions_[result.target_index];
    reaction.direction = horizontal_length > 1.0e-5F
                             ? std::array<float, 3>{
                                   normalized[0] / horizontal_length, 0.0F,
                                   normalized[2] / horizontal_length}
                             : std::array<float, 3>{0.0F, 0.0F, -1.0F};
    reaction.elapsed_seconds = 0.0F;
    reaction.active = true;
    last_applied_serial_ = result.serial;
    ++hit_count_;
    return true;
}

std::array<float, 3> QuestSpellTargets::ReactionOffset(
    const std::size_t target_index) const {
    if (target_index >= reactions_.size() ||
        !reactions_[target_index].active) return {};
    const Reaction& reaction = reactions_[target_index];
    const float phase = std::clamp(
        reaction.elapsed_seconds / kReactionDurationSeconds, 0.0F, 1.0F);
    const float envelope = std::sin(3.14159265358979323846F * phase);
    return {reaction.direction[0] * kReactionPushMeters * envelope,
            kReactionLiftMeters * envelope,
            reaction.direction[2] * kReactionPushMeters * envelope};
}

std::size_t QuestSpellTargets::TargetCount() const { return targets_.size(); }
std::uint32_t QuestSpellTargets::HitCount() const { return hit_count_; }
std::uint32_t QuestSpellTargets::MissCount() const { return miss_count_; }

}  // namespace hpvr::quest
