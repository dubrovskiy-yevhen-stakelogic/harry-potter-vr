#include "hpvr/quest_map_events.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace hpvr::quest {
namespace {
constexpr std::size_t kMaximumNodes = 2048;
constexpr std::size_t kMaximumPending = 256;
constexpr std::size_t kMaximumEffects = 512;
constexpr std::size_t kMaximumWork = 4096;
constexpr std::size_t kMaximumSaveBytes = 16384;
constexpr std::size_t kMaximumTag = 128;

std::string Fold(std::string_view text) {
    std::string result(text);
    for (auto& c : result) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return result;
}
bool ValidTag(std::string_view text) {
    return text.size() <= kMaximumTag && std::ranges::none_of(text, [](char c) {
        return static_cast<unsigned char>(c) < 32;
    });
}
const wand::Hp1ClassDefaultProperty* Property(const wand::Hp1ActorVisual& actor,
                                           std::string_view name, std::int64_t index = -1) {
    for (const auto& p : actor.serialized_properties)
        if (Fold(p.name) == Fold(name) && p.array_index == index) return &p;
    return nullptr;
}
std::uint32_t Bits(const wand::Hp1ClassDefaultProperty* p, std::uint32_t fallback) {
    if (!p || p->value.empty()) return fallback;
    if (p->value.size() == 1) return p->value[0];
    if (p->value.size() != 4) return fallback;
    return std::uint32_t(p->value[0]) | (std::uint32_t(p->value[1]) << 8U) |
        (std::uint32_t(p->value[2]) << 16U) | (std::uint32_t(p->value[3]) << 24U);
}
float Number(const wand::Hp1ClassDefaultProperty* p, float fallback) {
    if (!p || p->value.size() != 4) return fallback;
    const float result = std::bit_cast<float>(Bits(p, 0));
    return std::isfinite(result) ? result : fallback;
}
bool Boolean(const wand::Hp1ActorVisual& actor, std::string_view name, bool fallback) {
    const auto* p = Property(actor, name);
    return p && p->boolean_value_serialized ? p->boolean_value : fallback;
}
std::string Text(const wand::Hp1ActorVisual& actor, std::string_view name) {
    const auto* p = Property(actor, name);
    return p && p->text_value_serialized ? p->text_value : std::string{};
}
std::string Asset(const wand::Hp1ActorVisual& actor, std::string_view name) {
    const auto* p = Property(actor, name);
    std::string result;
    if (p) for (const auto& component : p->object_path) {
        if (!result.empty()) result += '.';
        result += component;
    }
    return result;
}
bool IsMover(std::string_view name) {
    return name == "engine.mover" || name == "engine.loopmover" ||
        name == "engine.gradualmover" || name == "engine.gridmover" ||
        name == "engine.assertmover" || name == "engine.mixmover";
}
void Hash(std::uint64_t& hash, std::string_view value) {
    for (const auto c : value) { hash ^= static_cast<unsigned char>(c); hash *= 1099511628211ULL; }
    hash ^= 0xffU;
    hash *= 1099511628211ULL;
}
}  // namespace

