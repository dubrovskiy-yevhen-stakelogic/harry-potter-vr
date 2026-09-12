#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace hpvr::quest::movers {

using Vector = std::array<float, 3>;
using Matrix = std::array<Vector, 3>;
constexpr float kRadiansPerUnit = 6.2831853071795864769F / 65536.0F;

struct Key {
    Vector offset_unreal{};
    Vector rotation_units{};
};

struct Placement {
    Vector pivot_scene{};
    Vector base_rotation_units{};
    float player_yaw = 0;
    float meters_per_unit = .02F;
};

inline Vector Add(Vector a, const Vector& b) {
    for (unsigned i = 0; i < 3; ++i) a[i] += b[i];
    return a;
}
inline Vector Subtract(Vector a, const Vector& b) {
    for (unsigned i = 0; i < 3; ++i) a[i] -= b[i];
    return a;
}
inline Vector Yaw(const Vector& p, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return {c * p[0] + s * p[2], p[1], -s * p[0] + c * p[2]};
}
inline Vector ToUnreal(const Vector& p) { return {-p[2], p[0], p[1]}; }
inline Vector FromUnreal(const Vector& p) { return {p[1], p[2], -p[0]}; }

// Local X faces forward, positive pitch raises it, positive yaw turns X toward Y.
// Rows are the transformed local basis axes, independent of the scene axis map.
inline Matrix Rotation(const Vector& units) {
    const float p = units[0] * kRadiansPerUnit;
    const float y = units[1] * kRadiansPerUnit;
    const float r = units[2] * kRadiansPerUnit;
    const float cp = std::cos(p), sp = std::sin(p);
    const float cy = std::cos(y), sy = std::sin(y);
    const float cr = std::cos(r), sr = std::sin(r);
    return {{{cp * cy, cp * sy, sp},
        {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp},
        {-cr * sp * cy - sr * sy, sr * cy - cr * sp * sy, cr * cp}}};
}
inline Vector Apply(const Matrix& m, const Vector& p) {
    Vector result{};
    for (unsigned a = 0; a < 3; ++a)
        for (unsigned b = 0; b < 3; ++b) result[a] += m[b][a] * p[b];
    return result;
}
inline Vector InverseApply(const Matrix& m, const Vector& p) {
    Vector result{};
    for (unsigned a = 0; a < 3; ++a)
        for (unsigned b = 0; b < 3; ++b) result[a] += m[a][b] * p[b];
    return result;
}
inline Vector RotateBrushLocal(const Vector& converted_local_m,
    const Vector& rotation_units, float player_yaw) {
    return Yaw(FromUnreal(Apply(Rotation(rotation_units), ToUnreal(converted_local_m))), player_yaw);
}
inline Vector TransformPoint(const Vector& base_geometry_scene,
    const Placement& placement, const Key& pose) {
    const auto baked = ToUnreal(Yaw(Subtract(base_geometry_scene, placement.pivot_scene),
        -placement.player_yaw));
    const auto local = InverseApply(Rotation(placement.base_rotation_units), baked);
    auto current = Apply(Rotation(Add(placement.base_rotation_units, pose.rotation_units)), local);
    for (unsigned a = 0; a < 3; ++a)
        current[a] += pose.offset_unreal[a] * placement.meters_per_unit;
    return Add(placement.pivot_scene, Yaw(FromUnreal(current), placement.player_yaw));
}

struct Affine {
    Matrix basis{};
    Vector translation{};
};
// Build once for a moved brush; vertex/collision loops then contain only multiply/add.
inline Affine BuildTransform(const Placement& placement, const Key& pose) {
    Affine result;
    const auto base = Rotation(placement.base_rotation_units);
    const auto current = Rotation(Add(placement.base_rotation_units, pose.rotation_units));
    for (unsigned axis = 0; axis < 3; ++axis) {
        Vector unit{}; unit[axis] = 1;
        const auto local = InverseApply(base, ToUnreal(Yaw(unit, -placement.player_yaw)));
        result.basis[axis] = Yaw(FromUnreal(Apply(current, local)), placement.player_yaw);
    }
    auto offset = pose.offset_unreal;
    for (auto& value : offset) value *= placement.meters_per_unit;
    result.translation = Add(Subtract(placement.pivot_scene, Apply(result.basis, placement.pivot_scene)),
        Yaw(FromUnreal(offset), placement.player_yaw));
    return result;
}
inline Vector TransformPoint(const Affine& transform, const Vector& point) {
    return Add(Apply(transform.basis, point), transform.translation);
}

inline Key Interpolate(const Key& a, const Key& b, float fraction) {
    const float t = std::clamp(fraction, 0.0F, 1.0F);
    Key result;
    for (unsigned axis = 0; axis < 3; ++axis) {
        result.offset_unreal[axis] = a.offset_unreal[axis] +
            (b.offset_unreal[axis] - a.offset_unreal[axis]) * t;
        // Preserve the authored rotation direction, including half/full turns.
        result.rotation_units[axis] = a.rotation_units[axis] +
            (b.rotation_units[axis] - a.rotation_units[axis]) * t;
    }
    return result;
}

