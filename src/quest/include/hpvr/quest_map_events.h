#pragma once

#include "hpvr/hp1_gesture.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hpvr::quest {

enum class MapEventKind : std::uint8_t {
    mover_trigger, cutscene_start, sound, music, actor_trigger, actor_spell,
    checkpoint, star_collected
};

struct MapEventEffect {
    MapEventKind kind{};
    std::int32_t actor_reference{};
    std::string tag;
    std::string asset_path;
    bool enabled{true};
};

enum class MapEventNodeKind : std::uint8_t {
    trigger, spell_trigger, dispatcher, counter, round_robin, mover, cutscene,
    sound, music, actor, checkpoint, star, stars_trigger
};

struct MapEventNode {
    std::int32_t actor_reference{};
    MapEventNodeKind kind{};
    std::string class_name, object_name, tag, event, state, asset_path;
    std::vector<std::pair<std::string, float>> outputs;
    std::string low_stars_event, average_stars_event, all_stars_event;
    std::int32_t average_stars{6}, all_stars{8};
    std::uint32_t counter_initial{2}, remaining{2}, round_index{};
    bool initial_active{true}, active{true}, consumed{}, signaled{}, awaiting_signal{}, once{},
        touch_enabled{}, trigger_enabled{true}, silence{};
    float retrigger_delay{};
    double ready_at{};
};

// Bounded reducer for authored level events. Geometry, animation, audio and
// actor AI stay in their existing systems; this class never moves an actor.
class MapEventGraph {
public:
    bool Load(const wand::Hp1ActorVisualCensus& census);
    bool Dispatch(std::string_view tag);
    bool Touch(std::int32_t actor_reference);
    bool Spell(std::int32_t actor_reference);
    // Call only when the actor's physical action completes (e.g. mover arrival
    // or defeated gnome), not as soon as an animation or spell starts.
    bool Signal(std::int32_t actor_reference);
    bool CollectStar(std::int32_t actor_reference);
    bool Advance(float seconds);
    [[nodiscard]] std::vector<MapEventEffect> DrainEffects();
    [[nodiscard]] const MapEventNode* Find(std::int32_t reference) const;
    [[nodiscard]] const std::vector<MapEventNode>& nodes() const { return nodes_; }
    [[nodiscard]] std::uint32_t star_count() const { return stars_; }
    [[nodiscard]] bool healthy() const { return healthy_; }
    [[nodiscard]] std::string Serialize() const;
    // Transactional: invalid, oversized or another map's state changes nothing.
    bool Restore(std::string_view state);

private:
    struct Pending {
        double due{};
        std::uint64_t order{};
        std::string tag;
    };
    bool Queue(std::string_view tag, double due);
    bool Pump();
    bool Activate(std::size_t index, bool touch, bool spell=false);
    bool Emit(MapEventKind kind, const MapEventNode& node, bool enabled = true);
    std::vector<MapEventNode> nodes_;
    std::map<std::int32_t, std::size_t> by_reference_;
    std::map<std::string, std::vector<std::size_t>, std::less<>> by_tag_;
    std::vector<Pending> pending_;
    std::vector<MapEventEffect> effects_;
    std::uint64_t fingerprint_{}, order_{};
    std::uint64_t legacy_fingerprint_{};
    double clock_{};
    std::uint32_t stars_{};
    bool healthy_{true};
};

}  // namespace hpvr::quest