bool MapEventGraph::Load(const wand::Hp1ActorVisualCensus& census) {
    if (census.status != wand::Hp1ProfileStatus::ok) return false;
    MapEventGraph graph;
    graph.fingerprint_ = 14695981039346656037ULL;
    for (const auto& actor : census.actors) {
        const auto name = Fold(actor.qualified_class_name);
        MapEventNode node;
        if (name == "engine.trigger" || name == "hpbase.warntrigger") node.kind = MapEventNodeKind::trigger;
        else if (name == "hpbase.spelltrigger") node.kind = MapEventNodeKind::spell_trigger;
        else if (name == "engine.dispatcher") node.kind = MapEventNodeKind::dispatcher;
        else if (name == "engine.counter") node.kind = MapEventNodeKind::counter;
        else if (name == "engine.roundrobin") node.kind = MapEventNodeKind::round_robin;
        else if (IsMover(name)) node.kind = MapEventNodeKind::mover;
        else if (name == "hpbase.cutscene") node.kind = MapEventNodeKind::cutscene;
        else if (name == "engine.specialevent") node.kind = MapEventNodeKind::sound;
        else if (name == "engine.musicevent") node.kind = MapEventNodeKind::music;
        else if (name == "harrypotter.savepoint") node.kind = MapEventNodeKind::checkpoint;
        else if (name == "hprops.star") node.kind = MapEventNodeKind::star;
        else if (name == "hpbase.starstrigger") node.kind = MapEventNodeKind::stars_trigger;
        else if (name == "tut1.flipbarrel" || name == "tut1.tut1gnome" ||
                 name.starts_with("hprops.flipendovase") || name == "hprops.bronzecauldron" ||
                 name == "engine.triggerlight" || !actor.event.empty()) node.kind = MapEventNodeKind::actor;
        else continue;
        if (actor.actor_reference <= 0 || graph.nodes_.size() >= kMaximumNodes ||
            graph.by_reference_.contains(actor.actor_reference)) return false;
        node.actor_reference = actor.actor_reference;
        node.class_name = name;
        node.object_name = actor.object_name;
        const auto suffix = name.substr(name.find_last_of('.') + 1);
        node.tag = Fold(actor.tag.empty() ? suffix : actor.tag);
        node.event = Fold(actor.event);
        node.state = Fold(actor.initial_state);
        if (!ValidTag(node.tag) || !ValidTag(node.event)) return false;
        if (node.tag == "none") node.tag.clear();
        if (node.event == "none") node.event.clear();
        node.initial_active = Boolean(actor, "bInitiallyActive", true);
        if (node.kind == MapEventNodeKind::cutscene) node.initial_active = Boolean(actor, "bCanPlay", true);
        node.active = node.initial_active;
        node.once = Boolean(actor, "bTriggerOnceOnly", false);
        node.retrigger_delay = std::clamp(Number(Property(actor, "ReTriggerDelay"), 0.0F), 0.0F, 3600.0F);
        node.touch_enabled = node.kind == MapEventNodeKind::trigger || node.kind == MapEventNodeKind::stars_trigger ||
            node.kind == MapEventNodeKind::checkpoint;
        if (node.kind == MapEventNodeKind::cutscene) {
            node.once = Boolean(actor, "bPlayOnce", true);
            node.touch_enabled = Boolean(actor, "bTouchStarts", true);
            node.trigger_enabled = Boolean(actor, "bTriggerStarts", true);
        }
        if (node.kind == MapEventNodeKind::stars_trigger || node.kind == MapEventNodeKind::star ||
            node.kind == MapEventNodeKind::checkpoint) node.once = true;
        node.counter_initial = std::clamp(Bits(Property(actor, "NumToCount"), 2), 1U, 255U);
        node.remaining = node.counter_initial;
        node.average_stars = static_cast<std::int32_t>(std::min(Bits(Property(actor, "avgStarCount"), 6), 1024U));
        node.all_stars = static_cast<std::int32_t>(std::max(static_cast<std::uint32_t>(node.average_stars),
            std::min(Bits(Property(actor, "winnerStarCount"), 8), 1024U)));
        node.low_stars_event = Fold(Text(actor, "loserTrigger"));
        node.average_stars_event = Fold(Text(actor, "avgTrigger"));
        node.all_stars_event = Fold(Text(actor, "winnerTrigger"));
        node.asset_path = Asset(actor, node.kind == MapEventNodeKind::music ? "Song" : "Sound");
        node.silence = Boolean(actor, "bSilence", false);
        if (node.kind == MapEventNodeKind::dispatcher || node.kind == MapEventNodeKind::round_robin) {
            double delay = 0;
            for (std::int64_t i = 0; i < 16; ++i) {
                const auto* p = Property(actor, "OutEvents", i == 0 ? -1 : i);
                const auto* d = Property(actor, "OutDelays", i == 0 ? -1 : i);
                delay += std::clamp(Number(d, 0.0F), 0.0F, 3600.0F);
                if (p && p->text_value_serialized && !p->text_value.empty() && Fold(p->text_value) != "none") {
                    if (!ValidTag(p->text_value)) return false;
                    node.outputs.emplace_back(Fold(p->text_value), static_cast<float>(delay));
                }
            }
        }
        Hash(graph.fingerprint_, std::to_string(actor.actor_reference));
        Hash(graph.fingerprint_, actor.object_name);
        Hash(graph.fingerprint_, actor.qualified_class_name);
        Hash(graph.fingerprint_, node.tag);
        Hash(graph.fingerprint_, node.event);
        for (const auto& property : actor.serialized_properties) {
            Hash(graph.fingerprint_, property.name);
            Hash(graph.fingerprint_, std::to_string(property.array_index));
            Hash(graph.fingerprint_, property.text_value);
            Hash(graph.fingerprint_, property.boolean_value ? "1" : "0");
            for (const auto byte : property.value) { graph.fingerprint_ ^= byte; graph.fingerprint_ *= 1099511628211ULL; }
        }
        const auto index = graph.nodes_.size();
        graph.by_reference_[node.actor_reference] = index;
        if (!node.tag.empty()) graph.by_tag_[node.tag].push_back(index);
        graph.nodes_.push_back(std::move(node));
    }
    *this = std::move(graph);
    return true;
}