struct Motion {
    std::array<Key, 16> keys{};
    std::uint32_t count = 2, current = 0, target = 0;
    Key source{}, pose{};
    float phase = 0, seconds = 1;
    bool moving = false, looping = false, chain = true;
    int direction = 1;
};
struct StepResult {
    std::uint32_t arrivals = 0;
    bool finished = false;
};
inline bool ValidCount(const Motion& motion) {
    return motion.count > 0 && motion.count <= motion.keys.size() &&
        motion.current < motion.count && motion.target < motion.count;
}
inline bool Settle(Motion& motion, std::uint32_t key) {
    if (!ValidCount(motion) || key >= motion.count) return false;
    motion.current = motion.target = key;
    motion.source = motion.pose = motion.keys[key];
    motion.phase = 0;
    motion.moving = motion.looping = false;
    return true;
}
inline bool Start(Motion& motion, bool open, bool loop = false) {
    if (!ValidCount(motion) || motion.count < 2 || !std::isfinite(motion.seconds) ||
        motion.seconds < 0) return false;
    if (loop && motion.moving && motion.looping) return false;
    const int direction = open ? 1 : -1;
    std::uint32_t next = motion.current;
    if (loop) next = (motion.current + 1) % motion.count;
    else if (motion.moving) {
        if (direction == motion.direction) return false;
        next = motion.current;
    } else if (direction > 0 && motion.current + 1 < motion.count) ++next;
    else if (direction < 0 && motion.current > 0) --next;
    else return false;
    motion.source = motion.pose;
    motion.phase = 0;
    motion.target = next;
    motion.direction = direction;
    motion.looping = loop;
    motion.moving = true;
    return true;
}
inline StepResult Advance(Motion& motion, float seconds) {
    StepResult result;
    if (!ValidCount(motion) || !std::isfinite(seconds) || seconds < 0 ||
        !std::isfinite(motion.seconds) || motion.seconds < 0 || !motion.moving) return result;
    float remaining = std::min(seconds, 60.0F);
    // A suspended headset or a zero-time cyclic asset cannot cause unbounded work.
    for (unsigned budget = 0; budget < 128 && motion.moving; ++budget) {
        const float until_arrival = (1.0F - motion.phase) * motion.seconds;
        if (motion.seconds > 0 && remaining < until_arrival) {
            motion.phase = std::min(1.0F, motion.phase + remaining / motion.seconds);
            motion.pose = Interpolate(motion.source, motion.keys[motion.target], motion.phase);
            break;
        }
        remaining = std::max(0.0F, remaining - until_arrival);
        motion.pose = motion.keys[motion.target];
        motion.current = motion.target;
        ++result.arrivals;
        if (motion.looping) motion.target = (motion.current + 1) % motion.count;
        else if (motion.chain && motion.direction > 0 && motion.current + 1 < motion.count) ++motion.target;
        else if (motion.chain && motion.direction < 0 && motion.current > 0) --motion.target;
        else {
            motion.moving = false;
            motion.phase = 1;
            result.finished = true;
            break;
        }
        motion.source = motion.pose;
        motion.phase = 0;
        if (remaining <= 0 && motion.seconds > 0) break;
        if (motion.looping && motion.seconds == 0) break;
    }
    return result;
}

inline Vector GridStepUnreal(const Vector& mover, const Vector& pusher, float increment = 64) {
    if (!std::isfinite(increment) || increment <= 0) return {};
    const auto offset = Subtract(pusher, mover);
    const unsigned axis = std::abs(offset[0]) > std::abs(offset[1]) ? 0U : 1U;
    Vector step{};
    step[axis] = offset[axis] > 0 ? -increment : increment;
    return step;
}

inline bool FiniteKey(const Key& key) {
    for (float value : key.offset_unreal)
        if (!std::isfinite(value) || std::abs(value) > 10000000.0F) return false;
    for (float value : key.rotation_units)
        if (!std::isfinite(value) || std::abs(value) > 10000000.0F) return false;
    return true;
}
inline bool ValidMotion(const Motion& motion) {
    return ValidCount(motion) && FiniteKey(motion.source) && FiniteKey(motion.pose) &&
        std::isfinite(motion.phase) && motion.phase >= 0 && motion.phase <= 1 &&
        std::isfinite(motion.seconds) && motion.seconds >= 0 && motion.seconds <= 3600 &&
        (motion.direction == -1 || motion.direction == 1);
}
inline std::string SaveMotion(const Motion& motion) {
    if (!ValidMotion(motion)) return {};
    std::ostringstream out;
    out << std::setprecision(9) << "MM1 " << motion.count << ' ' << motion.current << ' '
        << motion.target << ' ' << motion.phase << ' ' << motion.seconds << ' '
        << motion.moving << ' ' << motion.looping << ' ' << motion.chain << ' ' << motion.direction;
    for (const auto* key : {&motion.source, &motion.pose}) {
        for (float value : key->offset_unreal) out << ' ' << value;
        for (float value : key->rotation_units) out << ' ' << value;
    }
    return out.str();
}
inline bool RestoreMotion(Motion& motion, std::string_view text) {
    if (text.empty() || text.size() > 2048) return false;
    Motion restored = motion;
    std::istringstream in{std::string(text)};
    std::string version;
    unsigned count = 0, moving = 0, looping = 0, chain = 0;
    if (!(in >> version >> count >> restored.current >> restored.target >> restored.phase >>
        restored.seconds >> moving >> looping >> chain >> restored.direction) || version != "MM1" ||
        count != motion.count || moving > 1 || looping > 1 || chain > 1) return false;
    for (auto* key : {&restored.source, &restored.pose}) {
        for (float& value : key->offset_unreal) if (!(in >> value)) return false;
        for (float& value : key->rotation_units) if (!(in >> value)) return false;
    }
    restored.moving = moving != 0; restored.looping = looping != 0; restored.chain = chain != 0;
    if (!ValidMotion(restored)) return false;
    in >> std::ws;
    if (!in.eof()) return false;
    motion = restored;
    return true;
}

}  // namespace hpvr::quest::movers
