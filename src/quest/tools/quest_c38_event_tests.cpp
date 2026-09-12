#include "hpvr/quest_map_events.h"
#include "../../../android/app/src/main/cpp/quest_challenge_movers.h"
#include "../../../android/app/src/main/cpp/quest_challenge_zones.h"

#include <algorithm>
#include <bit>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace hpvr::quest;
using namespace hpvr::wand;
namespace {
unsigned checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
Hp1ClassDefaultProperty Text(std::string name, std::string value, std::int64_t index = -1) {
    Hp1ClassDefaultProperty p;
    p.name = std::move(name); p.kind = 6; p.text_value = std::move(value);
    p.text_value_serialized = true; p.array_index = index;
    return p;
}
Hp1ClassDefaultProperty Bool(std::string name, bool value) {
    Hp1ClassDefaultProperty p; p.name = std::move(name); p.kind = 3;
    p.boolean_value = value; p.boolean_value_serialized = true; return p;
}
Hp1ClassDefaultProperty Number(std::string name, std::uint32_t bits, std::int64_t index = -1) {
    Hp1ClassDefaultProperty p; p.name = std::move(name); p.kind = 2; p.array_index = index;
    for (unsigned i = 0; i < 4; ++i) p.value.push_back(static_cast<std::uint8_t>(bits >> (i * 8U)));
    return p;
}
Hp1ActorVisual Actor(std::int32_t reference, std::string type, std::string tag, std::string event = {}) {
    Hp1ActorVisual a; a.actor_reference = reference; a.qualified_class_name = std::move(type);
    a.object_name = "Object" + std::to_string(reference); a.tag = std::move(tag); a.event = std::move(event);
    return a;
}
bool Has(const std::vector<MapEventEffect>& effects, MapEventKind kind, std::int32_t ref) {
    return std::ranges::any_of(effects, [&](const auto& e) { return e.kind == kind && e.actor_reference == ref; });
}

void MoverMath() {
    namespace m = hpvr::quest::movers;
    const auto near = [](const m::Vector& a, const m::Vector& b) {
        for (unsigned i = 0; i < 3; ++i) if (std::abs(a[i] - b[i]) > .0001F) return false;
        return true;
    };
    Check(near(m::Apply(m::Rotation({0,16384,0}), {1,0,0}), {0,1,0}), "mover yaw convention");
    Check(near(m::Apply(m::Rotation({16384,0,0}), {1,0,0}), {0,0,1}), "mover pitch convention");
    Check(near(m::Apply(m::Rotation({0,0,16384}), {0,1,0}), {0,0,-1}), "mover roll convention");
    const m::Vector base{1300,9700,-3100};
    const m::Vector local{.4F,.2F,-.6F};
    const auto rotated = m::Apply(m::Rotation(base), local);
    Check(near(m::InverseApply(m::Rotation(base), rotated), local), "full rotation inverse");
    m::Placement placement{{10,2,-6},base,.7F,.02F};
    const auto baked = m::Add(placement.pivot_scene, m::RotateBrushLocal(m::FromUnreal(local),base,.7F));
    Check(near(m::TransformPoint(baked,placement,{}),baked), "zero mover key preserves baked pose");
    m::Key key{{64,-32,16},{0,0,8192}};
    auto expected = m::Apply(m::Rotation(m::Add(base,key.rotation_units)),local);
    for (unsigned i=0;i<3;++i) expected[i]+=key.offset_unreal[i]*.02F;
    expected=m::Add(placement.pivot_scene,m::Yaw(m::FromUnreal(expected),.7F));
    Check(near(m::TransformPoint(baked,placement,key),expected), "mover full base plus key rotation");
    Check(near(m::TransformPoint(m::BuildTransform(placement,key),baked),expected), "cached affine equals direct mover transform");
    Check(near(m::GridStepUnreal({0,0,0},{90,2,40}),{-64,0,0}), "grid inherited increment and dominant axis");
    Check(near(m::GridStepUnreal({0,0,0},{2,-90,40}),{0,64,0}), "grid pushes away on other axis");
    m::Motion motion; motion.count=3;
    motion.keys[1].offset_unreal={100,0,0}; motion.keys[2].offset_unreal={100,0,-60};
    Check(m::Settle(motion,0)&&!motion.moving, "mover begins stationary");
    Check(m::Start(motion,true), "start authored multikey mover");
    auto result=m::Advance(motion,1.5F);
    Check(result.arrivals==1&&!result.finished&&near(motion.pose.offset_unreal,{100,0,-30}), "one trigger chains intermediate keys");
    const auto saved=m::SaveMotion(motion); m::Motion restored; restored.keys=motion.keys;restored.count=3;
    Check(m::RestoreMotion(restored,saved)&&m::SaveMotion(restored)==saved, "mover mid-segment transactional resume");
    Check(!m::RestoreMotion(restored,saved+" junk")&&m::SaveMotion(restored)==saved, "mover corrupt state rejected");
    result=m::Advance(restored,.5F);
    Check(result.arrivals==1&&result.finished&&near(restored.pose.offset_unreal,{100,0,-60}), "only final key completes mover event");
    Check(m::Start(restored,false), "multikey mover closes"); result=m::Advance(restored,2);
    Check(result.finished&&result.arrivals==2&&near(restored.pose.offset_unreal,{}), "multikey reverse follows intermediate key");
    restored.count=2;m::Settle(restored,0);Check(m::Start(restored,true,true), "loop starts only on activation");
    result=m::Advance(restored,2.5F);
    Check(result.arrivals==2&&!result.finished&&restored.moving&&near(restored.pose.offset_unreal,{50,0,0}), "loop wraps and consumes residual frame time");
    restored.seconds=0;result=m::Advance(restored,60);
    Check(result.arrivals==1&&restored.moving, "zero duration loop bounded");
}

void ZoneMath() {
    Hp1BspTopology topology;topology.status=Hp1ProfileStatus::ok;topology.zone_count=3;
    topology.zone_actor_references={0,1,2};
    Hp1BspNode root;root.plane={0,0,1,0};root.front_node_index=root.back_node_index=-1;root.zone_indices={2,1};
    topology.nodes.push_back(root);
    Hp1ActorVisualCensus census;census.status=Hp1ProfileStatus::ok;
    auto safe=Actor(1,"Engine.ZoneInfo","Safe"),kill=Actor(2,"Engine.ZoneInfo","Pit");
    kill.serialized_properties.push_back(Bool("bKillZone",true));census.actors={safe,kill};
    hpvr::quest::zones::Query query;
    Check(query.Load(topology,census),"zone topology load");
    Check(query.ZoneAt({0,0,10})==1&&!query.IsLethal({0,0,10}),"front safe zone");
    Check(query.ZoneAt({0,0,-10})==2&&query.IsLethal({0,0,-10}),"back kill zone from mapped actor");
    Check(query.ZoneAt({0,0,0})==1,"plane boundary deterministic");
    Check(!query.ZoneAt({0,0,std::numeric_limits<float>::quiet_NaN()}),"nonfinite zone query rejected");
    auto malformed=topology;malformed.nodes[0].front_node_index=30;
    Check(!query.Load(malformed,census)&&query.IsLethal({0,0,-10}),"zone invalid load transactional");
    malformed=topology;malformed.nodes[0].front_node_index=0;
    Check(query.Load(malformed,census)&&!query.ZoneAt({0,0,10}),"cyclic zone tree bounded");
}

Hp1ActorVisualCensus Fixture() {
    Hp1ActorVisualCensus c; c.status = Hp1ProfileStatus::ok;
    auto spell = Actor(1, "HPBase.spellTrigger", "Spell", "Cascade");
    spell.serialized_properties.push_back(Bool("bTriggerOnceOnly", true)); c.actors.push_back(spell);
    auto dispatcher = Actor(2, "Engine.Dispatcher", "Cascade");
    dispatcher.serialized_properties = {Text("OutEvents", "Button"), Text("OutEvents", "Counter", 1),
        Number("OutDelays", std::bit_cast<std::uint32_t>(0.5F), 1), Text("OutEvents", "Scene", 2),
        Number("OutDelays", std::bit_cast<std::uint32_t>(0.75F), 2)}; c.actors.push_back(dispatcher);
    c.actors.push_back(Actor(3, "Engine.Mover", "Button", "Counter"));
    c.actors.push_back(Actor(4, "Engine.Counter", "Counter", "Gate"));
    c.actors.push_back(Actor(5, "Engine.Mover", "Gate"));
    auto cut = Actor(6, "HPBase.CutScene", "Scene");
    cut.serialized_properties = {Bool("bTouchStarts", false)}; c.actors.push_back(cut);
    auto trigger = Actor(7, "Engine.Trigger", "Enable", "Gate");
    trigger.initial_state = "OtherTriggerTurnsOn";
    trigger.serialized_properties = {Bool("bInitiallyActive", false), Bool("bTriggerOnceOnly", true)};
    c.actors.push_back(trigger);
    c.actors.push_back(Actor(8, "Tut1.Tut1Gnome", "Gnome", "Counter"));
    c.actors.push_back(Actor(9, "HarryPotter.savepoint", "Save"));
    auto stars = Actor(10, "HPBase.StarsTrigger", "Finish");
    stars.serialized_properties = {Text("loserTrigger", "Low"), Text("avgTrigger", "Average"),
        Text("winnerTrigger", "All"), Number("avgStarCount", 6), Number("winnerStarCount", 8)};
    c.actors.push_back(stars);
    c.actors.push_back(Actor(11, "HPBase.CutScene", "Low"));
    c.actors.push_back(Actor(12, "HPBase.CutScene", "Average"));
    c.actors.push_back(Actor(13, "HPBase.CutScene", "All"));
    for (int i = 0; i < 8; ++i) c.actors.push_back(Actor(20+i, "HProps.Star", "Star"));
    return c;
}

void Synthetic() {
    const auto fixture = Fixture(); MapEventGraph graph;
    Check(graph.Load(fixture), "load synthetic graph");
    Check(graph.nodes().size() == fixture.actors.size(), "all event nodes imported");
    Check(graph.Find(4)->remaining == 2, "retail inherited counter default two");
    Check(graph.Touch(6) && graph.DrainEffects().empty(), "non touch scene never autoplays");
    Check(graph.Spell(1), "spell hits dispatcher");
    auto e = graph.DrainEffects();
    Check(e.size() == 1 && Has(e, MapEventKind::mover_trigger, 3), "first button immediate");
    Check(!graph.Spell(1), "spell trigger one shot");
    Check(graph.Find(4)->remaining == 2, "counter waits for actual mover arrival");
    Check(graph.Advance(0.5F), "delayed event");
    Check(graph.Find(4)->remaining == 1 && graph.DrainEffects().empty(), "only first count");
    Check(graph.Signal(3), "mover arrival");
    e = graph.DrainEffects(); Check(Has(e, MapEventKind::mover_trigger, 5), "counter releases gate");
    Check(!graph.Signal(3), "duplicate arrival cannot count twice");
    Check(graph.Advance(0.74F) && graph.DrainEffects().empty(), "dispatcher delays cumulative");
    const auto saved = graph.Serialize(); Check(!saved.empty(), "nonempty graph save");
    MapEventGraph resumed; Check(resumed.Load(fixture) && resumed.Restore(saved), "restore pending graph");
    Check(resumed.DrainEffects().empty(), "restore cannot replay side effects");
    Check(resumed.Advance(0.02F), "advance restored delay");
    e = resumed.DrainEffects(); Check(Has(e, MapEventKind::cutscene_start, 6), "restored pending scene delivered once");
    Check(resumed.Dispatch("SCENE") && resumed.DrainEffects().empty(), "casefold and once cutscene");
    Check(resumed.Touch(7) && resumed.DrainEffects().empty(), "inactive trigger no contact");
    Check(resumed.Dispatch("ENABLE") && resumed.DrainEffects().empty(), "enable does not fake touch");
    Check(resumed.Touch(7), "enabled trigger contact");
    e = resumed.DrainEffects(); Check(Has(e, MapEventKind::mover_trigger, 5), "contact emits event");
    Check(resumed.Touch(7) && resumed.DrainEffects().empty(), "contact once consumed");
    Check(resumed.Spell(8), "gnome spell becomes actor effect");
    e = resumed.DrainEffects(); Check(Has(e, MapEventKind::actor_spell, 8), "gnome reducer does not invent AI");
    Check(resumed.Signal(8) && !resumed.Signal(8), "gnome completion idempotent");
    (void)resumed.DrainEffects();
    Check(resumed.Touch(9), "savepoint");
    e = resumed.DrainEffects(); Check(Has(e, MapEventKind::checkpoint, 9), "savepoint effect");
    const auto stable = resumed.Serialize();
    Check(!resumed.Restore(stable + " junk") && resumed.Serialize() == stable, "trailing garbage transactional reject");
    Check(!resumed.Restore(std::string(16385, 'x')) && resumed.Serialize() == stable, "oversized transactional reject");
    auto changed = fixture; changed.actors[0].event = "Different"; MapEventGraph other;
    Check(other.Load(changed) && !other.Restore(stable), "other graph fingerprint rejected");
    Check(!resumed.Advance(std::numeric_limits<float>::quiet_NaN()) && !resumed.Advance(-1), "invalid delta rejected");
    for (int count : {0, 5, 6, 7, 8}) {
        MapEventGraph stars; Check(stars.Load(fixture), "stars load");
        for (int i = 0; i < count; ++i) Check(stars.CollectStar(20+i), "collect unique star");
        if (count) Check(!stars.CollectStar(20), "star duplicate rejected");
        (void)stars.DrainEffects();
        MapEventGraph restored; Check(restored.Load(fixture) && restored.Restore(stars.Serialize()), "star save roundtrip");
        Check(restored.star_count() == static_cast<unsigned>(count), "star count survives");
        Check(restored.Touch(10), "stars finish contact");
        const auto effects = restored.DrainEffects();
        Check(effects.size() == 1 && Has(effects, MapEventKind::cutscene_start, count >= 8 ? 13 : count >= 6 ? 12 : 11), "authored star award branch");
    }
    Hp1ActorVisualCensus cycle; cycle.status = Hp1ProfileStatus::ok;
    auto loop = Actor(30, "Engine.Dispatcher", "Loop"); loop.serialized_properties = {Text("OutEvents", "Loop")};
    cycle.actors.push_back(loop); MapEventGraph bounded;
    Check(bounded.Load(cycle) && !bounded.Dispatch("Loop") && !bounded.healthy(), "cyclic graph fails bounded");
    Check(bounded.Serialize().empty(), "broken graph cannot overwrite good save");
    MapEventGraph emitted; Check(emitted.Load(fixture) && emitted.Spell(1), "queued effect save fixture");
    Check(emitted.CollectStar(20), "star collected before save drain");
    const auto emitted_state = emitted.Serialize();
    MapEventGraph emitted_restore; Check(emitted_restore.Load(fixture) && emitted_restore.Restore(emitted_state), "restore undrained effects");
    auto queued = emitted_restore.DrainEffects();
    Check(queued.size() == 2 && Has(queued,MapEventKind::mover_trigger,3) && Has(queued,MapEventKind::star_collected,20), "save preserves undispatched mover and pickup effects");
    Check(emitted_restore.DrainEffects().empty() && !emitted_restore.CollectStar(20), "restored effect queue drains once");
    Check(emitted_restore.Advance(.5F) && emitted_restore.Signal(3), "arrival produces pending gate");
    const auto after_signal = emitted_restore.Serialize();
    Check(emitted_restore.Restore(after_signal) && Has(emitted_restore.DrainEffects(),MapEventKind::mover_trigger,5), "save after signal preserves gate effect");
    auto legacy = emitted_restore.Serialize(); legacy.replace(0,3,"ME1"); legacy.resize(legacy.size()-2);
    Check(emitted_restore.Restore(legacy) && emitted_restore.DrainEffects().empty(), "legacy ME1 graph accepted without replay");
    auto corrupt_effect = emitted_state; const auto last = corrupt_effect.find_last_of(' ');
    corrupt_effect.replace(last+1,1,"7");
    const auto before_bad=emitted_restore.Serialize();
    Check(!emitted_restore.Restore(corrupt_effect) && emitted_restore.Serialize()==before_bad, "invalid queued effect rejects transactionally");
}

void Owned(const std::filesystem::path& root) {
    const auto census = inspect_hp1_actor_visuals(root / "Maps/Lev_Tut1b.unr");
    const auto topology=load_hp1_bsp_topology(root / "Maps/Lev_Tut1b.unr");
    hpvr::quest::zones::Query zones;
    Check(zones.Load(topology,census),"owned exact BSP zone query");
    Check(zones.lethal_count()==2,"owned two authored lethal zones");
    for(const auto& actor:census.actors)if(actor.qualified_class_name=="Engine.ZoneInfo"){
        const auto zone=zones.ZoneAt({actor.location_unreal.x,actor.location_unreal.y,actor.location_unreal.z});
        Check(zone&&topology.zone_actor_references[*zone]==actor.actor_reference,"owned zone actor classifies into its linked BSP zone");
    }
    Check(!zones.IsLethal({883.783F,-6592.24F,799.681F}),"owned player entry is safe");
    Check(zones.IsLethal({-2711.73F,-7006.49F,106})&&zones.IsLethal({-2432.51F,-5437.74F,562.958F}),"owned both pit interiors lethal");
    MapEventGraph graph; Check(graph.Load(census), "owned map graph load");
    const auto count = [&](MapEventNodeKind kind) { return std::ranges::count_if(graph.nodes(), [kind](const auto& n) { return n.kind == kind; }); };
    Check(count(MapEventNodeKind::mover) == 67, "owned 67 movers");
    Check(count(MapEventNodeKind::spell_trigger) == 16, "owned 16 spell triggers");
    Check(count(MapEventNodeKind::cutscene) == 15, "owned 15 cutscenes");
    Check(count(MapEventNodeKind::star) == 8, "owned eight stars");
    Check(count(MapEventNodeKind::checkpoint) == 3, "owned three savepoints");
    Check(graph.Spell(4511) && Has(graph.DrainEffects(),MapEventKind::mover_trigger,4511), "owned grid push reaches physical mover");
    Check(graph.Signal(4511) && graph.DrainEffects().empty(), "grid signals only after arrival");
    Check(graph.Find(5628)->average_stars == 6 && graph.Find(5628)->all_stars == 8, "owned star thresholds");
    Check(graph.Find(4412)->counter_initial == 2 && graph.Find(4467)->counter_initial == 1, "owned inherited and override counters");
    Check(graph.Spell(2579), "first dual-door switch");
    auto effects = graph.DrainEffects(); Check(Has(effects, MapEventKind::mover_trigger, 3035), "first switch rotates authored mover");
    Check(!graph.Find(4467)->consumed && !graph.Find(4412)->consumed, "gate count waits arrival");
    Check(graph.Signal(3035), "first switch reached");
    effects = graph.DrainEffects(); Check(Has(effects, MapEventKind::mover_trigger, 3503) && !Has(effects, MapEventKind::mover_trigger, 2887), "one switch opens only first gate");
    Check(graph.Spell(2508) && graph.Signal(3535), "second dual-door switch arrival");
    effects = graph.DrainEffects(); Check(Has(effects, MapEventKind::mover_trigger, 2887), "second switch opens portcullis");
    Check(graph.Spell(2784), "bridge spell");
    effects = graph.DrainEffects(); Check(Has(effects, MapEventKind::mover_trigger, 2587) && !Has(effects, MapEventKind::mover_trigger, 3939), "bridge button before bridge motion");
    Check(graph.Advance(1.91F), "bridge delayed chain");
    effects = graph.DrainEffects(); Check(Has(effects, MapEventKind::mover_trigger, 3939), "bridge authored delayed event");
    MapEventGraph resumed; Check(resumed.Load(census) && resumed.Restore(graph.Serialize()), "owned graph restore");
    Check(resumed.DrainEffects().empty() && !resumed.Spell(2784), "owned restore no replay or reused switch");
    std::cout << "OWNED_MAP2_EVENTS=PASS nodes=" << graph.nodes().size() << " save_bytes=" << graph.Serialize().size() << '\n';
}
}  // namespace

int main(int argc, char** argv) {
    try {
        Synthetic();
        MoverMath();
        ZoneMath();
        if (argc == 2) Owned(std::filesystem::path(argv[1]));
        std::cout << "MAP_EVENT_GRAPH=PASS checks=" << checks << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "MAP_EVENT_GRAPH=FAIL " << e.what() << '\n'; return 1; }
}