const MapEventNode* MapEventGraph::Find(std::int32_t reference) const {
    const auto found = by_reference_.find(reference);
    return found == by_reference_.end() ? nullptr : &nodes_[found->second];
}

bool MapEventGraph::Queue(std::string_view tag, double due) {
    if (tag.empty() || Fold(tag) == "none") return true;
    if (!healthy_ || !ValidTag(tag) || !std::isfinite(due) || pending_.size() >= kMaximumPending) {
        healthy_ = false;
        return false;
    }
    pending_.push_back({due, order_++, Fold(tag)});
    return true;
}

bool MapEventGraph::Emit(MapEventKind kind, const MapEventNode& node, bool enabled) {
    if (effects_.size() >= kMaximumEffects) { healthy_ = false; return false; }
    effects_.push_back({kind, node.actor_reference, node.tag, node.asset_path, enabled});
    return true;
}

bool MapEventGraph::Activate(std::size_t index, bool touch) {
    auto& node = nodes_[index];
    if (touch && !node.touch_enabled) return true;
    if (!touch && !node.trigger_enabled) return true;
    if (!touch && node.kind == MapEventNodeKind::trigger) {
        if (node.state == "othertriggertoggles") node.active = !node.active;
        else if (node.state == "othertriggerturnson") node.active = true;
        else if (node.state == "othertriggerturnsoff") node.active = false;
        // Events change contact eligibility; they never simulate a player touch.
        return true;
    }
    if (!node.active || node.consumed || clock_ < node.ready_at) return true;
    if (node.kind == MapEventNodeKind::counter) {
        if (node.remaining > 0) --node.remaining;
        if (node.remaining != 0) return true;
        node.consumed = true;
        return Queue(node.event, clock_);
    }
    if (node.once) node.consumed = true;
    node.ready_at = clock_ + node.retrigger_delay;
    switch (node.kind) {
    case MapEventNodeKind::trigger:
    case MapEventNodeKind::spell_trigger:
        return Queue(node.event, clock_);
    case MapEventNodeKind::dispatcher:
        for (const auto& [tag, delay] : node.outputs) if (!Queue(tag, clock_ + delay)) return false;
        return true;
    case MapEventNodeKind::round_robin:
        if (node.outputs.empty()) return true;
        {
            const auto& output = node.outputs[node.round_index % node.outputs.size()];
            node.round_index = (node.round_index + 1U) % static_cast<std::uint32_t>(node.outputs.size());
            return Queue(output.first, clock_);
        }
    case MapEventNodeKind::mover:
        node.awaiting_signal = true;
        return Emit(MapEventKind::mover_trigger, node);
    case MapEventNodeKind::cutscene:
        return Emit(MapEventKind::cutscene_start, node);
    case MapEventNodeKind::sound:
        return Emit(MapEventKind::sound, node);
    case MapEventNodeKind::music:
        return Emit(MapEventKind::music, node, !node.silence);
    case MapEventNodeKind::actor:
        return Emit(MapEventKind::actor_trigger, node);
    case MapEventNodeKind::checkpoint:
        return Emit(MapEventKind::checkpoint, node);
    case MapEventNodeKind::stars_trigger:
        return Queue(stars_ >= static_cast<std::uint32_t>(node.all_stars) ? node.all_stars_event :
            stars_ >= static_cast<std::uint32_t>(node.average_stars) ? node.average_stars_event : node.low_stars_event, clock_);
    case MapEventNodeKind::star:
    case MapEventNodeKind::counter:
        return true;
    }
    return true;
}

bool MapEventGraph::Pump() {
    std::size_t work = 0;
    while (healthy_) {
        const auto first = std::ranges::min_element(pending_, [](const Pending& a, const Pending& b) {
            return a.due < b.due || (a.due == b.due && a.order < b.order);
        });
        if (first == pending_.end() || first->due > clock_ + 0.000001) break;
        const auto tag = first->tag;
        pending_.erase(first);
        if (++work > kMaximumWork) { healthy_ = false; break; }
        const auto found = by_tag_.find(tag);
        if (found == by_tag_.end()) continue;
        for (const auto index : found->second) {
            if (++work > kMaximumWork || !Activate(index, false)) { healthy_ = false; break; }
        }
    }
    if (!healthy_) pending_.clear();
    return healthy_;
}

bool MapEventGraph::Dispatch(std::string_view tag) { return Queue(tag, clock_) && Pump(); }

bool MapEventGraph::Touch(std::int32_t reference) {
    const auto found = by_reference_.find(reference);
    if (!healthy_ || found == by_reference_.end()) return false;
    return Activate(found->second, true) && Pump();
}

bool MapEventGraph::Spell(std::int32_t reference) {
    const auto found = by_reference_.find(reference);
    if (!healthy_ || found == by_reference_.end()) return false;
    auto& node = nodes_[found->second];
    if (!node.active || node.consumed) return false;
    if (node.kind == MapEventNodeKind::spell_trigger) return Activate(found->second, false) && Pump();
    if (node.kind == MapEventNodeKind::mover && node.class_name == "engine.gridmover")
        return Activate(found->second, false) && Pump();
    if (node.kind != MapEventNodeKind::actor) return false;
    return Emit(MapEventKind::actor_spell, node);
}

bool MapEventGraph::Signal(std::int32_t reference) {
    const auto found = by_reference_.find(reference);
    if (!healthy_ || found == by_reference_.end()) return false;
    auto& node = nodes_[found->second];
    if (node.kind == MapEventNodeKind::mover) {
        if (!node.awaiting_signal) return false;
        node.awaiting_signal = false;
    } else {
        if (node.signaled) return false;
        node.signaled = true;
    }
    return Queue(node.event, clock_) && Pump();
}

bool MapEventGraph::CollectStar(std::int32_t reference) {
    const auto found = by_reference_.find(reference);
    if (!healthy_ || found == by_reference_.end()) return false;
    auto& node = nodes_[found->second];
    if (node.kind != MapEventNodeKind::star || node.consumed || !node.active) return false;
    node.consumed = true;
    ++stars_;
    return Emit(MapEventKind::star_collected, node) && Queue(node.event, clock_) && Pump();
}

bool MapEventGraph::Advance(float seconds) {
    if (!healthy_ || !std::isfinite(seconds) || seconds < 0 || seconds > 3600) return false;
    clock_ += seconds;
    return Pump();
}

std::vector<MapEventEffect> MapEventGraph::DrainEffects() {
    auto result = std::move(effects_);
    effects_.clear();
    return result;
}

std::string MapEventGraph::Serialize() const {
    if (!healthy_) return {};
    std::ostringstream body;
    body.imbue(std::locale::classic());
    body << std::setprecision(17);
    std::size_t changes = 0;
    for (const auto& node : nodes_) {
        if (node.active == node.initial_active && !node.consumed && !node.signaled && !node.awaiting_signal &&
            node.remaining == node.counter_initial && node.round_index == 0 && node.ready_at <= clock_) continue;
        const unsigned flags = (node.active ? 1U : 0U) | (node.consumed ? 2U : 0U) |
            (node.signaled ? 4U : 0U) | (node.awaiting_signal ? 8U : 0U);
        body << ' ' << node.actor_reference << ' ' << flags << ' ' << node.remaining << ' ' << node.round_index << ' '
             << std::max(0.0, node.ready_at - clock_);
        ++changes;
    }
    auto pending = pending_;
    std::ranges::sort(pending, [](const Pending& a, const Pending& b) {
        return a.due < b.due || (a.due == b.due && a.order < b.order);
    });
    body << ' ' << pending.size();
    for (const auto& item : pending) body << ' ' << std::max(0.0, item.due - clock_) << ' ' << std::quoted(item.tag);
    body << ' ' << effects_.size();
    for (const auto& effect : effects_) body << ' ' << static_cast<unsigned>(effect.kind) << ' '
        << effect.actor_reference << ' ' << effect.enabled;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "ME2 " << fingerprint_ << ' ' << stars_ << ' ' << changes << body.str();
    auto result = output.str();
    return result.size() <= kMaximumSaveBytes ? result : std::string{};
}

bool MapEventGraph::Restore(std::string_view state) {
    if (state.empty() || state.size() > kMaximumSaveBytes) return false;
    std::istringstream input{std::string(state)};
    input.imbue(std::locale::classic());
    std::string magic;
    std::uint64_t fingerprint = 0;
    std::uint32_t stars = 0;
    std::size_t changes = 0, pending_count = 0;
    if (!(input >> magic >> fingerprint >> stars >> changes) || (magic != "ME1" && magic != "ME2") || fingerprint != fingerprint_ ||
        changes > nodes_.size() || stars > nodes_.size()) return false;
    auto nodes = nodes_;
    for (auto& node : nodes) {
        node.active = node.initial_active;
        node.consumed = node.signaled = node.awaiting_signal = false;
        node.remaining = node.counter_initial;
        node.round_index = 0;
        node.ready_at = 0;
    }
    std::set<std::int32_t> unique;
    for (std::size_t i = 0; i < changes; ++i) {
        std::int32_t reference = 0;
        unsigned flags = 0;
        std::uint32_t remaining = 0, round = 0;
        double delay = 0;
        if (!(input >> reference >> flags >> remaining >> round >> delay) || flags > 15 ||
            !std::isfinite(delay) || delay < 0 || delay > 3600 || !unique.insert(reference).second) return false;
        const auto found = by_reference_.find(reference);
        if (found == by_reference_.end()) return false;
        auto& node = nodes[found->second];
        if (remaining > node.counter_initial || round > node.outputs.size()) return false;
        node.active = (flags & 1U) != 0;
        node.consumed = (flags & 2U) != 0;
        node.signaled = (flags & 4U) != 0;
        node.awaiting_signal = (flags & 8U) != 0;
        node.remaining = remaining;
        node.round_index = round;
        node.ready_at = delay;
    }
    const auto collected = std::ranges::count_if(nodes, [](const MapEventNode& node) {
        return node.kind == MapEventNodeKind::star && node.consumed;
    });
    if (stars != static_cast<std::uint32_t>(collected)) return false;
    if (!(input >> pending_count) || pending_count > kMaximumPending) return false;
    std::vector<Pending> pending;
    for (std::size_t i = 0; i < pending_count; ++i) {
        Pending item;
        if (!(input >> item.due >> std::quoted(item.tag)) || !std::isfinite(item.due) ||
            item.due < 0 || item.due > 57600 || item.tag.empty() || !ValidTag(item.tag)) return false;
        item.tag = Fold(item.tag);
        item.order = static_cast<std::uint64_t>(i);
        pending.push_back(std::move(item));
    }
    std::vector<MapEventEffect> effects;
    if (magic == "ME2") {
        std::size_t count = 0;
        if (!(input >> count) || count > kMaximumEffects) return false;
        for (std::size_t i = 0; i < count; ++i) {
            unsigned kind = 0, enabled = 0;
            std::int32_t reference = 0;
            if (!(input >> kind >> reference >> enabled) || kind > static_cast<unsigned>(MapEventKind::star_collected) || enabled > 1) return false;
            const auto found = by_reference_.find(reference);
            if (found == by_reference_.end()) return false;
            const auto& node = nodes[found->second];
            const auto type = static_cast<MapEventKind>(kind);
            bool compatible = false;
            switch (type) {
            case MapEventKind::mover_trigger: compatible = node.kind == MapEventNodeKind::mover; break;
            case MapEventKind::cutscene_start: compatible = node.kind == MapEventNodeKind::cutscene; break;
            case MapEventKind::sound: compatible = node.kind == MapEventNodeKind::sound; break;
            case MapEventKind::music: compatible = node.kind == MapEventNodeKind::music; break;
            case MapEventKind::actor_trigger:
            case MapEventKind::actor_spell: compatible = node.kind == MapEventNodeKind::actor; break;
            case MapEventKind::checkpoint: compatible = node.kind == MapEventNodeKind::checkpoint; break;
            case MapEventKind::star_collected: compatible = node.kind == MapEventNodeKind::star && node.consumed; break;
            }
            if (!compatible || (enabled == 0 && type != MapEventKind::music)) return false;
            effects.push_back({type, reference, node.tag, node.asset_path, enabled != 0});
        }
    }
    input >> std::ws;
    if (!input.eof()) return false;
    nodes_ = std::move(nodes);
    pending_ = std::move(pending);
    effects_ = std::move(effects);
    order_ = static_cast<std::uint64_t>(pending_count);
    clock_ = 0;
    stars_ = stars;
    healthy_ = true;
    return true;
}

}  // namespace hpvr::quest
