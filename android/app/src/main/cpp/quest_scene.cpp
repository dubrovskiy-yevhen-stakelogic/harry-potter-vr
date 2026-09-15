#ifndef HPVR_QUEST_CPU_ONLY
#include "quest_scene.h"
#include "quest_audio.h"
#include "quest_reflections.h"
#include "quest_planar_mirror.h"
#include "quest_load_trace.h"

#include <android/log.h>

#include "hogwarts_frag.spv.h"
#include "hogwarts_vert.spv.h"
#include "mirror_vert.spv.h"
#include "mirror_frag.spv.h"
#include "mirror_veil_frag.spv.h"
#include "mirror_capture_frag.spv.h"
#include "particle_frag.spv.h"
#include "particle_vert.spv.h"
#include "wand_frag.spv.h"
#include "wand_vert.spv.h"
#else
#include "hpvr/quest_gesture.h"
#include "hpvr/quest_spell_targets.h"
#include <cstdio>
#endif

#include "hpvr/hp1_gesture_c.h"
#include "hpvr/hp1_package_linker.h"
#include "hpvr/hp1_abyss_lighting.h"
#include "hpvr/hp1_authored_environment.h"
#include "hpvr/quest_lighting.h"
#include "hpvr/quest_package_resource.h"
#include "hpvr/quest_view.h"
#include "hpvr/quest_frontend.h"
#include "hpvr/quest_campaign_progress.h"
#include "hpvr/quest_chest.h"
#include "hpvr/quest_basic_cast.h"
#include "hpvr/quest_cast_permissions.h"
#include "hpvr/quest_target_marker.h"
#include "hpvr/quest_target_tracking.h"
#include "hpvr/quest_voice_hint.h"
#include "hpvr/quest_original_spell.h"
#include "hpvr/quest_fixture_effects.h"
#include "hpvr/quest_ambient_effects.h"
#include "hpvr/quest_effects_clock.h"
#include "hpvr/quest_mover_visibility.h"
#include "hpvr/quest_prop_motion.h"
#include "hpvr/quest_death_transition.h"
#include "hpvr/quest_asset_cache.h"
#include "hpvr/quest_reflection_math.h"
#include "hpvr/quest_abyss_fog.h"
#include "hpvr/quest_cinematic.h"
#include "hpvr/quest_map_events.h"
#include "hpvr/quest_gnome.h"
#include "hpvr/quest_gnome_visibility.h"
#include "hpvr/quest_vertex_layout.h"
#include "hpvr/quest_award_facing.h"
#include "hpvr/quest_grid_push.h"
#include "hpvr/quest_grid_motion.h"
#include "hpvr/quest_maps.h"
#include "hpvr/quest_broom_flight.h"
#include "hpvr/quest_broom_lesson.h"
#include "hpvr/quest_broom_session.h"
#include "hpvr/quest_charms_lesson.h"
#include "hpvr/quest_lesson_speech.h"
#include "hpvr/quest_charms_block.h"
#include "quest_challenge_movers.h"
#include "quest_challenge_zones.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <functional>
#include <iterator>
#include <iomanip>
#include <sstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <type_traits>
#include <vector>

namespace hpvr::quest {
namespace {

constexpr char kLogTag[] = "HPVR.Quest";
constexpr float kMetersPerUnrealUnit = 0.02F;
constexpr float kTau = 6.28318530717958647692F;
constexpr std::uint32_t kPolyNotSolid = 0x00000008U;
constexpr float kWalkableNormalY = 0.64F;
constexpr float kGroundContactEpsilonMeters = 0.006F;
constexpr float kStageReferenceFootY = -42.0F * kMetersPerUnrealUnit;
constexpr float kStageGroundSearchDownMeters = 0.45F;
constexpr float kStageGroundSearchUpMeters = 0.32F;
constexpr float kPlayerEyeHeightMeters = 0.815F;
constexpr float kPlayerCapsuleRadiusMeters =
    kPlayerEyeHeightMeters * (15.0F / 40.75F);
constexpr float kPlayerCapsuleHalfHeightMeters =
    kPlayerEyeHeightMeters * (42.0F / 40.75F);
constexpr float kCollisionMaximumSubstepMeters = 0.08F;
constexpr std::uint32_t kMaximumTriangles =
    HPVR_HP1_BSP_MAX_SLICE_TRIANGLES;
constexpr std::int32_t kFireTextureReference = 72;
constexpr std::int32_t kHarryActorReference = 603;
constexpr std::uint32_t kMaximumCombinedTextureLayers = 256;
constexpr std::uint32_t kCharacterAnimationFrameCount = 16;
constexpr float kCharacterAnimationFramesPerSecond = 10.0F;
constexpr float kWandLengthMeters = 0.34F;
constexpr float kTemplateRibbonWidthMeters = 0.006F;
constexpr float kTrailRibbonWidthMeters = 0.012F;
constexpr float kFlipendoSpeedMetersPerSecond = 600.0F*kMetersPerUnrealUnit;
constexpr float kFlipendoMissDistanceMeters = 18.0F;
constexpr float kFlipendoImpactSeconds = 2.1F;

#ifdef HPVR_QUEST_CPU_ONLY
#define HPVR_LOGI(...) do { std::printf(__VA_ARGS__); std::puts(""); } while (false)
#define HPVR_LOGE(...) do { std::printf(__VA_ARGS__); std::puts(""); } while (false)
#else
#define HPVR_LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define HPVR_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)
#endif

struct GpuVertex {
    float position[3];
    float texture_uv[2];
    float lightmap_uv[2];
    std::uint32_t texture_layer;
    std::uint32_t polygon_flags;
    std::uint32_t has_lightmap;
    std::uint32_t packed_light;
};

struct CollisionTriangle {
    std::array<std::array<float, 3>, 3> vertices{};
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
    std::array<float, 3> normal{};
};

static_assert(sizeof(GpuVertex) == 44);
static_assert(offsetof(GpuVertex, texture_uv) == 12);
static_assert(offsetof(GpuVertex, lightmap_uv) == 20);
static_assert(offsetof(GpuVertex, texture_layer) == 28);
static_assert(offsetof(GpuVertex, has_lightmap) == 36);
static_assert(offsetof(GpuVertex, packed_light) == 40);

struct WandGpuVertex {
    float position[3];
    float color[4];
};

struct WandPushConstants {
    std::array<float, 16> model_view_projection;
    std::array<float, 4> color_multiplier;
};

struct ParticleGpuVertex {
    float position[3];
    float texture_uv[2];
    float color[4]{1,1,1,1};
};

struct ParticlePushConstants {
    std::array<float, 16> model_view_projection;
    std::array<float, 4> tint;
    std::uint32_t texture_layer{};
    std::array<std::uint32_t,3> padding{};
    std::array<float,4> uv_rect{0,0,1,1};
};

struct DoorDraw {
    movers::Placement placement;
    movers::Motion motion;
    bool challenge=false,collision_only=false,two_sided=false,cutscene_hold=false;
    float open_seconds=1,close_seconds=1;
    std::int32_t actor_reference=0;
    std::string initial_state;
    bool looping=false,loop_started=false,grid=false,completion_sent=false;
    float hold=0,stay_open=0,grid_increment=1.28F;
    std::array<float,3> grid_offset{},grid_target{};
    // Transient physics/collision bookkeeping; prepared scenes and saves keep
    // their existing explicit layouts and re-evaluate support after loading.
    grid_motion::State grid_physics;
    std::size_t collision_first=std::numeric_limits<std::size_t>::max(),collision_count=0;
    std::uint32_t first_vertex{}, vertex_count{};
    std::array<float, 3> pivot{};
    std::array<float,3> open_offset{};
    float open_yaw = 0.0F;
    float phase = 0.0F;
    float duration = 1.0F;
    bool opening = false;
    std::string tag;
};

struct CharacterClip {
    std::uint32_t first_vertex{};
    float duration = 1.0F;
    std::uint32_t frame_count = kCharacterAnimationFrameCount;
};

struct CharacterDraw {
    bool player=false,flying=false;
    bool child_template = false;
    bool enabled = true;
    bool animation_loop = true;
    bool collision_disabled = false;
    std::map<std::string, CharacterClip> clips;
    std::string active_clip = "breathe";
    float animation_time = 0.0F;
    float base_yaw = 0.0F;
    float yaw = 0.0F;
    float desired_yaw = 0.0F;
    std::uint32_t first_vertex = 0;
    std::uint32_t vertex_count = 0;
    std::int32_t actor_reference = 0;
    std::string object_name;
    std::string class_name;
    std::array<float, 3> collision_center{};
    std::array<float, 3> base_origin{};
    std::array<float, 3> cutscene_offset{};
    std::array<float,3> visual_minimum{},visual_maximum{};
    float collision_radius = 0.25F;
    float collision_min_y = 0.0F;
    float collision_max_y = 1.7F;
    bool staged = false;
};

using SceneLight = AuthoredLight;

struct ScriptTrigger {
    std::int32_t actor_reference = 0;
    std::array<float, 3> position{};
    float radius = 0.8F;
    float half_height = 1.0F;
    std::string object_name;
    std::string tag;
    std::string event;
    bool inside = false;
};

struct KnightDraw {
    std::array<float,3> origin{};
    float yaw=0,scale=1,time=0;
    std::uint32_t layer=0,first=0,count=0,frames=0;
    int last_sound_phase=-1;
};
struct FlameEmitter {
    std::array<float, 3> position{};
    float phase = 0.0F;
    float scale = 1.0F;
};

struct GlowEmitter {
    std::array<float, 3> position{};
    float radius = 0.25F;
    float intensity = 0.5F;
    float phase = 0.0F;
};

struct CutsceneLocation {
    std::int32_t actor_reference = 0;
    std::string alias;
    std::array<float, 3> position{};
};

struct CutsceneTrack {
    float authored_yaw=0.0F;
    bool dialogue_waiting=false;
    std::size_t dialogue_index=0;
    unsigned cast_slot=0;
    std::int32_t speaking_actor=0;
    std::array<float, 3> position{};
    bool speaking = false;
    bool animating = false;
    std::int32_t actor_reference = 0;
    std::string alias;
    std::array<std::string, 40> commands;
    std::size_t next_command = 0;
    float delay_seconds = 0.0F;
    float move_seconds = 0.0F;
    float move_duration_seconds = 0.0F;
    std::array<float, 3> move_start{};
    std::array<float, 3> move_target{};
    std::string waiting_for;
    std::string idle_clip="breathe",walk_clip;
    bool moving = false;
    bool finished = false;
    bool camera = false;
};

struct IntroCutscene {
    bool harry_released=false, control_released=false;
    std::string object_name;
    std::array<float,3> trigger_position{};
    float trigger_radius=0;
    float trigger_width=0,trigger_height=0,trigger_yaw=0;
    bool trigger_box=false;
    std::string camera_target;
    std::vector<CutsceneTrack> tracks;
    std::vector<CutsceneLocation> locations;
    std::set<std::string> cues;
    std::array<float, 3> camera_position{};
    float camera_speed = 0.2F;
    std::size_t executed_commands = 0;
    std::size_t spoken_lines = 0;
    bool available = false;
    bool play_on_load = false;
    bool playing = false;
    bool camera_active = false;
    bool camera_position_valid = false;
};

std::string AsciiFold(std::string_view value);
bool ReleaseCutsceneControlIfReady(IntroCutscene& scene){
    if(!scene.playing||scene.control_released||!scene.harry_released||scene.camera_active)return false;
    scene.control_released=true;return true;
}
bool FinishJumpCameraTour(IntroCutscene& scene){
    // CutScene54's independent camera tour can outlast the spoken lesson.
    // Once its authored CutEnd cue arrives, skip only that camera's remaining
    // tour, keeping RELEASE and Trigger HelpWithJumping after its WAITFOR.
    if(scene.object_name!="cutscene54"||!scene.cues.contains("cutend"))return false;
    bool changed=false;
    for(auto& track:scene.tracks)if(track.camera&&!track.finished){
        for(auto i=track.next_command;i<track.commands.size();++i)
            if(AsciiFold(track.commands[i])=="waitfor cutend"){
                track.next_command=i+1;track.moving=false;track.delay_seconds=0;
                track.waiting_for.clear();scene.camera_active=false;changed=true;break;
            }
    }
    return changed;
}
std::optional<unsigned> SelectStoryEncounter(unsigned stage,const std::array<IntroCutscene,4>& scenes,
                                            const std::array<float,3>& head,bool filch_seen=false){
    const auto near=[&](unsigned i,float radius){const auto& p=scenes[i].trigger_position;
        const std::array<float,3> d{head[0]-p[0],head[1]-p[1],head[2]-p[2]};
        return std::hypot(d[0],d[2])<radius&&std::abs(d[1]-kPlayerEyeHeightMeters)<2.0F;};
    // Draco greets Harry at the entrance; Filch is a separate later approach.
    if((stage==12||stage==14)&&near(1,7.0F))return 1;
    if(stage>=16&&!filch_seen&&near(0,scenes[0].trigger_radius+1.0F))return 0;
    if(stage>=16&&stage<20&&stage%2==0){const auto i=(stage-12)/2;
        if(near(i,scenes[i].trigger_radius+0.26F))return i;
        if(i==3){
            const auto& scene=scenes[i];
            auto d=std::array<float,3>{head[0]-scene.trigger_position[0],head[1]-scene.trigger_position[1]-kPlayerEyeHeightMeters,head[2]-scene.trigger_position[2]};
            const float c=std::cos(scene.trigger_yaw),s=std::sin(scene.trigger_yaw);
            const float x=d[0]*c-d[2]*s,z=d[0]*s+d[2]*c;
            if(scene.trigger_box&&std::abs(x)<scene.trigger_width+.3F&&std::abs(z)<scene.trigger_radius+.3F&&
               std::abs(d[1])<scene.trigger_height+kPlayerCapsuleHalfHeightMeters)return i;
            // Saved C32 players may already be beyond the missed doorway.
            // Recover only along the authored route INSIDE the classroom,
            // never merely because Hermione's encounter has finished.
            for(const auto& loc:scene.locations)if(AsciiFold(loc.alias)=="locname0"||AsciiFold(loc.alias)=="locname1"||AsciiFold(loc.alias)=="locname2"){
                const auto dx=head[0]-loc.position[0],dz=head[2]-loc.position[2];
                if(std::hypot(dx,dz)<3.0F&&std::abs(head[1]-loc.position[1]-kPlayerEyeHeightMeters)<3)return i;
            }
        }
    }
    return std::nullopt;
}

struct ChildWaypoint {
    std::array<float, 3> position{};
    float pause = 0.0F;
    bool destroy = false;
};
struct ChildChoice { std::int32_t model_reference{}; float speed = 4.4F; };
struct ChildSpawner {
    std::string tag;
    std::vector<ChildChoice> choices;
    std::vector<ChildWaypoint> path;
    std::size_t next_choice = 0;
};
struct ChildInstance {
    std::int32_t model_reference{};
    std::size_t spawner{}, waypoint = 1;
    std::array<float, 3> position{};
    float speed = 4.4F, yaw = 0.0F, age = 0.0F, pause = 0.0F;
    bool active = true;
};
struct TimedSceneEvent { float time{}; std::string tag; };
struct ChildSystem {
    std::vector<wand::Hp1ActorVisual> prototypes;
    std::vector<ChildSpawner> spawners;
    std::map<std::string, std::vector<TimedSceneEvent>> dispatchers;
    std::vector<TimedSceneEvent> pending;
    std::vector<ChildInstance> actors;
    float time = 0.0F;
    std::size_t spawned = 0, destroyed = 0, ground_misses = 0;
};

struct WorldMetadata {
    ChildSystem children;
    std::vector<SceneLight> lights;
    std::vector<FlameEmitter> flames;
    std::vector<GlowEmitter> glows;
    std::vector<ScriptTrigger> triggers;
    wand::Hp1PcmSound ambient_sound;
    wand::Hp1MpegSound wand_trace_sound;
    wand::Hp1MpegSound wand_start_sound;
    wand::Hp1MpegSound spell_cast_sound;
    wand::Hp1MpegSound incantation_sound;
    wand::Hp1PcmSound spell_hit_sound;
    std::vector<wand::Hp1MpegSound> cutscene_dialogue;
    IntroCutscene intro_cutscene;
    std::string ambient_object;
    std::array<float,3> ambient_position{};
    float ambient_radius=0,ambient_volume=0;
    std::size_t actor_count = 0;
    std::size_t script_class_count = 0;
    std::size_t tagged_actor_count = 0;
    std::size_t event_actor_count = 0;
    std::size_t event_edge_count = 0;
    std::size_t ambient_actor_count = 0;
};

struct SpellProjectile {
    std::array<float, 3> origin{};
    std::array<float, 3> direction{0.0F, 0.0F, -1.0F};
    std::array<float, 3> impact_position{};
    SpellTargetResult target{};
    float distance_m = 0.0F;
    float terminal_distance_m = 0.0F;
    float impact_seconds = 0.0F;
    bool flying = false;
    bool impacting = false;
};

struct LoadedCharacterMesh {
    std::vector<hpvr_hp1_skeletal_mesh_vertex> vertices;
    std::vector<hpvr_hp1_skeletal_animation_point> frames;
    std::vector<std::uint8_t> textures;
    hpvr_hp1_skeletal_mesh_report report{};
    hpvr_hp1_skeletal_animation_report animation_report{};
    std::uint32_t layer_base = 0;
};

struct LoadedStaticMesh {
    std::vector<hpvr_hp1_skeletal_mesh_vertex> vertices;
    std::vector<std::uint8_t> textures;
    hpvr_hp1_skeletal_mesh_report report{};
    std::uint32_t layer_base = 0;
};

static_assert(sizeof(WandGpuVertex) == 28);
static_assert(sizeof(WandPushConstants) == 80);
static_assert(sizeof(ParticleGpuVertex) == 36);
static_assert(sizeof(ParticlePushConstants) == 112);
static_assert(offsetof(ParticlePushConstants,uv_rect)==96);

constexpr std::array<WandGpuVertex, 6> kGuideQuad{{
    {{0.0F, -0.5F, 0.0F}, {1.0F, 1.0F, 1.0F, 1.0F}},
    {{1.0F, -0.5F, 0.0F}, {1.0F, 1.0F, 1.0F, 1.0F}},
    {{1.0F, 0.5F, 0.0F}, {1.0F, 1.0F, 1.0F, 1.0F}},
    {{0.0F, -0.5F, 0.0F}, {1.0F, 1.0F, 1.0F, 1.0F}},
    {{1.0F, 0.5F, 0.0F}, {1.0F, 1.0F, 1.0F, 1.0F}},
    {{0.0F, 0.5F, 0.0F}, {1.0F, 1.0F, 1.0F, 1.0F}},
}};

constexpr std::array<ParticleGpuVertex, 6> kParticleQuad{{
    {{0.0F, -0.5F, 0.0F}, {0.0F, 1.0F}},
    {{1.0F, -0.5F, 0.0F}, {0.0F, 0.0F}},
    {{1.0F, 0.5F, 0.0F}, {1.0F, 0.0F}},
    {{0.0F, -0.5F, 0.0F}, {0.0F, 1.0F}},
    {{1.0F, 0.5F, 0.0F}, {1.0F, 0.0F}},
    {{0.0F, 0.5F, 0.0F}, {1.0F, 1.0F}},
}};

std::vector<WandGpuVertex> BuildRadialGlowDiscVertices() {
    constexpr std::size_t kSegments = 24;
    constexpr float kMiddleRadius = 0.34F;
    constexpr float kMiddleAlpha = 0.52F;
    std::vector<WandGpuVertex> vertices;
    vertices.reserve(kSegments * 9U);
    const auto vertex = [](const float radius, const float angle,
                           const float alpha) {
        return WandGpuVertex{
            {0.5F + 0.5F * radius * std::cos(angle),
             0.5F * radius * std::sin(angle), 0.0F},
            {1.0F, 1.0F, 1.0F, alpha}};
    };
    const WandGpuVertex center{
        {0.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F, 1.0F}};
    for (std::size_t index = 0; index < kSegments; ++index) {
        const float angle0 = kTau * static_cast<float>(index) /
                             static_cast<float>(kSegments);
        const float angle1 = kTau * static_cast<float>(index + 1U) /
                             static_cast<float>(kSegments);
        const auto middle0 = vertex(kMiddleRadius, angle0, kMiddleAlpha);
        const auto middle1 = vertex(kMiddleRadius, angle1, kMiddleAlpha);
        const auto outer0 = vertex(1.0F, angle0, 0.0F);
        const auto outer1 = vertex(1.0F, angle1, 0.0F);
        vertices.insert(vertices.end(), {
            center, middle0, middle1,
            middle0, outer0, outer1,
            middle0, outer1, middle1,
        });
    }
    return vertices;
}

bool NormalizeVector(const std::array<float, 3>& input,
                     std::array<float, 3>* const output) {
    if (output == nullptr) return false;
    const float length = std::sqrt(input[0] * input[0] +
                                   input[1] * input[1] +
                                   input[2] * input[2]);
    if (!std::isfinite(length) || length < 1.0e-5F) return false;
    *output = {input[0] / length, input[1] / length, input[2] / length};
    return true;
}

std::array<float, 3> RotateYaw(const std::array<float, 3>& value,
                               const float yaw) {
    const float cosine = std::cos(yaw);
    const float sine = std::sin(yaw);
    return {cosine * value[0] + sine * value[2], value[1],
            -sine * value[0] + cosine * value[2]};
}

std::string AsciiFold(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return result;
}

std::array<float, 3> ActorLocalPosition(
    const wand::Hp1ActorVisual& actor,
    const hpvr_hp1_player_start_report& player_start,
    const float player_start_yaw) {
    const std::array<float, 3> package_position{
        actor.location_unreal.y * kMetersPerUnrealUnit,
        actor.location_unreal.z * kMetersPerUnrealUnit,
        -actor.location_unreal.x * kMetersPerUnrealUnit};
    return RotateYaw(
        {package_position[0] - player_start.position_m[0],
         package_position[1] - player_start.position_m[1],
         package_position[2] - player_start.position_m[2]},
        player_start_yaw);
}

std::array<float, 3> HsvLightColor(const std::uint8_t hue,
                                   const std::uint8_t saturation) {
    const float h = static_cast<float>(hue) * 6.0F / 255.0F;
    const float s = 1.0F - static_cast<float>(saturation) / 255.0F;
    const float chroma = s;
    const float x = chroma * (1.0F - std::abs(
        std::fmod(h, 2.0F) - 1.0F));
    std::array<float, 3> rgb{};
    if (h < 1.0F) rgb = {chroma, x, 0.0F};
    else if (h < 2.0F) rgb = {x, chroma, 0.0F};
    else if (h < 3.0F) rgb = {0.0F, chroma, x};
    else if (h < 4.0F) rgb = {0.0F, x, chroma};
    else if (h < 5.0F) rgb = {x, 0.0F, chroma};
    else rgb = {chroma, 0.0F, x};
    const float match = 1.0F - chroma;
    for (float& component : rgb) component += match;
    return rgb;
}

std::optional<wand::Hp1PcmSound> LoadNamedSound(
    const std::filesystem::path& package,
    std::string_view object_name) {
    const auto census = wand::inspect_hp1_sound_assets(package);
    if (census.status != wand::Hp1ProfileStatus::ok) return std::nullopt;
    for (const auto& sound : census.sounds) {
        if (sound.wave_bytes == 0 ||
            AsciiFold(sound.object_name) != AsciiFold(object_name)) continue;
        auto pcm = wand::load_hp1_pcm_sound(package, sound.sound_reference);
        if (pcm.status == wand::Hp1ProfileStatus::ok) return pcm;
    }
    return std::nullopt;
}

std::optional<wand::Hp1MpegSound> LoadNamedMpegSound(
    const std::filesystem::path& package,
    std::string_view object_name) {
    const auto census = wand::inspect_hp1_sound_assets(package);
    if (census.status != wand::Hp1ProfileStatus::ok) return std::nullopt;
    for (const auto& sound : census.sounds) {
        if (AsciiFold(sound.object_name) != AsciiFold(object_name)) continue;
        auto encoded = wand::load_hp1_mpeg_sound(
            package, sound.sound_reference);
        if (encoded.status == wand::Hp1ProfileStatus::ok) return encoded;
    }
    return std::nullopt;
}

bool LoadNamedMpegSounds(
    const std::filesystem::path& package,
    const std::array<std::string_view, 5>& names,
    std::vector<wand::Hp1MpegSound>* const output) {
    if (output == nullptr) return false;
    const auto census = wand::inspect_hp1_sound_assets(package);
    if (census.status != wand::Hp1ProfileStatus::ok) return false;
    output->clear();
    output->reserve(names.size());
    for (const auto name : names) {
        const auto found = std::ranges::find_if(
            census.sounds, [name](const auto& sound) {
                return AsciiFold(sound.object_name) == AsciiFold(name);
            });
        if (found == census.sounds.end()) return false;
        auto sound = wand::load_hp1_mpeg_sound(
            package, found->sound_reference);
        if (sound.status != wand::Hp1ProfileStatus::ok) return false;
        output->push_back(std::move(sound));
    }
    return true;
}

float SerializedFloat(const wand::Hp1ActorVisual& actor, std::string_view name,
                      std::int64_t index, float fallback) {
    for (const auto& p : actor.serialized_properties) {
        if (AsciiFold(p.name) == name && std::max<std::int64_t>(0,p.array_index) == index &&
            p.value.size() == 4) {
            float value;
            std::memcpy(&value,p.value.data(),4);
            if (std::isfinite(value)) return value;
        }
    }
    return fallback;
}
std::string SerializedName(const wand::Hp1ActorVisual& actor, std::string_view name) {
    for (const auto& p:actor.serialized_properties)
        if (AsciiFold(p.name)==name && p.text_value_serialized) return AsciiFold(p.text_value);
    return {};
}
bool LoadChildSystem(const wand::Hp1ActorVisualCensus& census,
                     const hpvr_hp1_player_start_report& start, float yaw, ChildSystem* output) {
    ChildSystem result;
    std::map<std::string,const wand::Hp1ActorVisual*> points;
    std::map<std::int32_t,std::int32_t> model_ids;
    for (const auto& actor:census.actors) {
        if (AsciiFold(actor.qualified_class_name)=="engine.patrolpoint")
            points.emplace(AsciiFold(actor.object_name),&actor);
        if (AsciiFold(actor.qualified_class_name)!="engine.dispatcher" ||
            !AsciiFold(actor.tag).starts_with("momdis")) continue;
        std::map<std::int64_t,std::string> events;
        for (const auto& p:actor.serialized_properties)
            if (AsciiFold(p.name)=="outevents" && p.text_value_serialized &&
                !p.text_value.empty() && AsciiFold(p.text_value)!="none")
                events.emplace(std::max<std::int64_t>(0,p.array_index),AsciiFold(p.text_value));
        float elapsed=0;
        for (const auto& [index,event]:events) {
            elapsed+=std::max(0.0F,SerializedFloat(actor,"outdelays",index,0));
            result.dispatchers[AsciiFold(actor.tag)].push_back({elapsed,event});
        }
    }
    for (const auto& actor:census.actors) {
        if (AsciiFold(actor.qualified_class_name)!="hpbase.triggerspwnbschronppnt") continue;
        auto next=SerializedName(actor,"startpatrolpoint_objectname");
        // Unrouted, unreferenced KidPath7 is not invented as a visible actor.
        if (next.empty() || next=="none") continue;
        ChildSpawner spawner;
        spawner.tag=AsciiFold(actor.tag);
        std::set<std::string> visited;
        const wand::Hp1ActorVisual* first=nullptr;
        while (!next.empty() && next!="none") {
            if (!visited.insert(next).second || visited.size()>64 || !points.contains(next)) return false;
            const auto& point=*points.at(next);
            if (!first) first=&point;
            bool destroy=false;
            for(const auto& p:point.serialized_properties)
                if (AsciiFold(p.name)=="bdestroypawn" && p.boolean_value_serialized) destroy=p.boolean_value;
            spawner.path.push_back({ActorLocalPosition(point,start,yaw),
                std::max(0.0F,SerializedFloat(point,"pausetime",0,0)),destroy});
            if(destroy) break;
            next=SerializedName(point,"nextpatrol_objectname");
        }
        if(spawner.path.size()<2 || !first) return false;
        for(const auto& p:actor.serialized_properties) {
            if(AsciiFold(p.name)!="basechartospawn" || !p.object_reference_serialized ||
                p.object_reference==0) continue;
            if(!model_ids.contains(p.object_reference)) {
                wand::Hp1ActorVisual prototype;
                prototype.actor_reference=0x10000000+static_cast<std::int32_t>(result.prototypes.size());
                prototype.class_reference=p.object_reference;
                prototype.location_serialized=true;
                prototype.location_unreal=first->location_unreal;
                prototype.object_name="ScriptChildModel"+std::to_string(result.prototypes.size());
                for(const auto& part:p.object_path) {
                    if(!prototype.qualified_class_name.empty()) prototype.qualified_class_name+='.';
                    prototype.qualified_class_name+=part;
                }
                model_ids.emplace(p.object_reference,prototype.actor_reference);
                result.prototypes.push_back(prototype);
            }
            spawner.choices.push_back({model_ids.at(p.object_reference),
                std::clamp(SerializedFloat(actor,"basechargroundspeed",
                    std::max<std::int64_t>(0,p.array_index),220)*kMetersPerUnrealUnit,0.2F,8.0F)});
        }
        if(spawner.choices.empty()) return false;
        result.spawners.push_back(std::move(spawner));
    }
    if(result.dispatchers.size()!=4 || result.spawners.size()!=8 || result.prototypes.empty()) return false;
    HPVR_LOGI("[hpvr.quest.children.data] status=READY dispatchers=%zu spawners=%zu models=%zu source=OWNED_PATROL_CHAINS",
        result.dispatchers.size(),result.spawners.size(),result.prototypes.size());
    *output=std::move(result);
    return true;
}

bool LoadIntroCutscene(
    const wand::Hp1ActorVisualCensus& census,
    const hpvr_hp1_player_start_report& player_start,
    const float player_start_yaw,
    IntroCutscene* const output, const std::string& object_name="cutscene4",bool tutorial=true) {
    if (output == nullptr) return false;
    const auto scene = std::ranges::find_if(
        census.actors, [&](const auto& actor) {
            return AsciiFold(actor.qualified_class_name) ==
                       "hpbase.cutscene" &&
                   AsciiFold(actor.object_name) == AsciiFold(object_name);
        });
    if (scene == census.actors.end()) return false;

    std::array<CutsceneTrack, 10> tracks{};
    std::array<bool, 10> track_present{};
    struct LocationReference {
        std::int32_t actor_reference{};
        std::string alias;
    };
    std::vector<LocationReference> location_references;
    bool play_on_load = false;
    for (const auto& property : scene->serialized_properties) {
        const auto property_name = AsciiFold(property.name);
        if (property_name == "blevelloadstarts" &&
            property.boolean_value_serialized) {
            play_on_load = property.boolean_value;
        } else if (property_name == "cast" &&
                   property.object_reference_serialized &&
                   property.text_value_serialized) {
            if(property.object_reference==0)continue;
            const std::size_t index = property.array_index < 0
                ? 0U : static_cast<std::size_t>(property.array_index);
            if (index >= tracks.size() || track_present[index]) return false;
            tracks[index].actor_reference = property.object_reference;
            tracks[index].cast_slot=static_cast<unsigned>(index);
            tracks[index].alias = property.text_value;
            tracks[index].camera =
                AsciiFold(property.text_value).find("cam") !=
                std::string::npos;
            track_present[index] = true;
        } else if (property_name == "locs" &&
                   property.object_reference_serialized &&
                   property.text_value_serialized) {
            location_references.push_back(
                {property.object_reference, property.text_value});
        } else if (property_name.size() == 11U &&
                   property_name.starts_with("cast") &&
                   property_name.ends_with("script") &&
                   property.text_value_serialized) {
            const char digit = property_name[4];
            if (digit < '0' || digit > '9') return false;
            const std::size_t track_index =
                static_cast<std::size_t>(digit - '0');
            const std::size_t command_index = property.array_index < 0
                ? 0U : static_cast<std::size_t>(property.array_index);
            if (command_index >= tracks[track_index].commands.size() ||
                !tracks[track_index].commands[command_index].empty()) {
                return false;
            }
            tracks[track_index].commands[command_index] = property.text_value;
        }
    }
    IntroCutscene cutscene;
    cutscene.object_name=AsciiFold(object_name);
    cutscene.trigger_position=ActorLocalPosition(*scene,player_start,player_start_yaw);
    cutscene.trigger_radius=scene->collision_radius*kMetersPerUnrealUnit;
    cutscene.trigger_height=scene->collision_height*kMetersPerUnrealUnit;
    cutscene.trigger_yaw=player_start_yaw+scene->rotation_units[1]*kTau/65536;
    for(const auto& p:scene->serialized_properties){
        if(AsciiFold(p.name)=="collisionwidth"&&p.value.size()==4){float width=0;std::memcpy(&width,p.value.data(),4);
            if(std::isfinite(width)&&width>0&&width<10000)cutscene.trigger_width=width*kMetersPerUnrealUnit;}
        if(AsciiFold(p.name)=="collidetype"&&p.value.size()==1)cutscene.trigger_box=p.value[0]==2;
    }
    cutscene.play_on_load = play_on_load;
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        if (track_present[index]) {
            const auto actor = std::ranges::find_if(census.actors,
                [&](const auto& a) { return a.actor_reference == tracks[index].actor_reference; });
            if (actor != census.actors.end()){
                tracks[index].position = ActorLocalPosition(*actor, player_start, player_start_yaw);
                tracks[index].authored_yaw=static_cast<float>(actor->rotation_units[1])*kTau/65536.0F+player_start_yaw;
                const auto camera_class=AsciiFold(actor->qualified_class_name);
                tracks[index].camera=tracks[index].camera||camera_class.find("potcam")!=std::string::npos||camera_class=="hpbase.basecam";
            }
            cutscene.tracks.push_back(std::move(tracks[index]));
        }
    }
    for (const auto& reference : location_references) {
        if(reference.actor_reference==0)continue;
        const auto actor = std::ranges::find_if(
            census.actors, [&reference](const auto& item) {
                return item.actor_reference == reference.actor_reference;
            });
        if (actor == census.actors.end() || !actor->location_serialized) {
            return false;
        }
        cutscene.locations.push_back(
            {reference.actor_reference, reference.alias,
             ActorLocalPosition(*actor, player_start, player_start_yaw)});
    }
    if(cutscene.tracks.empty() || (tutorial&&cutscene.locations.empty()))return false;
    if(tutorial&&cutscene.object_name=="cutscene4" && (!cutscene.play_on_load ||
        cutscene.tracks.size()!=5U || cutscene.locations.size()!=21U))return false;
    cutscene.available = true;
    cutscene.playing = true;
    HPVR_LOGI(
        "[hpvr.quest.cutscene.data] status=READY object=%s "
        "source=SERIALIZED_ACTOR_PROPERTIES tracks=%zu locations=%zu "
        "play_on_load=%s interpreter=PARALLEL_UE1_SUBSET",
        cutscene.object_name.c_str(),cutscene.tracks.size(), cutscene.locations.size(),
        cutscene.play_on_load?"YES":"NO");
    *output = std::move(cutscene);
    return true;
}

bool LoadWorldMetadata(
    const std::filesystem::path& data_root,
    const std::filesystem::path& map_package,
    const hpvr_hp1_player_start_report& player_start,
    const float player_start_yaw,
    WorldMetadata* const output) {
    if (output == nullptr) return false;
    const auto census = wand::inspect_hp1_actor_visuals(map_package);
    if (census.status != wand::Hp1ProfileStatus::ok) {
        HPVR_LOGE("[hpvr.quest.world] status=ACTOR_REJECTED error=%s",
                  census.error.c_str());
        return false;
    }
    WorldMetadata metadata;
    const bool tutorial=AsciiFold(map_package.stem().string())=="lev_tut1";
    if (tutorial&&!LoadChildSystem(census, player_start, player_start_yaw, &metadata.children)) return false;
    metadata.actor_count = census.actors.size();
    if (!LoadIntroCutscene(census, player_start, player_start_yaw,
                           &metadata.intro_cutscene,tutorial?"cutscene4":AsciiFold(map_package.stem().string())=="lev_tut2"?"cutscene10":"cutscene0",tutorial)) {
        HPVR_LOGE("[hpvr.quest.cutscene.data] status=REJECTED "
                  "object=CutScene4");
        return false;
    }
    std::set<std::string> script_classes;
    std::map<std::string, std::size_t> tag_counts;
    struct AmbientCandidate {
        std::int32_t sound_reference{};
        float distance_squared{};
        std::array<float,3> position{};
        float radius=0,volume=0;
    };
    std::vector<AmbientCandidate> ambient_candidates;
    for (const auto& actor : census.actors) {
        script_classes.insert(AsciiFold(actor.qualified_class_name));
        if (actor.tag_serialized && !actor.tag.empty() &&
            AsciiFold(actor.tag) != "none") {
            ++metadata.tagged_actor_count;
            ++tag_counts[AsciiFold(actor.tag)];
        }
        if (actor.event_serialized && !actor.event.empty() &&
            AsciiFold(actor.event) != "none") {
            ++metadata.event_actor_count;
        }
        if (actor.location_serialized &&
            AsciiFold(actor.qualified_class_name) == "engine.light" &&
            actor.light_brightness > 0 && actor.light_radius > 0) {
            const bool dark = std::ranges::any_of(actor.serialized_properties,
                [](const auto& property) {
                    return AsciiFold(property.name) == "bdarklight" &&
                           property.boolean_value_serialized && property.boolean_value;
                });
            metadata.lights.push_back({
                ActorLocalPosition(actor, player_start, player_start_yaw),
                HsvLightColor(actor.light_hue, actor.light_saturation),
                static_cast<float>(actor.light_radius) * 25.0F *
                    kMetersPerUnrealUnit,
                static_cast<float>(actor.light_brightness) / 255.0F * (dark ? -1.0F : 1.0F),
                actor.light_effect});
        }
        if (actor.location_serialized) {
            const std::string actor_class =
                AsciiFold(actor.qualified_class_name);
            if (actor_class == "hpparticle.torchfire02"||(!tutorial&&actor_class=="hpparticle.fire01")) {
                auto position =
                    ActorLocalPosition(actor, player_start, player_start_yaw);
                metadata.flames.push_back({
                    position,
                    static_cast<float>(actor.actor_reference & 255) *
                        0.071F,
                    2.4F});
                metadata.glows.push_back({
                    position, 0.34F, 0.55F,
                    static_cast<float>(actor.actor_reference & 255) *
                        0.043F});
                metadata.lights.push_back(
                    {position, {1.0F, 0.43F, 0.12F}, 4.8F, 1.0F});
            }
        }
        if (actor.location_serialized && actor.event_serialized &&
            !actor.event.empty() && AsciiFold(actor.event) != "none" &&
            actor.collision_radius > 0.0F) {
            metadata.triggers.push_back({
                actor.actor_reference,
                ActorLocalPosition(actor, player_start, player_start_yaw),
                actor.collision_radius * kMetersPerUnrealUnit,
                std::max(actor.collision_height * kMetersPerUnrealUnit,
                         0.8F),
                actor.object_name, actor.tag, actor.event, false});
        }
        if (actor.location_serialized && actor.ambient_sound_serialized &&
            actor.ambient_sound_reference != 0) {
            const auto local =
                ActorLocalPosition(actor, player_start, player_start_yaw);
            ambient_candidates.push_back({
                actor.ambient_sound_reference,
                local[0] * local[0] + local[1] * local[1] +
                    local[2] * local[2],local,
                actor.sound_radius*25.0F*kMetersPerUnrealUnit,float(actor.sound_volume)/255});
            ++metadata.ambient_actor_count;
        }
    }
    metadata.script_class_count = script_classes.size();
    for (const auto& actor : census.actors) {
        if (!actor.event_serialized) continue;
        const auto found = tag_counts.find(AsciiFold(actor.event));
        if (found != tag_counts.end()) metadata.event_edge_count += found->second;
    }

    std::ranges::sort(ambient_candidates, {},
                      &AmbientCandidate::distance_squared);
    const auto linked = wand::link_hp1_package_graph(data_root, map_package);
    if (linked.status != wand::Hp1PackageLinkStatus::ok) {
        HPVR_LOGE("[hpvr.quest.world] status=LINK_REJECTED error=%s",
                  linked.error.c_str());
        return false;
    }
    const std::string map_source = AsciiFold(map_package.stem().string());
    for (const auto& candidate : ambient_candidates) {
        if (candidate.sound_reference >= 0) continue;
        const auto resolved = std::ranges::find_if(
            linked.imports, [&candidate, &map_source](const auto& import) {
                return AsciiFold(import.source_package) == map_source &&
                       import.source_reference == candidate.sound_reference &&
                       import.target_kind ==
                           wand::Hp1ImportTargetKind::export_object &&
                       import.target_reference > 0;
            });
        if (resolved == linked.imports.end()) continue;
        const auto package = std::ranges::find_if(
            linked.graph.packages, [&resolved](const auto& item) {
                return item.kind == wand::Hp1ResolvedPackageKind::data_package &&
                       AsciiFold(item.package_name) ==
                           AsciiFold(resolved->target_package);
            });
        if (package == linked.graph.packages.end()) continue;
        auto sound = wand::load_hp1_pcm_sound(
            package->path, resolved->target_reference);
        if (sound.status != wand::Hp1ProfileStatus::ok) continue;
        metadata.ambient_object = sound.object_name;
        metadata.ambient_position=candidate.position;
        metadata.ambient_radius=candidate.radius;metadata.ambient_volume=candidate.volume;
        metadata.ambient_sound = std::move(sound);
        break;
    }
    if (metadata.ambient_sound.status != wand::Hp1ProfileStatus::ok) {
        const auto fallback = LoadNamedSound(
            data_root / "Sounds/Ambient.uax", "s_fire_loop");
        if (fallback.has_value()) {
            metadata.ambient_object = fallback->object_name;
            metadata.ambient_sound = std::move(*fallback);
            HPVR_LOGI(
                "[hpvr.quest.audio.data] status=STREAM_WRAPPERS_DEFERRED "
                "fallback=%s",
                metadata.ambient_object.c_str());
        }
    }
    const auto magic_package = data_root / "Sounds/Magic_sfx.uax";
    auto wand_trace = LoadNamedMpegSound(
        magic_package, "spell_tracing_loop");
    auto wand_start = LoadNamedMpegSound(magic_package, "wand_ready_loop");
    auto cast = LoadNamedMpegSound(magic_package, "spell_cast");
    auto incantation = LoadNamedMpegSound(magic_package, "flipendo_no");
    auto hit = LoadNamedSound(magic_package, "s_spell_hit1");
    constexpr std::array<std::string_view, 5> kIntroDialogueNames{{
        "111DumbledoreInfo1",
        "111DumbledoreInfo2",
        "111DumbledoreInfo3",
        "111DumbledoreInfo4",
        "Dumbledore_01",
    }};
    const bool dialogue_loaded = LoadNamedMpegSounds(
        data_root / "Sounds/AllDialog.uax", kIntroDialogueNames,
        &metadata.cutscene_dialogue);
    if (!wand_trace.has_value() || !wand_start.has_value() ||
        !cast.has_value() || !incantation.has_value() || !hit.has_value() ||
        metadata.ambient_sound.status != wand::Hp1ProfileStatus::ok ||
        !dialogue_loaded) {
        HPVR_LOGE(
            "[hpvr.quest.audio.data] status=REJECTED ambient=%u "
            "wand_trace=%s wand_start=%s cast=%s voice=%s hit=%s "
            "cutscene_dialogue=%s",
            static_cast<unsigned>(metadata.ambient_sound.status),
            wand_trace.has_value() ? "YES" : "NO",
            wand_start.has_value() ? "YES" : "NO",
            cast.has_value() ? "YES" : "NO",
            incantation.has_value() ? "YES" : "NO",
            hit.has_value() ? "YES" : "NO",
            dialogue_loaded ? "YES" : "NO");
        return false;
    }
    metadata.wand_trace_sound = std::move(*wand_trace);
    metadata.wand_start_sound = std::move(*wand_start);
    metadata.spell_cast_sound = std::move(*cast);
    metadata.incantation_sound = std::move(*incantation);
    metadata.spell_hit_sound = std::move(*hit);
    HPVR_LOGI(
        "[hpvr.quest.cutscene.dialogue] status=READY package=AllDialog.uax "
        "clips=%zu names=111DumbledoreInfo1,111DumbledoreInfo2,"
        "111DumbledoreInfo3,111DumbledoreInfo4,Dumbledore_01",
        metadata.cutscene_dialogue.size());
    HPVR_LOGI(
        "[hpvr.quest.world] status=READY actors=%zu classes=%zu tags=%zu "
        "events=%zu event_edges=%zu triggers=%zu lights=%zu flames=%zu "
        "glows=%zu "
        "ambient_actors=%zu",
        metadata.actor_count, metadata.script_class_count,
        metadata.tagged_actor_count, metadata.event_actor_count,
        metadata.event_edge_count, metadata.triggers.size(),
        metadata.lights.size(), metadata.flames.size(), metadata.glows.size(),
        metadata.ambient_actor_count);
    HPVR_LOGI(
        "[hpvr.quest.audio.data] status=READY ambient=%s "
        "wand_trace=%s wand_start=%s cast=%s voice=%s hit=%s",
        metadata.ambient_object.c_str(),
        metadata.wand_trace_sound.object_name.c_str(),
        metadata.wand_start_sound.object_name.c_str(),
        metadata.spell_cast_sound.object_name.c_str(),
        metadata.incantation_sound.object_name.c_str(),
        metadata.spell_hit_sound.object_name.c_str());
    *output = std::move(metadata);
    return true;
}

bool TriangleHeightAtXZ(const GpuVertex* const triangle,
                        const float x,
                        const float z,
                        float* const output) {
    if (triangle == nullptr || output == nullptr) return false;
    const float ax = triangle[0].position[0];
    const float ay = triangle[0].position[1];
    const float az = triangle[0].position[2];
    const float bx = triangle[1].position[0];
    const float by = triangle[1].position[1];
    const float bz = triangle[1].position[2];
    const float cx = triangle[2].position[0];
    const float cy = triangle[2].position[1];
    const float cz = triangle[2].position[2];
    const float denominator =
        (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
    if (std::abs(denominator) <= 1.0e-7F) return false;
    const float u =
        ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / denominator;
    const float v =
        ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / denominator;
    const float w = 1.0F - u - v;
    constexpr float kBarycentricEpsilon = 1.0e-4F;
    if (u < -kBarycentricEpsilon || v < -kBarycentricEpsilon ||
        w < -kBarycentricEpsilon) {
        return false;
    }
    *output = u * ay + v * by + w * cy;
    return std::isfinite(*output);
}

[[maybe_unused]] bool FindStageGroundHeight(const std::vector<GpuVertex>& map_vertices,
                           const std::uint32_t map_vertex_count,
                           const float x,
                           const float z,
                           float* const output) {
    if (output == nullptr || map_vertex_count == 0 ||
        map_vertex_count > map_vertices.size() ||
        map_vertex_count % 3 != 0 || !std::isfinite(x) ||
        !std::isfinite(z)) {
        return false;
    }
    const float minimum_y =
        kStageReferenceFootY - kStageGroundSearchDownMeters;
    const float maximum_y =
        kStageReferenceFootY + kStageGroundSearchUpMeters;
    float best = -std::numeric_limits<float>::infinity();
    for (std::size_t index = 0; index < map_vertex_count; index += 3) {
        const GpuVertex* const triangle = map_vertices.data() + index;
        if ((triangle[0].polygon_flags & kPolyNotSolid) != 0U) continue;
        const std::array<float, 3> ab{
            triangle[1].position[0] - triangle[0].position[0],
            triangle[1].position[1] - triangle[0].position[1],
            triangle[1].position[2] - triangle[0].position[2]};
        const std::array<float, 3> ac{
            triangle[2].position[0] - triangle[0].position[0],
            triangle[2].position[1] - triangle[0].position[1],
            triangle[2].position[2] - triangle[0].position[2]};
        const std::array<float, 3> cross{
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0]};
        const float cross_length = std::sqrt(
            cross[0] * cross[0] + cross[1] * cross[1] +
            cross[2] * cross[2]);
        if (!std::isfinite(cross_length) || cross_length <= 1.0e-7F ||
            std::abs(cross[1]) / cross_length < kWalkableNormalY) {
            continue;
        }
        float minimum_x = triangle[0].position[0];
        float maximum_x = minimum_x;
        float minimum_z = triangle[0].position[2];
        float maximum_z = minimum_z;
        for (std::size_t vertex = 1; vertex < 3; ++vertex) {
            minimum_x = std::min(minimum_x, triangle[vertex].position[0]);
            maximum_x = std::max(maximum_x, triangle[vertex].position[0]);
            minimum_z = std::min(minimum_z, triangle[vertex].position[2]);
            maximum_z = std::max(maximum_z, triangle[vertex].position[2]);
        }
        if (x < minimum_x - kGroundContactEpsilonMeters ||
            x > maximum_x + kGroundContactEpsilonMeters ||
            z < minimum_z - kGroundContactEpsilonMeters ||
            z > maximum_z + kGroundContactEpsilonMeters) {
            continue;
        }
        float height = 0.0F;
        if (!TriangleHeightAtXZ(triangle, x, z, &height) ||
            height < minimum_y || height > maximum_y) {
            continue;
        }
        best = std::max(best, height);
    }
    if (!std::isfinite(best)) return false;
    *output = best;
    return true;
}

std::array<float, 3> AddVector(const std::array<float, 3>& left,
                               const std::array<float, 3>& right) {
    return {left[0] + right[0], left[1] + right[1],
            left[2] + right[2]};
}

std::array<float, 3> SubtractVector(const std::array<float, 3>& left,
                                    const std::array<float, 3>& right) {
    return {left[0] - right[0], left[1] - right[1],
            left[2] - right[2]};
}

std::array<float, 3> ScaleVector(const std::array<float, 3>& value,
                                 const float scale) {
    return {value[0] * scale, value[1] * scale, value[2] * scale};
}

float DotVector(const std::array<float, 3>& left,
                const std::array<float, 3>& right) {
    return left[0] * right[0] + left[1] * right[1] +
           left[2] * right[2];
}

std::vector<CollisionTriangle> BuildCollisionTriangles(
    const std::vector<GpuVertex>& map_vertices,
    const std::uint32_t map_vertex_count) {
    std::vector<CollisionTriangle> output;
    if (map_vertex_count == 0 || map_vertex_count > map_vertices.size() ||
        map_vertex_count % 3 != 0) {
        return output;
    }
    output.reserve(map_vertex_count / 3);
    for (std::size_t index = 0; index < map_vertex_count; index += 3) {
        const GpuVertex* const source = map_vertices.data() + index;
        if ((source[0].polygon_flags & kPolyNotSolid) != 0U) continue;
        CollisionTriangle triangle{};
        for (std::size_t vertex = 0; vertex < 3; ++vertex) {
            triangle.vertices[vertex] = {
                source[vertex].position[0], source[vertex].position[1],
                source[vertex].position[2]};
        }
        const auto edge_ab =
            SubtractVector(triangle.vertices[1], triangle.vertices[0]);
        const auto edge_ac =
            SubtractVector(triangle.vertices[2], triangle.vertices[0]);
        const std::array<float, 3> cross{
            edge_ab[1] * edge_ac[2] - edge_ab[2] * edge_ac[1],
            edge_ab[2] * edge_ac[0] - edge_ab[0] * edge_ac[2],
            edge_ab[0] * edge_ac[1] - edge_ab[1] * edge_ac[0]};
        if (!NormalizeVector(cross, &triangle.normal)) continue;
        triangle.minimum = triangle.vertices[0];
        triangle.maximum = triangle.vertices[0];
        for (std::size_t vertex = 1; vertex < 3; ++vertex) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                triangle.minimum[axis] = std::min(
                    triangle.minimum[axis], triangle.vertices[vertex][axis]);
                triangle.maximum[axis] = std::max(
                    triangle.maximum[axis], triangle.vertices[vertex][axis]);
            }
        }
        output.push_back(triangle);
    }
    return output;
}

bool CollisionTriangleHeightAtXZ(const CollisionTriangle& triangle,
                                 const float x,
                                 const float z,
                                 float* const output) {
    if (output == nullptr) return false;
    const auto& a = triangle.vertices[0];
    const auto& b = triangle.vertices[1];
    const auto& c = triangle.vertices[2];
    const float denominator =
        (b[2] - c[2]) * (a[0] - c[0]) +
        (c[0] - b[0]) * (a[2] - c[2]);
    if (std::abs(denominator) <= 1.0e-7F) return false;
    const float u =
        ((b[2] - c[2]) * (x - c[0]) +
         (c[0] - b[0]) * (z - c[2])) /
        denominator;
    const float v =
        ((c[2] - a[2]) * (x - c[0]) +
         (a[0] - c[0]) * (z - c[2])) /
        denominator;
    const float w = 1.0F - u - v;
    constexpr float kBarycentricEpsilon = 1.0e-4F;
    if (u < -kBarycentricEpsilon || v < -kBarycentricEpsilon ||
        w < -kBarycentricEpsilon) {
        return false;
    }
    *output = u * a[1] + v * b[1] + w * c[1];
    return std::isfinite(*output);
}

bool CollisionTriangleSupportHeightAtXZ(const CollisionTriangle& triangle,float x,float z,float* support,float* surface=nullptr){
    if(std::abs(triangle.normal[1])<kWalkableNormalY)return false;
    const float radius=kPlayerCapsuleRadiusMeters;
    float height=0,distance_squared=0;
    if(!CollisionTriangleHeightAtXZ(triangle,x,z,&height)){
        distance_squared=std::numeric_limits<float>::infinity();
        for(unsigned edge=0;edge<3;++edge){
            const auto& a=triangle.vertices[edge];const auto& b=triangle.vertices[(edge+1)%3];
            const float dx=b[0]-a[0],dz=b[2]-a[2],length=dx*dx+dz*dz;
            const float t=length>1e-10F?std::clamp(((x-a[0])*dx+(z-a[2])*dz)/length,0.0F,1.0F):0.0F;
            const float ex=x-a[0]-t*dx,ez=z-a[2]-t*dz,d=ex*ex+ez*ez;
            if(d<distance_squared){distance_squared=d;height=a[1]+t*(b[1]-a[1]);}
        }
    }
    if(distance_squared>=radius*radius)return false;
    if(surface)*surface=height;
    *support=height-radius+std::sqrt(std::max(0.0F,radius*radius-distance_squared));
    return std::isfinite(*support);
}

bool FindCollisionGroundHeight(
    const std::vector<CollisionTriangle>& triangles,
    const float x,
    const float z,
    const float current_foot,
    float* const output) {
    if (output == nullptr || !std::isfinite(x) || !std::isfinite(z) ||
        !std::isfinite(current_foot)) {
        return false;
    }
    const float minimum_y =
        current_foot - kStageGroundSearchDownMeters;
    const float maximum_y =
        current_foot + kStageGroundSearchUpMeters;
    const float radius=kPlayerCapsuleRadiusMeters;
    float best = -std::numeric_limits<float>::infinity();
    for (const auto& triangle : triangles) {
        if (std::abs(triangle.normal[1]) < kWalkableNormalY ||
            x < triangle.minimum[0] - radius ||
            x > triangle.maximum[0] + radius ||
            z < triangle.minimum[2] - radius ||
            z > triangle.maximum[2] + radius) {
            continue;
        }
        // The lower hemisphere contacts the tread before the centre crosses
        // its riser. Follow that support envelope instead of treating stairs
        // as walls or teleporting the whole capsule up one step at a time.
        float support=0,surface=0;
        if(CollisionTriangleSupportHeightAtXZ(triangle,x,z,&support,&surface)&&
           surface>=minimum_y&&surface<=maximum_y&&support>=minimum_y)
            best=std::max(best,support);
    }
    if (!std::isfinite(best)) return false;
    *output = best;
    return true;
}

bool FindPropGroundBelow(
    const std::vector<CollisionTriangle>& triangles,
    const std::array<float, 3>& origin,
    float* const output) {
    if (output == nullptr) return false;
    constexpr float kMaximumProbeRise = 0.30F;
    constexpr float kMaximumProbeDrop = 3.0F;
    constexpr std::array<std::array<float, 2>, 5> kOffsets{{
        {0.0F, 0.0F}, {-0.10F, 0.0F}, {0.10F, 0.0F},
        {0.0F, -0.10F}, {0.0F, 0.10F},
    }};
    for (const auto& offset : kOffsets) {
        const float x = origin[0] + offset[0];
        const float z = origin[2] + offset[1];
        float best = -std::numeric_limits<float>::infinity();
        for (const auto& triangle : triangles) {
            if (std::abs(triangle.normal[1]) < kWalkableNormalY ||
                x < triangle.minimum[0] - kGroundContactEpsilonMeters ||
                x > triangle.maximum[0] + kGroundContactEpsilonMeters ||
                z < triangle.minimum[2] - kGroundContactEpsilonMeters ||
                z > triangle.maximum[2] + kGroundContactEpsilonMeters) {
                continue;
            }
            float height = 0.0F;
            if (!CollisionTriangleHeightAtXZ(triangle, x, z, &height) ||
                height > origin[1] + kMaximumProbeRise ||
                height < origin[1] - kMaximumProbeDrop) {
                continue;
            }
            best = std::max(best, height);
        }
        if (std::isfinite(best)) {
            *output = best;
            return true;
        }
    }
    return false;
}

std::array<float, 3> ClosestPointOnTriangle(
    const std::array<float, 3>& point,
    const CollisionTriangle& triangle) {
    const auto& a = triangle.vertices[0];
    const auto& b = triangle.vertices[1];
    const auto& c = triangle.vertices[2];
    const auto ab = SubtractVector(b, a);
    const auto ac = SubtractVector(c, a);
    const auto ap = SubtractVector(point, a);
    const float d1 = DotVector(ab, ap);
    const float d2 = DotVector(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F) return a;

    const auto bp = SubtractVector(point, b);
    const float d3 = DotVector(ab, bp);
    const float d4 = DotVector(ac, bp);
    if (d3 >= 0.0F && d4 <= d3) return b;

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) {
        return AddVector(a, ScaleVector(ab, d1 / (d1 - d3)));
    }

    const auto cp = SubtractVector(point, c);
    const float d5 = DotVector(ab, cp);
    const float d6 = DotVector(ac, cp);
    if (d6 >= 0.0F && d5 <= d6) return c;

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) {
        return AddVector(a, ScaleVector(ac, d2 / (d2 - d6)));
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0F && d4 - d3 >= 0.0F && d5 - d6 >= 0.0F) {
        const auto bc = SubtractVector(c, b);
        const float amount =
            (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return AddVector(b, ScaleVector(bc, amount));
    }

    const float inverse = 1.0F / (va + vb + vc);
    return AddVector(
        a, AddVector(ScaleVector(ab, vb * inverse),
                     ScaleVector(ac, vc * inverse)));
}

bool OverlapsCollisionWall(
    const std::vector<CollisionTriangle>& triangles,
    const std::array<float, 3>& center) {
    const float segment_half =
        kPlayerCapsuleHalfHeightMeters - kPlayerCapsuleRadiusMeters;
    const std::array<std::array<float, 3>, 3> samples{{
        {center[0], center[1] - segment_half, center[2]},
        center,
        {center[0], center[1] + segment_half, center[2]},
    }};
    const float contact_radius =
        kPlayerCapsuleRadiusMeters - kGroundContactEpsilonMeters;
    const float radius_squared = contact_radius * contact_radius;
    for (const auto& triangle : triangles) {
        const bool walkable=std::abs(triangle.normal[1]) >= kWalkableNormalY;
        if ((walkable && triangle.maximum[1]<=center[1]) ||
            center[0] + kPlayerCapsuleRadiusMeters < triangle.minimum[0] ||
            center[0] - kPlayerCapsuleRadiusMeters > triangle.maximum[0] ||
            center[1] + kPlayerCapsuleHalfHeightMeters <
                triangle.minimum[1] ||
            center[1] - kPlayerCapsuleHalfHeightMeters >
                triangle.maximum[1] ||
            center[2] + kPlayerCapsuleRadiusMeters < triangle.minimum[2] ||
            center[2] - kPlayerCapsuleRadiusMeters > triangle.maximum[2]) {
            continue;
        }
        for (const auto& sample : samples) {
            const auto closest = ClosestPointOnTriangle(sample, triangle);
            if(walkable && closest[1]<=center[1])continue;
            const auto delta = SubtractVector(sample, closest);
            if (DotVector(delta, delta) < radius_squared) return true;
        }
    }
    return false;
}

// Movable walls contribute contacts, not a second ground query: their mesh
// does not contain the room floor. Sweep also catches airborne crossings.
void ResolveMoverContacts(const std::vector<CollisionTriangle>& triangles,
                          const std::array<float,3>& origin,LocomotionMove& move){
    const auto delta=move.displacement;const float length=std::sqrt(DotVector(delta,delta));
    const unsigned steps=std::max(1U,static_cast<unsigned>(std::ceil(length/.05F)));
    for(unsigned i=1;i<=steps;++i){
        const auto candidate=AddVector(origin,ScaleVector(delta,float(i)/steps));
        if(OverlapsCollisionWall(triangles,candidate)){
            move.displacement=ScaleVector(delta,float(i-1)/steps);++move.blocked_substeps;return;
        }
    }
}

bool TryCollisionPosition(
    const std::vector<CollisionTriangle>& triangles,
    const std::array<float, 3>& current,
    const std::array<float, 3>& horizontal_step,
    std::array<float, 3>* const output,
    bool* const grounded) {
    if (output == nullptr || grounded == nullptr) return false;
    auto candidate = AddVector(current, horizontal_step);
    const float current_foot =
        current[1] - kPlayerCapsuleHalfHeightMeters;
    float ground_height = 0.0F;
    *grounded = FindCollisionGroundHeight(
        triangles, candidate[0], candidate[2], current_foot,
        &ground_height);
    if (*grounded) {
        candidate[1] =
            ground_height + kPlayerCapsuleHalfHeightMeters;
    } else return false; // No gravity/jump yet: never walk unsupported into air.
    if (OverlapsCollisionWall(triangles, candidate)) return false;
    *output = candidate;
    return true;
}

bool ResolveCollisionMovement(
    const std::vector<CollisionTriangle>& triangles,
    const std::array<float, 3>& capsule_center,
    const std::array<float, 3>& requested_displacement,
    LocomotionMove* const output) {
    if (output == nullptr || triangles.empty() ||
        !std::all_of(capsule_center.begin(), capsule_center.end(),
                     [](const float value) { return std::isfinite(value); }) ||
        !std::all_of(requested_displacement.begin(),
                     requested_displacement.end(),
                     [](const float value) { return std::isfinite(value); })) {
        return false;
    }
    *output = {};
    const std::array<float, 3> horizontal{
        requested_displacement[0], 0.0F, requested_displacement[2]};
    const float length = std::sqrt(
        horizontal[0] * horizontal[0] + horizontal[2] * horizontal[2]);
    if (length <= std::numeric_limits<float>::epsilon()) return true;
    const unsigned int substep_count = static_cast<unsigned int>(
        std::max(1.0F, std::ceil(
            length / kCollisionMaximumSubstepMeters)));
    const auto substep =
        ScaleVector(horizontal, 1.0F / static_cast<float>(substep_count));
    auto position = capsule_center;
    for (unsigned int step = 0; step < substep_count; ++step) {
        std::array<float, 3> candidate{};
        bool grounded = false;
        if (TryCollisionPosition(triangles, position, substep,
                                 &candidate, &grounded)) {
            position = candidate;
            output->grounded_substeps += grounded ? 1U : 0U;
            continue;
        }
        ++output->blocked_substeps;
        for (const auto& slide :
             std::array<std::array<float, 3>, 2>{{
                 {substep[0], 0.0F, 0.0F},
                 {0.0F, 0.0F, substep[2]},
             }}) {
            if (std::abs(slide[0]) <=
                    std::numeric_limits<float>::epsilon() &&
                std::abs(slide[2]) <=
                    std::numeric_limits<float>::epsilon()) {
                continue;
            }
            if (TryCollisionPosition(triangles, position, slide,
                                     &candidate, &grounded)) {
                position = candidate;
                output->grounded_substeps += grounded ? 1U : 0U;
            }
        }
    }
    output->displacement = SubtractVector(position, capsule_center);
    return true;
}

bool GroundScriptActor(const std::vector<CollisionTriangle>& triangles,const std::array<float,3>& previous,
                       const std::array<float,3>& requested,bool following,std::array<float,3>* out){
    if(!out)return false;auto probe=requested;
    if(following)probe[1]=previous[1]+0.12F;
    float floor;
    if(!FindPropGroundBelow(triangles,probe,&floor))return false;
    *out=requested;(*out)[1]=floor;return true;
}
void GroundCutsceneCast(IntroCutscene& scene,std::vector<CharacterDraw>& actors,
                       const std::vector<CollisionTriangle>& triangles){
    for(auto& track:scene.tracks)if(!track.camera)for(auto& actor:actors)
        if(actor.actor_reference==track.actor_reference){
            if(actor.flying)continue;
            if(actor.actor_reference==1329||actor.actor_reference==1326)
                actor.yaw=actor.desired_yaw=track.authored_yaw;
            std::array<float,3> feet;
            if(GroundScriptActor(triangles,actor.base_origin,track.position,false,&feet)){
                track.position=feet;actor.cutscene_offset=SubtractVector(feet,actor.base_origin);
            }
            if(scene.object_name=="cutscene52"&&(actor.actor_reference==1329||actor.actor_reference==1326)){
                const auto d=SubtractVector(scene.trigger_position,track.position);
                actor.yaw=actor.desired_yaw=std::atan2(d[0],d[2]);
            }
        }
}

// CutScene0 supplies the missing intermediate Ron route. Keep only its
// authored movement/attention; no Harry capture or silent camera speech.
void MakeRonLead(IntroCutscene& scene){
    std::erase_if(scene.tracks,[](const auto& t){return t.actor_reference!=1348;});
    for(auto& track:scene.tracks){
        std::array<std::string,40> movement{};std::size_t i=0;
        for(const auto& cmd:track.commands)if(AsciiFold(cmd).starts_with("moveto "))movement[i++]=cmd;
        movement[i]="Face AllPath2";track.commands=movement;
    }
    scene.harry_released=scene.control_released=true;
}
const CutsceneLocation* RonDestination(const IntroCutscene& scene){
    for(const auto& loc:scene.locations)if(AsciiFold(loc.alias)=="ronhallloc")return &loc;
    return nullptr;
}
bool RonAtTwins(const IntroCutscene& lead,const std::vector<CharacterDraw>& actors){
    const auto* target=RonDestination(lead);if(!target)return false;
    for(const auto& actor:actors)if(actor.actor_reference==1348){
        const auto d=SubtractVector(AddVector(actor.base_origin,actor.cutscene_offset),target->position);
        return std::hypot(d[0],d[2])<.25F&&std::abs(d[1])<1.0F;
    }
    return false;
}
void ResumeRonLead(IntroCutscene& scene,const std::vector<CharacterDraw>& actors){
    for(auto& t:scene.tracks)for(const auto& a:actors)if(a.actor_reference==t.actor_reference){
        t.position=AddVector(a.base_origin,a.cutscene_offset);
        // Resume from the nearest authored route segment, not the old spawn.
        float nearest=1e9F;std::array<float,3> previous{};bool have_previous=false;
        for(std::size_t i=0;i<t.commands.size();++i){
            const auto cmd=AsciiFold(t.commands[i]);if(!cmd.starts_with("moveto "))continue;
            for(const auto& loc:scene.locations)if(AsciiFold(loc.alias)==cmd.substr(7)){
                auto closest=loc.position;
                if(have_previous){auto d=SubtractVector(loc.position,previous);
                    const auto delta=SubtractVector(t.position,previous);const float len=DotVector(d,d);
                    closest=AddVector(previous,ScaleVector(d,len>0?std::clamp(DotVector(delta,d)/len,0.0F,1.0F):0));}
                const auto delta=SubtractVector(t.position,closest);const float distance=DotVector(delta,delta);
                if(distance<=nearest){nearest=distance;t.next_command=i;}
                previous=loc.position;have_previous=true;
            }
        }
    }
    scene.playing=true;
}

std::int32_t BumpProfileActor(std::int32_t actor,unsigned stage,bool final_twins){
    if(actor==1672)return stage>=1?actor:0;
    if(actor==1510)return stage>=14?actor:0;
    if(actor==1329||actor==1326){
        if(stage<6)return 0;
        if(final_twins||stage>=11)return actor==1329?1477:1506;
        if(stage>=7)return actor==1329?1346:1233;
    }
    return actor;
}
std::optional<std::size_t> GameplayDialogueIndex(const FrontAssets& assets,const std::string& name){
    std::size_t index=19;
    for(std::size_t i=0;i<assets.gameplay_audio.size();++i){
        if(i==1)continue;
        if(AsciiFold(assets.gameplay_audio[i].object_name)==AsciiFold(name))return index;
        ++index;
    }
    return std::nullopt;
}
std::optional<std::array<float,3>> CinematicTarget(const IntroCutscene& scene,const std::vector<CharacterDraw>& actors){
    for(const auto& loc:scene.locations)if(AsciiFold(loc.alias)==scene.camera_target){
        for(const auto& actor:actors)if(actor.actor_reference==loc.actor_reference){
            auto p=AddVector(actor.base_origin,actor.cutscene_offset);p[1]+=.9F;return p;
        }
        return loc.position;
    }
    for(const auto& track:scene.tracks)if(AsciiFold(track.alias)==scene.camera_target){
        auto p=track.position;if(!track.camera)p[1]+=.9F;return p;
    }
    return std::nullopt;
}
std::optional<std::array<float,3>> CharmsFirstPersonFocus(const IntroCutscene& scene,
                                                       const std::vector<CharacterDraw>& actors){
    if(scene.object_name=="cutscene1"){
        for(const auto& loc:scene.locations)if(AsciiFold(loc.alias)=="hpgoto")return loc.position;
    }
    if(scene.object_name=="cutscene6"&&!scene.harry_released){
        for(const auto& track:scene.tracks)if(AsciiFold(track.alias)=="hermione")return track.position;
    }
    if(scene.object_name=="cutscene51"||scene.object_name=="cutscene52"||
       scene.object_name=="cutscene53"||scene.object_name=="cutscene54"||
       scene.object_name=="cutscene3"||scene.object_name=="cutscene4")return CinematicTarget(scene,actors);
    return std::nullopt;
}
void FaceCharmsCinematicTarget(const IntroCutscene& scene,std::vector<CharacterDraw>& actors){
    const auto focus=CharmsFirstPersonFocus(scene,actors);
    if(!focus)return;
    for(auto& actor:actors)if(actor.player){
        const auto delta=SubtractVector(*focus,AddVector(actor.base_origin,actor.cutscene_offset));
        if(std::hypot(delta[0],delta[2])>.05F)actor.yaw=actor.desired_yaw=std::atan2(delta[0],delta[2]);
    }
}
void PlaceWaitingTwins(IntroCutscene scene,std::vector<CharacterDraw>& actors,
                       const std::vector<CollisionTriangle>& triangles){
    std::erase_if(scene.tracks,[](const auto& t){return t.actor_reference!=1329&&t.actor_reference!=1326;});
    GroundCutsceneCast(scene,actors,triangles);
    for(auto& actor:actors)if(actor.actor_reference==1329||actor.actor_reference==1326){
        const auto d=SubtractVector(scene.trigger_position,AddVector(actor.base_origin,actor.cutscene_offset));
        actor.yaw=actor.desired_yaw=std::atan2(d[0],d[2]);
    }
}

// A completed MOVETO leaves an actor standing, not running on the spot while
// waiting for another cast's cue or for the player to reach the next trigger.
void SettleStoppedActors(const IntroCutscene& scene,std::vector<CharacterDraw>& actors){
    for(auto& actor:actors){
        if(actor.actor_reference==2968||actor.actor_reference==3148||(actor.active_clip!="run"&&actor.active_clip!="walk"))continue;
        const bool moving=scene.playing&&std::ranges::any_of(scene.tracks,[&](const auto& t){
            return t.actor_reference==actor.actor_reference&&!t.finished&&t.moving;
        });
        if(!moving){actor.active_clip="breathe";actor.animation_time=0;}
    }
}
// CutScene6 swaps two different pairs in the retail map. Our canonical pair
// is staged, never asked to navigate through the intervening walls.
void PrepareTwinsTransfer(IntroCutscene& scene){
    std::erase_if(scene.tracks,[](const auto& t){return t.actor_reference!=1329&&t.actor_reference!=1326;});
    for(auto& t:scene.tracks)t.commands={};
    scene.harry_released=scene.control_released=true;
}
void PreparePeevesDeparture(IntroCutscene& scene){
    const auto mark=[&](std::string_view name)->std::array<float,3>{
        for(const auto& loc:scene.locations)if(AsciiFold(loc.alias)==name)return loc.position;
        return scene.trigger_position;
    };
    scene.camera_active=scene.camera_position_valid=true;
    scene.camera_position=mark("startcam");scene.camera_target="target";
    for(auto& t:scene.tracks){
        if(t.camera){
            t.position=scene.camera_position;t.commands={};
            const std::array<std::string,7> script{"Capture","PreFace Target","Cue CutStart",
                "WaitFor Leave","WaitFor CutEnd","CamRestore","Release"};
            std::copy(script.begin(),script.end(),t.commands.begin());
        }else if(t.actor_reference==kHarryActorReference){
            t.commands={};t.commands[0]="Capture";t.commands[1]="WaitFor CutEnd";t.commands[2]="Release";
        }else if(t.actor_reference==1346||t.actor_reference==1233){
            t.position=mark(t.actor_reference==1346?"fredloc":"georgeloc");
            // Keep the owned speeches, cues and visible exit route.
            std::array<std::string,40> commands{};std::size_t n=0;bool conversation=false;
            for(const auto& line:t.commands){
                if(AsciiFold(line).starts_with("turnto "))conversation=true;
                if(conversation&&!line.empty())commands[n++]=line;
            }
            t.commands=std::move(commands);
        }
    }
}
struct ClimbMotion {
    bool active=false;
    std::array<float,3> start{}, corner{}, target{};
};
struct JumpMotion {
    bool active=false;
    float velocity=0,elapsed=0;
    std::array<float,3> safe_origin{};
};
bool StartJump(const std::vector<CollisionTriangle>& triangles,const std::array<float,3>& p,JumpMotion& jump){
    float floor;
    if(jump.active||!FindCollisionGroundHeight(triangles,p[0],p[2],p[1]-kPlayerCapsuleHalfHeightMeters,&floor)||
        std::abs(p[1]-kPlayerCapsuleHalfHeightMeters-floor)>0.08F)return false;
    jump={true,6.0F,0,p};return true;
}
bool StepJump(const std::vector<CollisionTriangle>& triangles,JumpMotion& jump,const std::array<float,3>& current,
              const std::array<float,3>& request,float seconds,LocomotionMove* output){
    if(!jump.active||!output||!std::isfinite(seconds)||seconds<0)return false;
    *output={};auto p=current;const float dt=std::min(seconds,0.05F);
    const int steps=std::max(1,static_cast<int>(std::ceil(dt/0.008F)));
    for(int i=0;i<steps&&jump.active;++i){
        const float h=dt/steps;const float dy=jump.velocity*h-0.5F*19.0F*h*h;jump.velocity-=19.0F*h;
        auto next=p;next[1]+=dy;
        if(dy<0){
            const float from=p[1]-kPlayerCapsuleHalfHeightMeters,to=next[1]-kPlayerCapsuleHalfHeightMeters;
            float floor=-std::numeric_limits<float>::infinity();
            for(const auto& t:triangles){float y;
                if(std::abs(t.normal[1])>=kWalkableNormalY&&CollisionTriangleHeightAtXZ(t,p[0],p[2],&y)&&
                   y<=from+0.005F&&y>=to-0.005F)floor=std::max(floor,y);}
            if(std::isfinite(floor)){next[1]=floor+kPlayerCapsuleHalfHeightMeters;jump.active=false;jump.velocity=0;++output->grounded_substeps;}
        }
        if(OverlapsCollisionWall(triangles,next)){next=p;jump.velocity=std::min(0.0F,jump.velocity);++output->blocked_substeps;}
        p=next;
        for(unsigned axis:{0U,2U}){next=p;next[axis]+=request[axis]*1.20F/steps;
            if(!OverlapsCollisionWall(triangles,next))p=next;
            else{
                // Capsule contact with a landing lip can precede center-point
                // floor contact. Resolve a small step while descending.
                float floor;const float foot=next[1]-kPlayerCapsuleHalfHeightMeters;
                if(jump.velocity<=0&&FindCollisionGroundHeight(triangles,next[0],next[2],foot,&floor)&&
                   floor>=foot&&floor-foot<=0.16F){
                    next[1]=floor+kPlayerCapsuleHalfHeightMeters;
                    if(!OverlapsCollisionWall(triangles,next)){
                        p=next;jump.active=false;jump.velocity=0;++output->grounded_substeps;continue;
                    }
                }
                ++output->blocked_substeps;
            }}
    }
    jump.elapsed+=dt;output->displacement=SubtractVector(p,current);return true;
}
std::array<float,3> SafeCheckpointHead(const std::array<float,3>& head,const ClimbMotion& climb,const JumpMotion& jump){
    if(!climb.active&&!jump.active)return head;
    auto safe=climb.active?climb.start:jump.safe_origin;safe[1]+=kPlayerEyeHeightMeters;return safe;
}
bool ClearCapsuleSegment(const std::vector<CollisionTriangle>& triangles,
                         const std::array<float,3>& a,const std::array<float,3>& b){
    const auto d=SubtractVector(b,a);const float length=std::sqrt(DotVector(d,d));
    const auto steps=std::max(1,static_cast<int>(std::ceil(length/0.04F)));
    for(int i=0;i<=steps;++i)
        if(OverlapsCollisionWall(triangles,AddVector(a,ScaleVector(d,float(i)/steps))))return false;
    return true;
}
bool BeginClimb(const std::vector<CollisionTriangle>& triangles,
                const std::array<float,3>& current,const std::array<float,3>& request,ClimbMotion* climb){
    const float length=std::hypot(request[0],request[2]);if(!climb||length<0.00001F)return false;
    const std::array<float,3> dir{request[0]/length,0,request[2]/length};
    const float foot=current[1]-kPlayerCapsuleHalfHeightMeters;
    // Reach a nearby ledge, never the top of a distant wall. Both legs of the
    // mantle are swept with the same capsule used by ordinary locomotion.
    for(float distance=0.45F;distance<=1.05F;distance+=0.10F){
        auto landing=AddVector(current,ScaleVector(dir,distance));
        float top=std::numeric_limits<float>::infinity();
        for(const auto& t:triangles){float y;
            if(std::abs(t.normal[1])>=kWalkableNormalY &&
               CollisionTriangleHeightAtXZ(t,landing[0],landing[2],&y) &&
               y>foot+0.34F && y<=foot+2.8F)top=std::min(top,y);
        }
        if(!std::isfinite(top))continue;
        landing[1]=top+kPlayerCapsuleHalfHeightMeters;
        auto corner=current;corner[1]=landing[1];
        if(!ClearCapsuleSegment(triangles,current,corner)||!ClearCapsuleSegment(triangles,corner,landing))continue;
        *climb={true,current,corner,landing};return true;
    }
    return false;
}
bool StepClimb(const std::vector<CollisionTriangle>& triangles,ClimbMotion& climb,
               const std::array<float,3>& current,float distance,LocomotionMove* output){
    if(!climb.active||!output)return false;
    auto p=current;float budget=std::clamp(distance,0.0F,0.08F);
    for(const auto& goal:{climb.corner,climb.target}){
        if(goal==climb.corner && p[1]>=goal[1]-0.0001F)continue;
        const auto delta=SubtractVector(goal,p);const float len=std::sqrt(DotVector(delta,delta));
        if(len<0.0001F)continue;
        const float used=std::min(len,budget);auto next=AddVector(p,ScaleVector(delta,used/len));
        if(!ClearCapsuleSegment(triangles,p,next)){climb.active=false;return false;}
        p=next;budget-=used;if(budget<=0)break;
    }
    *output={};output->displacement=SubtractVector(p,current);
    const auto remaining=SubtractVector(p,climb.target);
    if(DotVector(remaining,remaining)<0.000001F)climb.active=false;
    return true;
}

float CharacterCollisionPenalty(
    const std::vector<CharacterDraw>& characters,
    const QuestSpellTargets& spell_targets,
    const std::array<float, 3>& center) {
    float penalty = 0.0F;
    for (std::size_t index = 0; index < characters.size(); ++index) {
        const auto& character = characters[index];
        if (character.child_template || !character.enabled || character.collision_disabled) continue;
        if (character.player||character.flying||character.actor_reference==kHarryActorReference||character.actor_reference==2968||character.actor_reference==3148) continue;
        const auto reaction = spell_targets.ReactionOffset(index);
        const float minimum_y = character.collision_min_y + reaction[1] +
                                character.cutscene_offset[1];
        const float maximum_y = character.collision_max_y + reaction[1] +
                                character.cutscene_offset[1];
        if (center[1] + kPlayerCapsuleHalfHeightMeters < minimum_y ||
            center[1] - kPlayerCapsuleHalfHeightMeters > maximum_y) {
            continue;
        }
        const float dx = center[0] -
                         (character.collision_center[0] + reaction[0] +
                          character.cutscene_offset[0]);
        const float dz = center[2] -
                         (character.collision_center[2] + reaction[2] +
                          character.cutscene_offset[2]);
        const float distance = std::sqrt(dx * dx + dz * dz);
        const float required =
            kPlayerCapsuleRadiusMeters + character.collision_radius;
        penalty += std::max(0.0F, required - distance);
    }
    return penalty;
}

void ResolveCharacterMovement(
    const std::vector<CharacterDraw>& characters,
    const QuestSpellTargets& spell_targets,
    const std::array<float, 3>& capsule_center,
    LocomotionMove* const move) {
    if (move == nullptr || characters.empty()) return;
    constexpr float kPenaltyEpsilon = 0.002F;
    const float current_penalty = CharacterCollisionPenalty(
        characters, spell_targets, capsule_center);
    const auto requested_position =
        AddVector(capsule_center, move->displacement);
    const float requested_penalty = CharacterCollisionPenalty(
        characters, spell_targets, requested_position);
    if (requested_penalty <= current_penalty + kPenaltyEpsilon) return;

    std::array<LocomotionMove, 2> slides{*move, *move};
    slides[0].displacement[2] = 0.0F;
    slides[1].displacement[0] = 0.0F;
    const std::array<float, 2> penalties{
        CharacterCollisionPenalty(
            characters, spell_targets,
            AddVector(capsule_center, slides[0].displacement)),
        CharacterCollisionPenalty(
            characters, spell_targets,
            AddVector(capsule_center, slides[1].displacement))};
    std::size_t best = penalties[0] <= penalties[1] ? 0U : 1U;
    if (penalties[best] <= current_penalty + kPenaltyEpsilon) {
        *move = slides[best];
    } else {
        move->displacement[0] = 0.0F;
        move->displacement[2] = 0.0F;
    }
    ++move->blocked_substeps;
}

template <std::size_t Capacity>
bool FixedString(const char (&source)[Capacity], std::string* const output) {
    if (output == nullptr) return false;
    const auto terminator = std::find(source, source + Capacity, '\0');
    if (terminator == source + Capacity) return false;
    output->assign(source, terminator);
    return true;
}

bool LoadCharacterMesh(const std::string& package,
                       const std::int32_t mesh_reference,
                       const std::uint32_t layer_base,
                       LoadedCharacterMesh* const output) {
    if (output == nullptr || package.empty() || mesh_reference <= 0) {
        return false;
    }
    hpvr_hp1_skeletal_mesh_report query{};
    const std::uint32_t query_status = hpvr_hp1_load_skeletal_mesh_utf8(
        package.c_str(), mesh_reference, kMetersPerUnrealUnit, nullptr, 0,
        nullptr, 0, &query);
    if (query_status != HPVR_HP1_PROFILE_BUFFER_TOO_SMALL ||
        query.status != query_status ||
        query.abi_version != HPVR_HP1_SKELETAL_MESH_ABI_VERSION ||
        query.required_vertex_count == 0 ||
        query.required_vertex_count % 3 != 0 ||
        query.required_texture_bytes == 0 || query.point_count == 0 ||
        query.texture_layer_width != HPVR_HP1_SKELETAL_TEXTURE_SIZE ||
        query.texture_layer_height != HPVR_HP1_SKELETAL_TEXTURE_SIZE ||
        query.texture_layer_count == 0) {
        HPVR_LOGE(
            "[hpvr.quest.characters.mesh] status=QUERY_REJECTED package=%s "
            "mesh_ref=%d result=%u report=%u vertices=%u layers=%u error=%s",
            package.c_str(), mesh_reference, query_status, query.status,
            query.required_vertex_count, query.texture_layer_count,
            query.error);
        return false;
    }
    output->vertices.assign(query.required_vertex_count, {});
    output->textures.assign(query.required_texture_bytes, 0);
    hpvr_hp1_skeletal_mesh_report loaded{};
    const std::uint32_t load_status = hpvr_hp1_load_skeletal_mesh_utf8(
        package.c_str(), mesh_reference, kMetersPerUnrealUnit,
        output->vertices.data(),
        static_cast<std::uint32_t>(output->vertices.size()),
        output->textures.data(),
        static_cast<std::uint32_t>(output->textures.size()), &loaded);
    if (load_status != HPVR_HP1_PROFILE_OK || loaded.status != load_status ||
        loaded.abi_version != HPVR_HP1_SKELETAL_MESH_ABI_VERSION ||
        loaded.written_vertex_count != output->vertices.size() ||
        loaded.written_texture_bytes != output->textures.size() ||
        loaded.point_count != query.point_count ||
        loaded.texture_layer_count != query.texture_layer_count) {
        HPVR_LOGE(
            "[hpvr.quest.characters.mesh] status=LOAD_REJECTED package=%s "
            "mesh_ref=%d result=%u report=%u error=%s",
            package.c_str(), mesh_reference, load_status, loaded.status,
            loaded.error);
        return false;
    }

    hpvr_hp1_skeletal_animation_report animation_query{};
    const std::uint32_t animation_query_status =
        hpvr_hp1_load_skeletal_animation_utf8(
            package.c_str(), mesh_reference, kMetersPerUnrealUnit,
            kCharacterAnimationFrameCount, nullptr, 0, &animation_query);
    if (animation_query_status != HPVR_HP1_PROFILE_BUFFER_TOO_SMALL ||
        animation_query.status != animation_query_status ||
        animation_query.abi_version !=
            HPVR_HP1_SKELETAL_ANIMATION_ABI_VERSION ||
        animation_query.frame_count != kCharacterAnimationFrameCount ||
        animation_query.point_count != loaded.point_count ||
        animation_query.required_position_count !=
            loaded.point_count * kCharacterAnimationFrameCount) {
        HPVR_LOGE(
            "[hpvr.quest.characters.animation] status=QUERY_REJECTED "
            "package=%s mesh_ref=%d result=%u report=%u points=%u error=%s",
            package.c_str(), mesh_reference, animation_query_status,
            animation_query.status, animation_query.point_count,
            animation_query.error);
        return false;
    }
    output->frames.assign(animation_query.required_position_count, {});
    hpvr_hp1_skeletal_animation_report animation_loaded{};
    const std::uint32_t animation_status =
        hpvr_hp1_load_skeletal_animation_utf8(
            package.c_str(), mesh_reference, kMetersPerUnrealUnit,
            kCharacterAnimationFrameCount, output->frames.data(),
            static_cast<std::uint32_t>(output->frames.size()),
            &animation_loaded);
    if (animation_status != HPVR_HP1_PROFILE_OK ||
        animation_loaded.status != animation_status ||
        animation_loaded.written_position_count != output->frames.size() ||
        animation_loaded.point_count != loaded.point_count ||
        animation_loaded.frame_count != kCharacterAnimationFrameCount ||
        !std::isfinite(animation_loaded.duration_seconds) ||
        animation_loaded.duration_seconds <= 0.0F) {
        HPVR_LOGE(
            "[hpvr.quest.characters.animation] status=LOAD_REJECTED "
            "package=%s mesh_ref=%d result=%u report=%u error=%s",
            package.c_str(), mesh_reference, animation_status,
            animation_loaded.status, animation_loaded.error);
        return false;
    }
    output->report = loaded;
    output->animation_report = animation_loaded;
    output->layer_base = layer_base;
    HPVR_LOGI(
        "[hpvr.quest.characters.mesh] status=READY package=%s mesh_ref=%d "
        "vertices=%zu layers=%u layer_base=%u idle=%s frames=%u "
        "source_duration_s=%.3f",
        package.c_str(), mesh_reference, output->vertices.size(),
        loaded.texture_layer_count, layer_base,
        animation_loaded.sequence_name, animation_loaded.frame_count,
        animation_loaded.duration_seconds);
    return true;
}

bool LoadStaticMesh(const std::string& package,
                    const std::int32_t mesh_reference,
                    const std::uint32_t layer_base,
                    LoadedStaticMesh* const output) {
    if (output == nullptr || package.empty() || mesh_reference <= 0) {
        return false;
    }
    hpvr_hp1_skeletal_mesh_report query{};
    const std::uint32_t query_status = hpvr_hp1_load_skeletal_mesh_utf8(
        package.c_str(), mesh_reference, kMetersPerUnrealUnit, nullptr, 0,
        nullptr, 0, &query);
    if (query_status != HPVR_HP1_PROFILE_BUFFER_TOO_SMALL ||
        query.status != query_status || query.required_vertex_count == 0 ||
        query.required_texture_bytes == 0 ||
        query.texture_layer_width != 256 || query.texture_layer_height != 256) {
        HPVR_LOGE("[hpvr.quest.fixtures.mesh] status=QUERY_REJECTED "
                  "mesh_ref=%d result=%u error=%s",
                  mesh_reference, query_status, query.error);
        return false;
    }
    output->vertices.resize(query.required_vertex_count);
    output->textures.resize(query.required_texture_bytes);
    hpvr_hp1_skeletal_mesh_report loaded{};
    const std::uint32_t status = hpvr_hp1_load_skeletal_mesh_utf8(
        package.c_str(), mesh_reference, kMetersPerUnrealUnit,
        output->vertices.data(),
        static_cast<std::uint32_t>(output->vertices.size()),
        output->textures.data(),
        static_cast<std::uint32_t>(output->textures.size()), &loaded);
    if (status != HPVR_HP1_PROFILE_OK || loaded.status != status ||
        loaded.written_vertex_count != output->vertices.size() ||
        loaded.written_texture_bytes != output->textures.size()) {
        HPVR_LOGE("[hpvr.quest.fixtures.mesh] status=LOAD_REJECTED "
                  "mesh_ref=%d result=%u error=%s",
                  mesh_reference, status, loaded.error);
        return false;
    }
    output->report = loaded;
    output->layer_base = layer_base;
    return true;
}

bool PoseStaticMesh(const std::filesystem::path& package,std::int32_t reference,
                    std::string_view clip,LoadedStaticMesh& mesh){
    const auto skin=wand::load_hp1_skeletal_skin(package,reference);
    if(skin.status!=wand::Hp1ProfileStatus::ok)return false;
    const auto animation=wand::load_hp1_animation(package,skin.census.animation_reference);
    if(animation.status!=wand::Hp1ProfileStatus::ok)return false;
    const auto sequence=std::ranges::find_if(animation.sequences,[&](const auto& s){return AsciiFold(s.name)==clip;});
    if(sequence==animation.sequences.end())return false;
    const auto pose=wand::sample_hp1_skeletal_animation(skin,animation,
        static_cast<std::size_t>(sequence-animation.sequences.begin()),0);
    if(pose.status!=wand::Hp1ProfileStatus::ok)return false;
    for(unsigned axis=0;axis<3;++axis){mesh.report.bounds_min_m[axis]=1e9F;mesh.report.bounds_max_m[axis]=-1e9F;}
    for(auto& vertex:mesh.vertices){
        if(vertex.point_index>=pose.points.size())return false;
        const auto& point=pose.points[vertex.point_index];
        const std::array<float,3> p{point.y*kMetersPerUnrealUnit,point.z*kMetersPerUnrealUnit,point.x*kMetersPerUnrealUnit};
        for(unsigned axis=0;axis<3;++axis){
            vertex.position_m[axis]=p[axis];
            mesh.report.bounds_min_m[axis]=std::min(mesh.report.bounds_min_m[axis],p[axis]);
            mesh.report.bounds_max_m[axis]=std::max(mesh.report.bounds_max_m[axis],p[axis]);
        }
    }
    return true;
}

struct BeanDraw {
    std::int32_t actor_reference=0;
    std::uint32_t first=0,count=0;
    std::array<float,3> position{};
    unsigned kind=0; // 0 bean, 1 chocolate frog, 2 first wizard card.
    unsigned frames=1;
    float duration=1,yaw=0;
    std::int32_t source_actor=0; // Persisted pot activation controls spawned rewards.
    std::array<float,3> emission{};
    float emission_time=1;
    std::array<std::array<float,3>,17> emission_path{};
    unsigned emission_points=0; // Runtime sweep path, never part of cooked geometry.
    std::array<float,3> attachment_offset{};
};

std::array<float,3> BeanWorldPosition(const BeanDraw& bean){
    if(!bean.source_actor)return AddVector(bean.position,bean.attachment_offset);
    const float t=std::clamp(bean.emission_time,0.0F,1.0F);
    if(bean.emission_points>1&&bean.emission_points<=bean.emission_path.size()){
        const float sample=t*float(bean.emission_points-1);
        const auto index=std::min(bean.emission_points-2,static_cast<unsigned>(sample));
        return AddVector(bean.emission_path[index],ScaleVector(
            SubtractVector(bean.emission_path[index+1],bean.emission_path[index]),sample-float(index)));
    }
    auto p=AddVector(bean.emission,ScaleVector(SubtractVector(bean.position,bean.emission),t));
    p[1]+=.6F*4*t*(1-t);
    return p;
}

struct CardPickupEffect {
    std::int32_t actor=0;
    float elapsed=0,duration=1;
    float initial_angle=0;
    bool active()const{return actor!=0&&elapsed<duration;}
    void Advance(float dt){if(active())elapsed=std::min(duration,elapsed+std::max(0.0F,dt));}
    float scale()const{
        const float t=std::clamp(elapsed/duration,0.0F,1.0F);
        // Grow visibly, then collapse to zero instead of popping out of view.
        return t<.7F?1.0F+2.0F*(t/.7F):3.0F*(1.0F-t)/.3F;
    }
    float angle()const{return initial_angle+elapsed*kTau*2.0F;}
};
struct PickupFlight {
    std::int32_t actor=0;
    std::uint32_t first=0,count=0;
    std::array<float,3> origin{};
    unsigned kind=0;
    float elapsed=0,duration=.25F,angle=0;
};
bool LoadOwnedBeans(const std::filesystem::path& root,const std::filesystem::path& map,
                    const hpvr_hp1_player_start_report& start,float yaw,
                    std::vector<GpuVertex>& vertices,std::vector<std::uint8_t>& pixels,
                    std::uint32_t& layers,std::vector<BeanDraw>& beans,
                    const std::vector<CollisionTriangle>* ground=nullptr){
    const auto census=wand::inspect_hp1_actor_visuals(map);
    const auto package=root/"System/HProps.u";
    const auto table=wand::inspect_hp1_package_link_table(package);
    if(census.status!=wand::Hp1ProfileStatus::ok||table.status!=wand::Hp1ProfileStatus::ok)return false;
    std::map<std::string,LoadedStaticMesh> meshes;
    auto pickup_actors=census.actors;
    struct RewardSource { std::int32_t actor; std::array<float,3> emission,landing; };
    std::map<std::int32_t,RewardSource> rewards;
    const auto map_name=AsciiFold(map.stem().string());
    if(map_name=="lev_tut1b"||map_name=="lev_tut3")for(const auto& pot:census.actors){
        const auto cls=AsciiFold(pot.qualified_class_name);
        const bool cauldron=cls=="hprops.bronzecauldron";
        const bool chest=map_name=="lev_tut3"&&chest::IsChest(cls);
        if(!cauldron&&!chest&&!cls.starts_with("hprops.flipendovase"))continue;
        const auto def=std::ranges::find_if(table.exports,[&](const auto& e){
            return e.qualified_class_name=="Core.Class"&&!e.object_path.empty()&&AsciiFold(e.object_path.back())==cls.substr(7);});
        if(def==table.exports.end())return false;
        const auto defaults=wand::inspect_hp1_class_visual_defaults(package,def->reference);
        if(defaults.status!=wand::Hp1ProfileStatus::ok)return false;
        std::vector<wand::Hp1ClassDefaultProperty> properties;
        if(chest&&cls=="hprops.ironchest"){
            const auto base=std::ranges::find_if(table.exports,[](const auto& e){
                return e.qualified_class_name=="Core.Class"&&!e.object_path.empty()&&AsciiFold(e.object_path.back())=="bronzechest";
            });
            if(base==table.exports.end())return false;
            const auto inherited=wand::inspect_hp1_class_visual_defaults(package,base->reference);
            if(inherited.status!=wand::Hp1ProfileStatus::ok)return false;
            properties=inherited.serialized_properties;
        }
        properties.insert(properties.end(),defaults.serialized_properties.begin(),defaults.serialized_properties.end());
        properties.insert(properties.end(),pot.serialized_properties.begin(),pot.serialized_properties.end());
        std::map<unsigned,std::string> colors;
        unsigned count=1;
        bool random_beans=false;
        for(const auto& p:properties){
            const auto key=AsciiFold(p.name);
            if((cauldron||chest)&&key=="inumberofbeans"&&p.value.size()==4){
                std::uint32_t n=0;std::memcpy(&n,p.value.data(),4);count=std::min(n,16U);
            }
            if(chest&&key=="brandombeans"&&p.boolean_value_serialized)random_beans=p.boolean_value;
            if((((cauldron||chest)&&key=="ejectedobjects")||(!cauldron&&!chest&&key=="transforminto"))&&!p.object_path.empty()){
                const auto name=AsciiFold(p.object_path.back());
                if(name.ends_with("bean")||(chest&&chest::RewardCardId("hprops."+name)))
                    colors[cauldron||chest?unsigned(std::max<std::int64_t>(0,p.array_index)):0]="hprops."+name;
            }
        }
        if(chest&&random_beans){
            constexpr std::array<const char*,5> types{"hprops.bluejellybean","hprops.greenjellybean","hprops.spottedjellybean",
                "hprops.greenpurplecheckerbean","hprops.redblackstripebean"};
            std::uint32_t seed=static_cast<std::uint32_t>(pot.actor_reference);
            for(unsigned index=0;index<count;++index){seed=seed*1664525U+1013904223U;colors[index]=types[seed%types.size()];}
        }
        for(const auto& [index,color]:colors){
            if(index>=count)continue;
            auto actor=pot;actor.actor_reference=0x20000000+pot.actor_reference*16+static_cast<std::int32_t>(index);
            actor.qualified_class_name=color;actor.draw_scale=1;actor.hidden=false;actor.location_serialized=true;
            pickup_actors.push_back(actor);
            const float launch_yaw=yaw+(pot.rotation_units[1]+(cauldron?5000:0))*kTau/65536.0F;
            auto emission=ActorLocalPosition(pot,start,yaw);
            const auto forward=RotateYaw({0,0,-1},launch_yaw);
            if(!cauldron&&!chest)emission=AddVector(emission,ScaleVector(forward,20*kMetersPerUnrealUnit));
            std::array<float,3> velocity=ScaleVector(forward,80*kMetersPerUnrealUnit);
            for(const auto& property:properties)if((cauldron||chest)&&unsigned(std::max<std::int64_t>(0,property.array_index))==index&&property.value.size()==12){
                const auto key=AsciiFold(property.name);
                if(key!="objectstartpoint"&&key!="objectstartvelocity")continue;
                std::array<float,3> value{};std::memcpy(value.data(),property.value.data(),12);
                const auto local=RotateYaw({value[1]*kMetersPerUnrealUnit,value[2]*kMetersPerUnrealUnit,-value[0]*kMetersPerUnrealUnit},
                    launch_yaw);
                if(key=="objectstartpoint")emission=AddVector(ActorLocalPosition(pot,start,yaw),local);
                else velocity=local;
            }
            auto landing=AddVector(emission,ScaleVector(velocity,.7F));
            if(ground){float floor=0;if(FindPropGroundBelow(*ground,landing,&floor))landing[1]=floor+.18F;}
            rewards.emplace(actor.actor_reference,RewardSource{pot.actor_reference,emission,landing});
        }
    }
    if(map_name=="lev_tut3")for(const auto& source:census.actors){
        if(AsciiFold(source.qualified_class_name)!="hpbase.spawnthingy")continue;
        std::string spawn_class;
        for(const auto& property:source.serialized_properties)
            if(AsciiFold(property.name)=="spawnclass"&&property.object_reference_serialized)
                for(const auto& part:property.object_path){if(!spawn_class.empty())spawn_class+='.';spawn_class+=part;}
        const auto name=AsciiFold(spawn_class);
        if(!name.starts_with("hprops.")||!name.ends_with("bean"))continue;
        auto actor=source;
        actor.actor_reference=0x30000000+source.actor_reference;
        actor.qualified_class_name=spawn_class;actor.hidden=false;actor.draw_scale=1;
        pickup_actors.push_back(std::move(actor));
        const auto emission=ActorLocalPosition(source,start,yaw);
        const auto forward=RotateYaw({0,0,-1},source.rotation_units[1]*kTau/65536.0F+yaw);
        auto landing=AddVector(emission,ScaleVector(forward,.65F));
        if(ground){float floor=0;if(FindPropGroundBelow(*ground,landing,&floor))landing[1]=floor+.18F;}
        rewards.emplace(0x30000000+source.actor_reference,RewardSource{source.actor_reference,emission,landing});
    }
    for(const auto& actor:pickup_actors){
        const auto name=AsciiFold(actor.qualified_class_name);
        if(!name.starts_with("hprops.")||!name.ends_with("bean")||actor.hidden||!actor.location_serialized)continue;
        if(!meshes.contains(name)){
            const auto def=std::ranges::find_if(table.exports,[&](const auto& e){
                return e.qualified_class_name=="Core.Class"&&!e.object_path.empty()&&
                    AsciiFold(e.object_path.back())==name.substr(7);});
            if(def==table.exports.end())return false;
            const auto defaults=wand::inspect_hp1_class_visual_defaults(package,def->reference);
            LoadedStaticMesh mesh;
            if(defaults.status!=wand::Hp1ProfileStatus::ok||!LoadStaticMesh(package.string(),defaults.mesh_reference,layers,&mesh)||
                layers+mesh.report.texture_layer_count>kMaximumCombinedTextureLayers)return false;
            layers+=mesh.report.texture_layer_count;pixels.insert(pixels.end(),mesh.textures.begin(),mesh.textures.end());
            meshes.emplace(name,std::move(mesh));
        }
        const auto& mesh=meshes.at(name);
        BeanDraw bean{actor.actor_reference,static_cast<std::uint32_t>(vertices.size()),
            static_cast<std::uint32_t>(mesh.vertices.size()),ActorLocalPosition(actor,start,yaw)};
        for(const auto& v:mesh.vertices)vertices.push_back({
            {v.position_m[0]*actor.draw_scale,v.position_m[1]*actor.draw_scale,v.position_m[2]*actor.draw_scale},
            {v.texture_uv[0],v.texture_uv[1]},{0,0},mesh.layer_base+v.texture_layer,v.polygon_flags,0,0xc8c8c8U});
        if(const auto reward=rewards.find(actor.actor_reference);reward!=rewards.end()){
            bean.source_actor=reward->second.actor;bean.emission=reward->second.emission;bean.position=reward->second.landing;
        }
        beans.push_back(bean);
    }
    HPVR_LOGI("[hpvr.quest.beans] status=READY actors=%zu colors=%zu",beans.size(),meshes.size());
    for(const auto& actor:census.actors)if((map_name=="lev_tut1"&&(actor.actor_reference==1778||actor.actor_reference==1617))||
        (map_name=="lev_tut3"&&AsciiFold(actor.qualified_class_name)=="harrypotter.chocolatefrog"&&!actor.hidden)){
        const bool frog=AsciiFold(actor.qualified_class_name)=="harrypotter.chocolatefrog";
        const auto pkg=root/(frog?"System/HarryPotter.u":"System/HProps.u");
        if(frog){
            LoadedCharacterMesh mesh;if(!LoadCharacterMesh(pkg.string(),1677,layers,&mesh))return false;
            const auto skin=wand::load_hp1_skeletal_skin(pkg,1677);
            const auto animation=wand::load_hp1_animation(pkg,skin.census.animation_reference);
            if(skin.status!=wand::Hp1ProfileStatus::ok||animation.status!=wand::Hp1ProfileStatus::ok)return false;
            const auto sequence=[&](std::string_view name){
                for(std::size_t i=0;i<animation.sequences.size();++i)if(AsciiFold(animation.sequences[i].name)==name)return i;
                return animation.sequences.size();
            };
            const auto croak=sequence("croak"),hop=sequence("hop"),breath=sequence("breath");
            if(std::max({croak,hop,breath})>=animation.sequences.size())return false;
            const auto rest=wand::sample_hp1_skeletal_animation(skin,animation,croak,0);
            if(rest.status!=wand::Hp1ProfileStatus::ok)return false;
            const float scale=kMetersPerUnrealUnit*.5F;
            float foot=std::numeric_limits<float>::infinity();
            for(const auto& v:mesh.vertices)foot=std::min(foot,rest.points.at(v.point_index).z*scale);
            BeanDraw pickup{actor.actor_reference,static_cast<std::uint32_t>(vertices.size()),
                static_cast<std::uint32_t>(mesh.vertices.size()),ActorLocalPosition(actor,start,yaw),1};
            if(ground){float floor;if(!FindPropGroundBelow(*ground,pickup.position,&floor))return false;pickup.position[1]=floor;}
            pickup.yaw=actor.rotation_units[1]*kTau/65536.0F+yaw;
            const std::array<std::size_t,4> clips{croak,hop,breath,hop};
            const std::array<float,4> duration{animation.moves[croak].track_time,animation.moves[hop].track_time,1.67F,animation.moves[hop].track_time};
            pickup.duration=duration[0]+duration[1]+duration[2]+duration[3];
            pickup.frames=static_cast<unsigned>(std::ceil(pickup.duration*30));
            for(unsigned frame=0;frame<pickup.frames;++frame){
                float time=pickup.duration*frame/pickup.frames;unsigned part=0;
                while(part<3&&time>=duration[part])time-=duration[part++];
                const auto pose=wand::sample_hp1_skeletal_animation(skin,animation,clips[part],
                    std::fmod(time*(part==2?.6F:1.0F),animation.moves[clips[part]].track_time));
                if(pose.status!=wand::Hp1ProfileStatus::ok)return false;
                for(const auto& v:mesh.vertices){
                    const auto& p=pose.points.at(v.point_index);
                    vertices.push_back({{p.y*scale,p.z*scale-foot,p.x*scale},{v.texture_uv[0],v.texture_uv[1]},
                        {0,0},layers+v.texture_layer,v.polygon_flags,0,0xe8e8e8U});
                }
            }
            layers+=mesh.report.texture_layer_count;if(layers>kMaximumCombinedTextureLayers)return false;
            pixels.insert(pixels.end(),mesh.textures.begin(),mesh.textures.end());
            beans.push_back(pickup);continue;
        }
        LoadedStaticMesh mesh;if(!LoadStaticMesh(pkg.string(),frog?1677:1709,layers,&mesh))return false;
        if(layers+mesh.report.texture_layer_count>kMaximumCombinedTextureLayers)return false;
        if(!frog){
            const auto texture=wand::load_hp1_p8_texture(pkg,1766);
            if(texture.status!=wand::Hp1ProfileStatus::ok||texture.mips.empty())return false;
            for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){
                const auto src=(std::size_t(y*texture.mips[0].height/256)*texture.mips[0].width+x*texture.mips[0].width/256)*4;
                std::copy_n(texture.rgba8.begin()+src,4,mesh.textures.begin()+(y*256+x)*4);
            }
        }
        layers+=mesh.report.texture_layer_count;pixels.insert(pixels.end(),mesh.textures.begin(),mesh.textures.end());
        BeanDraw pickup{actor.actor_reference,static_cast<std::uint32_t>(vertices.size()),static_cast<std::uint32_t>(mesh.vertices.size()),
            ActorLocalPosition(actor,start,yaw),frog?1U:2U};
        for(const auto& v:mesh.vertices)vertices.push_back({
            {v.position_m[0]*.5F,v.position_m[1]*.5F,v.position_m[2]*.5F},
            {v.texture_uv[0],v.texture_uv[1]},{0,0},mesh.layer_base+v.texture_layer,v.polygon_flags,0,0xe8e8e8U});
        beans.push_back(pickup);
    }
    if(AsciiFold(map.stem().string())=="lev_tut2"||AsciiFold(map.stem().string())=="lev_tut3"){
        const auto manifest=wand::build_hp1_character_manifest(root,map,0,{},true);
        if(manifest.status!=wand::Hp1ProfileStatus::ok)return false;
        auto cards=manifest.actors;
        if(map_name=="lev_tut3")for(const auto& reward:pickup_actors)if(chest::RewardCardId(AsciiFold(reward.qualified_class_name))){
            const auto icon=std::ranges::find_if(manifest.actors,[](const auto& actor){return AsciiFold(actor.qualified_class_name)=="hprops.wctoke";});
            const auto def=std::ranges::find_if(table.exports,[&](const auto& e){return e.qualified_class_name=="Core.Class"&&
                !e.object_path.empty()&&AsciiFold(e.object_path.back())==AsciiFold(reward.qualified_class_name).substr(7);});
            if(icon==manifest.actors.end()||def==table.exports.end())return false;
            const auto defaults=wand::inspect_hp1_class_visual_defaults(package,def->reference);
            if(defaults.status!=wand::Hp1ProfileStatus::ok)return false;
            auto card=*icon;card.actor_reference=reward.actor_reference;card.qualified_class_name=reward.qualified_class_name;
            card.location_unreal=reward.location_unreal;card.skins.clear();
            for(const auto& property:defaults.serialized_properties)if(AsciiFold(property.name)=="skin"&&property.object_reference>0)
                card.skins.push_back({0,package,property.object_reference});
            if(card.skins.empty())return false;
            cards.push_back(std::move(card));
        }
        for(auto actor:cards){
            const auto cls=AsciiFold(actor.qualified_class_name);
            if(cls!="hprops.wcmerlin"&&cls!="hprops.wctoke"&&!chest::RewardCardId(cls))continue;
            const auto def=std::ranges::find_if(table.exports,[&](const auto& e){return e.qualified_class_name=="Core.Class"&&
                !e.object_path.empty()&&AsciiFold(e.object_path.back())==cls.substr(7);});
            if(def==table.exports.end())return false;
            const auto defaults=wand::inspect_hp1_class_visual_defaults(package,def->reference);
            if(defaults.status!=wand::Hp1ProfileStatus::ok)return false;
            for(const auto& property:defaults.serialized_properties)if(AsciiFold(property.name)=="skin"&&property.object_reference>0){
                std::erase_if(actor.skins,[](const auto& skin){return skin.material_slot==0;});
                actor.skins.push_back({0,package,property.object_reference});
            }
            LoadedStaticMesh mesh;
            if(!LoadStaticMesh(actor.mesh_package.string(),actor.mesh_reference,layers,&mesh)||
               layers+mesh.report.texture_layer_count>kMaximumCombinedTextureLayers)return false;
            for(const auto& skin:actor.skins){
                if(skin.material_slot>=mesh.report.texture_layer_count)return false;
                const bool masked=std::ranges::any_of(mesh.vertices,[&](const auto& v){
                    return v.texture_layer==skin.material_slot&&(v.polygon_flags&2U)!=0;
                });
                const auto texture=wand::load_hp1_p8_texture(skin.package,skin.reference,masked);
                if(texture.status!=wand::Hp1ProfileStatus::ok||texture.mips.empty())return false;
                const auto& mip=texture.mips.front();
                constexpr std::size_t size=HPVR_HP1_SKELETAL_TEXTURE_SIZE;
                for(std::size_t y=0;y<size;++y)for(std::size_t x=0;x<size;++x){
                    const auto source=((y*mip.height/size)*mip.width+x*mip.width/size)*4;
                    const auto target=(skin.material_slot*size*size+y*size+x)*4;
                    std::copy_n(texture.rgba8.begin()+source,4,mesh.textures.begin()+target);
                }
            }
            const auto position=RotateYaw({actor.location_unreal.y*kMetersPerUnrealUnit-start.position_m[0],
                actor.location_unreal.z*kMetersPerUnrealUnit-start.position_m[1],
                -actor.location_unreal.x*kMetersPerUnrealUnit-start.position_m[2]},yaw);
            BeanDraw pickup{actor.actor_reference,static_cast<std::uint32_t>(vertices.size()),
                static_cast<std::uint32_t>(mesh.vertices.size()),position,4};
            if(const auto reward=rewards.find(actor.actor_reference);reward!=rewards.end()){
                pickup.source_actor=reward->second.actor;pickup.emission=reward->second.emission;pickup.position=reward->second.landing;
            }
            for(const auto& v:mesh.vertices)vertices.push_back({
                {v.position_m[0]*actor.draw_scale,v.position_m[1]*actor.draw_scale,v.position_m[2]*actor.draw_scale},
                {v.texture_uv[0],v.texture_uv[1]},{0,0},mesh.layer_base+v.texture_layer,v.polygon_flags,0,0xe8e8e8U});
            pixels.insert(pixels.end(),mesh.textures.begin(),mesh.textures.end());
            layers+=mesh.report.texture_layer_count;beans.push_back(pickup);
        }
    }
    return beans.size()<=1024;
}

bool AppendOwnedFireTexture(
    const std::filesystem::path& data_root,
    const std::uint32_t layer_width,
    const std::uint32_t layer_height,
    std::vector<std::uint8_t>* const texture_rgba8,
    std::uint32_t* const texture_layers,
    std::uint32_t* const fire_texture_layer) {
    if (layer_width == 0 || layer_height == 0 || texture_rgba8 == nullptr ||
        texture_layers == nullptr || fire_texture_layer == nullptr ||
        *texture_layers >= kMaximumCombinedTextureLayers) {
        return false;
    }
    const auto texture = wand::load_hp1_p8_texture(
        data_root / "System/HPParticle.u", kFireTextureReference);
    if (texture.status != wand::Hp1ProfileStatus::ok ||
        texture.object_name != "PotFire08" || texture.mips.empty() ||
        texture.mips.front().width != 64 ||
        texture.mips.front().height != 64 ||
        texture.rgba8.size() != 64U * 64U * 4U) {
        HPVR_LOGE("[hpvr.quest.fire.texture] status=REJECTED ref=%d "
                  "object=%s error=%s",
                  kFireTextureReference, texture.object_name.c_str(),
                  texture.error.c_str());
        return false;
    }
    *fire_texture_layer = *texture_layers;
    const std::size_t layer_bytes =
        static_cast<std::size_t>(layer_width) * layer_height * 4U;
    const std::size_t output_begin = texture_rgba8->size();
    texture_rgba8->resize(output_begin + layer_bytes);
    // Share the existing layer: fire in the upper-left quadrant, the original
    // 128px Flipendo particle in the upper-right. No extra texture-array layer.
    for (std::uint32_t y = 0; y < layer_height/2; ++y) {
        const std::uint32_t source_y = y * 64U / (layer_height/2);
        for (std::uint32_t x = 0; x < layer_width/2; ++x) {
            const std::uint32_t source_x = x * 64U / (layer_width/2);
            const std::size_t source =
                (static_cast<std::size_t>(source_y) * 64U + source_x) * 4U;
            const std::size_t destination = output_begin +
                (static_cast<std::size_t>(y) * layer_width + x) * 4U;
            std::copy_n(texture.rgba8.data() + source, 4,
                        texture_rgba8->begin() + destination);
        }
    }
    const auto flip=wand::load_hp1_p8_texture(data_root/"System/HPParticle.u",100);
    if(flip.status!=wand::Hp1ProfileStatus::ok||flip.object_name!="Particle_02"||flip.mips.empty())return false;
    const auto& mip=flip.mips.front();
    for(std::uint32_t y=0;y<layer_height/2;++y)for(std::uint32_t x=0;x<layer_width/2;++x){
        const auto source=((y*mip.height/(layer_height/2))*mip.width+x*mip.width/(layer_width/2))*4;
        const auto destination=output_begin+(std::size_t(y)*layer_width+x+layer_width/2)*4;
        std::copy_n(flip.rgba8.data()+source,4,texture_rgba8->begin()+destination);
    }
    // Original additive candle core and broad corona share the unused lower
    // half. They need no bloom pass or additional texture-array layer.
    for(unsigned quadrant=0;quadrant<2;++quadrant){
        const auto image=wand::load_hp1_p8_texture(data_root/"System/HPParticle.u",quadrant?20:640);
        if(image.status!=wand::Hp1ProfileStatus::ok||image.object_name!=(quadrant?"CandleF":"glow00")||image.mips.empty())return false;
        const auto& level=image.mips.front();
        for(std::uint32_t y=0;y<layer_height/2;++y)for(std::uint32_t x=0;x<layer_width/2;++x){
            const auto source=((y*level.height/(layer_height/2))*level.width+x*level.width/(layer_width/2))*4;
            const auto destination=output_begin+(std::size_t(y+layer_height/2)*layer_width+x+quadrant*layer_width/2)*4;
            std::copy_n(image.rgba8.data()+source,4,texture_rgba8->begin()+destination);
        }
    }
    ++*texture_layers;
    HPVR_LOGI("[hpvr.quest.fire.texture] status=READY "
              "source=HPParticle.PotFire08 ref=%d source_size=64x64 "
              "layer=%u layer_size=%ux%u runtime_owned_data=YES",
              kFireTextureReference, *fire_texture_layer,
              layer_width, layer_height);
    return true;
}

bool LoadOwnedSceneProps(
    const std::filesystem::path& data_root,
    const std::filesystem::path& map_package,
    const hpvr_hp1_player_start_report& player_start,
    const float player_start_yaw,
    const std::vector<SceneLight>& lights,
    const std::vector<CollisionTriangle>& collision_triangles,
    std::vector<GpuVertex>* const vertices,
    std::vector<std::uint8_t>* const texture_rgba8,
    std::uint32_t* const texture_layers,
    std::uint32_t* const fixture_vertex_count,
    std::size_t* const fixture_actor_count,
    std::vector<GlowEmitter>* const glows,
    std::vector<FlameEmitter>* const flames=nullptr,
    std::vector<KnightDraw>* const knights=nullptr) {
    if (vertices == nullptr || texture_rgba8 == nullptr ||
        texture_layers == nullptr || fixture_vertex_count == nullptr ||
        fixture_actor_count == nullptr || glows == nullptr) return false;
    struct Definition {
        std::string_view class_name;
        std::int32_t class_reference;
        std::int32_t mesh_reference;
    };
    constexpr std::array<Definition, 9> definitions{{
        {"hprops.lamppost", 415, 1292},
        {"hprops.singlecandlestick", 376, 1495},
        {"hprops.threearmfloorcandlestick", 400, 1556},
        {"hprops.knight", 114, 152},
        {"hprops.rectanglewoodtable",580,1471},
        {"hprops.gregorysmarmy",237,1145},
        {"hprops.transblackboard",404,1564},
        {"hprops.transtrestletable",418,1590},
        {"hprops.hogwartsurn",277,1275},
    }};
    const auto actors = wand::inspect_hp1_actor_visuals(map_package);
    if (actors.status != wand::Hp1ProfileStatus::ok) return false;
    const std::string props_package =
        (data_root / "System/HProps.u").string();
    std::map<std::string, LoadedStaticMesh> meshes;
    std::uint32_t next_layer = *texture_layers;
    for (const auto& definition : definitions) {
        const auto defaults = wand::inspect_hp1_class_visual_defaults(
            props_package, definition.class_reference);
        if (defaults.status != wand::Hp1ProfileStatus::ok ||
            defaults.mesh_reference != definition.mesh_reference) {
            HPVR_LOGE("[hpvr.quest.fixtures] status=DEFAULTS_REJECTED "
                      "class=%.*s mesh=%d error=%s",
                      static_cast<int>(definition.class_name.size()),
                      definition.class_name.data(), defaults.mesh_reference,
                      defaults.error.c_str());
            return false;
        }
        LoadedStaticMesh mesh;
        if (!LoadStaticMesh(props_package, definition.mesh_reference,
                            next_layer, &mesh) ||
            mesh.report.texture_layer_count >
                kMaximumCombinedTextureLayers - next_layer) return false;
        next_layer += mesh.report.texture_layer_count;
        texture_rgba8->insert(texture_rgba8->end(), mesh.textures.begin(),
                              mesh.textures.end());
        meshes.emplace(std::string(definition.class_name), std::move(mesh));
    }
    const std::size_t first_vertex = vertices->size();
    std::size_t actor_count = 0;
    std::size_t fixture_glow_count = 0;
    std::size_t grounded_knight_count = 0;
    std::size_t corrected_knight_yaw_count = 0;
    auto ordered=actors.actors;
    std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){
        return (AsciiFold(a.qualified_class_name)=="hprops.rectanglewoodtable")>
               (AsciiFold(b.qualified_class_name)=="hprops.rectanglewoodtable");});
    auto support_triangles=collision_triangles;
    for (const auto& actor : ordered) {
        const auto actor_class = AsciiFold(actor.qualified_class_name);
        const auto found = meshes.find(actor_class);
        if (found == meshes.end() || !actor.location_serialized ||
            actor.hidden || actor.rotation_units[0] != 0 ||
            actor.rotation_units[2] != 0 || !std::isfinite(actor.draw_scale) ||
            actor.draw_scale <= 0.0F) continue;
        auto actor_position =
            ActorLocalPosition(actor, player_start, player_start_yaw);
        float actor_yaw =
            static_cast<float>(actor.rotation_units[1]) * kTau / 65536.0F +
            player_start_yaw;
        if(actor_class=="hprops.gregorysmarmy")actor_yaw+=kTau*.5F;
        if(actor_class=="hprops.rectanglewoodtable"||actor_class=="hprops.threearmfloorcandlestick"||
           actor_class=="hprops.singlecandlestick"||actor_class=="hprops.gregorysmarmy"||actor_class=="hprops.transblackboard"||actor_class=="hprops.transtrestletable"||actor_class=="hprops.hogwartsurn"){
            float floor;
            if(!FindPropGroundBelow(support_triangles,actor_position,&floor))return false;
            actor_position[1]=floor-found->second.report.bounds_min_m[1]*actor.draw_scale;
        }
        if (actor_class == "hprops.knight") {
            float ground_height = 0.0F;
            if (!FindPropGroundBelow(collision_triangles, actor_position,
                                     &ground_height)) {
                HPVR_LOGE("[hpvr.quest.props.knight] "
                          "status=GROUND_REJECTED actor_ref=%d",
                          actor.actor_reference);
                return false;
            }
            actor_position[1] = ground_height -
                found->second.report.bounds_min_m[1] * actor.draw_scale;
            ++grounded_knight_count;
            // Only the two landing knights have the near-zero authored yaw
            // reported by the headset as facing the rear wall.  The four
            // side-wall actors already use opposing quarter turns and must
            // retain them.
            if (std::abs(actor.rotation_units[1]) < 1024) {
                actor_yaw += kTau * 0.5F;
                ++corrected_knight_yaw_count;
            }
        }
        const auto prop_first=vertices->size();
        if(knights&&actor_class=="hprops.knight")
            knights->push_back({actor_position,actor_yaw,actor.draw_scale,float(actor.actor_reference%17)*.7F,found->second.layer_base});
        for (const auto& source : found->second.vertices) {
            const auto oriented = RotateYaw(
                {source.position_m[0] * actor.draw_scale,
                 source.position_m[1] * actor.draw_scale,
                 source.position_m[2] * actor.draw_scale}, actor_yaw);
            const std::array<float, 3> local{
                actor_position[0] + oriented[0],
                actor_position[1] + oriented[1],
                actor_position[2] + oriented[2]};
            vertices->push_back({
                {local[0], local[1], local[2]},
                {source.texture_uv[0], source.texture_uv[1]},
                {0.0F, 0.0F},
                found->second.layer_base + source.texture_layer,
                source.polygon_flags|((knights&&actor_class=="hprops.knight")?1U:0U),
                0U,
                PackAuthoredLighting(local, lights)});
        }
        if(actor_class=="hprops.rectanglewoodtable"){
            std::vector<GpuVertex> solid(vertices->begin()+prop_first,vertices->end());
            auto extra=BuildCollisionTriangles(solid,static_cast<std::uint32_t>(solid.size()));
            support_triangles.insert(support_triangles.end(),extra.begin(),extra.end());
        }
        const auto transform_anchor = [&](const std::array<float, 3>& anchor) {
            const auto oriented = RotateYaw(
                {anchor[0] * actor.draw_scale,
                 anchor[1] * actor.draw_scale,
                 anchor[2] * actor.draw_scale}, actor_yaw);
            return std::array<float, 3>{
                actor_position[0] + oriented[0],
                actor_position[1] + oriented[1],
                actor_position[2] + oriented[2]};
        };
        const float phase =
            static_cast<float>(actor.actor_reference & 255) * 0.071F;
        if(flames&&(actor_class=="hprops.singlecandlestick"||actor_class=="hprops.threearmfloorcandlestick")){
            const bool three=actor_class=="hprops.threearmfloorcandlestick";
            for(unsigned wick=0;wick<(three?3U:1U);++wick){
                float high=-1000,min_x=1000,max_x=-1000,min_z=1000,max_z=-1000;
                for(const auto& v:found->second.vertices){
                    const float x=v.position_m[0];
                    const unsigned group=x<-.12F?0U:x>.12F?2U:1U;
                    if(three&&group!=wick)continue;
                    high=std::max(high,v.position_m[1]);
                }
                for(const auto& v:found->second.vertices){
                    const unsigned group=v.position_m[0]<-.12F?0U:v.position_m[0]>.12F?2U:1U;
                    // The wax top is not horizontal: the highest vertex alone
                    // is a corner, not the wick. Center the upper wax shaft.
                    const float band=(found->second.report.bounds_max_m[1]-found->second.report.bounds_min_m[1])*.12F;
                    if((three&&group!=wick)||v.position_m[1]<high-band)continue;
                    min_x=std::min(min_x,v.position_m[0]);max_x=std::max(max_x,v.position_m[0]);
                    min_z=std::min(min_z,v.position_m[2]);max_z=std::max(max_z,v.position_m[2]);
                }
                if(high<-100)return false;
                flames->push_back({transform_anchor({(min_x+max_x)*.5F,high,(min_z+max_z)*.5F}),phase+wick,.32F});
            }
        }
        if (actor_class == "hprops.lamppost") {
            const float minimum_y = found->second.report.bounds_min_m[1];
            const float maximum_y = found->second.report.bounds_max_m[1];
            const auto position = transform_anchor(
                {0.0F, maximum_y - (maximum_y - minimum_y) * 0.22F, 0.0F});
            glows->push_back({position, 0.36F * actor.draw_scale,
                              0.62F, phase});
            ++fixture_glow_count;
        }
        ++actor_count;
    }
    if(knights){
        const auto skin=wand::load_hp1_skeletal_skin(props_package,152);
        const auto anim=wand::load_hp1_animation(props_package,1511);
        const auto mesh=wand::build_hp1_skeletal_triangle_mesh(props_package,152,kMetersPerUnrealUnit);
        if(skin.status!=wand::Hp1ProfileStatus::ok||anim.status!=wand::Hp1ProfileStatus::ok||mesh.status!=wand::Hp1ProfileStatus::ok)return false;
        const std::array<std::pair<std::string_view,float>,8> phases{{{"idle2lookright",.666667F},{"lookright",2.0F},
            {"lookright2idle",.666667F},{"idle",3.0F},{"idle2lookleft",.666667F},{"lookleft",2.0F},
            {"lookleft2idle",.666667F},{"idle",3.0F}}};
        for(auto& draw:*knights){
            draw.first=static_cast<std::uint32_t>(vertices->size());draw.count=static_cast<std::uint32_t>(mesh.vertices.size());draw.frames=380;
            for(unsigned frame=0;frame<draw.frames;++frame){
                float t=float(frame)/30;auto phase=phases.begin();
                while(phase+1!=phases.end()&&t>=phase->second){t-=phase->second;++phase;}
                const auto seq=std::ranges::find_if(anim.sequences,[&](const auto& s){return AsciiFold(s.name)==phase->first;});
                if(seq==anim.sequences.end())return false;
                const auto index=static_cast<std::size_t>(seq-anim.sequences.begin());
                const bool transition=phase->first.find('2')!=std::string_view::npos;
                const auto pose=wand::sample_hp1_skeletal_animation(skin,anim,index,t,!transition);
                if(pose.status!=wand::Hp1ProfileStatus::ok)return false;
                float floor=1000;
                for(const auto& v:mesh.vertices)floor=std::min(floor,pose.points[v.point_index].z*kMetersPerUnrealUnit*draw.scale);
                for(const auto& v:mesh.vertices){
                    const auto& p=pose.points[v.point_index];const float scale=kMetersPerUnrealUnit*draw.scale;
                    auto local=RotateYaw({p.y*scale,p.z*scale-floor,p.x*scale},draw.yaw);
                    // origin was positioned for the centered static mesh; animated feet use the same pedestal.
                    local=AddVector(local,AddVector(draw.origin,{0,meshes.at("hprops.knight").report.bounds_min_m[1]*draw.scale,0}));
                    vertices->push_back({{local[0],local[1],local[2]},{v.texture_uv[0],v.texture_uv[1]},{0,0},
                        draw.layer+v.material_index,v.polygon_flags,0,PackAuthoredLighting(local,lights)});
                }
            }
        }
    }
    const std::size_t added = vertices->size() - first_vertex;
    if (actor_count != 60 || grounded_knight_count != 6 ||
        corrected_knight_yaw_count != 2 || added == 0 ||
        added > std::numeric_limits<std::uint32_t>::max()) {
        HPVR_LOGE("[hpvr.quest.fixtures] status=COUNT_REJECTED actors=%zu "
                  "vertices=%zu", actor_count, added);
        return false;
    }
    *fixture_vertex_count = static_cast<std::uint32_t>(added);
    *fixture_actor_count = actor_count;
    *texture_layers = next_layer;
    HPVR_LOGI("[hpvr.quest.props] status=READY actors=%zu vertices=%u "
              "meshes=%zu layers=%u knight_actors=6 knights_grounded=%zu "
              "knight_yaw_corrected=%zu fixture_glows=%zu",
              actor_count, *fixture_vertex_count, meshes.size(), next_layer,
              grounded_knight_count, corrected_knight_yaw_count,
              fixture_glow_count);
    return true;
}

bool LoadIntroDoors(
    const std::filesystem::path& data_root,
    const std::filesystem::path& map_package,
    const hpvr_hp1_player_start_report& player_start, float player_yaw,
    const std::vector<SceneLight>& lights, std::uint32_t width, std::uint32_t height,
    std::vector<GpuVertex>* vertices, std::vector<std::uint8_t>* pixels,
    std::uint32_t* layers, std::vector<DoorDraw>* doors) {
    const auto first_mover_vertex=vertices->size();
    std::map<std::string,std::set<std::uint32_t>> shared_materials;
    const auto bsp_context=wand::prepare_hp1_bsp_build_context(data_root,map_package);
    if(bsp_context.status()!=wand::Hp1ProfileStatus::ok){
        HPVR_LOGE("[hpvr.quest.doors] status=CONTEXT_REJECTED error=%s",std::string(bsp_context.error()).c_str());
        return false;
    }
    const auto actors = wand::inspect_hp1_actor_visuals(map_package);
    if (actors.status != wand::Hp1ProfileStatus::ok) return false;
    auto ordered=actors.actors;
    std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){
        return (AsciiFold(a.tag)=="grandhalldoors")>(AsciiFold(b.tag)=="grandhalldoors");});
    for (const auto& actor : ordered) {
        const bool tutorial=AsciiFold(map_package.stem().string())=="lev_tut1";
        const auto cls=AsciiFold(actor.qualified_class_name);
        if (!cls.starts_with("engine.")||!cls.ends_with("mover") ||
            (tutorial&&AsciiFold(actor.tag) != "grandhalldoors"&&AsciiFold(actor.tag)!="fgsec1"&&AsciiFold(actor.tag)!="fgsec2")) continue;
        std::int32_t brush = 0;
        std::array<float, 3> pre{};
        DoorDraw door;
        door.challenge=!tutorial;
        door.actor_reference=actor.actor_reference;door.initial_state=AsciiFold(actor.initial_state);
        door.looping=cls=="engine.loopmover";door.grid=cls=="engine.gridmover";
        door.opening=false;
        door.stay_open=std::max(0.0F,SerializedFloat(actor,"stayopentime",0,4));
        door.grid_increment=SerializedFloat(actor,"moveincrement",0,64)*kMetersPerUnrealUnit;
        door.tag = AsciiFold(actor.tag);
        door.pivot = ActorLocalPosition(actor, player_start, player_yaw);
        door.placement.pivot_scene=door.pivot;door.placement.player_yaw=player_yaw;
        door.placement.meters_per_unit=kMetersPerUnrealUnit;
        for(unsigned i=0;i<3;++i)door.placement.base_rotation_units[i]=static_cast<float>(actor.rotation_units[i]);
        door.motion.count=2;
        for (const auto& prop : actor.serialized_properties) {
            const auto name = AsciiFold(prop.name);
            if(name=="numkeys"&&prop.value.size()==1)door.motion.count=std::clamp(unsigned(prop.value[0]),1U,16U);
            const auto key_index=static_cast<std::uint64_t>(std::max<std::int64_t>(0,prop.array_index));
            if(name=="keypos"&&key_index<16&&prop.value.size()==12)
                std::memcpy(door.motion.keys[key_index].offset_unreal.data(),prop.value.data(),12);
            if(name=="keyrot"&&key_index<16&&prop.value.size()==12){
                std::array<std::int32_t,3> units{};std::memcpy(units.data(),prop.value.data(),12);
                for(unsigned i=0;i<3;++i)door.motion.keys[key_index].rotation_units[i]=static_cast<float>(units[i]);
            }
            if (name == "brush" && prop.object_reference_serialized) brush = prop.object_reference;
            if (name == "prepivot" && prop.value.size() == 12)
                std::memcpy(pre.data(), prop.value.data(), 12);
            if (name == "keyrot" && prop.array_index == 1 && prop.value.size() == 12) {
                std::int32_t yaw = 0;
                std::memcpy(&yaw, prop.value.data() + 4, 4);
                door.open_yaw = -static_cast<float>(yaw) * kTau / 65536.0F;
            }
            if (name == "movetime" && prop.value.size() == 4)
                std::memcpy(&door.duration, prop.value.data(), 4);
            if(name=="keypos"&&prop.array_index==1&&prop.value.size()==12){
                std::array<float,3> p;std::memcpy(p.data(),prop.value.data(),12);
                door.open_offset=RotateYaw({p[1]*kMetersPerUnrealUnit,p[2]*kMetersPerUnrealUnit,-p[0]*kMetersPerUnrealUnit},player_yaw);
            }
        }
        if (brush <= 0 || !std::isfinite(door.duration) || door.duration < 0.0F) return false;
        // Zero-time decorative movers still contribute visible, solid geometry.
        door.duration=std::max(.01F,door.duration);
        door.open_seconds=cls=="engine.gradualmover"?SerializedFloat(actor,"opentimes",0,1):door.duration;
        door.close_seconds=cls=="engine.gradualmover"?SerializedFloat(actor,"closetimes",0,.2F):door.duration;
        door.motion.seconds=door.open_seconds;
        if(door.grid){door.motion.count=1;door.motion.keys={};}
        if(!movers::Settle(door.motion,0))return false;
        const auto model = wand::build_hp1_textured_bsp_scene(
            data_root, map_package, kMetersPerUnrealUnit, 4096, brush, &bsp_context, door.grid);
        const bool camera_helper=AsciiFold(map_package.stem().string())=="lev_tut2"&&
            door.tag=="cammover"&&SerializedName(actor,"attachtag")=="rotateit"&&
            std::ranges::any_of(actors.actors,[](const auto& a){
                return AsciiFold(a.tag)=="rotateit"&&AsciiFold(a.qualified_class_name).find("cutcamerapos")!=std::string::npos;});
        door.collision_only=!tutorial&&model.decoded_texture_count==0&&
            (door.initial_state=="none"||camera_helper);
        if(!tutorial&&model.decoded_texture_count==0&&!door.collision_only){
            const auto authored=wand::load_hp1_brush_topology(map_package,brush);
            door.collision_only=authored.status==wand::Hp1ProfileStatus::ok&&!authored.surfaces.empty()&&
                std::ranges::all_of(authored.surfaces,[](const auto& surface){return surface.texture_reference==0;});
        }
        if(cls=="engine.attachmover"&&model.decoded_texture_count==0){
            const auto tag=SerializedName(actor,"attachtag");
            door.collision_only=!tag.empty()&&std::ranges::any_of(actors.actors,[&](const auto& target){return AsciiFold(target.tag)==tag;});
        }
        if (model.status != wand::Hp1ProfileStatus::ok || model.vertices.empty() ||
            (!door.collision_only&&model.fallback_triangle_count != 0) || model.omitted_triangle_count != 0) {
            HPVR_LOGE("[hpvr.quest.doors] status=REJECTED brush=%d error=%s", brush, model.error.c_str());
            return false;
        }
        std::vector<std::uint32_t> material_layers;
        for (std::uint32_t layer = 0; layer < model.texture_layer_count; ++layer) {
            std::vector<std::uint8_t> resized;resized.reserve(std::size_t(width)*height*4);
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    const auto sx = static_cast<std::uint64_t>(x) * model.texture_layer_width / width;
                    const auto sy = static_cast<std::uint64_t>(y) * model.texture_layer_height / height;
                    const auto offset = ((layer * model.texture_layer_height + sy) * model.texture_layer_width + sx) * 4U;
                    resized.insert(resized.end(), model.texture_rgba8.begin() + offset,
                                   model.texture_rgba8.begin() + offset + 4U);
                }
            }
            std::uint32_t shared=0;
            for(;shared<*layers;++shared)
                if(std::equal(resized.begin(),resized.end(),pixels->begin()+std::size_t(shared)*resized.size()))break;
            if(shared==*layers){
                if(*layers>=kMaximumCombinedTextureLayers)return false;
                pixels->insert(pixels->end(),resized.begin(),resized.end());++*layers;
            }
            material_layers.push_back(shared);
            if(layer>0&&layer<model.texture_layer_names.size())
                shared_materials[model.texture_layer_names[layer]].insert(shared);
        }
        door.first_vertex = static_cast<std::uint32_t>(vertices->size());
        const std::array<float, 3> prepivot{
            pre[1] * kMetersPerUnrealUnit, pre[2] * kMetersPerUnrealUnit,
            -pre[0] * kMetersPerUnrealUnit};
        const float yaw = player_yaw - static_cast<float>(actor.rotation_units[1]) * kTau / 65536.0F;
        for (const auto& v : model.vertices) {
            const auto local=SubtractVector({v.position_m.x,v.position_m.y,v.position_m.z},prepivot);
            const auto p=AddVector(door.pivot,tutorial?RotateYaw(local,yaw):
                movers::RotateBrushLocal(local,door.placement.base_rotation_units,player_yaw));
            vertices->push_back({{p[0], p[1], p[2]}, {v.texture_uv[0], v.texture_uv[1]},
                {0.0F, 0.0F}, material_layers.at(v.texture_layer), v.polygon_flags,
                0U, PackAuthoredLighting(p, lights)});
        }
        door.vertex_count = static_cast<std::uint32_t>(vertices->size()) - door.first_vertex;
        HPVR_LOGI("[hpvr.quest.doors] status=READY actor=%s brush=%d vertices=%u key_yaw=%.3f",
                  actor.object_name.c_str(), brush, door.vertex_count, door.open_yaw);
        doors->push_back(std::move(door));
    }
    // Some brushes omit PF_Masked even though another brush uses the very
    // same gate texture with holes. Share only its authored alpha, and only
    // when every RGB texel matches; never infer transparency from black.
    const auto texels=std::size_t(width)*height,bytes=texels*4;
    std::set<std::uint32_t> masked_layers;
    for(const auto& [name,candidates]:shared_materials){
        (void)name;
        for(const auto source:candidates){
            const auto source_offset=std::size_t(source)*bytes;
            bool masked=false;
            for(std::size_t i=0;i<texels;++i)if((*pixels)[source_offset+i*4+3]==0){masked=true;break;}
            if(!masked)continue;
            for(const auto target:candidates){
                const auto target_offset=std::size_t(target)*bytes;
                bool same=true;
                for(std::size_t i=0;i<texels&&same;++i)for(unsigned c=0;c<3;++c)
                    if((*pixels)[source_offset+i*4+c]!=(*pixels)[target_offset+i*4+c]){same=false;break;}
                if(!same)continue;
                for(std::size_t i=0;i<texels;++i)(*pixels)[target_offset+i*4+3]=(*pixels)[source_offset+i*4+3];
                masked_layers.insert(target);
            }
        }
    }
    for(std::size_t i=first_mover_vertex;i<vertices->size();++i)
        if(masked_layers.contains((*vertices)[i].texture_layer))(*vertices)[i].polygon_flags|=2U;
    return AsciiFold(map_package.stem().string())=="lev_tut1"?doors->size()==5:!doors->empty();
}

bool IsClassroomActor(std::int32_t ref){
    constexpr std::array<std::int32_t,7> roster{2279,2244,3468,3466,3455,2784,2780};
    return std::ranges::find(roster,ref)!=roster.end();
}
bool LoadOwnedCharacters(
    const std::filesystem::path& data_root,
    const std::filesystem::path& map_package,
    const hpvr_hp1_player_start_report& player_start,
    const float player_start_yaw,
    const std::uint32_t map_texture_layers,
    const std::vector<GpuVertex>& map_vertices,
    const std::uint32_t map_vertex_count,
    const std::vector<SceneLight>& lights,
    std::vector<GpuVertex>* const vertices,
    std::vector<std::uint8_t>* const texture_rgba8,
    std::uint32_t* const texture_layers,
    std::uint32_t* const character_frame_vertex_count,
    std::uint32_t* const animation_frame_count,
    std::vector<CharacterDraw>* const draws,
    QuestSpellTargets* const spell_targets,std::vector<SpellTargetDescriptor>* target_descriptors=nullptr) {
    const auto census = wand::inspect_hp1_actor_visuals(map_package);
    ChildSystem children;
    const bool tutorial=AsciiFold(map_package.stem().string())=="lev_tut1";
    if (tutorial&&!LoadChildSystem(census, player_start, player_start_yaw, &children)) return false;
    const auto manifest = wand::build_hp1_character_manifest(
        data_root, map_package, 0, children.prototypes);
    if (manifest.status != wand::Hp1ProfileStatus::ok) return false;
    const auto& actors = manifest.actors;
    IntroCutscene intro;
    const bool flying_lesson=AsciiFold(map_package.stem().string())=="lev_tut2";
    const bool charms_lesson=AsciiFold(map_package.stem().string())=="lev_tut3";
    if (!LoadIntroCutscene(census, player_start, player_start_yaw, &intro,tutorial?"cutscene4":flying_lesson?"cutscene10":"cutscene0",tutorial)) return false;
    const auto triangles = BuildCollisionTriangles(map_vertices, map_vertex_count);
    std::set<std::string> challenge_clips{"breathe","breath","walk","run","roll","stop","idle","talk1","talk2","float","attack","hit","die","faint","stunned"};
    if(flying_lesson)challenge_clips.insert("hover");
    if(charms_lesson)for(const auto* name:{"runattack","runattackbite","knockback","downbreath","downdizzy","look","fidget_1"})challenge_clips.insert(name);
    if(!tutorial)for(const auto& a:census.actors)for(const auto& p:a.serialized_properties)
        if(p.text_value_serialized&&AsciiFold(p.name).starts_with("cast")&&AsciiFold(p.text_value).starts_with("animate "))
            challenge_clips.insert(AsciiFold(p.text_value.substr(8)));
    if(flying_lesson)for(const auto& a:census.actors)for(const auto& p:a.serialized_properties){
        const auto command=AsciiFold(p.text_value);
        if(p.text_value_serialized&&AsciiFold(p.name).starts_with("cast")&&
            (command.starts_with("setidle ")||command.starts_with("setwalk ")))challenge_clips.insert(command.substr(8));
    }
    std::vector<SpellTargetDescriptor> targets;
    draws->clear();
    std::uint32_t next_layer = map_texture_layers;
    std::map<std::vector<std::uint8_t>,std::uint32_t> skin_layers;
    for (const auto& actor : actors) {
        // Cast only: Harry is drawn in the theatrical intro, hidden in play.
        const bool child = actor.actor_reference >= 0x10000000;
        const auto cls=AsciiFold(actor.qualified_class_name);
        const bool apparition=tutorial?actor.actor_reference==3148:cls=="harrypotter.nhnick";
        const bool ghost=(tutorial&&actor.actor_reference==2968)||apparition;
        const bool classroom=tutorial&&IsClassroomActor(actor.actor_reference);
        const bool story_cast=ghost||classroom||actor.actor_reference==1510||actor.actor_reference==1627||actor.actor_reference==1538||
            actor.actor_reference==1618||actor.actor_reference==1296||actor.actor_reference==777;
        const bool ron_cast=actor.actor_reference==1348 || actor.actor_reference==1396 || actor.actor_reference==1390 ||
            actor.actor_reference==1329 || actor.actor_reference==1326;
        if (tutorial&&!child && !ron_cast && !story_cast && std::ranges::none_of(intro.tracks, [&actor](const auto& track) {
                return !track.camera && track.actor_reference == actor.actor_reference;
            })) continue;
        const bool broom_cast=cls=="harrypotter.broomharry"||cls=="tut2.broomhooch";
        const bool flying_scene_cast=flying_lesson&&std::ranges::any_of(intro.tracks,[&](const auto& t){return !t.camera&&t.actor_reference==actor.actor_reference;});
        const bool charms_cast=charms_lesson&&(cls.find("hermione")!=std::string::npos||cls.find("flitwick")!=std::string::npos||
            cls.rfind("harrypotter.gen_fem_",0)==0||cls.rfind("harrypotter.gen_male_",0)==0);
        if(!tutorial&&!broom_cast&&!flying_scene_cast&&!charms_cast&&cls!="harrypotter.harry"&&cls!="tut1.tut1quirrell"&&cls!="tut1.tut1gnome"&&
           cls!="harrypotter.nhnick"&&cls!="tut1.flipbarrel"&&cls!="tut1.cutharry")continue;
        const auto package = actor.mesh_package.string();
        const auto& object = actor.object_name;
        const auto& class_name = actor.qualified_class_name;
        LoadedCharacterMesh mesh;
        if (!LoadCharacterMesh(package, actor.mesh_reference, next_layer, &mesh)) return false;
        for (const auto& override_skin : actor.skins) {
            if (override_skin.material_slot >= mesh.report.texture_layer_count) {
                HPVR_LOGE("[hpvr.quest.characters.skin] status=SLOT_REJECTED actor=%s slot=%zu layers=%u",
                    object.c_str(),override_skin.material_slot,mesh.report.texture_layer_count);return false;
            }
            const bool masked=std::ranges::any_of(mesh.vertices,[&](const auto& v){
                return v.texture_layer==override_skin.material_slot&&(v.polygon_flags&2U)!=0;});
            const auto texture=wand::load_hp1_p8_texture(override_skin.package,override_skin.reference,masked);
            if (texture.status!=wand::Hp1ProfileStatus::ok || texture.mips.empty()) {
                HPVR_LOGE("[hpvr.quest.characters.skin] status=TEXTURE_REJECTED actor=%s error=%s",
                    object.c_str(),texture.error.c_str());return false;
            }
            const auto& mip=texture.mips.front();
            const std::size_t size=HPVR_HP1_SKELETAL_TEXTURE_SIZE;
            for(std::size_t y=0;y<size;++y) for(std::size_t x=0;x<size;++x) {
                const auto src=((y*mip.height/size)*mip.width+x*mip.width/size)*4;
                const auto dst=(override_skin.material_slot*size*size+y*size+x)*4;
                std::copy_n(texture.rgba8.begin()+src,4,mesh.textures.begin()+dst);
            }
        }
        if(const auto cached=skin_layers.find(mesh.textures);cached!=skin_layers.end()){
            mesh.layer_base=cached->second;
        }else{
            skin_layers.emplace(mesh.textures,next_layer);
            texture_rgba8->insert(texture_rgba8->end(), mesh.textures.begin(), mesh.textures.end());
            next_layer += mesh.report.texture_layer_count;
        }
        if(next_layer>kMaximumCombinedTextureLayers)return false;
        const auto skin = wand::load_hp1_skeletal_skin(package, actor.mesh_reference);
        const auto animation = wand::load_hp1_animation(package, skin.census.animation_reference);
        if (skin.status != wand::Hp1ProfileStatus::ok ||
            animation.status != wand::Hp1ProfileStatus::ok) return false;
        CharacterDraw draw;
        draw.player=tutorial?actor.actor_reference==kHarryActorReference:cls=="harrypotter.harry"||cls=="harrypotter.broomharry";draw.flying=ghost;
        draw.child_template = child;
        draw.enabled = tutorial?(!ron_cast&&!ghost):cls!="tut1.cutharry";
        draw.actor_reference = actor.actor_reference;
        draw.object_name = object;
        draw.class_name = class_name;
        draw.vertex_count = static_cast<std::uint32_t>(mesh.vertices.size());
        draw.base_yaw = static_cast<float>(actor.rotation_units[1]) * kTau / 65536.0F + player_start_yaw;
        const bool charms_student=charms_lesson&&(cls.starts_with("harrypotter.gen_fem_")||cls.starts_with("harrypotter.gen_male_"));
        if(charms_student||(charms_lesson&&cls=="tut3.tut3flitwick"))
            draw.base_yaw=player_start_yaw+kTau*.5F-static_cast<float>(actor.rotation_units[1])*kTau/65536.0F;
        draw.yaw = draw.base_yaw;
        draw.desired_yaw = draw.yaw;
        draw.base_origin = RotateYaw({
            actor.location_unreal.y * kMetersPerUnrealUnit - player_start.position_m[0],
            actor.location_unreal.z * kMetersPerUnrealUnit - player_start.position_m[1],
            -actor.location_unreal.x * kMetersPerUnrealUnit - player_start.position_m[2]}, player_start_yaw);
        float floor = draw.base_origin[1];
        if (!ghost && !broom_cast && !FindPropGroundBelow(triangles, draw.base_origin, &floor)) {
            HPVR_LOGE("[hpvr.quest.characters] status=GROUND_REJECTED actor=%s",object.c_str());return false;
        }
        draw.base_origin[1] = floor;
        float radius = 0.0F, height = 0.0F;
        const auto packed_light = PackAuthoredLighting(
            AddVector(draw.base_origin, {0.0F, 0.8F, 0.0F}), lights);
        auto idle=std::ranges::find_if(animation.sequences,[&](const auto& s){
            const auto name=AsciiFold(s.name);return name=="breathe"||name=="breath"||(apparition&&name=="float")||(!tutorial&&(name=="stop"||name=="idle"));});
        if(charms_student){
            const auto source=std::ranges::find_if(census.actors,[&](const auto& a){return a.actor_reference==actor.actor_reference;});
            const auto authored=source==census.actors.end()?std::string{}:SerializedName(*source,"idleanimname");
            if(!authored.empty()){
                const auto selected=std::ranges::find_if(animation.sequences,[&](const auto& s){return AsciiFold(s.name)==authored;});
                if(selected==animation.sequences.end())return false;
                idle=selected;
            }
        }
        if(idle==animation.sequences.end()&&!tutorial&&!animation.sequences.empty())idle=animation.sequences.begin();
        if(idle==animation.sequences.end())return false;
        const auto rest=wand::sample_hp1_skeletal_animation(skin,animation,
            static_cast<std::size_t>(idle-animation.sequences.begin()),0);
        if(rest.status!=wand::Hp1ProfileStatus::ok)return false;
        float rest_floor=std::numeric_limits<float>::infinity();
        for(const auto& v:mesh.vertices){
            if(v.point_index>=rest.points.size())return false;
            rest_floor=std::min(rest_floor,rest.points[v.point_index].z*kMetersPerUnrealUnit*actor.draw_scale);
        }
        for (std::size_t sequence = 0; sequence < animation.sequences.size(); ++sequence) {
            auto name = AsciiFold(animation.sequences[sequence].name);
            if(!tutorial&&sequence==static_cast<std::size_t>(idle-animation.sequences.begin()))name="breathe";
            if(name=="breath")name="breathe"; // Harry's authored idle uses the singular name.
            if(name=="talk")name=std::ranges::any_of(animation.sequences,[](const auto& s){return AsciiFold(s.name)=="talk2";})?"talk1":"talk2";
            if(name=="trot")name="run"; // The twins' authored locomotion clip.
            if(ghost&&name=="float")name=apparition?"breathe":"run";
            if(!tutorial&&!challenge_clips.contains(name))continue;
            if(!tutorial&&draw.clips.contains(name))continue;
            if(classroom&&name!="breathe")continue;
            if (child && name != "breathe" && name != "walk" && name != "run") continue;
            if (tutorial&&!child && name != "breathe" && name != "walk" && name != "run" &&
                name != "look2" && name != "scratch" && name != "adjustglasses" && name != "talk1" &&
                name != "talk2" && name != "trans2talk2" &&
                name != "transfromtalk2" && name != "look" && name!="sweep"&&name!="snicker"&&name!="intro1"&&name!="intro2"&&name!="wcardcast"&&name!="wcardreact"&&!(ghost&&name=="grab")) continue;
            if (sequence >= animation.moves.size()) return false;
            const float duration = animation.moves[sequence].track_time;
            if (!(duration > 0.0F)) continue;
            const auto clip_frames = static_cast<std::uint32_t>(
                std::clamp(std::ceil(duration * 30.0F), 2.0F, 512.0F));
            CharacterClip clip{static_cast<std::uint32_t>(vertices->size()), duration, clip_frames};
            for (std::uint32_t frame = 0; frame < clip_frames; ++frame) {
                const auto pose = wand::sample_hp1_skeletal_animation(
                    skin, animation, sequence, duration * static_cast<float>(frame) /
                    static_cast<float>(clip_frames),true,name=="run"||name=="walk");
                if (pose.status != wand::Hp1ProfileStatus::ok) return false;
                float min_y = std::numeric_limits<float>::infinity();
                for (const auto& v : mesh.vertices) {
                    if (v.point_index >= pose.points.size()) return false;
                    min_y = std::min(min_y, pose.points[v.point_index].z *
                        kMetersPerUnrealUnit * actor.draw_scale);
                }
                for (const auto& v : mesh.vertices) {
                    const auto p = pose.points[v.point_index];
                    const float scale = kMetersPerUnrealUnit * actor.draw_scale;
                    const float anchor=(name=="run"||name=="walk")?rest_floor:min_y;
                    const std::array<float, 3> model{p.y * scale, p.z * scale - anchor, p.x * scale};
                    height = std::max(height, model[1]);
                    radius = std::max(radius, std::hypot(model[0], model[2]));
                    const auto local = AddVector(draw.base_origin, RotateYaw(model, draw.base_yaw));
                    vertices->push_back({{local[0], local[1], local[2]},
                        {v.texture_uv[0], v.texture_uv[1]}, {0.0F, 0.0F},
                        mesh.layer_base + v.texture_layer, v.polygon_flags|(apparition?0x20000000U:0U), 0U, packed_light});
                }
            }
            draw.clips.emplace(name, clip);
        }
        if (!draw.clips.contains("breathe") || (tutorial&&(
            (!classroom && !apparition && !draw.clips.contains("run") && !draw.clips.contains("walk")) ||
            (!classroom && !ghost && !child && !ron_cast && actor.actor_reference!=kHarryActorReference && !draw.clips.contains("talk2")) ||
            (child && !draw.clips.contains("run"))))) {
            HPVR_LOGE("[hpvr.quest.characters] status=CLIPS_REJECTED actor=%s count=%zu",object.c_str(),draw.clips.size());
            return false;
        }
        draw.first_vertex = draw.clips.at("breathe").first_vertex;
        draw.visual_minimum={1e9F,1e9F,1e9F};draw.visual_maximum={-1e9F,-1e9F,-1e9F};
        for(std::size_t i=draw.first_vertex;i<draw.first_vertex+draw.vertex_count;++i)
            for(unsigned axis=0;axis<3;++axis){
                draw.visual_minimum[axis]=std::min(draw.visual_minimum[axis],(*vertices)[i].position[axis]);
                draw.visual_maximum[axis]=std::max(draw.visual_maximum[axis],(*vertices)[i].position[axis]);
            }
        draw.collision_radius = std::clamp(radius * 0.65F, 0.20F, 0.42F);
        draw.collision_min_y = floor;
        draw.collision_max_y = floor + height;
        draw.collision_center = AddVector(draw.base_origin, {0.0F, height * 0.5F, 0.0F});
        targets.push_back({actor.actor_reference,
            {draw.base_origin[0] - radius, floor, draw.base_origin[2] - radius},
            {draw.base_origin[0] + radius, floor + height, draw.base_origin[2] + radius}, !ghost && !child && !draw.player});
        HPVR_LOGI("[hpvr.quest.characters.cast] actor=%s clips=%zu feet_y=%.3f skin_overrides=%zu debug_crowd=REMOVED",
                  object.c_str(), draw.clips.size(), floor,actor.skins.size());
        draws->push_back(std::move(draw));
    }
    *texture_layers = next_layer;
    *character_frame_vertex_count = draws->empty() ? 0U : draws->front().vertex_count;
    *animation_frame_count = kCharacterAnimationFrameCount;
    if(target_descriptors)*target_descriptors=targets;
    return !draws->empty() && spell_targets->SetTargets(targets);
}

std::vector<CollisionTriangle> BuildPropAimTriangles(const std::vector<GpuVertex>& vertices,
    std::uint32_t map_count,const std::vector<KnightDraw>& knights){
    const auto end=knights.empty()?vertices.size():knights.front().first;
    std::vector<GpuVertex> solid(vertices.begin()+map_count,vertices.begin()+end);
    for(auto& v:solid)if(v.polygon_flags&1U)v.polygon_flags|=kPolyNotSolid;
    // Use the visible skeletal rest pose, not the retired static mesh pivot.
    for(const auto& knight:knights)solid.insert(solid.end(),vertices.begin()+knight.first,vertices.begin()+knight.first+knight.count);
    return BuildCollisionTriangles(solid,static_cast<std::uint32_t>(solid.size()));
}
float BasicRayDistance(const std::vector<CollisionTriangle>& triangles,
                       const std::array<float,3>& origin,const std::array<float,3>& direction,
                       std::size_t exclude_first=0,std::size_t exclude_count=0) {
    float nearest=24.0F;
    const auto cross=[](const auto& a,const auto& b)->std::array<float,3>{
        return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    };
    for(std::size_t index=0;index<triangles.size();++index){
        if(index>=exclude_first&&index-exclude_first<exclude_count)continue;
        const auto& t=triangles[index];
        const auto e1=SubtractVector(t.vertices[1],t.vertices[0]);
        const auto e2=SubtractVector(t.vertices[2],t.vertices[0]);
        const auto p=cross(direction,e2);const float det=DotVector(e1,p);
        if(std::abs(det)<1e-7F)continue;
        const auto s=SubtractVector(origin,t.vertices[0]);
        const float u=DotVector(s,p)/det;if(u<0||u>1)continue;
        const auto q=cross(s,e1);const float v=DotVector(direction,q)/det;
        if(v<0||u+v>1)continue;
        const float distance=DotVector(e2,q)/det;
        if(distance>=0.01F)nearest=std::min(nearest,distance);
    }
    return nearest;
}
bool InClimbLesson(const IntroCutscene& twins,const IntroCutscene& next,const std::array<float,3>& p){
    auto a=twins.trigger_position;const auto& b=next.trigger_position;
    for(const auto& mark:twins.locations)if(AsciiFold(mark.alias)=="hploc2")a=mark.position;
    return p[0]>=std::min(a[0],b[0])-8&&p[0]<=std::max(a[0],b[0])+8&&
           p[2]>=std::min(a[2],b[2])-8&&p[2]<=std::max(a[2],b[2])+8;
}
bool CanCollectBean(const std::vector<CollisionTriangle>& triangles,const std::array<float,3>& body,
                    const std::array<float,3>& bean){
    const auto d=SubtractVector(bean,body);if(std::hypot(d[0],d[2])>0.85F||std::abs(d[1])>0.9F)return false;
    const float length=std::sqrt(DotVector(d,d));
    return length<=0.001F||BasicRayDistance(triangles,body,ScaleVector(d,1.0F/length))>=length-0.03F;
}
std::int32_t CutsceneSpeaker(const IntroCutscene& scene,const CutsceneTrack& track,const std::string& op){
    if(op.size()==5&&op.starts_with("talk"))
        for(const auto& cast:scene.tracks)if(cast.cast_slot==static_cast<unsigned>(op[4]-'0'))return cast.actor_reference;
    return track.actor_reference;
}
std::array<float,3> CrossVector(const std::array<float,3>& left,const std::array<float,3>& right){
    return {left[1]*right[2]-left[2]*right[1],left[2]*right[0]-left[0]*right[2],left[0]*right[1]-left[1]*right[0]};
}
#include "quest_challenge_scene.inl"
#include "quest_broom_scene.inl"
#include "quest_charms_scene.inl"
#include "quest_character_checkpoint.inl"
#include "quest_broom_avatar.inl"
#include "quest_broom_visuals.inl"

struct FrontDrawRange {std::uint32_t first=0,count=0;};
bool AppendFrontGeometry(QuestFrontEnd& front,std::vector<GpuVertex>& vertices,
                         std::vector<std::uint8_t>& textures,std::uint32_t& layers,
                         std::map<std::string,FrontDrawRange>& ranges,std::uint32_t& count) {
    const auto base=layers;
    for(const auto& t:front.assets.textures){
        if(t.rgba.size()!=256*256*4)return false;
        textures.insert(textures.end(),t.rgba.begin(),t.rgba.end());++layers;
    }
    const auto begin=vertices.size();
    QuestFrontEnd layout;layout.assets=front.assets;
    auto emit_quads=[&](const std::string& key,const std::vector<FrontQuad>& quads){
        if(ranges.contains(key))return;
        FrontDrawRange range{static_cast<std::uint32_t>(vertices.size()),0};
        for(const auto& q:quads){
            const float z=0; // Ordered UI pass, no depth competition between tiles.
            const float x=(q.x-320)*0.004375F,y=(240-q.y)*0.004375F;
            const float w=q.w*0.004375F,h=q.h*0.004375F;
            for(const auto& corner:std::array<std::array<float,2>,6>{{{0,0},{0,1},{1,1},{0,0},{1,1},{1,0}}})
                vertices.push_back({{x+corner[0]*w,y-corner[1]*h,z},
                    {q.u+corner[0]*q.uw,q.v+corner[1]*q.vh},{0,0},
                    base+q.texture,0x40000002U,0U,q.tint});
        }
        range.count=static_cast<std::uint32_t>(vertices.size())-range.first;ranges.emplace(key,range);
    };
    auto emit=[&](){emit_quads(layout.DrawKey(),layout.Quads(false));};
    layout.screen=FrontScreen::Vr;
    const auto same_quad=[](const FrontQuad& a,const FrontQuad& b){
        return a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h&&a.u==b.u&&a.v==b.v&&
            a.uw==b.uw&&a.vh==b.vh&&a.texture==b.texture&&a.tint==b.tint;
    };
    // Bake shared panel/row labels once per selection. Settings combinations
    // contain only their changed text, keeping cache headroom and two UI draws.
    for(unsigned i=0;i<kVrMenuRowCount;++i){
        std::vector<std::pair<std::string,std::vector<FrontQuad>>> variants;
        for(bool relaxed:{false,true})for(bool failed:{false,true})for(bool harry:{false,true})
        for(auto mode:{CastingMode::Classic,CastingMode::VisibleGesture,CastingMode::Gesture})for(bool voice:{false,true})for(bool hints:{false,true}){
            layout.vr.casting_mode=mode;layout.vr.voice_cast=voice;layout.vr.first_person_cutscenes=harry;
            layout.vr.voice_hints=hints;
            layout.vr.relaxed_lesson=relaxed;layout.vr_save_failed=failed;layout.selection=i;
            variants.emplace_back(layout.DrawKey(),layout.Quads());
        }
        auto common=variants.front().second;
        std::erase_if(common,[&](const auto& q){return std::ranges::any_of(variants,[&](const auto& variant){
            return std::ranges::none_of(variant.second,[&](const auto& v){return same_quad(q,v);});});});
        emit_quads("vr_common_"+std::to_string(i),common);
        for(auto& [key,quads]:variants){
            std::erase_if(quads,[&](const auto& q){return std::ranges::any_of(common,[&](const auto& c){return same_quad(q,c);});});
            emit_quads(key,quads);
        }
    }
    layout.screen=FrontScreen::Debug;layout.selection=0;emit();
    layout.debug_pinned=true;emit();layout.debug_pinned=false;
    layout.screen=FrontScreen::Controls;layout.selection=0;
    for(unsigned page=0;page<kControlsPageCount;++page){layout.controls_page=page;emit();}
    for(unsigned ch=33;ch<127;++ch)emit_quads("glyph_"+std::to_string(ch),{{0,0,9,12.6F,
        float(ch%16*16+2)/256,float(ch/16*16+2)/256,5.0F/256,7.0F/256,layout.assets.font,0xffffff}});
    for(int scale=50;scale<=175;scale+=5)emit_quads("vr_scale_"+std::to_string(scale),layout.VrValueQuads(scale,true));
    for(int ssr=0;ssr<=100;ssr+=5)emit_quads("vr_ssr_"+std::to_string(ssr),layout.VrValueQuads(ssr,false));
    for(int hz:{0,72,80,90,120})emit_quads("vr_hz_"+std::to_string(hz),layout.VrRefreshQuads(hz));
    for(bool smooth:{false,true})emit_quads("vr_turning_"+std::to_string(int(smooth)),layout.VrTurningQuads(smooth));
    for(bool boost:{false,true})emit_quads("vr_gpu_boost_"+std::to_string(int(boost)),layout.VrGpuBoostQuads(boost));
    for(int speed:{30,60,90,120,150,180})emit_quads("vr_turn_speed_"+std::to_string(speed),layout.VrTurnSpeedQuads(speed));
    for(unsigned status=0;status<8;++status){
        emit_quads("vr_voice_status_"+std::to_string(status),layout.VrVoiceStatusQuads(status));
        emit_quads("voice_aim_"+std::to_string(status),layout.VoiceAimQuads(status));
        emit_quads("voice_aloho_"+std::to_string(status),layout.VoiceAimQuads(status,true));
    }
    if(front.assets.map_id==2){
        emit_quads("broom_labels",layout.BroomLabelQuads());
        for(unsigned field=0;field<3;++field)for(unsigned value=0;value<=(field==0?82U:field==1?180U:5U);++value)
            emit_quads("broom_"+std::to_string(field)+"_"+std::to_string(value),layout.BroomNumberQuads(value,field));
    }
    for(const auto screen:{FrontScreen::Levels,FrontScreen::LevelSlots,FrontScreen::LevelStart}){
        layout.screen=screen;
        for(unsigned i=0;i<(screen==FrontScreen::Levels?static_cast<unsigned>(kQuestMaps.size()+1):screen==FrontScreen::LevelSlots?4U:2U);++i){layout.selection=i;emit();}
    }
    layout.screen=FrontScreen::Main;for(unsigned i=0;i<5;++i){layout.selection=i;emit();}
    layout.screen=FrontScreen::Slots;
    for(unsigned mask=0;mask<8;++mask){for(unsigned s=0;s<3;++s)layout.occupied[s]=(mask&(1U<<s))!=0;
        for(unsigned i=0;i<4;++i){layout.selection=i;emit();}}
    layout.screen=FrontScreen::Slot;
    for(unsigned s=0;s<3;++s){layout.slot=s;
        for(bool b:{false,true}){layout.occupied[s]=b;
            for(unsigned i=0;i<3;++i){layout.selection=i;emit();}}}
    layout.screen=FrontScreen::Replace;for(unsigned i=0;i<2;++i){layout.selection=i;emit();}
    layout.screen=FrontScreen::Pause;
    for(auto paused:{FrontScreen::Game,FrontScreen::Story}){layout.paused=paused;
        for(unsigned stage=0;stage<=23;++stage){layout.progress.quest_stage=stage;
            for(unsigned i=0;i<5;++i){layout.selection=i;emit();}}}
    layout.screen=FrontScreen::Cards;
    for(bool earned:{false,true})for(unsigned page=0;page<7;++page){layout.card_page=page;layout.progress.card_awarded=earned;
        for(unsigned i=0;i<3;++i){layout.selection=i;emit();}}
    layout.screen=FrontScreen::Report;
    for(unsigned i=0;i<5;++i){layout.selection=i;emit();}
    for(unsigned field=0;field<7;++field)for(unsigned place=0;place<7;++place)for(unsigned digit=0;digit<10;++digit)
        emit_quads("report_digit_"+std::to_string(field)+"_"+std::to_string(place)+"_"+std::to_string(digit),
            layout.ReportDigitQuads(digit,field,place));
    for(unsigned house=0;house<4;++house)for(unsigned fill=1;fill<=256;++fill)
        emit_quads("report_sand_"+std::to_string(house)+"_"+std::to_string(fill),layout.ReportSandQuads(fill,house));
    for(unsigned card=0;card<campaign::kCardIds.size();++card)
        emit_quads("folio_card_"+std::to_string(card),layout.FolioCardQuads(card));
    layout.selection=0;
    layout.screen=FrontScreen::Objective;emit();
    for(auto notice:{FrontScreen::Welcome,FrontScreen::DemoEnd}){
        layout.screen=notice;for(unsigned i=0;i<2;++i){layout.selection=i;emit();}
    }
    layout.screen=FrontScreen::Story;for(unsigned i=0;i<14;++i){layout.page=i;emit();}
    layout.screen=FrontScreen::Stub;layout.selection=0;
    for(const auto& message:{"NOT AVAILABLE IN THIS BUILD","USE THE QUEST MENU TO CLOSE THE APP",
                            "SAVE FAILED - PREVIOUS SAVE KEPT"}){
        layout.message=message;emit();}
    for(unsigned health=0;health<=100;++health){
        layout.progress.health=health;emit_quads("health_"+std::to_string(health),layout.HudQuads(0,false));
        auto hurt=layout.HudQuads(0,false);for(auto& q:hurt)q.tint=0x4040ff;
        emit_quads("hurt_"+std::to_string(health),hurt);
    }
    layout.progress.health=100;
    if(front.assets.map_id==1||front.assets.map_id==3)for(unsigned stars=0;stars<=(front.assets.map_id==3?6U:8U);++stars){
        emit_quads("stars_"+std::to_string(stars),layout.ChallengeStarQuads(stars));
        emit_quads("report_stars_"+std::to_string(stars),layout.ChallengeStarQuads(stars,true));
    }
    for(unsigned passes=0;passes<=4;++passes)for(bool ready:{false,true})
        emit_quads("lesson_"+std::to_string(passes)+(ready?"_ready":"_wait"),layout.LessonQuads(passes,ready));
    for(unsigned n=0;n<=512;++n){
        auto quads=layout.HudQuads(n,true);quads.erase(quads.begin(),quads.begin()+2);
        emit_quads("hud_"+std::to_string(n),quads);
    }
    emit_quads("house_points_badge",layout.HousePointQuads());
    for(unsigned n=0;n<=8;++n)emit_quads("star_pickup_"+std::to_string(n),layout.StarPickupQuads(n));
    for(unsigned digits=1;digits<=7;++digits)for(unsigned place=0;place<digits;++place)for(unsigned digit=0;digit<10;++digit)
        emit_quads("house_points_"+std::to_string(digits)+"_"+std::to_string(place)+"_"+std::to_string(digit),layout.HousePointQuads(static_cast<int>(digit),place,digits));
    count=static_cast<std::uint32_t>(vertices.size()-begin);
    return !ranges.empty();
}

void EmitChildEvent(ChildSystem& children, std::vector<DoorDraw>& doors,
                    const std::vector<CollisionTriangle>& triangles, const std::string& raw_tag) {
    const auto tag=AsciiFold(raw_tag);
    if (const auto dispatcher=children.dispatchers.find(tag); dispatcher!=children.dispatchers.end()) {
        if (children.pending.size()+dispatcher->second.size()>128) return;
        for (const auto& event:dispatcher->second)
            children.pending.push_back({children.time+event.time,event.tag});
        HPVR_LOGI("[hpvr.quest.dispatcher] status=SCHEDULED tag=%s events=%zu",tag.c_str(),dispatcher->second.size());
    }
    for (auto& door:doors) if(door.tag==tag) {
        door.opening=!door.opening;
        HPVR_LOGI("[hpvr.quest.doors] status=AUTHORED_TOGGLE time=%.3f open=%d",children.time,door.opening);
    }
    for(std::size_t i=0;i<children.spawners.size();++i) {
        auto& spawner=children.spawners[i];
        if(spawner.tag!=tag || children.actors.size()>=64) continue;
        const auto& choice=spawner.choices[spawner.next_choice++%spawner.choices.size()];
        ChildInstance actor;
        actor.model_reference=choice.model_reference;
        actor.spawner=i;
        actor.speed=choice.speed;
        actor.position=spawner.path.front().position;
        float floor;
        if(!FindPropGroundBelow(triangles,actor.position,&floor)) {
            ++children.ground_misses;
            HPVR_LOGE("[hpvr.quest.children.spawn] status=NO_GROUND tag=%s",tag.c_str());
            continue;
        }
        actor.position[1]=floor;
        actor.pause=spawner.path.front().pause;
        const auto delta=SubtractVector(spawner.path[1].position,actor.position);
        actor.yaw=std::atan2(delta[0],delta[2]);
        children.actors.push_back(actor);
        ++children.spawned;
        HPVR_LOGI("[hpvr.quest.children.spawn] status=ACTIVE tag=%s model=%d speed=%.3f time=%.3f",
            tag.c_str(),actor.model_reference,actor.speed,children.time);
    }
}
void AdvanceChildren(ChildSystem& children, std::vector<DoorDraw>& doors,
                     const std::vector<CollisionTriangle>& triangles, float step) {
    if(!std::isfinite(step) || step<=0) return;
    children.time+=step;
    // Copy due events before delivery; a dispatcher may enqueue more events.
    std::vector<TimedSceneEvent> due;
    for(auto it=children.pending.begin();it!=children.pending.end();) {
        if(it->time<=children.time) { due.push_back(*it); it=children.pending.erase(it); }
        else ++it;
    }
    std::stable_sort(due.begin(),due.end(),[](const auto& a,const auto& b){return a.time<b.time;});
    for(const auto& event:due) EmitChildEvent(children,doors,triangles,event.tag);
    for(auto& actor:children.actors) {
        if(!actor.active) continue;
        actor.age+=step;
        if(actor.pause>0) {actor.pause=std::max(0.0F,actor.pause-step);continue;}
        const auto& path=children.spawners[actor.spawner].path;
        if(actor.waypoint>=path.size()) {actor.active=false;++children.destroyed;continue;}
        const auto& point=path[actor.waypoint];
        const auto delta=SubtractVector(point.position,actor.position);
        const float distance=std::hypot(delta[0],delta[2]);
        const float amount=std::min(distance,actor.speed*step);
        auto proposed=actor.position;
        if(distance>0.0001F) {
            proposed[0]+=delta[0]*amount/distance;
            proposed[2]+=delta[2]*amount/distance;
            const auto angle=std::remainder(std::atan2(delta[0],delta[2])-actor.yaw,kTau);
            actor.yaw+=std::clamp(angle,-step*10,step*10);
        }
        // Follow the last supported floor with a small step-up allowance;
        // the next patrol point may be a whole storey above us.
        proposed[1]=actor.position[1]+0.05F;
        float floor=actor.position[1];
        if(FindPropGroundBelow(triangles,proposed,&floor)) {
            proposed[1]=floor; actor.position=proposed;
        } else {
            ++children.ground_misses;
            actor.active=false; ++children.destroyed;
            HPVR_LOGE("[hpvr.quest.children.move] status=NO_GROUND spawner=%zu waypoint=%zu",
                actor.spawner,actor.waypoint);
            continue;
        }
        if(distance<=amount+0.001F) {
            actor.pause=point.pause;
            ++actor.waypoint;
            if(point.destroy || actor.waypoint>=path.size()) {
                actor.active=false;++children.destroyed;
                HPVR_LOGI("[hpvr.quest.children.move] status=DESTROY_AT_PATROL_END spawner=%zu",actor.spawner);
            }
        }
    }
}

#include "quest_broom_environment.inl"
#include "quest_prepared_geometry.inl"
#include "quest_mirror_surfaces.inl"
#include "quest_prepared_codec.inl"
#include "quest_gnome_restore.inl"
#include "quest_gnome_movement.inl"
#include "quest_grid_visual_restore.inl"
#include "quest_candle_restore.inl"
#include "quest_prop_orientation_restore.inl"
#include "quest_ambient_restore.inl"
#include "quest_lock_particles.inl"

#ifndef HPVR_QUEST_CPU_ONLY
bool BuildRibbonModel(const std::array<float, 3>& start,
                      const std::array<float, 3>& end,
                      const std::array<float, 3>& plane_normal,
                      const float width,
                      const float normal_offset,
                      Matrix4* const output) {
    if (output == nullptr || !std::isfinite(width) || width <= 0.0F)
        return false;
    const std::array<float, 3> delta{
        end[0] - start[0], end[1] - start[1], end[2] - start[2]};
    std::array<float, 3> tangent{};
    std::array<float, 3> normal{};
    if (!NormalizeVector(delta, &tangent) ||
        !NormalizeVector(plane_normal, &normal)) return false;
    std::array<float, 3> side{};
    if (!NormalizeVector(CrossVector(normal, tangent), &side)) return false;
    *output = {
        delta[0], delta[1], delta[2], 0.0F,
        side[0] * width, side[1] * width, side[2] * width, 0.0F,
        normal[0] * 0.001F, normal[1] * 0.001F,
        normal[2] * 0.001F, 0.0F,
        start[0] + normal[0] * normal_offset,
        start[1] + normal[1] * normal_offset,
        start[2] + normal[2] * normal_offset, 1.0F};
    return true;
}

bool CheckVk(const VkResult result, const char* operation) {
    if (result == VK_SUCCESS) {
        return true;
    }
    HPVR_LOGE("[hpvr.quest.scene.vk.error] operation=%s result=%d", operation,
              static_cast<int>(result));
    return false;
}

bool FindMemoryType(const VkPhysicalDevice physical_device,
                    const std::uint32_t type_bits,
                    const VkMemoryPropertyFlags required,
                    std::uint32_t* output_index) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((type_bits & (1U << index)) != 0 &&
            (properties.memoryTypes[index].propertyFlags & required) ==
                required) {
            *output_index = index;
            return true;
        }
    }
    return false;
}

bool CreateBuffer(const VkPhysicalDevice physical_device,
                  const VkDevice device,
                  const VkDeviceSize size,
                  const VkBufferUsageFlags usage,
                  const VkMemoryPropertyFlags memory_properties,
                  VkBuffer* output_buffer,
                  VkDeviceMemory* output_memory) {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!CheckVk(vkCreateBuffer(device, &buffer_info, nullptr, output_buffer),
                 "vkCreateBuffer")) {
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, *output_buffer, &requirements);
    std::uint32_t memory_type = 0;
    if (!FindMemoryType(physical_device, requirements.memoryTypeBits,
                        memory_properties, &memory_type)) {
        HPVR_LOGE("[hpvr.quest.scene.vk] status=BUFFER_MEMORY_TYPE_MISSING");
        vkDestroyBuffer(device, *output_buffer, nullptr);
        *output_buffer = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = memory_type;
    if (!CheckVk(vkAllocateMemory(device, &allocate_info, nullptr,
                                  output_memory),
                 "vkAllocateMemory(buffer)")) {
        vkDestroyBuffer(device, *output_buffer, nullptr);
        *output_buffer = VK_NULL_HANDLE;
        return false;
    }
    if (!CheckVk(vkBindBufferMemory(device, *output_buffer, *output_memory, 0),
                 "vkBindBufferMemory")) {
        vkFreeMemory(device, *output_memory, nullptr);
        vkDestroyBuffer(device, *output_buffer, nullptr);
        *output_memory = VK_NULL_HANDLE;
        *output_buffer = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool CreateSampledTexture2D(
    const VkPhysicalDevice physical_device, const VkDevice device,
    const VkQueue queue, const std::uint32_t queue_family,
    const std::vector<std::uint8_t>& rgba8, const std::uint32_t width,
    const std::uint32_t height, VkImage* const output_image,
    VkDeviceMemory* const output_memory, VkImageView* const output_view,
    VkSampler* const output_sampler) {
    if (rgba8.empty() || width == 0 || height == 0 ||
        output_image == nullptr || output_memory == nullptr ||
        output_view == nullptr || output_sampler == nullptr) return false;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    if (!CreateBuffer(physical_device, device, rgba8.size(),
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging, &staging_memory)) return false;
    void* mapped = nullptr;
    if (!CheckVk(vkMapMemory(device, staging_memory, 0, rgba8.size(), 0,
                             &mapped),
                 "vkMapMemory(lightmap)")) {
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, staging_memory, nullptr);
        return false;
    }
    std::memcpy(mapped, rgba8.data(), rgba8.size());
    vkUnmapMemory(device, staging_memory);

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!CheckVk(vkCreateImage(device, &image_info, nullptr, output_image),
                 "vkCreateImage(lightmap)")) {
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, staging_memory, nullptr);
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, *output_image, &requirements);
    std::uint32_t memory_type = 0;
    if (!FindMemoryType(physical_device, requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memory_type)) {
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, staging_memory, nullptr);
        return false;
    }
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type;
    if (!CheckVk(vkAllocateMemory(device, &allocate, nullptr, output_memory),
                 "vkAllocateMemory(lightmap)") ||
        !CheckVk(vkBindImageMemory(device, *output_image, *output_memory, 0),
                 "vkBindImageMemory(lightmap)")) {
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, staging_memory, nullptr);
        return false;
    }

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = queue_family;
    bool ok = CheckVk(vkCreateCommandPool(device, &pool_info, nullptr, &pool),
                      "vkCreateCommandPool(lightmap)");
    VkCommandBuffer command = VK_NULL_HANDLE;
    if (ok) {
        VkCommandBufferAllocateInfo allocate_command{};
        allocate_command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate_command.commandPool = pool;
        allocate_command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate_command.commandBufferCount = 1;
        ok = CheckVk(vkAllocateCommandBuffers(device, &allocate_command,
                                               &command),
                     "vkAllocateCommandBuffers(lightmap)");
    }
    if (ok) {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ok = CheckVk(vkBeginCommandBuffer(command, &begin),
                     "vkBeginCommandBuffer(lightmap)");
    }
    if (ok) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = *output_image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(command, staging, *output_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
        ok = CheckVk(vkEndCommandBuffer(command),
                     "vkEndCommandBuffer(lightmap)");
    }
    if (ok) {
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        ok = CheckVk(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE),
                     "vkQueueSubmit(lightmap)") &&
             CheckVk(vkQueueWaitIdle(queue), "vkQueueWaitIdle(lightmap)");
    }
    if (pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, staging_memory, nullptr);
    if (!ok) return false;

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = *output_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    if (!CheckVk(vkCreateImageView(device, &view_info, nullptr, output_view),
                 "vkCreateImageView(lightmap)")) return false;
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 0.0F;
    return CheckVk(vkCreateSampler(device, &sampler_info, nullptr,
                                   output_sampler),
                   "vkCreateSampler(lightmap)");
}

VkShaderModule CreateShaderModule(const VkDevice device,
                                  const unsigned char* bytes,
                                  const std::size_t byte_count) {
    if (byte_count == 0 || byte_count % sizeof(std::uint32_t) != 0) {
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = byte_count;
    info.pCode = reinterpret_cast<const std::uint32_t*>(bytes);
    VkShaderModule module = VK_NULL_HANDLE;
    if (!CheckVk(vkCreateShaderModule(device, &info, nullptr, &module),
                 "vkCreateShaderModule")) {
        return VK_NULL_HANDLE;
    }
    return module;
}

#endif

bool LoadOwnedWand(const std::filesystem::path& data_root,
                   std::vector<WandGpuVertex>* const output) {
    if (output == nullptr) {
        return false;
    }
    const std::string package =
        (data_root / "system" / "HPBase.u").string();
    const auto table = wand::inspect_hp1_package_link_table(package);
    const auto wand_reference = FindUniqueRootExport(table, "WandMesh", "Engine.SkeletalMesh");
    if (wand_reference == 0) {
        HPVR_LOGE("[hpvr.quest.wand.data] status=RESOURCE_REJECTED "
                  "package=%s object=WandMesh class=Engine.SkeletalMesh "
                  "reason=missing_ambiguous_or_invalid_package error=%s",
                  package.c_str(), table.error.c_str());
        return false;
    }
    hpvr_hp1_skeletal_mesh_report query{};
    const std::uint32_t query_status =
        hpvr_hp1_load_skeletal_geometry_utf8(
            package.c_str(), wand_reference, 1.0F, nullptr, 0, &query);
    if (query_status != HPVR_HP1_PROFILE_BUFFER_TOO_SMALL ||
        query.status != query_status ||
        query.abi_version != HPVR_HP1_SKELETAL_MESH_ABI_VERSION ||
        query.required_vertex_count == 0 ||
        query.required_vertex_count % 3 != 0) {
        HPVR_LOGE(
            "[hpvr.quest.wand.data] status=QUERY_REJECTED result=%u "
            "report=%u abi=%u vertices=%u error=%s",
            query_status, query.status, query.abi_version,
            query.required_vertex_count, query.error);
        return false;
    }
    std::vector<hpvr_hp1_skeletal_mesh_vertex> source(
        query.required_vertex_count);
    hpvr_hp1_skeletal_mesh_report loaded{};
    const std::uint32_t load_status =
        hpvr_hp1_load_skeletal_geometry_utf8(
            package.c_str(), wand_reference, 1.0F, source.data(),
            static_cast<std::uint32_t>(source.size()), &loaded);
    if (load_status != HPVR_HP1_PROFILE_OK ||
        loaded.status != load_status ||
        loaded.abi_version != HPVR_HP1_SKELETAL_MESH_ABI_VERSION ||
        loaded.written_vertex_count != source.size() ||
        loaded.required_vertex_count != query.required_vertex_count) {
        HPVR_LOGE(
            "[hpvr.quest.wand.data] status=LOAD_REJECTED result=%u "
            "report=%u abi=%u written=%u required=%u error=%s",
            load_status, loaded.status, loaded.abi_version,
            loaded.written_vertex_count, loaded.required_vertex_count,
            loaded.error);
        return false;
    }

    const std::array<float, 3> minimum{
        loaded.bounds_min_m[0], loaded.bounds_min_m[1],
        loaded.bounds_min_m[2]};
    const std::array<float, 3> maximum{
        loaded.bounds_max_m[0], loaded.bounds_max_m[1],
        loaded.bounds_max_m[2]};
    const std::array<float, 3> extent{
        maximum[0] - minimum[0], maximum[1] - minimum[1],
        maximum[2] - minimum[2]};
    std::size_t long_axis = 0;
    if (extent[1] >= extent[0] && extent[1] >= extent[2]) {
        long_axis = 1;
    } else if (extent[2] >= extent[0]) {
        long_axis = 2;
    }
    if (!std::all_of(extent.begin(), extent.end(),
                     [](const float value) {
                         return std::isfinite(value) && value > 0.0F;
                     })) {
        HPVR_LOGE("[hpvr.quest.wand.data] status=INVALID_BOUNDS");
        return false;
    }
    const std::array<std::size_t, 2> cross_axes =
        long_axis == 0 ? std::array<std::size_t, 2>{1, 2}
                       : long_axis == 1
                             ? std::array<std::size_t, 2>{0, 2}
                             : std::array<std::size_t, 2>{0, 1};
    const std::array<float, 3> center{
        (minimum[0] + maximum[0]) * 0.5F,
        (minimum[1] + maximum[1]) * 0.5F,
        (minimum[2] + maximum[2]) * 0.5F};
    const float end_band = extent[long_axis] * 0.18F;
    const auto radial_at = [&](const bool maximum_end) {
        float sum = 0.0F;
        std::uint32_t count = 0;
        for (const auto& vertex : source) {
            const bool at_end =
                maximum_end
                    ? vertex.position_m[long_axis] >=
                          maximum[long_axis] - end_band
                    : vertex.position_m[long_axis] <=
                          minimum[long_axis] + end_band;
            if (at_end) {
                const float u =
                    vertex.position_m[cross_axes[0]] - center[cross_axes[0]];
                const float v =
                    vertex.position_m[cross_axes[1]] - center[cross_axes[1]];
                sum += u * u + v * v;
                ++count;
            }
        }
        return sum / static_cast<float>(std::max(1U, count));
    };
    const bool handle_is_max = radial_at(true) > radial_at(false);
    const float scale = kWandLengthMeters / extent[long_axis];
    output->clear();
    output->reserve(source.size());
    for (const auto& vertex : source) {
        const float t =
            std::clamp(handle_is_max
                           ? (maximum[long_axis] -
                              vertex.position_m[long_axis]) /
                                 extent[long_axis]
                           : (vertex.position_m[long_axis] -
                              minimum[long_axis]) /
                                 extent[long_axis],
                       0.0F, 1.0F);
        output->push_back({
            {(vertex.position_m[cross_axes[0]] - center[cross_axes[0]]) *
                 scale,
             (vertex.position_m[cross_axes[1]] - center[cross_axes[1]]) *
                 scale,
             -t * kWandLengthMeters},
            {0.22F + 0.22F * t, 0.055F + 0.075F * t, 0.015F, 1.0F},
        });
    }
    HPVR_LOGI(
        "[hpvr.quest.wand.data] status=READY mesh=HPBase.WandMesh "
        "mesh_ref=%d vertices=%zu triangles=%zu length_m=%.3f "
        "long_axis=%zu handle_end=%s material=PROCEDURAL_BROWN",
        wand_reference, output->size(), output->size() / 3,
        kWandLengthMeters, long_axis, handle_is_max ? "max" : "min");
    return true;
}

#ifndef HPVR_QUEST_CPU_ONLY

}  // namespace

struct QuestScene::State {
    unsigned map_id=0;
    std::int32_t harry_actor=kHarryActorReference;
    ChallengeRuntime challenge;
    CharmsRuntime charms;
    BroomRuntime broom;
    BroomVisuals broom_visuals;
    BroomAvatar broom_avatar;
    VkBuffer broom_avatar_buffer=VK_NULL_HANDLE;
    VkDeviceMemory broom_avatar_memory=VK_NULL_HANDLE;
    VkBuffer broom_particle_buffer=VK_NULL_HANDLE;
    VkDeviceMemory broom_particle_memory=VK_NULL_HANDLE;
    void* broom_particle_mapped=nullptr;
    bool travel_pending=false,travel_blocked=false;
    ProgressSave travel_progress;
    ProgressSave travel_origin;
    unsigned travel_slot=0;
    ReflectionHistory reflections;
    PlanarMirrorTarget mirror_target;
    std::vector<MirrorSurface> mirrors;
    std::vector<bool> captured_mirrors;
    VkPipeline mirror_capture_pipeline=VK_NULL_HANDLE,mirror_composite_pipeline=VK_NULL_HANDLE,mirror_veil_pipeline=VK_NULL_HANDLE;
    VkPipelineLayout mirror_layout=VK_NULL_HANDLE;
    QuestFrontEnd frontend;
    DemoFirstStep first_step;
    DemoCameraReturn exit_return;
    CinematicPanelAnchor front_anchor;
    bool community_requested=false,lesson_finish_pending=false;
    std::map<std::string,FrontDrawRange> front_draws;
    std::uint32_t front_vertex_count=0;
    Matrix4 front_transform{};
    PerformanceSnapshot performance;
    Matrix4 hud_transform{};
    Matrix4 effect_view_transform{};
    float bean_hud_time=0;
    float star_hud_time=0;
    HousePointHud house_point_hud;
    bool front_anchor_valid=false, restore_pending=false, restoring=false;
    std::array<float,3> platform_transport{};
    ProgressSave placement;
    IntroCutscene initial_intro;
    IntroCutscene ron_intro;
    IntroCutscene twins_intro, next_room, jump_finish, ron_lead, twins_transfer, peeves_departure;
    IntroCutscene card_scene,lesson_exit;
    bool lesson_intro_started=false,tracking_active=true,peeves_hit=false;
    float damage_cooldown=0,lesson_wait=0;
    std::size_t frog_sound=0,card_sound=0,star_sound=0;
    float peeves_time=0;
    std::array<float,3> peeves_from{},peeves_to{};
    std::array<float,3> peeves_trigger{},peeves_home{};
    std::array<float,3> peeves_retreat{},peeves_path_a{},peeves_path_b{};
    struct BumpState { bool near=false; float cooldown=0; std::size_t next=0; };
    std::map<std::int32_t,BumpState> bump_states;
    std::int32_t bump_actor=0;
    float bump_cooldown=0,bump_restore_yaw=0;
    std::vector<CollisionTriangle> closed_secret_wall,closed_reward_wall;
    std::vector<CollisionTriangle> prop_aim_triangles;
    bool bean_twins_staged=false;
    RewardApproach reward_approach;
    float health_flash_time=0;
    float death_time=-1,death_duration=0;
    ProgressSave death_checkpoint,challenge_start_checkpoint,challenge_initial_checkpoint;
    bool death_checkpoint_valid=false,challenge_start_checkpoint_valid=false,challenge_initial_checkpoint_valid=false;
    float lesson_ghost_time=0;
    std::array<float,3> lesson_ghost_center{},lesson_ghost_direction{};
    std::array<IntroCutscene,4> story_encounters;
    std::vector<KnightDraw> knights;
    std::vector<BeanDraw> beans;
    ClimbMotion climb;
    JumpMotion jump;
    bool jump_down=true,jump_pending=false;
    float physics_step=0;
    float bean_time=0;
    CardPickupEffect card_pickup;
    std::vector<PickupFlight> pickup_flights;
    std::array<float,3> last_player{};
    std::array<float,3> player_capsule{};
    bool player_capsule_valid=false;
    float last_yaw=0, save_clock=0;
    ChildSystem children;
    std::vector<DoorDraw> doors;
    std::vector<SceneLight> dark_lights;
    AbyssFogVolume abyss_fog;
    Matrix4 abyss_scene_to_source{};
    VkBuffer abyss_uniform_buffer = VK_NULL_HANDLE;
    VkDeviceMemory abyss_uniform_memory = VK_NULL_HANDLE;
    void* abyss_uniform_mapped = nullptr;
    std::uint32_t abyss_uniform_stride = 0, abyss_frame_slots = 0, abyss_disabled_offset = 0;
    std::vector<GpuVertex> vertices;
    std::uint32_t map_vertex_count = 0;
    std::uint32_t fixture_vertex_count = 0;
    std::size_t fixture_actor_count = 0;
    std::uint32_t character_frame_vertex_count = 0;
    std::uint32_t animation_frame_count = 0;
    std::uint32_t current_animation_frame = 0;
    float animation_elapsed_seconds = 0.0F;
    WorldEffectsClock effects_clock;
    std::vector<std::uint8_t> texture_rgba8;
    std::uint32_t texture_width = 0;
    std::uint32_t texture_height = 0;
    std::uint32_t texture_layers = 0;
    std::uint32_t fire_texture_layer =
        std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint8_t> lightmap_rgba8;
    std::uint32_t lightmap_width = 0;
    std::uint32_t lightmap_height = 0;
    std::size_t decoded_lightmaps = 0;
    std::size_t decoded_textures = 0;
    std::size_t fallback_materials = 0;
    float player_start_yaw = 0.0F;
    std::vector<WandGpuVertex> wand_vertices;
    std::vector<WandGpuVertex> glow_vertices;
    std::vector<CharacterDraw> character_draws;
    QuestSpellTargets spell_targets;
    SpellProjectile projectile;
    BasicCast basic_cast;
    std::int32_t aim_actor=0,wand_lock_actor=0;
    std::array<float,3> aim_minimum{},aim_maximum{};
    bool wand_lock_valid=false;
    bool wand_was_held=false,wand_cast_consumed=false;
    CastingMode wand_cast_mode=CastingMode::Classic;
    std::array<float,3> wand_lock_point{};
    std::array<float,3> wand_lock_actor_offset{};
    std::uint64_t automatic_cast_serial=0;
    TargetMarkerBatch target_marker;
    std::array<TargetMarkerBatch,2> charms_markers;
    std::uint32_t target_marker_texture_layer=0;
    std::uint32_t smoke_texture_layer=0;
    unsigned voice_status=0;
    bool wand_voice_mode=false;
    bool voice_cast_this_hold=false;
    VoiceRepeatCooldown voice_repeat;
    std::vector<CollisionTriangle> collision_triangles;
    std::vector<ScriptTrigger> script_triggers;
    std::vector<FlameEmitter> flames;
    std::vector<GlowEmitter> glows;
    std::vector<AmbientParticleEmitter> ambient_emitters;
    std::vector<AmbientParticleEmitter> lock_emitters;
    std::uint32_t lock_texture_layer=0;
    std::uint32_t feather_texture_layer=0;
    std::size_t script_actor_count = 0;
    std::size_t script_event_edge_count = 0;
    std::size_t authored_light_count = 0;
    IntroCutscene intro_cutscene;
    QuestAudio audio;
    std::array<float,3> ambient_position{};
    float ambient_radius=0,ambient_volume=0;

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
    VkBuffer perf_buffer=VK_NULL_HANDLE;
    VkDeviceMemory perf_memory=VK_NULL_HANDLE;
    void* perf_mapped=nullptr;
    VkBuffer spell_particle_buffer=VK_NULL_HANDLE;
    VkDeviceMemory spell_particle_memory=VK_NULL_HANDLE;
    void* spell_particle_mapped=nullptr;
    VkBuffer candle_particle_buffer=VK_NULL_HANDLE;
    VkDeviceMemory candle_particle_memory=VK_NULL_HANDLE;
    void* candle_particle_mapped=nullptr;
    std::uint32_t candle_particle_capacity=0;
    unsigned perf_count=0;
    VkImage texture_image = VK_NULL_HANDLE;
    VkDeviceMemory texture_memory = VK_NULL_HANDLE;
    VkImageView texture_view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImage lightmap_image = VK_NULL_HANDLE;
    VkDeviceMemory lightmap_memory = VK_NULL_HANDLE;
    VkImageView lightmap_view = VK_NULL_HANDLE;
    VkSampler lightmap_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline mover_pipeline = VK_NULL_HANDLE;
    VkPipeline death_pipeline = VK_NULL_HANDLE;
    VkPipeline frontend_pipeline = VK_NULL_HANDLE;
    VkPipeline ghost_pipeline = VK_NULL_HANDLE;
    VkBuffer wand_vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory wand_vertex_memory = VK_NULL_HANDLE;
    VkPipelineLayout wand_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline wand_pipeline = VK_NULL_HANDLE;
    VkPipeline effect_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout particle_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline particle_pipeline = VK_NULL_HANDLE;
    VkBuffer guide_vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory guide_vertex_memory = VK_NULL_HANDLE;
    VkBuffer glow_vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory glow_vertex_memory = VK_NULL_HANDLE;
    VkBuffer particle_vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory particle_vertex_memory = VK_NULL_HANDLE;
};

QuestScene::QuestScene() : state_(std::make_unique<State>()) {}

QuestScene::~QuestScene() {
    DestroyGpu();
}

bool QuestScene::LoadFromOwnedData(const std::filesystem::path& data_root,const std::filesystem::path& save_root,unsigned map_id) {
    wand::Hp1PackageReadScope package_reads;
    // Only ANativeActivity private storage. Never derive saves from owned data.
    const auto private_save=save_root.lexically_normal().generic_string();
    if(private_save!="/data/user/0/io.github.hpvr.quest/files/SaveGames" &&
       private_save!="/data/data/io.github.hpvr.quest/files/SaveGames")return false;
    State& state = *state_;
    const auto* descriptor=FindQuestMap(map_id);
    if(!descriptor)return false;
    if (IsLoaded()) {
        return true;
    }
    state.map_id=map_id;state.harry_actor=map_id==0?kHarryActorReference:map_id==1?1102:map_id==3?346:75;
    SceneLoadTrace load_trace(save_root,map_id);
    const std::filesystem::path map_package = data_root / descriptor->package_path;
    try {
        hpvr_hp1_player_start_report player_start{};
        const std::string map_utf8 = map_package.string();
        const std::uint32_t player_status = hpvr_hp1_load_player_start_utf8(
            map_utf8.c_str(), kMetersPerUnrealUnit, 0, &player_start);
        if (player_status != HPVR_HP1_PROFILE_OK ||
            player_start.status != HPVR_HP1_PROFILE_OK ||
            player_start.abi_version != HPVR_HP1_PLAYER_START_ABI_VERSION ||
            player_start.location_serialized == 0 ||
            player_start.rotation_units[0] != 0 ||
            player_start.rotation_units[2] != 0) {
            HPVR_LOGE(
                "[hpvr.quest.scene.data] status=PLAYER_START_REJECTED "
                "result=%u report=%u error=%s",
                player_status, player_start.status, player_start.error);
            return false;
        }

        const float player_start_yaw =
            static_cast<float>(player_start.rotation_units[1]) * kTau /
            65536.0F;
        WorldMetadata world;
        if (!LoadWorldMetadata(data_root, map_package, player_start,
                               player_start_yaw, &world)) {
            return false;
        }
        PreparedGeometry geometry;
        bool prepared=false;
        const auto cache_path=data_root/"Cache/Scenes"/("map-"+std::to_string(map_id)+".hpvc");
        std::error_code cache_error;
        if(std::filesystem::is_regular_file(cache_path,cache_error)){
            load_trace.Stage("ASSET_CACHE_VALIDATE_BEGIN");
            try{
                const auto fingerprint=cache::ComputeSourceFingerprint(data_root,map_id);
                cache::Reader reader(cache_path,map_id,fingerprint);
                const auto reserve=prepared_codec::kMaxFrontendVertexReserve+((map_id==1||map_id==3)?kPreparedGnomeVertexReserve:0);
                auto candidate=ReadPreparedGeometry(reader,reserve);
                reader.Finish();
                if(!ValidatePreparedGeometry(candidate))throw std::runtime_error("invalid prepared geometry layout");
                geometry=std::move(candidate);prepared=true;
                HPVR_LOGI("[hpvr.quest.scene.prepared] status=HIT map=%u schema=1 cook=%u vertices=%zu",map_id,cache::CookRevision(map_id),geometry.vertices.size());
                load_trace.Stage("ASSET_CACHE_READY");
            }catch(const std::exception& e){
                HPVR_LOGE("[hpvr.quest.scene.prepared] status=REBUILD map=%u reason=%s",map_id,e.what());
                load_trace.Stage("ASSET_CACHE_REJECTED_REBUILD");
            }
        }
        if(!prepared){
            if(!PrepareGeometryFromOwnedData(data_root,map_id,player_start,player_start_yaw,world,geometry,
                [&](const char* stage){load_trace.Stage(stage);}))return false;
            HPVR_LOGI("[hpvr.quest.scene.prepared] status=RUNTIME_PREPARED map=%u",map_id);
        }
        const auto quest_census=wand::inspect_hp1_actor_visuals(map_package);
        GridVisualRestoreStats grid_visuals;
        if(!RestorePreparedGridVisuals(geometry,data_root,map_package,quest_census,&grid_visuals)){
            HPVR_LOGE("[hpvr.quest.grid.visual] status=REJECTED");return false;
        }
        HPVR_LOGI("[hpvr.quest.grid.visual] status=READY movers=%u vertices=%u new_layers=%u",
            grid_visuals.movers,grid_visuals.vertices,grid_visuals.texture_layers_added);
        if(map_id==1||map_id==3){
            if(!prepared)geometry.vertices.reserve(geometry.vertices.size()+prepared_codec::kMaxFrontendVertexReserve+kPreparedGnomeVertexReserve);
            const auto clips=RestorePreparedGnomeClips(geometry,data_root);
            if(!clips.valid){HPVR_LOGE("[hpvr.quest.gnomes.clips] status=REJECTED reason=%s",clips.error);return false;}
            HPVR_LOGI("[hpvr.quest.gnomes.clips] status=READY actors=%zu clips=%zu vertices=%zu",clips.actors,clips.clips,clips.vertices);
            const auto dark=wand::repair_hp1_bsp_dark_lightmaps(map_package,kMaximumTriangles,
                geometry.decoded_lightmaps,geometry.lightmap_width,geometry.lightmap_height,geometry.lightmaps);
            if(dark.status!=wand::Hp1ProfileStatus::ok){HPVR_LOGE("[hpvr.quest.darklight] status=REJECTED reason=%s",dark.error.c_str());return false;}
            HPVR_LOGI("[hpvr.quest.darklight] status=READY lights=%zu tiles=%zu texels=%zu staged_bytes=%zu",
                dark.dark_light_actor_count,dark.affected_lightmaps,dark.changed_texels,dark.staged_bytes);
        }
        const auto prop_restore=RestorePreparedPropOrientations(geometry,quest_census,player_start,player_start_yaw);
        if(!prop_restore.valid){HPVR_LOGE("[hpvr.quest.props.orientation] status=REJECTED reason=%s",prop_restore.error);return false;}
        HPVR_LOGI("[hpvr.quest.props.orientation] status=RESTORED props=%zu vertices=%zu",prop_restore.props,prop_restore.vertices);
        const auto candle_restore=RestorePreparedCandleFixtures(geometry);
        HPVR_LOGI("[hpvr.quest.fixtures] restored=%zu removed_flame_faces=%zu wick_anchors=%zu",candle_restore.fixtures,candle_restore.removed_faces,candle_restore.wick_anchors);
        state.ambient_emitters=RestorePreparedAmbientParticles(geometry,quest_census,player_start,player_start_yaw);
        HPVR_LOGI("[hpvr.quest.ambient] blue_emitters=%zu",state.ambient_emitters.size());
        if(map_id==3){state.mirrors=FindMirrorSurfaces(geometry.vertices,geometry.map_vertices);BindMirrorMovers(state.mirrors,geometry.doors);}
        HPVR_LOGI("[hpvr.quest.mirror] surfaces=%zu",state.mirrors.size());
        auto loaded_vertices=std::move(geometry.vertices);
        if(map_id==3)AppendWaterSurfaceGeometry(state.mirrors,loaded_vertices);
        auto loaded_textures=std::move(geometry.textures);
        auto loaded_collision_triangles=std::move(geometry.collision);
        auto loaded_doors=std::move(geometry.doors);
        auto loaded_character_draws=std::move(geometry.characters);
        if(map_id==2){
            const auto player=std::ranges::find_if(loaded_character_draws,[](const auto& draw){return draw.player;});
            if(player==loaded_character_draws.end()||!BuildBroomAvatar(data_root,loaded_vertices,*player,state.broom_avatar))return false;
            HPVR_LOGI("[hpvr.quest.broom.avatar] body=%d vertices=%u frames=%u hidden_triangles=%u broom_triangles=%u",
                !state.broom_avatar.broom_only,state.broom_avatar.vertex_count,state.broom_avatar.frame_count,
                state.broom_avatar.removed_triangles,state.broom_avatar.broom_triangles);
        }
        auto loaded_texture_layers=geometry.texture_layers;
        if(map_id==3&&!LoadLockParticles(data_root,quest_census,player_start,player_start_yaw,
            geometry.texture_width,geometry.texture_height,loaded_textures,loaded_texture_layers,
            state.lock_texture_layer,state.lock_emitters))return false;
        if(map_id==3&&!LoadWingFeatherTexture(data_root,geometry.texture_width,geometry.texture_height,
            loaded_textures,loaded_texture_layers,state.feather_texture_layer))return false;
        if(map_id==2&&!LoadBroomVisuals(data_root,geometry.texture_width,geometry.texture_height,
            loaded_textures,loaded_texture_layers,state.broom_visuals))return false;
        const auto map_vertex_count=geometry.map_vertices;
        float minimum_light=1.0F,maximum_light=0.0F;
        double accumulated_light=0.0;
        std::size_t shadow_vertices=0,highlight_vertices=0;
        for(std::uint32_t i=0;i<map_vertex_count;++i){
            const float light=PackedLightingLuminance(loaded_vertices[i].packed_light);
            minimum_light=std::min(minimum_light,light);maximum_light=std::max(maximum_light,light);
            accumulated_light+=light;shadow_vertices+=light<0.32F;highlight_vertices+=light>0.78F;
        }
        HPVR_LOGI("[hpvr.quest.lighting] status=CONTRAST_READY lights=%zu luminance_min=%.3f average=%.3f max=%.3f shadow_vertices=%zu highlight_vertices=%zu",
            world.lights.size(),minimum_light,accumulated_light/std::max(map_vertex_count,1U),maximum_light,shadow_vertices,highlight_vertices);
        const auto loaded_fixture_vertex_count=geometry.fixture_vertices;
        const auto loaded_fixture_actor_count=geometry.fixture_actors;
        const auto loaded_character_frame_vertex_count=geometry.character_frame_vertices;
        const auto loaded_animation_frame_count=geometry.animation_frames;
        QuestSpellTargets loaded_spell_targets;
        if(!loaded_spell_targets.SetTargets(geometry.targets))return false;
        state.beans=std::move(geometry.beans);state.knights=std::move(geometry.knights);
        state.challenge.props=std::move(geometry.challenge_props);
        state.prop_aim_triangles=std::move(geometry.prop_aim);
        world.flames=std::move(geometry.flames);world.glows=std::move(geometry.glows);
        wand::Hp1TexturedBspScene scene;
        scene.texture_layer_width=geometry.texture_width;scene.texture_layer_height=geometry.texture_height;
        scene.lightmap_width=geometry.lightmap_width;scene.lightmap_height=geometry.lightmap_height;
        scene.lightmap_rgba8=std::move(geometry.lightmaps);
        scene.decoded_lightmap_count=geometry.decoded_lightmaps;
        scene.decoded_texture_count=geometry.decoded_textures;scene.fallback_material_count=geometry.fallback_materials;
        std::uint32_t loaded_fire_texture_layer = 0;
        if (!AppendOwnedFireTexture(
                data_root, scene.texture_layer_width,
                scene.texture_layer_height, &loaded_textures,
                &loaded_texture_layers, &loaded_fire_texture_layer)) {
            return false;
        }
        std::vector<WandGpuVertex> loaded_wand;
        if (!LoadOwnedWand(data_root, &loaded_wand)) {
            return false;
        }
        load_trace.Stage("PICKUPS_AND_WAND_READY");
        const auto symbol=wand::load_hp1_spell_profile(data_root/"system/HPBase.u",data_root/"Maps/Lev_Tut1.unr","FlipPattern","spellFlip");
        if(symbol.status!=wand::Hp1ProfileStatus::ok||symbol.template_points.size()<2)return false;
        std::vector<std::array<float,2>> marker_pattern;
        for(const auto& point:symbol.template_points)marker_pattern.push_back({point.x,point.y});
        state.target_marker=BuildTargetMarkerBatch(marker_pattern);
        if(map_id==3)for(unsigned i=0;i<state.charms_markers.size();++i){
            const auto profile=wand::load_hp1_spell_profile(data_root/"system/HPBase.u",data_root/"Maps/Lev_Tut3.unr",
                i?"LevPattern":"AlohoPattern",i?"SPELLLEV":"spellAloho");
            if(profile.status!=wand::Hp1ProfileStatus::ok)return false;
            std::vector<std::array<float,2>> points;for(const auto& point:profile.template_points)points.push_back({point.x,point.y});
            state.charms_markers[i]=BuildTargetMarkerBatch(points);if(!state.charms_markers[i].valid())return false;
        }
        const auto sparkle=wand::load_hp1_p8_texture(data_root/"system/HPParticle.u",3);
        if(!state.target_marker.valid()||sparkle.status!=wand::Hp1ProfileStatus::ok||
            sparkle.object_name!="Sparkle_3"||sparkle.mips.empty()||
            sparkle.mips.front().width!=32||sparkle.mips.front().height!=32)return false;
        const auto marker_atlas=BuildTargetMarkerAtlas(sparkle.rgba8,32,32);
        if(marker_atlas.empty()||scene.texture_layer_width!=kTargetMarkerAtlasSize||
            scene.texture_layer_height!=kTargetMarkerAtlasSize||loaded_texture_layers>=kMaximumCombinedTextureLayers)return false;
        state.target_marker_texture_layer=loaded_texture_layers++;
        loaded_textures.insert(loaded_textures.end(),marker_atlas.begin(),marker_atlas.end());
        HPVR_LOGI("[hpvr.quest.target] source=HPParticle.Les_SpellShape texture=Sparkle_3 pattern=FlipPattern particles=%zu draws_per_eye=1",
            kTargetMarkerParticles);
        if (!LoadFrontAssets(data_root,&state.frontend.assets,map_id)) {
            HPVR_LOGE("[hpvr.quest.frontend] status=ASSETS_FAILED error=%s",state.frontend.assets.error.c_str());
            return false;
        }
        state.smoke_texture_layer=loaded_texture_layers+state.frontend.assets.smoke;
        if(scene.texture_layer_width!=256 || scene.texture_layer_height!=256 ||
            !AppendFrontGeometry(state.frontend,loaded_vertices,loaded_textures,loaded_texture_layers,
                state.front_draws,state.front_vertex_count))return false;
        state.frontend.saves=save_root;
        state.frontend.progress.map_id=map_id;
        state.frontend.vr=ReadVrSettings(save_root.parent_path());
        state.frontend.RefreshSlots();
        load_trace.Stage("FRONTEND_READY_AUDIO_BEGIN");
        auto all_dialogue=world.cutscene_dialogue;
        for(const auto& page:state.frontend.assets.story)all_dialogue.push_back(page.voice);
        for(std::size_t i=0;i<state.frontend.assets.gameplay_audio.size();++i)
            if(i!=1)all_dialogue.push_back(state.frontend.assets.gameplay_audio[i]);
        if (!state.audio.Configure(world.ambient_sound,
                                   world.wand_trace_sound,
                                   world.wand_start_sound,
                                   world.spell_cast_sound,
                                   world.incantation_sound,
                                   world.spell_hit_sound,
                                   state.frontend.assets.gameplay_audio.at(1),
                                   all_dialogue,
                                   data_root / "Cache/Audio")) {
            HPVR_LOGE("[hpvr.quest.audio] status=CONFIGURE_REJECTED");
            return false;
        }
        load_trace.Stage("DIALOGUE_READY_MUSIC_BEGIN");
        state.ambient_position=world.ambient_position;state.ambient_radius=world.ambient_radius;state.ambient_volume=world.ambient_volume;
        state.audio.SetAmbientLoopGain(map_id==3?0.0F:1.0F);
        if(!state.audio.ConfigureMusic(state.frontend.assets.music,data_root/"Cache/Audio"))return false;
        load_trace.Stage("MUSIC_READY");
        state.frog_sound=state.audio.DialogueClipCount();
        if(!state.audio.ConfigureTutorialFrog(state.frontend.assets.frog_pickup))return false;
        const auto card_sound=GameplayDialogueIndex(state.frontend.assets,"pickup_wizardcard2");
        if(!card_sound)return false;state.card_sound=*card_sound;
        const auto star_sound=GameplayDialogueIndex(state.frontend.assets,"pickup_star");
        if(!star_sound)return false;state.star_sound=*star_sound;
        const auto bean_sound=GameplayDialogueIndex(state.frontend.assets,"pickup11");
        if(!bean_sound||!state.audio.ConfigureBeanPickup(*bean_sound))return false;
        HPVR_LOGI("[hpvr.quest.audio.pickup] sound=pickup11 channel=DEDICATED_RETRIGGER");
        state.audio.SelectMusic(0);
        load_trace.Stage("AUDIO_READY");
        HPVR_LOGI("[hpvr.quest.frontend] status=READY story_pages=14 save_slots=3 start=MAIN_MENU spell_lock=STORY");
        state.vertices = std::move(loaded_vertices);
        state.doors = std::move(loaded_doors);
        const auto challenge_topology = (map_id == 1 || map_id == 3)
            ? wand::load_hp1_bsp_topology(map_package) : wand::Hp1BspTopology{};
        const wand::Hp1ChallengeAbyssLighting abyss_lighting(challenge_topology, quest_census, map_id == 1);
        if (abyss_lighting.Enabled()) {
            auto footprints = abyss_lighting.RectangularFootprints();
            for (auto& rectangle : footprints) for (auto& value : rectangle) value *= kMetersPerUnrealUnit;
            // The original moving-column tops / walkable rim are at1008UU.
            //728UU is the lethal portal, not the visible top of the abyss.
            state.abyss_fog = MakeAbyssFogVolume(footprints, 1008.0F * kMetersPerUnrealUnit, 0.55F);
            const float c = std::cos(player_start_yaw), s = std::sin(player_start_yaw);
            state.abyss_scene_to_source = {-s,c,0,0, 0,0,1,0, -c,-s,0,0,
                -player_start.position_m[2],player_start.position_m[0],player_start.position_m[1],1};
            if (state.abyss_fog.count != 12) return false;
            HPVR_LOGI("[hpvr.quest.abyss.volume] source=AUTHORED_PORTAL_UNION rim=1008 rectangles=%u model=ANALYTIC_HEIGHT extra_passes=0",
                state.abyss_fog.count);
        }
        const auto source_point = [&](const GpuVertex& vertex) {
            auto point = RotateYaw({vertex.position[0], vertex.position[1], vertex.position[2]}, -player_start_yaw);
            for (std::size_t i = 0; i < 3; ++i) point[i] += player_start.position_m[i];
            return wand::Hp1BspVector{-point[2] / kMetersPerUnrealUnit,
                                      point[0] / kMetersPerUnrealUnit,
                                      point[1] / kMetersPerUnrealUnit};
        };
        state.dark_lights.clear();
        std::vector<SceneLight> positive_lights;
        for (const auto& light : world.lights) {
            if (light.intensity < 0) state.dark_lights.push_back(light);
            else positive_lights.push_back(light);
        }
        std::size_t relit_mover_vertices = 0, relit_fixture_vertices = 0;
        if (!state.dark_lights.empty()) {
            const auto fixture_end = std::size_t(map_vertex_count) + loaded_fixture_vertex_count;
            if (fixture_end > state.vertices.size()) return false;
            for (std::size_t i = map_vertex_count; i < fixture_end; ++i) {
                auto& vertex = state.vertices[i];
                const std::array<float, 3> position{vertex.position[0], vertex.position[1], vertex.position[2]};
                const bool affected = std::ranges::any_of(state.dark_lights, [&](const auto& light) {
                    const auto delta = SubtractVector(position, light.position);
                    return DotVector(delta, delta) < light.radius * light.radius;
                });
                if (affected && !vertex.has_lightmap) {
                    vertex.packed_light = PackAuthoredLighting(position, world.lights);
                    ++relit_fixture_vertices;
                }
            }
        }
        for(auto& door:state.doors){
            const auto all=std::span<const GpuVertex>(state.vertices);
            if(door.first_vertex>all.size()||door.vertex_count>all.size()-door.first_vertex)return false;
            door.two_sided=mover_visibility::MoverNeedsTwoSided(all.subspan(door.first_vertex,door.vertex_count));
            if (!state.dark_lights.empty()) {
                // C45 caches treated bDarkLight as positive. Rebuild only this
                // small immutable base; the shader subtracts darkness at the
                // brush's current world position, not its saved starting height.
                for (std::size_t i = door.first_vertex; i < std::size_t(door.first_vertex) + door.vertex_count; ++i) {
                    auto& vertex = state.vertices[i];
                    vertex.packed_light = PackAuthoredLighting(
                        {vertex.position[0], vertex.position[1], vertex.position[2]}, positive_lights);
                    ++relit_mover_vertices;
                }
            }
        }
        HPVR_LOGI("[hpvr.quest.darklight.movers] lights=%zu base_vertices=%zu fixtures=%zu dynamic=VERTEX_RADIAL extra_passes=0",
            state.dark_lights.size(), relit_mover_vertices, relit_fixture_vertices);
        std::size_t abyss_fixture_vertices = 0, abyss_mover_vertices = 0;
        if (abyss_lighting.Enabled()) {
            const auto fixture_end = std::size_t(map_vertex_count) + loaded_fixture_vertex_count;
            if (fixture_end > state.vertices.size()) return false;
            for (std::size_t i = map_vertex_count; i < fixture_end; ++i) {
                auto& vertex = state.vertices[i];
                if (vertex.has_lightmap) continue;
                const auto light = abyss_lighting.AttenuatePacked(vertex.packed_light, source_point(vertex));
                abyss_fixture_vertices += light != vertex.packed_light ? 1U : 0U;
                vertex.packed_light = light;
            }
            // The authored moving columns start above this volume (bottom880,
            // fade top792UU). Do not bake darkness into a moving brush's base.
            for (const auto& door : state.doors)
                for (std::size_t i = door.first_vertex; i < std::size_t(door.first_vertex) + door.vertex_count; ++i)
                    abyss_mover_vertices += abyss_lighting.Visibility(source_point(state.vertices[i])) < 1.0F ? 1U : 0U;
            HPVR_LOGI("[hpvr.quest.abyss] zone=3 portal=%.1f fade_top=%.1f polygons=%zu fixtures=%zu mover_vertices_in_band=%zu extra_passes=0",
                abyss_lighting.PortalHeight(), abyss_lighting.FadeTop(), abyss_lighting.PolygonCount(),
                abyss_fixture_vertices, abyss_mover_vertices);
        }
        for(const auto& door:state.doors)if(door.tag=="fgsec1"||door.tag=="fgsec2"){
            std::vector<GpuVertex> solid(state.vertices.begin()+door.first_vertex,
                state.vertices.begin()+door.first_vertex+door.vertex_count);
            auto extra=BuildCollisionTriangles(solid,static_cast<std::uint32_t>(solid.size()));
            auto& walls=door.tag=="fgsec1"?state.closed_secret_wall:state.closed_reward_wall;
            walls.insert(walls.end(),extra.begin(),extra.end());
        }
        state.children = std::move(world.children);
        state.map_vertex_count = map_vertex_count;
        state.fixture_vertex_count = loaded_fixture_vertex_count;
        state.fixture_actor_count = loaded_fixture_actor_count;
        state.character_frame_vertex_count =
            loaded_character_frame_vertex_count;
        state.animation_frame_count = loaded_animation_frame_count;
        state.current_animation_frame = 0;
        state.animation_elapsed_seconds = 0.0F;
        state.effects_clock.Reset();
        state.wand_vertices = std::move(loaded_wand);
        state.glow_vertices = BuildRadialGlowDiscVertices();
        state.character_draws = std::move(loaded_character_draws);
        state.spell_targets = std::move(loaded_spell_targets);
        state.collision_triangles =
            std::move(loaded_collision_triangles);
        state.script_triggers = std::move(world.triggers);
        state.flames = std::move(world.flames);
        state.glows = std::move(world.glows);
        state.script_actor_count = world.actor_count;
        state.script_event_edge_count = world.event_edge_count;
        state.authored_light_count = world.lights.size();
        state.intro_cutscene = std::move(world.intro_cutscene);
        state.initial_intro=state.intro_cutscene;
        if(map_id==1||map_id==3){
            if(!LoadChallengeMetadata(quest_census,player_start,player_start_yaw,state.challenge,map_id==3?23:15))return false;
            if(map_id==1)SetBridgeProfessor(state.character_draws,false);
            if(!state.challenge.zones.Load(challenge_topology,quest_census))return false;
            state.challenge.source_origin={player_start.position_m[0],player_start.position_m[1],player_start.position_m[2]};
            state.challenge.source_yaw=player_start_yaw;
            state.challenge.collision_base=state.collision_triangles.size();
            for(const auto& door:state.doors){
                std::vector<GpuVertex> mesh(state.vertices.begin()+door.first_vertex,state.vertices.begin()+door.first_vertex+door.vertex_count);
                state.challenge.mover_triangles.push_back(BuildCollisionTriangles(mesh,door.vertex_count));
            }
            state.intro_cutscene.playing=false;
            RebuildChallengeCollision();
            if(map_id==3&&!LoadCharmsMetadata(data_root,quest_census,player_start,player_start_yaw,
                state.challenge.props,state.charms,&state.vertices))return false;
            if(map_id==3)BindCharmsKnightTargets(state.challenge,quest_census,player_start,player_start_yaw);
            if(map_id==3)RebuildChallengeCollision();
        }else if(map_id==2){
            if(!LoadBroomMetadata(data_root,quest_census,player_start,player_start_yaw,state.broom)||
               !LoadChallengeMetadata(quest_census,player_start,player_start_yaw,state.challenge,14))return false;
            for(const auto& a:state.character_draws)if(a.actor_reference==state.broom.lesson.player_reference){
                state.broom.start_head=a.base_origin;state.broom.start_head[1]+=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;
                state.broom.start_yaw=a.base_yaw;
            }
            state.challenge.collision_base=state.collision_triangles.size();
            for(const auto& door:state.doors){
                std::vector<GpuVertex> mesh(state.vertices.begin()+door.first_vertex,state.vertices.begin()+door.first_vertex+door.vertex_count);
                state.challenge.mover_triangles.push_back(door.tag=="cammover"?std::vector<CollisionTriangle>{}:BuildCollisionTriangles(mesh,door.vertex_count));
            }
            state.intro_cutscene.playing=false;
            RebuildChallengeCollision();
        }else if(map_id==0){
        for(const auto& actor:quest_census.actors){
            if(actor.actor_reference==1858)state.peeves_trigger=ActorLocalPosition(actor,player_start,player_start_yaw);
            if(actor.actor_reference==861)state.peeves_home=ActorLocalPosition(actor,player_start,player_start_yaw);
            if(actor.actor_reference==1101)state.peeves_retreat=ActorLocalPosition(actor,player_start,player_start_yaw);
            if(actor.actor_reference==866)state.peeves_path_a=ActorLocalPosition(actor,player_start,player_start_yaw);
            if(actor.actor_reference==886)state.peeves_path_b=ActorLocalPosition(actor,player_start,player_start_yaw);
        }
        if(!LoadIntroCutscene(quest_census,player_start,player_start_yaw,&state.ron_intro,"cutscene51"))return false;
        if(!LoadIntroCutscene(quest_census,player_start,player_start_yaw,&state.ron_lead,"cutscene0"))return false;
        MakeRonLead(state.ron_lead);
        if(!LoadIntroCutscene(quest_census,player_start,player_start_yaw,&state.twins_transfer,"cutscene6")||
           !LoadIntroCutscene(quest_census,player_start,player_start_yaw,&state.peeves_departure,"cutscene5"))return false;
        PrepareTwinsTransfer(state.twins_transfer);
        PreparePeevesDeparture(state.peeves_departure);
        if(!LoadIntroCutscene(quest_census,player_start,player_start_yaw,&state.twins_intro,"cutscene52") ||
           !LoadIntroCutscene(quest_census,player_start,player_start_yaw,&state.next_room,"cutscene54")||
           !LoadIntroCutscene(quest_census,player_start,player_start_yaw,&state.jump_finish,"cutscene55"))return false;
        for(unsigned i=0;i<4;++i){
            constexpr std::array<const char*,4> names{"cutscene56","cutscene1","cutscene58","cutscene59"};
            if(!LoadIntroCutscene(quest_census,player_start,player_start_yaw,&state.story_encounters[i],names[i]))return false;
        }
        state.intro_cutscene.playing=false;
        if(!LoadIntroCutscene(quest_census,player_start,player_start_yaw,&state.card_scene,"cutscene3")||
           !LoadIntroCutscene(quest_census,player_start,player_start_yaw,&state.lesson_exit,"cutscene60"))return false;
        std::erase_if(state.card_scene.tracks,[](const auto& t){return t.cast_slot>=5;});
        for(auto& t:state.card_scene.tracks)for(auto& command:t.commands){
            if(AsciiFold(command)=="teleport swaplocfred")command="MoveTo NewFredLoc";
            if(AsciiFold(command)=="teleport swaplocgeorge")command="MoveTo NewGeorgeLoc";
        }
        }
        state.texture_rgba8 = std::move(loaded_textures);
        state.texture_width = scene.texture_layer_width;
        state.texture_height = scene.texture_layer_height;
        state.texture_layers = loaded_texture_layers;
        state.fire_texture_layer = loaded_fire_texture_layer;
        state.lightmap_rgba8 = std::move(scene.lightmap_rgba8);
        state.lightmap_width = scene.lightmap_width;
        state.lightmap_height = scene.lightmap_height;
        state.decoded_lightmaps = scene.decoded_lightmap_count;
        state.decoded_textures = scene.decoded_texture_count;
        state.fallback_materials = scene.fallback_material_count;
        state.player_start_yaw = player_start_yaw;
        HPVR_LOGI(
            "[hpvr.quest.scene.data] status=READY vertices=%zu triangles=%zu "
            "textures=%zu fallback_materials=%zu layers=%u size=%ux%u "
            "rgba_bytes=%zu lightmaps=%zu lightmap_size=%ux%u "
            "lightmap_bytes=%zu player_start=%s yaw=%.6f scale=%.5f "
            "map_vertices=%u fixtures=%zu fixture_vertices=%u flames=%zu "
            "glows=%zu "
            "characters=%zu animation_frames=%u "
            "character_frame_vertices=%u collision_triangles=%zu "
            "capsule_radius_m=%.3f capsule_half_height_m=%.3f "
            "script_actors=%zu event_edges=%zu triggers=%zu lights=%zu "
            "audio_configured=%s cutscene=CutScene4 cutscene_tracks=%zu "
            "cutscene_locations=%zu dialogue_clips=%zu",
            state.vertices.size(), state.vertices.size() / 3,
            state.decoded_textures, state.fallback_materials,
            state.texture_layers, state.texture_width, state.texture_height,
            state.texture_rgba8.size(), state.decoded_lightmaps,
            state.lightmap_width, state.lightmap_height,
            state.lightmap_rgba8.size(), player_start.object_name,
            state.player_start_yaw, kMetersPerUnrealUnit,
            state.map_vertex_count, state.fixture_actor_count,
            state.fixture_vertex_count, state.flames.size(),
            state.glows.size(),
            state.character_draws.size(),
            state.animation_frame_count,
            state.character_frame_vertex_count,
            state.collision_triangles.size(),
            kPlayerCapsuleRadiusMeters,
            kPlayerCapsuleHalfHeightMeters,
            state.script_actor_count, state.script_event_edge_count,
            state.script_triggers.size(), state.authored_light_count,
            state.audio.IsConfigured() ? "YES" : "NO",
            state.intro_cutscene.tracks.size(),
            state.intro_cutscene.locations.size(),
            state.audio.DialogueClipCount());
        const auto read_stats=package_reads.stats();
        if(!IsLoaded()){
            load_trace.Stage("CPU_LAYOUT_REJECTED");
            HPVR_LOGE("[hpvr.quest.scene.data] status=CPU_LAYOUT_REJECTED map=%u characters=%zu doors=%zu scenes=%zu vertices=%zu layers=%u",
                map_id,state.character_draws.size(),state.doors.size(),state.challenge.scenes.size(),state.vertices.size(),state.texture_layers);
            return false;
        }
        load_trace.Ready();
        HPVR_LOGI("[hpvr.quest.scene.cache] map=%u reads=%zu hits=%zu released_bytes=%zu",map_id,
            read_stats.reads,read_stats.hits,read_stats.retained_bytes);
        return true;
    } catch (const std::exception& error) {
        HPVR_LOGE("[hpvr.quest.scene.data] status=EXCEPTION error=%s",
                  error.what());
    } catch (...) {
        HPVR_LOGE("[hpvr.quest.scene.data] status=UNKNOWN_EXCEPTION");
    }
    return false;
}

bool QuestScene::CreateGpu(const VkPhysicalDevice physical_device,
                           const VkDevice device,
                           const VkQueue queue,
                           const std::uint32_t queue_family,
                           const VkRenderPass render_pass,unsigned width,unsigned height,VkFormat color_format,VkFormat depth_format,unsigned frame_count) {
    State& state = *state_;
    if (!IsLoaded()) {
        HPVR_LOGE("[hpvr.quest.scene.gpu] status=UNLOADED_SCENE_REJECTED");
        return false;
    }
    if (IsGpuReady()) {
        return true;
    }
    state.physical_device = physical_device;
    state.device = device;
    state.queue = queue;
    state.queue_family = queue_family;
    const auto gpu_stage=[&](const char* name){SceneLoadTrace::Append(state.frontend.saves,state.map_id,name);};
    if(!state.reflections.Create(physical_device,device,queue,queue_family,width,height,color_format,depth_format)){DestroyGpu();return false;}
    if(!state.mirror_target.Create(physical_device,device,state.map_id==3?width:1,state.map_id==3?height:1,color_format,depth_format,static_cast<unsigned>(state.mirrors.size()))){DestroyGpu();return false;}
    gpu_stage("GPU_REFLECTION_RESOURCES_READY");

    VkPhysicalDeviceProperties device_properties{};
    vkGetPhysicalDeviceProperties(physical_device, &device_properties);
    const auto uniform_alignment = std::max<VkDeviceSize>(1, device_properties.limits.minUniformBufferOffsetAlignment);
    const auto uniform_stride = (sizeof(AbyssFogUniform) + uniform_alignment - 1) / uniform_alignment * uniform_alignment;
    if (!frame_count || frame_count > 32 || uniform_stride * (frame_count * 2U + 1U) > UINT32_MAX ||
        sizeof(AbyssFogUniform) > device_properties.limits.maxUniformBufferRange) { DestroyGpu(); return false; }
    state.abyss_uniform_stride = static_cast<std::uint32_t>(uniform_stride);
    state.abyss_frame_slots = frame_count * 2U;
    state.abyss_disabled_offset = state.abyss_uniform_stride * state.abyss_frame_slots;
    const auto uniform_bytes = uniform_stride * (state.abyss_frame_slots + 1U);
    if (!CreateBuffer(physical_device, device, uniform_bytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &state.abyss_uniform_buffer, &state.abyss_uniform_memory) ||
        !CheckVk(vkMapMemory(device, state.abyss_uniform_memory, 0, uniform_bytes, 0,
                            &state.abyss_uniform_mapped), "vkMapMemory(abyss uniforms)")) { DestroyGpu(); return false; }
    std::memset(state.abyss_uniform_mapped, 0, static_cast<std::size_t>(uniform_bytes));
    VkFormatProperties texture_format_properties{};
    vkGetPhysicalDeviceFormatProperties(physical_device,
                                        VK_FORMAT_R8G8B8A8_SRGB,
                                        &texture_format_properties);
    constexpr VkFormatFeatureFlags kRequiredTextureFeatures =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (state.texture_width >
            device_properties.limits.maxImageDimension2D ||
        state.texture_height >
            device_properties.limits.maxImageDimension2D ||
        state.lightmap_width >
            device_properties.limits.maxImageDimension2D ||
        state.lightmap_height >
            device_properties.limits.maxImageDimension2D ||
        state.texture_layers > device_properties.limits.maxImageArrayLayers ||
        (texture_format_properties.optimalTilingFeatures &
         kRequiredTextureFeatures) != kRequiredTextureFeatures) {
        HPVR_LOGE(
            "[hpvr.quest.scene.vk] status=TEXTURE_CAPABILITY_REJECTED "
            "size=%ux%u layers=%u max_dimension=%u max_layers=%u "
            "features=0x%x",
            state.texture_width, state.texture_height, state.texture_layers,
            device_properties.limits.maxImageDimension2D,
            device_properties.limits.maxImageArrayLayers,
            texture_format_properties.optimalTilingFeatures);
        DestroyGpu();
        return false;
    }

    const VkDeviceSize vertex_bytes =
        state.vertices.size() * sizeof(GpuVertex);
    if (!CreateBuffer(physical_device, device, vertex_bytes,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &state.vertex_buffer, &state.vertex_memory)) {
        DestroyGpu();
        return false;
    }
    void* mapped_vertices = nullptr;
    if (!CheckVk(vkMapMemory(device, state.vertex_memory, 0, vertex_bytes, 0,
                             &mapped_vertices),
                 "vkMapMemory(vertices)")) {
        DestroyGpu();
        return false;
    }
    std::memcpy(mapped_vertices, state.vertices.data(),
                static_cast<std::size_t>(vertex_bytes));
    vkUnmapMemory(device, state.vertex_memory);
    gpu_stage("GPU_SCENE_VERTICES_READY");
    if(state.map_id==2){
        const auto bytes=state.broom_avatar.vertices.size()*sizeof(GpuVertex);
        void* mapped=nullptr;
        if(!bytes||!CreateBuffer(physical_device,device,bytes,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &state.broom_avatar_buffer,&state.broom_avatar_memory)||
            !CheckVk(vkMapMemory(device,state.broom_avatar_memory,0,bytes,0,&mapped),"vkMapMemory(broom avatar)")){
            DestroyGpu();return false;
        }
        std::memcpy(mapped,state.broom_avatar.vertices.data(),bytes);
        vkUnmapMemory(device,state.broom_avatar_memory);
    }

    const VkDeviceSize wand_vertex_bytes =
        state.wand_vertices.size() * sizeof(WandGpuVertex);
    if (!CreateBuffer(physical_device, device, wand_vertex_bytes,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &state.wand_vertex_buffer, &state.wand_vertex_memory)) {
        DestroyGpu();
        return false;
    }
    void* mapped_wand_vertices = nullptr;
    if (!CheckVk(vkMapMemory(device, state.wand_vertex_memory, 0,
                             wand_vertex_bytes, 0, &mapped_wand_vertices),
                 "vkMapMemory(wand vertices)")) {
        DestroyGpu();
        return false;
    }
    std::memcpy(mapped_wand_vertices, state.wand_vertices.data(),
                static_cast<std::size_t>(wand_vertex_bytes));
    vkUnmapMemory(device, state.wand_vertex_memory);

    const VkDeviceSize guide_vertex_bytes = sizeof(kGuideQuad);
    if (!CreateBuffer(physical_device, device, guide_vertex_bytes,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &state.guide_vertex_buffer,
                      &state.guide_vertex_memory)) {
        DestroyGpu();
        return false;
    }
    void* mapped_guide_vertices = nullptr;
    if (!CheckVk(vkMapMemory(device, state.guide_vertex_memory, 0,
                             guide_vertex_bytes, 0, &mapped_guide_vertices),
                 "vkMapMemory(guide vertices)")) {
        DestroyGpu();
        return false;
    }
    std::memcpy(mapped_guide_vertices, kGuideQuad.data(),
                static_cast<std::size_t>(guide_vertex_bytes));
    vkUnmapMemory(device, state.guide_vertex_memory);

    const VkDeviceSize glow_vertex_bytes =
        state.glow_vertices.size() * sizeof(WandGpuVertex);
    if (!CreateBuffer(physical_device, device, glow_vertex_bytes,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &state.glow_vertex_buffer,
                      &state.glow_vertex_memory)) {
        DestroyGpu();
        return false;
    }
    void* mapped_glow_vertices = nullptr;
    if (!CheckVk(vkMapMemory(device, state.glow_vertex_memory, 0,
                             glow_vertex_bytes, 0, &mapped_glow_vertices),
                 "vkMapMemory(radial glow vertices)")) {
        DestroyGpu();
        return false;
    }
    std::memcpy(mapped_glow_vertices, state.glow_vertices.data(),
                static_cast<std::size_t>(glow_vertex_bytes));
    vkUnmapMemory(device, state.glow_vertex_memory);

    std::size_t marker_vertex_count=state.target_marker.vertices.size();
    for(const auto& marker:state.charms_markers)marker_vertex_count+=marker.vertices.size();
    const VkDeviceSize particle_vertex_bytes = sizeof(kParticleQuad)+marker_vertex_count*sizeof(ParticleGpuVertex);
    if (!CreateBuffer(physical_device, device, particle_vertex_bytes,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &state.particle_vertex_buffer,
                      &state.particle_vertex_memory)) {
        DestroyGpu();
        return false;
    }
    void* mapped_particle_vertices = nullptr;
    if (!CheckVk(vkMapMemory(device, state.particle_vertex_memory, 0,
                             particle_vertex_bytes, 0,
                             &mapped_particle_vertices),
                 "vkMapMemory(particle vertices)")) {
        DestroyGpu();
        return false;
    }
    std::memcpy(mapped_particle_vertices, kParticleQuad.data(),
                sizeof(kParticleQuad));
    auto* marker_vertices=static_cast<ParticleGpuVertex*>(mapped_particle_vertices)+kParticleQuad.size();
    for(std::size_t i=0;i<state.target_marker.vertices.size();++i){
        const auto& v=state.target_marker.vertices[i];
        marker_vertices[i]={{v.position[0],v.position[1],v.position[2]},{v.texture_uv[0],v.texture_uv[1]}};
    }
    marker_vertices+=state.target_marker.vertices.size();
    for(const auto& marker:state.charms_markers)for(const auto& v:marker.vertices)
        *marker_vertices++={{v.position[0],v.position[1],v.position[2]},{v.texture_uv[0],v.texture_uv[1]}};
    vkUnmapMemory(device, state.particle_vertex_memory);

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    const VkDeviceSize texture_bytes = state.texture_rgba8.size();
    if (!CreateBuffer(physical_device, device, texture_bytes,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging_buffer, &staging_memory)) {
        DestroyGpu();
        return false;
    }
    void* mapped_texture = nullptr;
    if (!CheckVk(vkMapMemory(device, staging_memory, 0, texture_bytes, 0,
                             &mapped_texture),
                 "vkMapMemory(texture)")) {
        vkDestroyBuffer(device, staging_buffer, nullptr);
        vkFreeMemory(device, staging_memory, nullptr);
        DestroyGpu();
        return false;
    }
    std::memcpy(mapped_texture, state.texture_rgba8.data(),
                static_cast<std::size_t>(texture_bytes));
    vkUnmapMemory(device, staging_memory);

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    image_info.extent = {state.texture_width, state.texture_height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = state.texture_layers;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!CheckVk(vkCreateImage(device, &image_info, nullptr,
                               &state.texture_image),
                 "vkCreateImage(scene texture)")) {
        vkDestroyBuffer(device, staging_buffer, nullptr);
        vkFreeMemory(device, staging_memory, nullptr);
        DestroyGpu();
        return false;
    }
    VkMemoryRequirements image_requirements{};
    vkGetImageMemoryRequirements(device, state.texture_image,
                                 &image_requirements);
    std::uint32_t image_memory_type = 0;
    if (!FindMemoryType(physical_device, image_requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        &image_memory_type)) {
        HPVR_LOGE("[hpvr.quest.scene.vk] status=IMAGE_MEMORY_TYPE_MISSING");
        vkDestroyBuffer(device, staging_buffer, nullptr);
        vkFreeMemory(device, staging_memory, nullptr);
        DestroyGpu();
        return false;
    }
    VkMemoryAllocateInfo image_allocate{};
    image_allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    image_allocate.allocationSize = image_requirements.size;
    image_allocate.memoryTypeIndex = image_memory_type;
    if (!CheckVk(vkAllocateMemory(device, &image_allocate, nullptr,
                                  &state.texture_memory),
                 "vkAllocateMemory(scene texture)") ||
        !CheckVk(vkBindImageMemory(device, state.texture_image,
                                   state.texture_memory, 0),
                 "vkBindImageMemory(scene texture)")) {
        vkDestroyBuffer(device, staging_buffer, nullptr);
        vkFreeMemory(device, staging_memory, nullptr);
        DestroyGpu();
        return false;
    }

    VkCommandPool upload_pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = queue_family;
    VkCommandBuffer upload_command = VK_NULL_HANDLE;
    VkFence upload_fence = VK_NULL_HANDLE;
    bool upload_ok = CheckVk(vkCreateCommandPool(
                                 device, &pool_info, nullptr, &upload_pool),
                             "vkCreateCommandPool(scene upload)");
    if (upload_ok) {
        VkCommandBufferAllocateInfo command_allocate{};
        command_allocate.sType =
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_allocate.commandPool = upload_pool;
        command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_allocate.commandBufferCount = 1;
        upload_ok = CheckVk(vkAllocateCommandBuffers(
                                device, &command_allocate, &upload_command),
                            "vkAllocateCommandBuffers(scene upload)");
    }
    if (upload_ok) {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        upload_ok = CheckVk(vkBeginCommandBuffer(upload_command, &begin),
                            "vkBeginCommandBuffer(scene upload)");
    }
    if (upload_ok) {
        VkImageMemoryBarrier to_transfer{};
        to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_transfer.srcAccessMask = 0;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.image = state.texture_image;
        to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_transfer.subresourceRange.levelCount = 1;
        to_transfer.subresourceRange.layerCount = state.texture_layers;
        vkCmdPipelineBarrier(upload_command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &to_transfer);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = state.texture_layers;
        copy.imageExtent = {state.texture_width, state.texture_height, 1};
        vkCmdCopyBufferToImage(upload_command, staging_buffer,
                               state.texture_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        VkImageMemoryBarrier to_sample{};
        to_sample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_sample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_sample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        to_sample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_sample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_sample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_sample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_sample.image = state.texture_image;
        to_sample.subresourceRange = to_transfer.subresourceRange;
        vkCmdPipelineBarrier(upload_command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &to_sample);
        upload_ok = CheckVk(vkEndCommandBuffer(upload_command),
                            "vkEndCommandBuffer(scene upload)");
    }
    if (upload_ok) {
        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        upload_ok = CheckVk(vkCreateFence(device, &fence_info, nullptr,
                                          &upload_fence),
                            "vkCreateFence(scene upload)");
    }
    if (upload_ok) {
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &upload_command;
        upload_ok = CheckVk(vkQueueSubmit(queue, 1, &submit, upload_fence),
                            "vkQueueSubmit(scene upload)") &&
                    CheckVk(vkWaitForFences(device, 1, &upload_fence, VK_TRUE,
                                            UINT64_MAX),
                            "vkWaitForFences(scene upload)");
    }
    if (upload_fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, upload_fence, nullptr);
    }
    if (upload_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, upload_pool, nullptr);
    }
    vkDestroyBuffer(device, staging_buffer, nullptr);
    vkFreeMemory(device, staging_memory, nullptr);
    if (!upload_ok) {
        DestroyGpu();
        return false;
    }
    gpu_stage("GPU_TEXTURE_ARRAY_READY");

    VkImageViewCreateInfo image_view_info{};
    image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_info.image = state.texture_image;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    image_view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.layerCount = state.texture_layers;
    if (!CheckVk(vkCreateImageView(device, &image_view_info, nullptr,
                                   &state.texture_view),
                 "vkCreateImageView(scene texture)")) {
        DestroyGpu();
        return false;
    }

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 0.0F;
    if (!CheckVk(vkCreateSampler(device, &sampler_info, nullptr,
                                 &state.sampler),
                 "vkCreateSampler(scene)")) {
        DestroyGpu();
        return false;
    }
    if (!CreateSampledTexture2D(
            physical_device, device, queue, queue_family,
            state.lightmap_rgba8, state.lightmap_width,
            state.lightmap_height, &state.lightmap_image,
            &state.lightmap_memory, &state.lightmap_view,
            &state.lightmap_sampler)) {
        HPVR_LOGE("[hpvr.quest.scene.vk] status=LIGHTMAP_UPLOAD_REJECTED");
        DestroyGpu();
        return false;
    }

    VkDescriptorSetLayoutBinding texture_binding{};
    texture_binding.binding = 0;
    texture_binding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texture_binding.descriptorCount = 1;
    texture_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutBinding lightmap_binding = texture_binding;
    lightmap_binding.binding = 1;
    auto reflection_color=texture_binding;reflection_color.binding=2;
    auto reflection_depth=texture_binding;reflection_depth.binding=3;
    auto reflection_uniform=texture_binding;reflection_uniform.binding=4;reflection_uniform.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    auto abyss_uniform = reflection_uniform;
    abyss_uniform.binding = 5;
    abyss_uniform.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    auto mirror_texture=texture_binding;mirror_texture.binding=6;
    const std::array descriptor_bindings{texture_binding, lightmap_binding,reflection_color,reflection_depth,reflection_uniform,abyss_uniform,mirror_texture};
    VkDescriptorSetLayoutCreateInfo set_layout_info{};
    set_layout_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout_info.bindingCount =
        static_cast<std::uint32_t>(descriptor_bindings.size());
    set_layout_info.pBindings = descriptor_bindings.data();
    if (!CheckVk(vkCreateDescriptorSetLayout(
                     device, &set_layout_info, nullptr,
                     &state.descriptor_set_layout),
                 "vkCreateDescriptorSetLayout(scene)")) {
        DestroyGpu();
        return false;
    }
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 5;
    const std::array<VkDescriptorPoolSize,3> pool_sizes{{pool_size,{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1}}};
    VkDescriptorPoolCreateInfo descriptor_pool_info{};
    descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_info.maxSets = 1;
    descriptor_pool_info.poolSizeCount = pool_sizes.size();
    descriptor_pool_info.pPoolSizes = pool_sizes.data();
    if (!CheckVk(vkCreateDescriptorPool(device, &descriptor_pool_info, nullptr,
                                        &state.descriptor_pool),
                 "vkCreateDescriptorPool(scene)")) {
        DestroyGpu();
        return false;
    }
    VkDescriptorSetAllocateInfo set_allocate{};
    set_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_allocate.descriptorPool = state.descriptor_pool;
    set_allocate.descriptorSetCount = 1;
    set_allocate.pSetLayouts = &state.descriptor_set_layout;
    if (!CheckVk(vkAllocateDescriptorSets(device, &set_allocate,
                                          &state.descriptor_set),
                 "vkAllocateDescriptorSets(scene)")) {
        DestroyGpu();
        return false;
    }
    VkDescriptorImageInfo descriptor_image{};
    descriptor_image.sampler = state.sampler;
    descriptor_image.imageView = state.texture_view;
    descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo lightmap_descriptor_image{};
    lightmap_descriptor_image.sampler = state.lightmap_sampler;
    lightmap_descriptor_image.imageView = state.lightmap_view;
    lightmap_descriptor_image.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::array<VkWriteDescriptorSet, 2> descriptor_writes{};
    for (std::size_t index = 0; index < descriptor_writes.size(); ++index) {
        descriptor_writes[index].sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_writes[index].dstSet = state.descriptor_set;
        descriptor_writes[index].dstBinding =
            static_cast<std::uint32_t>(index);
        descriptor_writes[index].descriptorCount = 1;
        descriptor_writes[index].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    descriptor_writes[0].pImageInfo = &descriptor_image;
    descriptor_writes[1].pImageInfo = &lightmap_descriptor_image;
    vkUpdateDescriptorSets(
        device, static_cast<std::uint32_t>(descriptor_writes.size()),
        descriptor_writes.data(), 0, nullptr);
    state.reflections.Bind(state.descriptor_set);
    state.mirror_target.Bind(state.descriptor_set);
    const VkDescriptorBufferInfo abyss_buffer_info{state.abyss_uniform_buffer, 0, sizeof(AbyssFogUniform)};
    VkWriteDescriptorSet abyss_write{};
    abyss_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    abyss_write.dstSet = state.descriptor_set; abyss_write.dstBinding = 5; abyss_write.descriptorCount = 1;
    abyss_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    abyss_write.pBufferInfo = &abyss_buffer_info;
    vkUpdateDescriptorSets(device, 1, &abyss_write, 0, nullptr);

    gpu_stage("GPU_LIGHTMAP_AND_DESCRIPTORS_READY");
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.size = sizeof(float) * 16 + sizeof(AuthoredDarkLightPush);
    static_assert(sizeof(float) * 16 + sizeof(AuthoredDarkLightPush) == 128);
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &state.descriptor_set_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_range;
    if (!CheckVk(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr,
                                        &state.pipeline_layout),
                 "vkCreatePipelineLayout(scene)")) {
        DestroyGpu();
        return false;
    }

    const VkShaderModule vertex_shader = CreateShaderModule(
        device, kHogwartsVertexSpirv, kHogwartsVertexSpirvSize);
    const VkShaderModule fragment_shader = CreateShaderModule(
        device, kHogwartsFragmentSpirv, kHogwartsFragmentSpirvSize);
    if (vertex_shader == VK_NULL_HANDLE || fragment_shader == VK_NULL_HANDLE) {
        if (fragment_shader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, fragment_shader, nullptr);
        }
        if (vertex_shader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, vertex_shader, nullptr);
        }
        DestroyGpu();
        return false;
    }
    VkPipelineShaderStageCreateInfo shader_stages[2]{};
    shader_stages[0].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vertex_shader;
    shader_stages[0].pName = "main";
    shader_stages[1].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = fragment_shader;
    shader_stages[1].pName = "main";

    VkVertexInputBindingDescription vertex_binding{};
    vertex_binding.binding = 0;
    vertex_binding.stride = sizeof(GpuVertex);
    vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attributes[7]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                     static_cast<std::uint32_t>(offsetof(GpuVertex, position))};
    attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,
                     static_cast<std::uint32_t>(offsetof(GpuVertex, texture_uv))};
    attributes[2] = {2, 0, VK_FORMAT_R32_UINT,
                     static_cast<std::uint32_t>(
                         offsetof(GpuVertex, texture_layer))};
    attributes[3] = {3, 0, VK_FORMAT_R32_UINT,
                     static_cast<std::uint32_t>(
                         offsetof(GpuVertex, polygon_flags))};
    attributes[4] = {4, 0, VK_FORMAT_R32_UINT,
                     static_cast<std::uint32_t>(
                         offsetof(GpuVertex, packed_light))};
    attributes[5] = {5, 0, VK_FORMAT_R32G32_SFLOAT,
                     static_cast<std::uint32_t>(
                         offsetof(GpuVertex, lightmap_uv))};
    attributes[6] = {6, 0, VK_FORMAT_R32_UINT,
                     static_cast<std::uint32_t>(
                         offsetof(GpuVertex, has_lightmap))};
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &vertex_binding;
    vertex_input.vertexAttributeDescriptionCount = 7;
    vertex_input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;
    constexpr VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT,
                                               VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pDepthStencilState = &depth;
    pipeline_info.pColorBlendState = &blend;
    pipeline_info.pDynamicState = &dynamic;
    pipeline_info.layout = state.pipeline_layout;
    pipeline_info.renderPass = render_pass;
    const bool pipeline_ok = CheckVk(
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                  nullptr, &state.pipeline),
        "vkCreateGraphicsPipelines(scene)");
    // Authored one-sided brushes must not show their embedded back faces
    // against the arch. Real PF_TwoSided movers keep the normal no-cull path.
    rasterization.cullMode=VK_CULL_MODE_BACK_BIT;
    const bool mover_ok=CheckVk(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipeline_info,nullptr,&state.mover_pipeline),"vkCreateGraphicsPipelines(mover_sidedness)");
    rasterization.cullMode=VK_CULL_MODE_NONE;
    bool mirror_ok=true;
    if(state.map_id==3){
        auto mirror_push=push_range;mirror_push.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
        auto mirror_layout_info=pipeline_layout_info;mirror_layout_info.pPushConstantRanges=&mirror_push;
        mirror_ok=vkCreatePipelineLayout(device,&mirror_layout_info,nullptr,&state.mirror_layout)==VK_SUCCESS;
        const auto mv=CreateShaderModule(device,kMirrorVertexSpirv,kMirrorVertexSpirvSize);
        const auto mf=CreateShaderModule(device,kMirrorFragmentSpirv,kMirrorFragmentSpirvSize);
        const auto mc=CreateShaderModule(device,kMirrorCaptureFragmentSpirv,kMirrorCaptureFragmentSpirvSize);
        const auto veil=CreateShaderModule(device,kMirrorVeilFragmentSpirv,kMirrorVeilFragmentSpirvSize);
        if(mirror_ok&&mv&&mf&&mc&&veil){
            auto mirror_info=pipeline_info;
            std::array<VkPipelineShaderStageCreateInfo,2> stages{shader_stages[0],shader_stages[1]};
            stages[0].module=mv;stages[1].module=mc;mirror_info.pStages=stages.data();mirror_info.layout=state.mirror_layout;
            mirror_info.renderPass=state.mirror_target.Pass();
            mirror_ok=vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&mirror_info,nullptr,&state.mirror_capture_pipeline)==VK_SUCCESS;
            mirror_info.renderPass=render_pass;
            stages[1].module=mf;
            auto composite_depth=depth;composite_depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;mirror_info.pDepthStencilState=&composite_depth;
            mirror_ok=(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&mirror_info,nullptr,&state.mirror_composite_pipeline)==VK_SUCCESS)&&mirror_ok;
            composite_depth.depthWriteEnable=VK_FALSE;
            auto veil_attachment=blend_attachment;veil_attachment.blendEnable=VK_TRUE;
            veil_attachment.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
            veil_attachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            veil_attachment.colorBlendOp=VK_BLEND_OP_ADD;
            veil_attachment.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
            veil_attachment.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            veil_attachment.alphaBlendOp=VK_BLEND_OP_ADD;
            auto veil_blend=blend;veil_blend.pAttachments=&veil_attachment;mirror_info.pColorBlendState=&veil_blend;
            stages[1].module=veil;
            mirror_ok=(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&mirror_info,nullptr,&state.mirror_veil_pipeline)==VK_SUCCESS)&&mirror_ok;
        }else mirror_ok=false;
        if(mv)vkDestroyShaderModule(device,mv,nullptr);if(mf)vkDestroyShaderModule(device,mf,nullptr);
        if(mc)vkDestroyShaderModule(device,mc,nullptr);
        if(veil)vkDestroyShaderModule(device,veil,nullptr);
    }
    depth.depthTestEnable=VK_FALSE;depth.depthWriteEnable=VK_FALSE;
    const bool frontend_ok=CheckVk(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipeline_info,
        nullptr,&state.frontend_pipeline),"vkCreateGraphicsPipelines(frontend_no_depth)");
    depth.depthTestEnable=VK_TRUE;depth.depthWriteEnable=VK_FALSE;
    blend_attachment.blendEnable=VK_TRUE;
    blend_attachment.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp=VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp=VK_BLEND_OP_ADD;
    const bool ghost_ok=CheckVk(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipeline_info,
        nullptr,&state.ghost_pipeline),"vkCreateGraphicsPipelines(ghost_depth_test)");
    depth.depthWriteEnable=VK_TRUE;blend_attachment.blendEnable=VK_FALSE;
    vkDestroyShaderModule(device, fragment_shader, nullptr);
    vkDestroyShaderModule(device, vertex_shader, nullptr);
    if (!pipeline_ok || !mover_ok || !frontend_ok || !ghost_ok || !mirror_ok) {
        DestroyGpu();
        return false;
    }

    gpu_stage("GPU_WORLD_PIPELINES_READY");
    VkPushConstantRange particle_push_range{};
    particle_push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    particle_push_range.size = sizeof(ParticlePushConstants);
    VkPipelineLayoutCreateInfo particle_layout_info{};
    particle_layout_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    particle_layout_info.setLayoutCount = 1;
    particle_layout_info.pSetLayouts = &state.descriptor_set_layout;
    particle_layout_info.pushConstantRangeCount = 1;
    particle_layout_info.pPushConstantRanges = &particle_push_range;
    if (!CheckVk(vkCreatePipelineLayout(
                     device, &particle_layout_info, nullptr,
                     &state.particle_pipeline_layout),
                 "vkCreatePipelineLayout(particle)")) {
        DestroyGpu();
        return false;
    }
    const VkShaderModule particle_vertex_shader = CreateShaderModule(
        device, kParticleVertexSpirv, kParticleVertexSpirvSize);
    const VkShaderModule particle_fragment_shader = CreateShaderModule(
        device, kParticleFragmentSpirv, kParticleFragmentSpirvSize);
    if (particle_vertex_shader == VK_NULL_HANDLE ||
        particle_fragment_shader == VK_NULL_HANDLE) {
        if (particle_fragment_shader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, particle_fragment_shader, nullptr);
        }
        if (particle_vertex_shader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, particle_vertex_shader, nullptr);
        }
        DestroyGpu();
        return false;
    }
    VkPipelineShaderStageCreateInfo particle_stages[2]{};
    particle_stages[0].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    particle_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    particle_stages[0].module = particle_vertex_shader;
    particle_stages[0].pName = "main";
    particle_stages[1].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    particle_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    particle_stages[1].module = particle_fragment_shader;
    particle_stages[1].pName = "main";
    VkVertexInputBindingDescription particle_binding{};
    particle_binding.binding = 0;
    particle_binding.stride = sizeof(ParticleGpuVertex);
    particle_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription particle_attributes[3]{};
    particle_attributes[0] = {
        0, 0, VK_FORMAT_R32G32B32_SFLOAT,
        static_cast<std::uint32_t>(offsetof(ParticleGpuVertex, position))};
    particle_attributes[1] = {
        1, 0, VK_FORMAT_R32G32_SFLOAT,
        static_cast<std::uint32_t>(offsetof(ParticleGpuVertex, texture_uv))};
    particle_attributes[2]={2,0,VK_FORMAT_R32G32B32A32_SFLOAT,
        static_cast<std::uint32_t>(offsetof(ParticleGpuVertex,color))};
    VkPipelineVertexInputStateCreateInfo particle_vertex_input{};
    particle_vertex_input.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    particle_vertex_input.vertexBindingDescriptionCount = 1;
    particle_vertex_input.pVertexBindingDescriptions = &particle_binding;
    particle_vertex_input.vertexAttributeDescriptionCount = 3;
    particle_vertex_input.pVertexAttributeDescriptions = particle_attributes;
    pipeline_info.pStages = particle_stages;
    pipeline_info.pVertexInputState = &particle_vertex_input;
    pipeline_info.layout = state.particle_pipeline_layout;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth.depthWriteEnable = VK_FALSE;
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    // RGB emission must preserve the opaque-world SSR mask in destination A.
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    const bool particle_pipeline_ok = CheckVk(
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                  nullptr, &state.particle_pipeline),
        "vkCreateGraphicsPipelines(particle)");
    vkDestroyShaderModule(device, particle_fragment_shader, nullptr);
    vkDestroyShaderModule(device, particle_vertex_shader, nullptr);
    if (!particle_pipeline_ok) {
        DestroyGpu();
        return false;
    }
    depth.depthWriteEnable = VK_TRUE;
    blend_attachment.blendEnable = VK_FALSE;

    VkPushConstantRange wand_push_range{};
    wand_push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    wand_push_range.size = sizeof(WandPushConstants);
    VkPipelineLayoutCreateInfo wand_layout_info{};
    wand_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    wand_layout_info.pushConstantRangeCount = 1;
    wand_layout_info.pPushConstantRanges = &wand_push_range;
    if (!CheckVk(vkCreatePipelineLayout(device, &wand_layout_info, nullptr,
                                        &state.wand_pipeline_layout),
                 "vkCreatePipelineLayout(wand)")) {
        DestroyGpu();
        return false;
    }
    const VkShaderModule wand_vertex_shader = CreateShaderModule(
        device, kWandVertexSpirv, kWandVertexSpirvSize);
    const VkShaderModule wand_fragment_shader = CreateShaderModule(
        device, kWandFragmentSpirv, kWandFragmentSpirvSize);
    if (wand_vertex_shader == VK_NULL_HANDLE ||
        wand_fragment_shader == VK_NULL_HANDLE) {
        if (wand_fragment_shader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, wand_fragment_shader, nullptr);
        }
        if (wand_vertex_shader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, wand_vertex_shader, nullptr);
        }
        DestroyGpu();
        return false;
    }
    VkPipelineShaderStageCreateInfo wand_stages[2]{};
    wand_stages[0].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    wand_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    wand_stages[0].module = wand_vertex_shader;
    wand_stages[0].pName = "main";
    wand_stages[1].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    wand_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    wand_stages[1].module = wand_fragment_shader;
    wand_stages[1].pName = "main";
    VkVertexInputBindingDescription wand_binding{};
    wand_binding.binding = 0;
    wand_binding.stride = sizeof(WandGpuVertex);
    wand_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription wand_attributes[2]{};
    wand_attributes[0] = {
        0, 0, VK_FORMAT_R32G32B32_SFLOAT,
        static_cast<std::uint32_t>(offsetof(WandGpuVertex, position))};
    wand_attributes[1] = {
        1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
        static_cast<std::uint32_t>(offsetof(WandGpuVertex, color))};
    VkPipelineVertexInputStateCreateInfo wand_vertex_input{};
    wand_vertex_input.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    wand_vertex_input.vertexBindingDescriptionCount = 1;
    wand_vertex_input.pVertexBindingDescriptions = &wand_binding;
    wand_vertex_input.vertexAttributeDescriptionCount = 2;
    wand_vertex_input.pVertexAttributeDescriptions = wand_attributes;
    pipeline_info.pStages = wand_stages;
    pipeline_info.pVertexInputState = &wand_vertex_input;
    pipeline_info.layout = state.wand_pipeline_layout;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    const bool wand_pipeline_ok = CheckVk(
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                  nullptr, &state.wand_pipeline),
        "vkCreateGraphicsPipelines(wand)");
    depth.depthWriteEnable = VK_FALSE;
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    const bool effect_pipeline_ok = wand_pipeline_ok && CheckVk(
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                  nullptr, &state.effect_pipeline),
        "vkCreateGraphicsPipelines(effect)");
    depth.depthTestEnable=VK_FALSE;
    blend_attachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    const bool death_ok=CheckVk(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipeline_info,nullptr,&state.death_pipeline),"vkCreateGraphicsPipelines(death_fade)");
    vkDestroyShaderModule(device, wand_fragment_shader, nullptr);
    vkDestroyShaderModule(device, wand_vertex_shader, nullptr);
    if (!wand_pipeline_ok || !effect_pipeline_ok || !death_ok) {
        DestroyGpu();
        return false;
    }

    if(!CreateBuffer(physical_device,device,4096*sizeof(GpuVertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&state.perf_buffer,&state.perf_memory)||
        vkMapMemory(device,state.perf_memory,0,VK_WHOLE_SIZE,0,&state.perf_mapped)!=VK_SUCCESS){
        DestroyGpu();return false;
    }
    gpu_stage("GPU_ALL_PIPELINES_READY");
    if(state.map_id==2&&(!CreateBuffer(physical_device,device,kBroomHoopVertexCapacity*sizeof(ParticleGpuVertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&state.broom_particle_buffer,&state.broom_particle_memory)||
        vkMapMemory(device,state.broom_particle_memory,0,VK_WHOLE_SIZE,0,&state.broom_particle_mapped)!=VK_SUCCESS)){
        DestroyGpu();return false;
    }
    if(!CreateBuffer(physical_device,device,178*6*sizeof(ParticleGpuVertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&state.spell_particle_buffer,&state.spell_particle_memory)||
        vkMapMemory(device,state.spell_particle_memory,0,VK_WHOLE_SIZE,0,&state.spell_particle_mapped)!=VK_SUCCESS){
        DestroyGpu();return false;
    }
    const auto candle_count=std::count_if(state.flames.begin(),state.flames.end(),[](const auto& flame){return flame.scale<.5F;});
    state.candle_particle_capacity=static_cast<std::uint32_t>(std::clamp<std::ptrdiff_t>(candle_count,1,4096))*12+
        static_cast<std::uint32_t>((std::min(state.ambient_emitters.size(),ambient::kMaximumAmbientEmitters)+
            std::min(state.lock_emitters.size(),ambient::kMaximumAmbientEmitters))*ambient::kMaximumAmbientParticlesPerEmitter*6);
    if(!CreateBuffer(physical_device,device,state.candle_particle_capacity*sizeof(ParticleGpuVertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&state.candle_particle_buffer,&state.candle_particle_memory)||
        vkMapMemory(device,state.candle_particle_memory,0,VK_WHOLE_SIZE,0,&state.candle_particle_mapped)!=VK_SUCCESS){
        DestroyGpu();return false;
    }
    SetPerformance({});
    const bool audio_started = state.audio.Start();
    if (!audio_started) {
        HPVR_LOGE("[hpvr.quest.audio] status=START_REJECTED");
    } else {
        HPVR_LOGI("[hpvr.quest.audio] status=RUNNING rate=48000 channels=2");
    }
    gpu_stage("GPU_AUDIO_START_COMPLETE");
    if (state.intro_cutscene.playing) {
        HPVR_LOGI(
            "[hpvr.quest.cutscene] status=STARTED object=CutScene4 "
            "trigger=LEVEL_LOAD vr_camera=POSITION_ONLY_HEAD_TRACKED");
    }

    HPVR_LOGI(
        "[hpvr.quest.scene.gpu] status=READY vertices=%zu vertex_bytes=%llu "
        "texture_bytes=%llu layers=%u wand_vertices=%zu wand_bytes=%llu "
        "guide_vertices=%zu guide_bytes=%llu radial_glow_vertices=%zu "
        "radial_glow_bytes=%llu particle_vertices=%zu particle_bytes=%llu "
        "fire_particles=OWNED_POTFIRE08 color_grade=UE1_GAMMA "
        "exposure=1.35 gamma=0.92 alpha_falloff=SMOOTH",
        state.vertices.size(), static_cast<unsigned long long>(vertex_bytes),
        static_cast<unsigned long long>(texture_bytes), state.texture_layers,
        state.wand_vertices.size(),
        static_cast<unsigned long long>(wand_vertex_bytes), kGuideQuad.size(),
        static_cast<unsigned long long>(guide_vertex_bytes),
        state.glow_vertices.size(),
        static_cast<unsigned long long>(glow_vertex_bytes),
        kParticleQuad.size(),
        static_cast<unsigned long long>(particle_vertex_bytes));
    return true;
}

void QuestScene::DestroyGpu() {
    state_->reflections.Destroy();
    state_->mirror_target.Destroy();
    State& state = *state_;
    state.audio.Stop();
    if (state.device == VK_NULL_HANDLE) {
        return;
    }
    if (state.abyss_uniform_mapped) vkUnmapMemory(state.device, state.abyss_uniform_memory);
    if (state.abyss_uniform_buffer) vkDestroyBuffer(state.device, state.abyss_uniform_buffer, nullptr);
    if (state.abyss_uniform_memory) vkFreeMemory(state.device, state.abyss_uniform_memory, nullptr);
    state.abyss_uniform_mapped = nullptr; state.abyss_uniform_buffer = VK_NULL_HANDLE; state.abyss_uniform_memory = VK_NULL_HANDLE;
    state.abyss_uniform_stride = state.abyss_frame_slots = state.abyss_disabled_offset = 0;
    if (state.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(state.device, state.pipeline, nullptr);
    }
    if(state.mover_pipeline)vkDestroyPipeline(state.device,state.mover_pipeline,nullptr);
    if(state.mirror_capture_pipeline)vkDestroyPipeline(state.device,state.mirror_capture_pipeline,nullptr);
    if(state.mirror_composite_pipeline)vkDestroyPipeline(state.device,state.mirror_composite_pipeline,nullptr);
    if(state.mirror_veil_pipeline)vkDestroyPipeline(state.device,state.mirror_veil_pipeline,nullptr);
    if(state.mirror_layout)vkDestroyPipelineLayout(state.device,state.mirror_layout,nullptr);
    state.mirror_capture_pipeline=state.mirror_composite_pipeline=state.mirror_veil_pipeline=VK_NULL_HANDLE;state.mirror_layout=VK_NULL_HANDLE;
    if(state.death_pipeline)vkDestroyPipeline(state.device,state.death_pipeline,nullptr);
    state.mover_pipeline=state.death_pipeline=VK_NULL_HANDLE;
    if (state.wand_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(state.device, state.wand_pipeline, nullptr);
    }
    if (state.effect_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(state.device, state.effect_pipeline, nullptr);
    }
    if (state.particle_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(state.device, state.particle_pipeline, nullptr);
    }
    if (state.pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(state.device, state.pipeline_layout, nullptr);
    }
    if(state.frontend_pipeline!=VK_NULL_HANDLE)vkDestroyPipeline(state.device,state.frontend_pipeline,nullptr);
    if(state.ghost_pipeline!=VK_NULL_HANDLE)vkDestroyPipeline(state.device,state.ghost_pipeline,nullptr);
    if (state.wand_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(state.device, state.wand_pipeline_layout,
                                nullptr);
    }
    if (state.particle_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(state.device, state.particle_pipeline_layout,
                                nullptr);
    }
    if (state.descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(state.device, state.descriptor_pool, nullptr);
    }
    if (state.descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(state.device, state.descriptor_set_layout,
                                     nullptr);
    }
    if (state.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(state.device, state.sampler, nullptr);
    }
    if (state.lightmap_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(state.device, state.lightmap_sampler, nullptr);
    }
    if (state.lightmap_view != VK_NULL_HANDLE) {
        vkDestroyImageView(state.device, state.lightmap_view, nullptr);
    }
    if (state.lightmap_image != VK_NULL_HANDLE) {
        vkDestroyImage(state.device, state.lightmap_image, nullptr);
    }
    if (state.lightmap_memory != VK_NULL_HANDLE) {
        vkFreeMemory(state.device, state.lightmap_memory, nullptr);
    }
    if (state.texture_view != VK_NULL_HANDLE) {
        vkDestroyImageView(state.device, state.texture_view, nullptr);
    }
    if (state.texture_image != VK_NULL_HANDLE) {
        vkDestroyImage(state.device, state.texture_image, nullptr);
    }
    if (state.texture_memory != VK_NULL_HANDLE) {
        vkFreeMemory(state.device, state.texture_memory, nullptr);
    }
    if (state.vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(state.device, state.vertex_buffer, nullptr);
    }
    if (state.vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(state.device, state.vertex_memory, nullptr);
    }
    if(state.broom_particle_mapped)vkUnmapMemory(state.device,state.broom_particle_memory);
    if(state.broom_avatar_buffer)vkDestroyBuffer(state.device,state.broom_avatar_buffer,nullptr);
    if(state.broom_avatar_memory)vkFreeMemory(state.device,state.broom_avatar_memory,nullptr);
    state.broom_avatar_buffer=VK_NULL_HANDLE;state.broom_avatar_memory=VK_NULL_HANDLE;
    if(state.broom_particle_buffer)vkDestroyBuffer(state.device,state.broom_particle_buffer,nullptr);
    if(state.broom_particle_memory)vkFreeMemory(state.device,state.broom_particle_memory,nullptr);
    state.broom_particle_mapped=nullptr;state.broom_particle_buffer=VK_NULL_HANDLE;state.broom_particle_memory=VK_NULL_HANDLE;
    if(state.spell_particle_mapped)vkUnmapMemory(state.device,state.spell_particle_memory);
    if(state.spell_particle_buffer)vkDestroyBuffer(state.device,state.spell_particle_buffer,nullptr);
    if(state.spell_particle_memory)vkFreeMemory(state.device,state.spell_particle_memory,nullptr);
    state.spell_particle_mapped=nullptr;state.spell_particle_buffer=VK_NULL_HANDLE;state.spell_particle_memory=VK_NULL_HANDLE;
    if(state.candle_particle_mapped)vkUnmapMemory(state.device,state.candle_particle_memory);
    if(state.candle_particle_buffer)vkDestroyBuffer(state.device,state.candle_particle_buffer,nullptr);
    if(state.candle_particle_memory)vkFreeMemory(state.device,state.candle_particle_memory,nullptr);
    state.candle_particle_mapped=nullptr;state.candle_particle_buffer=VK_NULL_HANDLE;
    state.candle_particle_memory=VK_NULL_HANDLE;state.candle_particle_capacity=0;
    if(state.perf_mapped)vkUnmapMemory(state.device,state.perf_memory);
    if(state.perf_buffer)vkDestroyBuffer(state.device,state.perf_buffer,nullptr);
    if(state.perf_memory)vkFreeMemory(state.device,state.perf_memory,nullptr);
    state.perf_mapped=nullptr;state.perf_buffer=VK_NULL_HANDLE;state.perf_memory=VK_NULL_HANDLE;state.perf_count=0;
    if (state.wand_vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(state.device, state.wand_vertex_buffer, nullptr);
    }
    if (state.wand_vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(state.device, state.wand_vertex_memory, nullptr);
    }
    if (state.guide_vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(state.device, state.guide_vertex_buffer, nullptr);
    }
    if (state.guide_vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(state.device, state.guide_vertex_memory, nullptr);
    }
    if (state.glow_vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(state.device, state.glow_vertex_buffer, nullptr);
    }
    if (state.glow_vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(state.device, state.glow_vertex_memory, nullptr);
    }
    if (state.particle_vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(state.device, state.particle_vertex_buffer, nullptr);
    }
    if (state.particle_vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(state.device, state.particle_vertex_memory, nullptr);
    }
    state.vertex_buffer = VK_NULL_HANDLE;
    state.vertex_memory = VK_NULL_HANDLE;
    state.wand_vertex_buffer = VK_NULL_HANDLE;
    state.wand_vertex_memory = VK_NULL_HANDLE;
    state.guide_vertex_buffer = VK_NULL_HANDLE;
    state.guide_vertex_memory = VK_NULL_HANDLE;
    state.glow_vertex_buffer = VK_NULL_HANDLE;
    state.glow_vertex_memory = VK_NULL_HANDLE;
    state.particle_vertex_buffer = VK_NULL_HANDLE;
    state.particle_vertex_memory = VK_NULL_HANDLE;
    state.texture_image = VK_NULL_HANDLE;
    state.texture_memory = VK_NULL_HANDLE;
    state.texture_view = VK_NULL_HANDLE;
    state.sampler = VK_NULL_HANDLE;
    state.lightmap_image = VK_NULL_HANDLE;
    state.lightmap_memory = VK_NULL_HANDLE;
    state.lightmap_view = VK_NULL_HANDLE;
    state.lightmap_sampler = VK_NULL_HANDLE;
    state.descriptor_set_layout = VK_NULL_HANDLE;
    state.descriptor_pool = VK_NULL_HANDLE;
    state.descriptor_set = VK_NULL_HANDLE;
    state.pipeline_layout = VK_NULL_HANDLE;
    state.pipeline = VK_NULL_HANDLE;
    state.frontend_pipeline = VK_NULL_HANDLE;
    state.ghost_pipeline = VK_NULL_HANDLE;
    state.wand_pipeline_layout = VK_NULL_HANDLE;
    state.wand_pipeline = VK_NULL_HANDLE;
    state.effect_pipeline = VK_NULL_HANDLE;
    state.particle_pipeline_layout = VK_NULL_HANDLE;
    state.particle_pipeline = VK_NULL_HANDLE;
    state.physical_device = VK_NULL_HANDLE;
    state.device = VK_NULL_HANDLE;
    state.queue = VK_NULL_HANDLE;
    state.queue_family = 0;
}

VrSettings QuestScene::GetVrSettings() const{return state_->frontend.vr;}
void QuestScene::SetPerformance(const PerformanceSnapshot& performance){
    auto& s=*state_;if(!s.perf_mapped)return;s.performance=performance;
    auto* out=static_cast<GpuVertex*>(s.perf_mapped);s.perf_count=0;
    // One small upload per second, two batched draws per eye (panel + text).
    // Caller is after the submitted fence, never while GPU is reading this buffer.
    const auto lines=PerformanceLines(performance);
    for(unsigned row=0;row<lines.size();++row){
        float x=40;
        for(unsigned char ch:lines[row]){
            if(auto glyph=s.front_draws.find("glyph_"+std::to_string(ch));glyph!=s.front_draws.end()){
                for(unsigned v=0;v<glyph->second.count&&s.perf_count<4096;++v){
                    auto point=s.vertices[glyph->second.first+v];point.position[0]+=x*.004375F;
                    point.position[1]-=float(150+row*26)*.004375F;out[s.perf_count++]=point;
                }
            }
            x+=10.8F;
        }
    }
}
void QuestScene::ToggleVrMenu(){
    if(!IsGpuReady())return;
    state_->frontend.ToggleVrMenu();state_->front_anchor_valid=false;
    state_->audio.SetWandDrawing(false);state_->basic_cast={};
    HPVR_LOGI("[hpvr.quest.vrmenu] visible=%d renderScale=%d ssr=%d",state_->frontend.screen==FrontScreen::Vr,state_->frontend.vr.render_scale,state_->frontend.vr.ssr);
}
void QuestScene::PrepareReflections(const Matrix4& matrix,unsigned width,unsigned height,bool active){
    if(IsGpuReady())state_->reflections.Prepare(active&&state_->frontend.WorldVisible(),state_->frontend.vr.ssr,matrix,width,height);
}
bool QuestScene::ReflectionCaptureEnabled()const{return IsGpuReady()&&state_->reflections.Enabled();}
void QuestScene::CaptureReflections(VkCommandBuffer command,VkImage color,VkImage depth,const Matrix4& matrix,unsigned width,unsigned height){
    if(IsGpuReady())state_->reflections.Capture(command,color,depth,matrix,width,height);
}
#include "quest_mirror_runtime.inl"
void QuestScene::RecordDraw(
    const VkCommandBuffer command_buffer,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::array<float, 16>& view_projection,unsigned frame_slot) const {
    const State& state = *state_;
    if (!IsGpuReady()) {
        return;
    }
    VkViewport viewport{};
    viewport.y = static_cast<float>(height);
    viewport.width = static_cast<float>(width);
    viewport.height = -static_cast<float>(height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    VkRect2D scissor{};
    scissor.extent = {width, height};
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      state.pipeline);
    auto abyss_offset = state.abyss_disabled_offset;
    if (state.map_id == 1 && state.abyss_fog.count && frame_slot < state.abyss_frame_slots) {
        // The caller has waited this swapchain image's fence. Separate eye slots
        // prevent either eye or another in-flight image from overwriting this data.
        const auto uniform = MakeAbyssFogUniform(state.abyss_fog, state.abyss_scene_to_source,
                                               view_projection, width, height);
        abyss_offset = frame_slot * state.abyss_uniform_stride;
        std::memcpy(static_cast<std::uint8_t*>(state.abyss_uniform_mapped) + abyss_offset,
                    &uniform, sizeof(uniform));
    }
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            state.pipeline_layout, 0, 1,
                            &state.descriptor_set, 1, &abyss_offset);
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &state.vertex_buffer, &offset);
    if(!state.frontend.WorldVisible())return;
    // Scene rendering continues behind world-anchored VR panels.
    const AuthoredDarkLightPush no_dark_lights{};
    auto map_lights=no_dark_lights;
    if(state.map_id==2||state.map_id==3){
        Matrix4 inverse{};
        auto eye=state.last_player;
        if(InvertReflectionMatrix(view_projection,inverse)&&std::abs(inverse[11])>1e-6F){
            for(unsigned axis=0;axis<3;++axis)eye[axis]=inverse[8+axis]/inverse[11];
        }
        // Negative radius reserves this slot for the sky origin, not a light.
        map_lights.position_radius[0]={eye[0],eye[1],eye[2],-1.0F};
    }
    vkCmdPushConstants(command_buffer, state.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 16,
                       sizeof(map_lights), &map_lights);
    vkCmdPushConstants(command_buffer, state.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(float) * view_projection.size(),
                       view_projection.data());
    if(state.mirrors.empty())vkCmdDraw(command_buffer,state.map_vertex_count,1,0,0);
    else{
        std::vector<std::pair<unsigned,unsigned>> skip;
        for(const auto& mirror:state.mirrors)skip.insert(skip.end(),mirror.ranges.begin(),mirror.ranges.end());
        std::ranges::sort(skip);unsigned first=0;
        for(const auto& range:skip){if(range.first>first)vkCmdDraw(command_buffer,range.first-first,1,first,0);first=range.first+range.second;}
        if(first<state.map_vertex_count)vkCmdDraw(command_buffer,state.map_vertex_count-first,1,first,0);
    }
    vkCmdPushConstants(command_buffer,state.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,
        sizeof(float)*16,sizeof(no_dark_lights),&no_dark_lights);
    if(state.map_id!=0){
        for(const auto& prop:state.challenge.props){
            if(state.map_id==2&&state.broom.hoop_indices.contains(prop.reference))continue;
            // These owned meshes have single-sided materials and coincident
            // inner/outer faces. Drawing their backs produces depth fighting.
            const bool single_sided=state.map_id==3&&(chest::IsChest(prop.name)||prop.name=="hprops.knight");
            vkCmdBindPipeline(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,
                single_sided?state.mover_pipeline:state.pipeline);
            const auto range=ChallengePropDrawRange(prop,ChallengeActivated(state.frontend.progress,prop.reference),state.bean_time);
            auto p=ChallengePropOffset(prop,state.bean_time);
            if(state.map_id==3)if(const auto block=state.charms.blocks.find(prop.reference);block!=state.charms.blocks.end())p=AddVector(p,block->second.offset);
            if(state.map_id==3)if(const auto attachment=state.charms.prop_attachments.find(prop.reference);attachment!=state.charms.prop_attachments.end())
                for(const auto& mover:state.doors)if(mover.actor_reference==attachment->second){
                    p=AddVector(p,SubtractVector(MoverPoint(mover,mover.pivot),mover.pivot));break;
                }
            if(state.map_id==2)if(const auto offset=state.broom.prop_offsets.find(prop.reference);offset!=state.broom.prop_offsets.end())p=AddVector(p,offset->second);
            const Matrix4 transform{1,0,0,0,0,1,0,0,0,0,1,0,p[0],p[1],p[2],1};
            const auto mvp=MultiplyMatrices(view_projection,transform);
            vkCmdPushConstants(command_buffer,state.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(float)*mvp.size(),mvp.data());
            if(range.second)vkCmdDraw(command_buffer,range.second,1,range.first,0);
        }
        vkCmdBindPipeline(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,state.pipeline);
    }else vkCmdDraw(command_buffer, state.knights.empty()?state.fixture_vertex_count:state.knights.front().first-state.map_vertex_count, 1,
              state.map_vertex_count, 0);
    for(const auto& knight:state.knights){
        const auto frame=static_cast<unsigned>(knight.time*30)%knight.frames;
        vkCmdDraw(command_buffer,knight.count,1,knight.first+frame*knight.count,0);
    }
    VkPipeline bound_mover=VK_NULL_HANDLE;
    for (const auto& door : state.doors) {
        if(door.collision_only)continue;
        if(IsMirrorBlocker(door,state.mirrors))continue;
        const auto selected=door.two_sided?state.pipeline:state.mover_pipeline;
        if(selected!=bound_mover){vkCmdBindPipeline(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,selected);bound_mover=selected;}
        const auto origin=MoverPoint(door,{0,0,0});
        const auto x=SubtractVector(MoverPoint(door,{1,0,0}),origin);
        const auto y=SubtractVector(MoverPoint(door,{0,1,0}),origin);
        const auto z=SubtractVector(MoverPoint(door,{0,0,1}),origin);
        const Matrix4 model{x[0],x[1],x[2],0,y[0],y[1],y[2],0,z[0],z[1],z[2],0,origin[0],origin[1],origin[2],1};
        const auto mvp = MultiplyMatrices(view_projection, model);
        const auto dark_lights = BuildAuthoredDarkLightPush(state.dark_lights,
            MoverPoint(door, door.pivot), origin, {x, y, z});
        vkCmdPushConstants(command_buffer, state.pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 16, sizeof(dark_lights), &dark_lights);
        vkCmdPushConstants(command_buffer, state.pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * mvp.size(), mvp.data());
        vkCmdDraw(command_buffer, door.vertex_count, 1, door.first_vertex, 0);
    }
    vkCmdBindPipeline(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,state.pipeline);
    vkCmdPushConstants(command_buffer, state.pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 16, sizeof(no_dark_lights), &no_dark_lights);
    const auto draw_character=[&](std::size_t index){
        // Actors follow after separately transformed mover brushes.
        const CharacterDraw& draw = state.character_draws[index];
        if (draw.child_template || !draw.enabled) return;
        if(state.map_id==2&&state.frontend.vr.first_person_cutscenes&&IsCutscenePlaying()&&
            draw.actor_reference==BroomCameraActor(state.intro_cutscene,state.harry_actor))return;
        if(draw.player){
            ViewPose camera;bool first_person=false;
            (void)GetCinematicCameraPose(&camera,&first_person);
            if(!IsCutscenePlaying()||first_person)return;
        }
        auto offset = state.spell_targets.ReactionOffset(index);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            offset[axis] += state.character_draws[index].cutscene_offset[axis];
        }
        const float angle = draw.yaw - draw.base_yaw;
        const float c = std::cos(angle), s = std::sin(angle);
        const auto rotated = RotateYaw(draw.base_origin, angle);
        for (std::size_t axis = 0; axis < 3; ++axis)
            offset[axis] += draw.base_origin[axis] - rotated[axis];
        const Matrix4 translation{
            c, 0.0F, -s, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            s, 0.0F, c, 0.0F,
            offset[0], offset[1], offset[2], 1.0F};
        const Matrix4 character_mvp =
            MultiplyMatrices(view_projection, translation);
        vkCmdPushConstants(command_buffer, state.pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(float) * character_mvp.size(),
                           character_mvp.data());
        const auto& clip = draw.clips.at(draw.active_clip);
        const auto frame = std::min(clip.frame_count - 1U,
            static_cast<std::uint32_t>((draw.active_clip=="faint"||!draw.animation_loop?std::min(draw.animation_time,clip.duration):std::fmod(draw.animation_time, clip.duration)) /
                clip.duration * clip.frame_count));
        const std::uint32_t animated_first_vertex =
            clip.first_vertex + frame * draw.vertex_count;
        vkCmdDraw(command_buffer, draw.vertex_count, 1,
                  animated_first_vertex, 0);
    };
    for(std::size_t i=0;i<state.character_draws.size();++i)
        if(state.map_id!=0?!state.character_draws[i].flying:state.character_draws[i].actor_reference!=3148)draw_character(i);
    for(const auto& bean:state.beans){
        if(bean.source_actor&&!ChallengeRewardsReady(state.challenge.props,bean.source_actor,state.frontend.progress))continue;
        if(state.map_id==0&&bean.kind==1&&state.frontend.progress.frog_taken)continue;
        const bool collecting=(bean.kind==2||bean.kind==4)&&state.card_pickup.active()&&state.card_pickup.actor==bean.actor_reference;
        if(bean.kind==2&&(!state.frontend.progress.card_awarded||(state.frontend.progress.card_taken&&!collecting)))continue;
        if(!collecting&&std::ranges::binary_search(state.frontend.progress.collected_beans,bean.actor_reference))continue;
        const float angle=collecting?state.card_pickup.angle():bean.kind==1?bean.yaw:state.bean_time*1.8F+bean.actor_reference;
        const float scale=collecting?state.card_pickup.scale():1.0F,c=std::cos(angle)*scale,s=std::sin(angle)*scale;
        const auto p=BeanWorldPosition(bean);
        const Matrix4 model{c,0,-s,0,0,scale,0,0,s,0,c,0,p[0],p[1]+(bean.kind==1||collecting?0:.06F*std::sin(angle*2)),p[2],1};
        const auto mvp=MultiplyMatrices(view_projection,model);
        vkCmdPushConstants(command_buffer,state.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(float)*mvp.size(),mvp.data());
        const auto frame=std::min(bean.frames-1,static_cast<unsigned>(std::fmod(state.bean_time,bean.duration)/bean.duration*bean.frames));
        vkCmdDraw(command_buffer,bean.count,1,bean.first+frame*bean.count,0);
    }
    for(const auto& flight:state.pickup_flights){
        const float phase=std::clamp(flight.elapsed/flight.duration,0.0F,1.0F);
        std::array<float,3> destination{};
        for(unsigned axis=0;axis<3;++axis)destination[axis]=state.hud_transform[12+axis]+
            state.hud_transform[axis]*.98F+state.hud_transform[4+axis]*.74F;
        if(flight.kind==3)destination=state.last_player;
        const auto p=AddVector(flight.origin,ScaleVector(SubtractVector(destination,flight.origin),phase));
        const float angle=flight.angle+flight.elapsed*6;
        const float c=std::cos(angle),s=std::sin(angle);
        const Matrix4 model{c,0,-s,0,0,1,0,0,s,0,c,0,p[0],p[1],p[2],1};
        const auto mvp=MultiplyMatrices(view_projection,model);
        vkCmdPushConstants(command_buffer,state.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(float)*mvp.size(),mvp.data());
        vkCmdDraw(command_buffer,flight.count,1,flight.first,0);
    }
    for (const auto& child : state.children.actors) {
        if (!child.active) continue;
        const auto model = std::ranges::find_if(state.character_draws, [&](const auto& draw) {
            return draw.child_template && draw.actor_reference == child.model_reference;
        });
        if (model == state.character_draws.end()) continue;
        const auto& clip = model->clips.at(child.pause > 0.0F ? "breathe" : "run");
        const float angle = child.yaw - model->base_yaw;
        const float c = std::cos(angle), s = std::sin(angle);
        const auto shift = SubtractVector(child.position, RotateYaw(model->base_origin, angle));
        const Matrix4 transform{
            c, 0.0F, -s, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
            s, 0.0F, c, 0.0F, shift[0], shift[1], shift[2], 1.0F};
        const auto mvp = MultiplyMatrices(view_projection, transform);
        vkCmdPushConstants(command_buffer, state.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(float) * mvp.size(), mvp.data());
        const auto frame = std::min(clip.frame_count-1U, static_cast<std::uint32_t>(
            std::fmod(child.age, clip.duration) / clip.duration * clip.frame_count));
        vkCmdDraw(command_buffer, model->vertex_count, 1,
            clip.first_vertex + frame * model->vertex_count, 0);
    }
    if(state.map_id==2&&state.broom.active&&!IsCutscenePlaying()&&state.broom_avatar_buffer){
        const auto& avatar=state.broom_avatar;
        auto anchor=state.last_player;
        if(state.player_capsule_valid){anchor=state.player_capsule;anchor[1]+=kPlayerEyeHeightMeters;}
        const float angle=state.last_yaw+kTau*.5F,c=std::cos(angle),s=std::sin(angle);
        const Matrix4 model{c,0,-s,0,0,1,0,0,s,0,c,0,anchor[0],anchor[1],anchor[2],1};
        const auto mvp=MultiplyMatrices(view_projection,model);
        const auto frame=std::min(avatar.frame_count-1,static_cast<unsigned>(
            std::fmod(state.bean_time,avatar.duration)/avatar.duration*avatar.frame_count));
        vkCmdBindVertexBuffers(command_buffer,0,1,&state.broom_avatar_buffer,&offset);
        vkCmdPushConstants(command_buffer,state.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(float)*mvp.size(),mvp.data());
        vkCmdDraw(command_buffer,avatar.vertex_count,1,frame*avatar.vertex_count,0);
        vkCmdBindVertexBuffers(command_buffer,0,1,&state.vertex_buffer,&offset);
    }
    RecordMirrorComposite(command_buffer,view_projection,width,height);
    vkCmdBindPipeline(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,state.ghost_pipeline);
    for(std::size_t i=0;i<state.character_draws.size();++i)
        if(state.map_id!=0?state.character_draws[i].flying:state.character_draws[i].actor_reference==3148)draw_character(i);
}

void QuestScene::RecordFrontDraw(VkCommandBuffer command_buffer,const Matrix4& view_projection)const{
    const auto& state=*state_;
    const bool pinned=!state.frontend.Visible()&&state.frontend.debug_pinned&&!IsCutscenePlaying();
    if(!IsGpuReady()||!state.front_anchor_valid||(!state.frontend.Visible()&&!pinned))return;
    const AuthoredDarkLightPush no_dark_lights{};
    vkCmdPushConstants(command_buffer, state.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
        sizeof(float) * 16, sizeof(no_dark_lights), &no_dark_lights);
    vkCmdBindDescriptorSets(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,state.pipeline_layout,0,1,&state.descriptor_set,1,&state.abyss_disabled_offset);
    constexpr VkDeviceSize offset=0;vkCmdBindVertexBuffers(command_buffer,0,1,&state.vertex_buffer,&offset);
        const auto found=state.front_draws.find(pinned?std::to_string(int(FrontScreen::Debug))+"_0_pinned":state.frontend.DrawKey());
        if(found==state.front_draws.end())return;
        vkCmdBindPipeline(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,state.frontend_pipeline);
        const auto mvp=MultiplyMatrices(view_projection,state.front_transform);
        vkCmdPushConstants(command_buffer,state.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,
            0,sizeof(float)*mvp.size(),mvp.data());
        if(state.frontend.screen==FrontScreen::Vr){
            const auto common=state.front_draws.find("vr_common_"+std::to_string(state.frontend.selection));
            if(common!=state.front_draws.end())vkCmdDraw(command_buffer,common->second.count,1,common->second.first,0);
        }
        vkCmdDraw(command_buffer,found->second.count,1,found->second.first,0);
        if(state.frontend.screen==FrontScreen::Vr){
            for(const auto& key:{"vr_scale_"+std::to_string(state.frontend.vr.render_scale),"vr_ssr_"+std::to_string(state.frontend.vr.ssr),"vr_hz_"+std::to_string(state.frontend.vr.refresh_rate),"vr_voice_status_"+std::to_string(state.voice_status),
                "vr_turning_"+std::to_string(static_cast<int>(state.frontend.vr.turning_mode)),"vr_turn_speed_"+std::to_string(state.frontend.vr.smooth_turn_speed),"vr_gpu_boost_"+std::to_string(int(state.frontend.vr.gpu_boost))}){
                const auto value=state.front_draws.find(key);
                if(value!=state.front_draws.end())vkCmdDraw(command_buffer,value->second.count,1,value->second.first,0);
            }
        }
        if(state.frontend.screen==FrontScreen::Pause||state.frontend.screen==FrontScreen::Report){
            const auto& p=state.frontend.progress;
            const auto nonbeans=static_cast<std::size_t>(std::ranges::count_if(state.beans,[&](const auto& b){
                return b.kind!=0&&std::ranges::binary_search(p.collected_beans,b.actor_reference);
            }));
            const auto bean_count=p.banked_beans+p.collected_beans.size()-std::min(nonbeans,p.collected_beans.size());
            const auto draw=[&](const std::string& key){
                const auto value=state.front_draws.find(key);
                if(value!=state.front_draws.end())vkCmdDraw(command_buffer,value->second.count,1,value->second.first,0);
            };
            if(IsWalkingChallenge(state.map_id))draw("stars_"+std::to_string(std::min(p.challenge_stars,state.map_id==3?6U:8U)));
            for(unsigned house=0;house<4;++house)
                draw("report_sand_"+std::to_string(house)+"_"+std::to_string(std::min(256U,p.house_points[house]*256U/400U)));
            for(unsigned field=0;field<7;++field){
                auto value=field==0?static_cast<unsigned>(std::min<std::size_t>(campaign::kPointLimit,bean_count)):
                    state.frontend.ReportValue(field);
                unsigned place=0;
                do{draw("report_digit_"+std::to_string(field)+"_"+std::to_string(place++)+"_"+std::to_string(value%10));value/=10;}
                while(value&&place<7);
            }
        }
        if(state.frontend.screen==FrontScreen::Cards){
            const auto mask=state.frontend.progress.earned_cards|(state.frontend.progress.card_taken?campaign::CardMask(101):0U);
            for(unsigned local=0;local<4;++local){
                const unsigned card=state.frontend.card_page*4+local;
                if(card>=campaign::kCardIds.size()||!(mask&(1U<<card)))continue;
                const auto range=state.front_draws.find("folio_card_"+std::to_string(card));
                if(range!=state.front_draws.end())vkCmdDraw(command_buffer,range->second.count,1,range->second.first,0);
            }
        }
        if(state.frontend.screen==FrontScreen::Debug||pinned){
            vkCmdBindVertexBuffers(command_buffer,0,1,&state.perf_buffer,&offset);
            vkCmdDraw(command_buffer,state.perf_count,1,0,0);
        }
}

void QuestScene::RecordHudDraw(VkCommandBuffer command_buffer,const Matrix4& view_projection)const{
    const auto& state=*state_;
    if(!IsGpuReady()||state.frontend.Visible())return;
    const bool cutscene=IsCutscenePlaying();
    if(cutscene&&state.house_point_hud.remaining<=0&&state.star_hud_time<=0)return;
    const AuthoredDarkLightPush no_dark_lights{};
    vkCmdPushConstants(command_buffer, state.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
        sizeof(float) * 16, sizeof(no_dark_lights), &no_dark_lights);
    const auto key=std::string(state.health_flash_time>0&&std::fmod(state.health_flash_time,.4F)<.2F?"hurt_":"health_")+std::to_string(state.frontend.progress.health);
    const auto found=state.front_draws.find(key);
    vkCmdBindPipeline(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,state.frontend_pipeline);
    vkCmdBindDescriptorSets(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,state.pipeline_layout,0,1,&state.descriptor_set,1,&state.abyss_disabled_offset);
    constexpr VkDeviceSize offset=0;vkCmdBindVertexBuffers(command_buffer,0,1,&state.vertex_buffer,&offset);
    const auto mvp=MultiplyMatrices(view_projection,state.hud_transform);
    vkCmdPushConstants(command_buffer,state.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(float)*mvp.size(),mvp.data());
    if(!cutscene&&found!=state.front_draws.end())vkCmdDraw(command_buffer,found->second.count,1,found->second.first,0);
    if(state.house_point_hud.remaining>0){
        const auto draw=[&](const std::string& key){const auto range=state.front_draws.find(key);
            if(range!=state.front_draws.end())vkCmdDraw(command_buffer,range->second.count,1,range->second.first,0);};
        draw("house_points_badge");unsigned value=state.house_point_hud.Value(),place=0;
        const auto digits=std::to_string(value).size();
        do{draw("house_points_"+std::to_string(digits)+"_"+std::to_string(place++)+"_"+std::to_string(value%10));value/=10;}while(value);
    }
    if(state.star_hud_time>0&&state.house_point_hud.remaining<=0){
        const auto star=state.front_draws.find("star_pickup_"+std::to_string(std::min(8U,state.frontend.progress.challenge_stars)));
        if(star!=state.front_draws.end())vkCmdDraw(command_buffer,star->second.count,1,star->second.first,0);
    }
    if(cutscene)return;
    if(state.bean_hud_time>0){
        const auto& p=state.frontend.progress;
        const auto cards=std::ranges::count_if(state.beans,[&](const auto& b){return (b.kind==4||b.kind==1)&&std::ranges::binary_search(p.collected_beans,b.actor_reference);});
        const auto nonbeans=static_cast<std::size_t>(cards)+(IsWalkingChallenge(state.map_id)?p.challenge_stars:0U);
        const auto bean_count=p.banked_beans+p.collected_beans.size()-std::min(nonbeans,p.collected_beans.size());
        const auto beans=state.front_draws.find("hud_"+std::to_string(std::min<std::size_t>(512,bean_count)));
        if(beans!=state.front_draws.end())vkCmdDraw(command_buffer,beans->second.count,1,beans->second.first,0);
    }
    if(state.map_id==2&&state.broom.active){
        const auto draw=[&](const std::string& name){const auto range=state.front_draws.find(name);
            if(range!=state.front_draws.end())vkCmdDraw(command_buffer,range->second.count,1,range->second.first,0);};
        draw("broom_labels");draw("broom_0_"+std::to_string(state.broom.hits));
        draw("broom_1_"+std::to_string(std::min(180U,static_cast<unsigned>(std::ceil(broom::SessionTimeRemaining(state.broom.session))))));
        draw("broom_2_"+std::to_string(state.broom.stage+1));
    }
    if(state.map_id==0&&state.frontend.progress.quest_stage==20){
        const auto lesson=state.front_draws.find("lesson_"+std::to_string(state.frontend.progress.lesson_passes)+(CanCast()?"_ready":"_wait"));
        if(lesson!=state.front_draws.end())vkCmdDraw(command_buffer,lesson->second.count,1,lesson->second.first,0);
    }
    if(state.frontend.vr.voice_cast&&state.frontend.vr.voice_hints&&state.basic_cast.charging&&state.wand_lock_valid&&
       state.death_time<0){
        const auto label=state.front_draws.find(std::string(ActiveGestureSpell()==GestureSpell::Alohomora?"voice_aloho_":"voice_aim_")+
            std::to_string(state.voice_repeat.pending?5U:state.voice_status));
        const auto placement=PlaceVoiceHint(state.aim_minimum,state.aim_maximum,state.last_player);
        if(label!=state.front_draws.end()&&label->second.count&&placement.valid){
            const auto& right=placement.right;
            const auto& center=placement.center;
            const Matrix4 world{right[0],right[1],right[2],0,placement.up[0],placement.up[1],placement.up[2],0,
                placement.normal[0],placement.normal[1],placement.normal[2],0,center[0],center[1],center[2],1};
            const auto label_mvp=MultiplyMatrices(view_projection,world);
            // One prebuilt text draw while aiming; world depth prevents text
            // showing through walls and it is outside the reflection input.
            vkCmdBindPipeline(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,state.pipeline);
            vkCmdPushConstants(command_buffer,state.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,
                sizeof(float)*label_mvp.size(),label_mvp.data());
            vkCmdDraw(command_buffer,label->second.count,1,label->second.first,0);
            vkCmdBindPipeline(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,state.frontend_pipeline);
        }
    }
}

void QuestScene::RecordDeathFade(VkCommandBuffer command_buffer)const{
    const auto& s=*state_;if(!IsGpuReady()||s.death_time<0)return;
    vkCmdBindPipeline(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,s.death_pipeline);
    constexpr VkDeviceSize offset=0;vkCmdBindVertexBuffers(command_buffer,0,1,&s.guide_vertex_buffer,&offset);
    const Matrix4 clip{2,0,0,0,0,2,0,0,0,0,1,0,-1,0,0,1};
    const WandPushConstants push{clip,{0,0,0,std::clamp(s.death_time/.4F,0.0F,1.0F)}};
    vkCmdPushConstants(command_buffer,s.wand_pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
    vkCmdDraw(command_buffer,6,1,0,0);
}
void QuestScene::RecordWandDraw(
    const VkCommandBuffer command_buffer,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::array<float, 16>& wand_mvp,
    const std::array<float, 4>& color_multiplier) const {
    const State& state = *state_;
    if (!IsGpuReady() || state.map_id==2) {
        return;
    }
    VkViewport viewport{};
    viewport.y = static_cast<float>(height);
    viewport.width = static_cast<float>(width);
    viewport.height = -static_cast<float>(height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    VkRect2D scissor{};
    scissor.extent = {width, height};
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      state.wand_pipeline);
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1,
                           &state.wand_vertex_buffer, &offset);
    const WandPushConstants push{wand_mvp, color_multiplier};
    vkCmdPushConstants(command_buffer, state.wand_pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdDraw(command_buffer,
              static_cast<std::uint32_t>(state.wand_vertices.size()), 1, 0, 0);
}

void QuestScene::RecordGestureGuideDraw(
    const VkCommandBuffer command_buffer,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::array<float, 16>& view_projection,
    const GestureGuide& guide) const {
    const State& state = *state_;
    if (!IsGpuReady() || !guide.visible ||
        (guide.template_points.size() < 2&&guide.trail_points.size()<2)) return;
    VkViewport viewport{};
    viewport.y = static_cast<float>(height);
    viewport.width = static_cast<float>(width);
    viewport.height = -static_cast<float>(height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    VkRect2D scissor{};
    scissor.extent = {width, height};
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      state.wand_pipeline);
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1,
                           &state.guide_vertex_buffer, &offset);

    const auto record_polyline = [&](
        const std::vector<std::array<float, 3>>& points,
        const float ribbon_width,
        const float normal_offset,
        const std::array<float, 4>& color) {
        for (std::size_t index = 1; index < points.size(); ++index) {
            Matrix4 model{};
            if (!BuildRibbonModel(points[index - 1], points[index],
                                  guide.plane_normal, ribbon_width,
                                  normal_offset, &model)) continue;
            const WandPushConstants push{
                MultiplyMatrices(view_projection, model), color};
            vkCmdPushConstants(command_buffer, state.wand_pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push),
                               &push);
            vkCmdDraw(command_buffer,
                      static_cast<std::uint32_t>(kGuideQuad.size()), 1, 0, 0);
        }
    };
    record_polyline(guide.template_points, kTemplateRibbonWidthMeters, 0.0F,
                    {0.15F, 1.8F, 2.8F, 1.0F});
    std::array<float, 4> trail_color{2.8F, 1.0F, 0.15F, 1.0F};
    if (guide.trail_state == GestureVisualState::Accepted) {
        trail_color = {0.25F, 3.0F, 0.45F, 1.0F};
    } else if (guide.trail_state == GestureVisualState::Rejected) {
        trail_color = {3.0F, 0.25F, 0.25F, 1.0F};
    } else if (guide.trail_state == GestureVisualState::Canceled) {
        trail_color = {2.4F, 0.9F, 0.15F, 1.0F};
    }
    record_polyline(guide.trail_points, kTrailRibbonWidthMeters, 0.0F,
                    trail_color);
    if(state.feather_texture_layer&&ActiveGestureSpell()==GestureSpell::Wingardium&&!guide.trail_points.empty()){
        vkCmdBindPipeline(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,state.particle_pipeline);
        vkCmdBindDescriptorSets(command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,state.particle_pipeline_layout,
            0,1,&state.descriptor_set,1,&state.abyss_disabled_offset);
        vkCmdBindVertexBuffers(command_buffer,0,1,&state.particle_vertex_buffer,&offset);
        const float time=state.effects_clock.Seconds();
        for(unsigned i=0;i<16;++i){
            const float age=std::fmod(time+float(i)*.09375F,1.5F),phase=float(i)*2.39996F;
            const auto index=guide.trail_points.size()-1-std::min<std::size_t>(i/2,guide.trail_points.size()-1);
            auto center=guide.trail_points[index];
            center=AddVector(center,{.055F*age*std::sin(phase+time),-.045F*age,.045F*age*std::cos(phase+time)});
            const std::array<float,3> up{.5F*std::sin(time*2+phase),1,0};
            Matrix4 model{};const float size=.035F+.035F*std::sin(phase)*std::sin(phase);
            if(!BuildRibbonModel(AddVector(center,ScaleVector(up,-size*.5F)),AddVector(center,ScaleVector(up,size*.5F)),guide.plane_normal,size,0,&model))continue;
            const ParticlePushConstants push{MultiplyMatrices(view_projection,model),{1,1,1,.7F*(1-age/1.5F)},state.feather_texture_layer,{}, {0,0,1,1}};
            vkCmdPushConstants(command_buffer,state.particle_pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
            vkCmdDraw(command_buffer,static_cast<unsigned>(kParticleQuad.size()),1,0,0);
        }
    }
}

void QuestScene::RecordSpellDraw(
    const VkCommandBuffer command_buffer,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::array<float, 16>& view_projection) const {
    const State& state = *state_;
    const float effect_seconds = state.effects_clock.Seconds();
    if (!state.frontend.WorldVisible() || !IsGpuReady() ||
        (!state.broom.active && state.flames.empty() && state.glows.empty() && state.ambient_emitters.empty() && state.lock_emitters.empty() &&
         !state.charms.held_block && !state.basic_cast.charging && !state.basic_cast.flying &&
         !state.projectile.flying &&
         !state.projectile.impacting)) return;
    VkViewport viewport{};
    viewport.y = static_cast<float>(height);
    viewport.width = static_cast<float>(width);
    viewport.height = -static_cast<float>(height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    VkRect2D scissor{};
    scissor.extent = {width, height};
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    constexpr VkDeviceSize offset = 0;

    // TorchFire02 is an additive sprite emitter in the retail package.  Draw
    // several staggered crossed sprites from its owned PotFire08 texture
    // instead of approximating the flame with solid ribbon segments.
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      state.particle_pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            state.particle_pipeline_layout, 0, 1,
                            &state.descriptor_set, 1, &state.abyss_disabled_offset);
    vkCmdBindVertexBuffers(command_buffer, 0, 1,
                           &state.particle_vertex_buffer, &offset);
    constexpr std::size_t kParticlesPerEmitter = 5;
    auto* candle_output=static_cast<ParticleGpuVertex*>(state.candle_particle_mapped);
    std::uint32_t candle_vertices=0;
    const std::array<float,3> effect_head{state.effect_view_transform[12],state.effect_view_transform[13],state.effect_view_transform[14]};
    const auto append_sprite=[&](const std::array<float,3>& center,float size,
        const std::array<float,4>& color,bool core,bool whole_texture=false){
        if(!candle_output||candle_vertices+6>state.candle_particle_capacity)return;
        const std::array<float,3> up{state.effect_view_transform[4],state.effect_view_transform[5],state.effect_view_transform[6]};
        const std::array<float,3> normal{state.effect_view_transform[8],state.effect_view_transform[9],state.effect_view_transform[10]};
        Matrix4 model{};
        if(!BuildRibbonModel(AddVector(center,ScaleVector(up,-size*.5F)),
            AddVector(center,ScaleVector(up,size*.5F)),normal,size,0,&model))return;
        for(const auto& v:kParticleQuad){
            ParticleGpuVertex out=v;
            for(unsigned axis=0;axis<3;++axis)out.position[axis]=model[axis]*v.position[0]+model[4+axis]*v.position[1]+
                model[8+axis]*v.position[2]+model[12+axis];
            out.texture_uv[0]=(core?.501953125F:.001953125F)+v.texture_uv[0]*.49609375F;
            out.texture_uv[1]=.501953125F+v.texture_uv[1]*.49609375F;
            if(whole_texture)std::copy_n(v.texture_uv,2,out.texture_uv);
            std::copy(color.begin(),color.end(),out.color);
            candle_output[candle_vertices++]=out;
        }
    };
    for (const auto& flame : state.flames) {
        const bool candle=flame.scale<.5F;
        if(candle){
            // Use the shared head, never a per-eye list: both command buffers
            // consume the same mapped batch. GPU clipping handles the frustum.
            const auto delta=SubtractVector(flame.position,effect_head);
            if(DotVector(delta,delta)>28*28)continue;
            const float pulse=.94F+.06F*std::sin(effect_seconds*4.2F+flame.phase);
            auto center=flame.position;center[1]+=.025F;
            append_sprite(center,.44F*pulse,{1.0F,.78F,.25F,.40F},false);
            append_sprite(center,.095F,{1.6F,1.35F,.78F,.95F},true);
            continue;
        }
        const auto particles=candle?2U:kParticlesPerEmitter;
        for (std::size_t particle_index = 0;
             particle_index < particles; ++particle_index) {
            const float seed = static_cast<float>(particle_index) /
                               static_cast<float>(particles);
            const float age = std::fmod(
                effect_seconds * 0.92F + seed +
                    flame.phase * 0.013F,
                1.0F);
            const float life_fade = std::sin(age * 0.5F * kTau);
            const float lateral = std::sin(
                effect_seconds * 8.0F + flame.phase +
                static_cast<float>(particle_index) * 2.17F);
            const float size = (0.15F + 0.13F * age) * flame.scale;
            const std::array<float, 3> center{
                flame.position[0] + (candle?0:lateral * 0.024F * flame.scale),
                flame.position[1] + (candle?size*.5F:age * 0.20F * flame.scale),
                flame.position[2] + (candle?0:std::cos(lateral + flame.phase) *
                                        0.012F * flame.scale)};
            const std::array<float, 3> start{
                center[0], center[1] - size * 0.5F, center[2]};
            const std::array<float, 3> end{
                center[0], center[1] + size * 0.5F, center[2]};
            constexpr std::array<std::array<float, 3>, 2> normals{{
                {1.0F, 0.0F, 0.0F},
                {0.0F, 0.0F, 1.0F},
            }};
            for (const auto& normal : normals) {
                Matrix4 model{};
                if (!BuildRibbonModel(start, end, normal, size, 0.0F,
                                      &model)) continue;
                const ParticlePushConstants push{
                    MultiplyMatrices(view_projection, model),
                    {1.42F, 1.02F, 0.72F,
                     (0.26F + 0.74F * life_fade) * 0.72F},
                    state.fire_texture_layer,{}, {.001953125F,.001953125F,.49609375F,.49609375F}};
                vkCmdPushConstants(command_buffer,
                                   state.particle_pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT, 0,
                                   sizeof(push), &push);
                vkCmdDraw(command_buffer,
                          static_cast<std::uint32_t>(kParticleQuad.size()),
                          1, 0, 0);
            }
        }
    }

    std::array<AmbientParticle,ambient::kMaximumAmbientEmitters*ambient::kMaximumAmbientParticlesPerEmitter> ambient_particles{};
    const auto ambient_count=BuildAmbientParticles(state.ambient_emitters,effect_seconds,effect_head,
        ambient_particles.data(),ambient_particles.size());
    for(std::size_t i=0;i<ambient_count;++i){const auto& p=ambient_particles[i];append_sprite(p.position,p.size_m,p.color,false);}
    if(candle_vertices){
        const ParticlePushConstants push{view_projection,{1,1,1,1},state.fire_texture_layer,{},
            {0,0,1,1}};
        vkCmdPushConstants(command_buffer,state.particle_pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
        vkCmdBindVertexBuffers(command_buffer,0,1,&state.candle_particle_buffer,&offset);
        vkCmdDraw(command_buffer,candle_vertices,1,0,0);
        vkCmdBindVertexBuffers(command_buffer,0,1,&state.particle_vertex_buffer,&offset);
    }
    if(!state.lock_emitters.empty()){
        auto emitters=state.lock_emitters;
        for(auto& e:emitters){
            e.enabled=e.enabled&&!ChallengeActivated(state.frontend.progress,e.actor_reference);
            if(const auto attached=state.charms.prop_attachments.find(e.actor_reference);attached!=state.charms.prop_attachments.end())
                for(const auto& door:state.doors)if(door.actor_reference==attached->second){
                    e.position=AddVector(e.position,SubtractVector(MoverPoint(door,door.pivot),door.pivot));break;
                }
        }
        const auto first=candle_vertices;
        const auto count=BuildAmbientParticles(emitters,effect_seconds,effect_head,ambient_particles.data(),ambient_particles.size());
        for(std::size_t i=0;i<count;++i){const auto& p=ambient_particles[i];append_sprite(p.position,p.size_m,p.color,false,true);}
        if(candle_vertices>first){
            const ParticlePushConstants push{view_projection,{1,1,1,1},state.lock_texture_layer,{}, {0,0,1,1}};
            vkCmdPushConstants(command_buffer,state.particle_pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
            vkCmdBindVertexBuffers(command_buffer,0,1,&state.candle_particle_buffer,&offset);
            vkCmdDraw(command_buffer,candle_vertices-first,1,first,0);
            vkCmdBindVertexBuffers(command_buffer,0,1,&state.particle_vertex_buffer,&offset);
        }
    }
    if(state.feather_texture_layer&&state.charms.held_block){
        const auto block=state.charms.blocks.find(state.charms.held_block);
        if(block!=state.charms.blocks.end()){
            AmbientParticleEmitter feathers;feathers.actor_reference=state.charms.held_block;
            feathers.position=AddVector(ScaleVector(AddVector(block->second.minimum,block->second.maximum),.5F),block->second.offset);
            feathers.radial=true;feathers.source_width_m=.2F;feathers.source_height_m=.2F;feathers.source_depth_m=.2F;
            feathers.speed_mps=.6F;feathers.lifetime=1;feathers.lifetime_range=1;
            feathers.size_m=.12F;feathers.size_range_m=.12F;feathers.size_end_scale=0;feathers.rate=20;
            const auto first=candle_vertices;
            const auto count=BuildAmbientParticles({feathers},effect_seconds,effect_head,ambient_particles.data(),16);
            for(std::size_t i=0;i<count;++i){const auto& p=ambient_particles[i];append_sprite(p.position,p.size_m,p.color,false,true);}
            const auto path=SubtractVector(feathers.position,state.charms.wand_tip);
            const float length=std::sqrt(DotVector(path,path));
            if(length>.05F)for(unsigned i=0;i<32;++i){
                const float phase=std::fmod(effect_seconds*2.8F/std::max(.5F,length)+float(i)/32.0F,1.0F);
                auto position=AddVector(state.charms.wand_tip,ScaleVector(path,phase));
                const float flutter=std::sin(phase*kTau)*.055F;
                position[0]+=flutter*std::sin(float(i)*2.4F+effect_seconds*5);
                position[1]+=flutter*std::cos(float(i)*1.7F+effect_seconds*4);
                append_sprite(position,.09F+.05F*std::sin(phase*kTau*.5F),{1,1,1,.85F},false,true);
            }
            if(candle_vertices>first){
                const ParticlePushConstants push{view_projection,{1,1,1,1},state.feather_texture_layer,{}, {0,0,1,1}};
                vkCmdPushConstants(command_buffer,state.particle_pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
                vkCmdBindVertexBuffers(command_buffer,0,1,&state.candle_particle_buffer,&offset);
                vkCmdDraw(command_buffer,candle_vertices-first,1,first,0);
                vkCmdBindVertexBuffers(command_buffer,0,1,&state.particle_vertex_buffer,&offset);
            }
        }
    }
    // One batched, animated original-texture emitter; same placement for both eyes.
    if(state.map_id==2&&state.broom.active&&state.broom_particle_mapped){
        const auto count=BuildBroomHoopVertices(state.broom,state.broom_visuals,state.broom.seconds,state.effect_view_transform,
            static_cast<ParticleGpuVertex*>(state.broom_particle_mapped),kBroomHoopVertexCapacity);
        const ParticlePushConstants push{view_projection,{1,1,1,1},state.broom_visuals.texture_layer,{}, {0,0,1,1}};
        vkCmdPushConstants(command_buffer,state.particle_pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
        vkCmdBindVertexBuffers(command_buffer,0,1,&state.broom_particle_buffer,&offset);
        vkCmdDraw(command_buffer,static_cast<std::uint32_t>(count),1,0,0);
        vkCmdBindVertexBuffers(command_buffer,0,1,&state.particle_vertex_buffer,&offset);
    }
    // Keep ordinary depth testing so walls still occlude the target; the whole
    // marker plane is in front of the object's bounds instead of inside it.
    if(state.basic_cast.charging&&state.aim_actor>0&&state.target_marker.valid()){
        const auto spell=static_cast<unsigned>(ActiveGestureSpell());
        const auto& marker=spell?state.charms_markers[spell-1]:state.target_marker;
        std::uint32_t marker_first=static_cast<std::uint32_t>(kParticleQuad.size());
        if(spell)marker_first+=static_cast<std::uint32_t>(state.target_marker.vertices.size());
        if(spell>1)marker_first+=static_cast<std::uint32_t>(state.charms_markers[0].vertices.size());
        const auto placement=PlaceTargetMarker(state.aim_minimum,state.aim_maximum,state.last_player);
        Matrix4 model{};
        if(placement.valid&&BuildRibbonModel(
            AddVector(placement.center,ScaleVector(placement.up,-placement.size*.5F)),
            AddVector(placement.center,ScaleVector(placement.up,placement.size*.5F)),
            placement.normal,placement.size,0,&model)){
            const ParticlePushConstants push{MultiplyMatrices(view_projection,model),
                {1.8F,1.65F,1.5F,.9F},state.target_marker_texture_layer};
            vkCmdPushConstants(command_buffer,state.particle_pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
            vkCmdDraw(command_buffer,static_cast<std::uint32_t>(kTargetMarkerVerticesPerFrame),1,
                marker_first+marker.first_vertex(effect_seconds),0);
        }
    }
    if((state.projectile.flying||state.projectile.impacting)&&state.spell_particle_mapped){
        const auto& cast=state.projectile;
        const float flight=cast.terminal_distance_m/kFlipendoSpeedMetersPerSecond;
        const float elapsed=cast.flying?cast.distance_m/kFlipendoSpeedMetersPerSecond:flight+cast.impact_seconds;
        const auto particles=BuildOriginalSpellParticles(cast.origin,cast.direction,elapsed,flight,cast.impacting,cast.target.serial);
        auto* output=static_cast<ParticleGpuVertex*>(state.spell_particle_mapped);unsigned count=0;
        for(const auto& p:particles){
            std::array<float,3> normal{},right{},up{};
            if(!NormalizeVector(SubtractVector(state.last_player,p.center),&normal))continue;
            if(!NormalizeVector(CrossVector({0,1,0},normal),&right))right={1,0,0};
            up=CrossVector(normal,right);
            const auto rotated_right=AddVector(ScaleVector(right,std::cos(p.rotation)),ScaleVector(up,std::sin(p.rotation)));
            const auto rotated_up=SubtractVector(ScaleVector(up,std::cos(p.rotation)),ScaleVector(right,std::sin(p.rotation)));
            for(const auto& uv:std::array<std::array<float,2>,6>{{{0,0},{0,1},{1,1},{0,0},{1,1},{1,0}}}){
                const auto pos=AddVector(p.center,AddVector(ScaleVector(rotated_right,(uv[0]-.5F)*p.size),ScaleVector(rotated_up,(.5F-uv[1])*p.size)));
                output[count++]={{pos[0],pos[1],pos[2]},{uv[0],uv[1]},{p.color[0],p.color[1],p.color[2],p.color[3]}};
            }
        }
        const ParticlePushConstants push{view_projection,{1,1,1,1},state.fire_texture_layer,{},
            {.501953125F,.001953125F,.49609375F,.49609375F}};
        vkCmdPushConstants(command_buffer,state.particle_pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
        vkCmdBindVertexBuffers(command_buffer,0,1,&state.spell_particle_buffer,&offset);
        vkCmdDraw(command_buffer,count,1,0,0);
        vkCmdBindVertexBuffers(command_buffer,0,1,&state.particle_vertex_buffer,&offset);
    }
    if(state.basic_cast.flying){
        const auto& cast=state.basic_cast;
        const float phase=std::clamp(cast.age/cast.duration,0.0F,1.0F);
        for(unsigned i=0;i<12;++i){
            const float trail=std::max(0.0F,phase-float(i)*0.035F);
            const auto center=AddVector(cast.origin,ScaleVector(
                SubtractVector(cast.destination,cast.origin),trail));
            const float size=0.10F+trail*0.22F+float(i)*0.006F;
            Matrix4 model{};
            const auto normal=SubtractVector(state.last_player,center);
            if(!BuildRibbonModel(AddVector(center,{0,-size/2,0}),AddVector(center,{0,size/2,0}),
                                 normal,size,0,&model))continue;
            const ParticlePushConstants push{MultiplyMatrices(view_projection,model),
                {0.7F,0.83F,1.0F,(1.0F-phase)*0.26F},state.smoke_texture_layer};
            vkCmdPushConstants(command_buffer,state.particle_pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,
                               0,sizeof(push),&push);
            vkCmdDraw(command_buffer,static_cast<std::uint32_t>(kParticleQuad.size()),1,0,0);
        }
    }
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      state.effect_pipeline);

    vkCmdBindVertexBuffers(command_buffer, 0, 1,
                           &state.glow_vertex_buffer, &offset);
    const auto draw_radial_glow = [&](const GlowEmitter& glow,
                                      const float pulse,
                                      const std::array<float, 3>& normal) {
        const float radius = glow.radius * pulse;
        const std::array<float, 3> start{
            glow.position[0], glow.position[1] - radius, glow.position[2]};
        const std::array<float, 3> end{
            glow.position[0], glow.position[1] + radius, glow.position[2]};
        Matrix4 model{};
        if (!BuildRibbonModel(start, end, normal, radius * 2.0F, 0.0F,
                              &model)) return;
        const WandPushConstants push{
            MultiplyMatrices(view_projection, model),
            {2.45F, 1.28F, 0.32F, 0.13F * glow.intensity}};
        vkCmdPushConstants(command_buffer, state.wand_pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDraw(command_buffer,
                  static_cast<std::uint32_t>(state.glow_vertices.size()),
                  1, 0, 0);
    };
    if(state.basic_cast.charging){
        const auto& aim=state.basic_cast.aim;
        std::array<float,3> normal{},side{};
        if(NormalizeVector(SubtractVector(state.last_player,aim),&normal) &&
           NormalizeVector(CrossVector({0,1,0},normal),&side)){
            for(unsigned i=0;i<2;++i){
                const float angle=effect_seconds*5.0F+float(i)*kTau*0.5F;
                auto center=AddVector(aim,ScaleVector(side,std::cos(angle)*0.09F));
                center[1]+=std::sin(angle)*0.09F;
                Matrix4 model{};
                if(!BuildRibbonModel(AddVector(center,{0,-0.055F,0}),AddVector(center,{0,0.055F,0}),
                                     normal,0.11F,0,&model))continue;
                const WandPushConstants push{MultiplyMatrices(view_projection,model),
                    i==0?std::array<float,4>{2,0.03F,0.03F,0.9F}:std::array<float,4>{0.03F,2,2,0.9F}};
                vkCmdPushConstants(command_buffer,state.wand_pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,
                                   0,sizeof(push),&push);
                vkCmdDraw(command_buffer,static_cast<std::uint32_t>(state.glow_vertices.size()),1,0,0);
            }
        }
    }
    for (const auto& glow : state.glows) {
        const float pulse = 0.94F + 0.06F * std::sin(
            effect_seconds * 7.0F + glow.phase);
        draw_radial_glow(glow, pulse, {1.0F, 0.0F, 0.0F});
        draw_radial_glow(glow, pulse, {0.0F, 0.0F, 1.0F});
        draw_radial_glow(glow, pulse,
                         {0.70710678F, 0.0F, 0.70710678F});
    }

}

bool QuestScene::IsFrontEndVisible() const {return state_->frontend.Visible();}
bool QuestScene::IsWorldPaused() const {return state_->frontend.PausesWorld();}
unsigned QuestScene::LessonRound() const {
    if(state_->map_id==3&&state_->charms.active_lesson>=0)return state_->charms.lesson.round;
    return state_->map_id==0&&state_->frontend.progress.quest_stage==20?state_->frontend.progress.lesson_passes:0;
}
bool QuestScene::ConsumeCommunityRequest(){
    const bool requested=state_->community_requested;state_->community_requested=false;return requested;
}
void QuestScene::UpdateExitTracking(const ViewPose& local_head,const ViewPose& reference,bool valid){
    ViewPose camera;bool first_person=false;
    state_->exit_return.valid=false;
    if(valid&&state_->tracking_active&&GetCinematicCameraPose(&camera,&first_person)&&first_person)
        (void)state_->exit_return.Capture(camera,local_head,reference);
}
bool QuestScene::ConsumePlayerPlacement(std::array<float,3>* position,float* yaw) {
    if(!position||!yaw||!state_->restore_pending)return false;
    *position=state_->placement.player;*yaw=state_->placement.yaw;
    if(state_->map_id==2){state_->broom.session.previous_valid=false;broom::ResetFlight(state_->broom.motion);}
    state_->restore_pending=false;state_->platform_transport={};state_->first_step.Reset();return true;
}
bool QuestScene::ConsumePlayerTransport(std::array<float,3>* displacement) {
    if(!displacement)return false;
    *displacement=state_->platform_transport;state_->platform_transport={};
    return DotVector(*displacement,*displacement)>0;
}
void QuestScene::StartRonEncounter(){
    auto& state=*state_;
    if(state.jump.active||state.climb.active)return;
    state.intro_cutscene=state.ron_intro;
    state.intro_cutscene.playing=true;
    state.frontend.progress.quest_stage=2;
    for(auto& track:state.intro_cutscene.tracks){
        if(track.actor_reference==kHarryActorReference){
            track.position=state.last_player;
            track.position[1]-=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;
        }
        for(auto& actor:state.character_draws)if(actor.actor_reference==track.actor_reference){
            actor.enabled=actor.actor_reference!=1390; // This authored track contains only disabled moves.
            actor.active_clip="breathe";actor.animation_time=0;
            actor.cutscene_offset=SubtractVector(track.position,actor.base_origin);
        }
    }
    state.audio.StopDialogue();
    GroundCutsceneCast(state.intro_cutscene,state.character_draws,state.collision_triangles);
    SaveCheckpoint();
    HPVR_LOGI("[hpvr.quest.first] status=RON_ENCOUNTER object=CutScene51 source=OWNED_SCRIPT");
}
void QuestScene::StartTwinsEncounter(){
    auto& state=*state_;if(state.jump.active||state.climb.active)return;
    state.intro_cutscene=state.twins_intro;state.intro_cutscene.playing=true;
    state.frontend.progress.quest_stage=5;
    for(auto& track:state.intro_cutscene.tracks)for(auto& actor:state.character_draws)
        if(actor.actor_reference==track.actor_reference){
            if(actor.actor_reference==kHarryActorReference||actor.actor_reference==1348)
                track.position=AddVector(actor.base_origin,actor.cutscene_offset);
            if(actor.actor_reference==kHarryActorReference){
                track.position=state.last_player;track.position[1]-=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;
            }
            actor.enabled=true;actor.active_clip="breathe";actor.animation_time=0;
            actor.cutscene_offset=SubtractVector(track.position,actor.base_origin);
        }
    GroundCutsceneCast(state.intro_cutscene,state.character_draws,state.collision_triangles);
    state.audio.StopDialogue();SaveCheckpoint();
    HPVR_LOGI("[hpvr.quest.twins] status=STARTED scene=CutScene52 lesson=CLIMB");
}
bool QuestScene::CanCast() const{
    const auto& s=*state_;
    if(!MapAllowsSpellInput(s.map_id,SpellInputPath::Gesture))return false;
    if(s.death_time>=0)return false;
    if(IsWalkingChallenge(s.map_id))return s.tracking_active&&!s.challenge.complete&&!IsCutscenePlaying()&&!IsFrontEndVisible()&&
        (s.map_id!=3||s.charms.active_lesson<0||(s.charms.lesson_wait==0&&!s.charms.finish_pending&&!s.audio.DialogueBusy()));
    const bool lesson=s.frontend.progress.quest_stage==20&&s.lesson_intro_started&&
        s.lesson_wait==0&&!s.audio.DialogueBusy()&&!s.lesson_finish_pending&&s.frontend.progress.lesson_passes<4;
    return s.tracking_active&&(lesson||s.frontend.progress.quest_stage==23)&&!IsCutscenePlaying()&&!IsFrontEndVisible();
}
bool QuestScene::IsGestureLesson() const{return (state_->map_id==0&&state_->frontend.progress.quest_stage==20)||
    (state_->map_id==3&&state_->charms.active_lesson>=0);}
bool QuestScene::WantsGesture() const{return CanCast()&&(IsGestureLesson()||UsesGestureCasting(state_->frontend.vr.casting_mode));}
bool QuestScene::GestureTargetLocked() const{return CanCast()&&(IsGestureLesson()||(state_->wand_lock_valid&&!state_->wand_cast_consumed));}
void QuestScene::SetVoiceStatus(unsigned status){
    status=std::min(status,7U);
    if(state_->voice_status!=status){
        state_->voice_status=status;
        HPVR_LOGI("[hpvr.quest.voice] status=STATE_CHANGE state=%u waveform_log=NONE",status);
    }
}
bool QuestScene::VoiceCaptureAllowed() const{
    // Only the supported voice-casting map may keep input ready. CanCast
    // excludes death, completion, menus, cutscenes and inactive tracking.
    return MapAllowsSpellInput(state_->map_id,SpellInputPath::Voice)&&CanCast()&&!IsGestureLesson()&&
        (ActiveGestureSpell()==GestureSpell::Flipendo||ActiveGestureSpell()==GestureSpell::Alohomora||
         ActiveGestureSpell()==GestureSpell::Wingardium);
}
std::int32_t QuestScene::VoiceTarget(std::array<float,3>* point) const{
    const auto& s=*state_;
    if(!VoiceCaptureAllowed()||!s.tracking_active||s.frontend.Visible()||IsCutscenePlaying()||
       !s.basic_cast.charging||s.wand_cast_consumed||s.audio.SpeechBusy())return 0;
    if(point)*point=s.wand_lock_valid?s.wand_lock_point:s.basic_cast.aim;
    return s.wand_lock_valid?s.wand_lock_actor:s.aim_actor;
}
bool QuestScene::DispatchVoiceCast(std::int32_t target,const std::array<float,3>&,const ViewPose& wand){
    auto& s=*state_;Matrix4 model{};
    std::array<float,3> point{};
    if(!s.frontend.vr.voice_cast||target<=0||VoiceTarget(&point)!=target||!BuildRigidTransform(wand,&model))return false;
    const std::array<float,3> forward{-model[8],-model[9],-model[10]};
    const auto tip=AddVector({model[12],model[13],model[14]},ScaleVector(forward,.34F));
    std::array<float,3> direction{};if(!NormalizeVector(SubtractVector(point,tip),&direction))return false;
    s.wand_lock_valid=true;s.wand_lock_actor=target;s.wand_lock_point=point;
    LaunchChallengeSpell(tip,direction,tip,++s.automatic_cast_serial);
    // Keep the target and trigger hold, but require a NEW utterance after the
    // game's incantation and its speaker tail have finished.
    s.wand_lock_valid=true;s.wand_lock_actor=target;s.wand_lock_point=point;
    s.basic_cast.charging=true;s.basic_cast.require_release=false;s.wand_cast_consumed=true;
    s.voice_repeat.Begin();s.voice_cast_this_hold=true;
    HPVR_LOGI("[hpvr.quest.voice] status=CAST keyword=%s target=%d",
        ActiveGestureSpell()==GestureSpell::Alohomora?"ALOHOMORA":
        ActiveGestureSpell()==GestureSpell::Wingardium?"WINGARDIUM":"FLIPENDO",target);
    return true;
}
void QuestScene::SetSupportedRefreshRates(const std::vector<int>& rates){
    if(state_->frontend.refresh_rates!=rates)state_->frontend.refresh_rates=rates;
}
void QuestScene::SetTrackingActive(bool active){
    auto& s=*state_;
    if(s.map_id==2&&s.tracking_active!=active){s.broom.session.previous_valid=false;broom::ResetFlight(s.broom.motion);}
    s.tracking_active=active;
    if(!active)s.exit_return.valid=false;
    s.audio.SetPresentationAudio(active&&s.frontend.WorldVisible(),!active||s.frontend.PausesAudio());
}
void QuestScene::StartTutorialScene(bool reward){
    auto& s=*state_;s.intro_cutscene=reward?s.card_scene:s.lesson_exit;
    s.exit_return={};
    s.intro_cutscene.playing=true;s.frontend.progress.quest_stage=reward?21U:22U;
    for(auto& t:s.intro_cutscene.tracks){
        if(t.actor_reference==1477)t.actor_reference=1329;
        if(t.actor_reference==1506)t.actor_reference=1326;
        for(auto& a:s.character_draws)if(t.actor_reference==a.actor_reference){
            t.position=AddVector(a.base_origin,a.cutscene_offset);
            if(t.actor_reference==kHarryActorReference){t.position=s.last_player;t.position[1]-=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;}
            a.enabled=true;a.active_clip="breathe";a.animation_time=0;
        }
    }
    GroundCutsceneCast(s.intro_cutscene,s.character_draws,s.collision_triangles);
    s.audio.StopDialogue();SaveCheckpoint();
}
void QuestScene::RejectLessonGesture(){
    if(state_->map_id==3){SubmitCharmsLesson(0);return;}
    auto& s=*state_;if(s.map_id!=0||s.frontend.progress.quest_stage!=20||s.lesson_wait>0)return;
    // The original lesson repeats tier one; later failures finish with the
    // tiers already earned. Relaxed practice may retry every tier instead.
    s.lesson_finish_pending=!s.frontend.vr.relaxed_lesson&&s.frontend.progress.lesson_passes>0;
    if(auto i=GameplayDialogueIndex(s.frontend.assets,"quirrell_lesson_43")){
        (void)s.audio.PlayDialogue(*i);s.lesson_wait=s.audio.DialogueDurationSeconds(*i);
    }
}
void QuestScene::StartStoryEncounter(unsigned index){
    auto& state=*state_;if(index>=state.story_encounters.size()||state.jump.active||state.climb.active)return;
    state.intro_cutscene=state.story_encounters[index];state.intro_cutscene.playing=true;
    // A replaced scene must not leave an orphaned locomotion animation.
    for(auto& a:state.character_draws)if(a.active_clip=="run"||a.active_clip=="walk")a.active_clip="breathe";
    if(index==0&&state.frontend.progress.quest_stage>=16)
        state.frontend.progress.filch_resume_stage=state.frontend.progress.quest_stage;
    state.frontend.progress.quest_stage=13+index*2;
    for(auto& track:state.intro_cutscene.tracks)for(auto& actor:state.character_draws)
        if(actor.actor_reference==track.actor_reference){
            if(actor.actor_reference==kHarryActorReference){track.position=state.last_player;
                track.position[1]-=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;}
            actor.cutscene_offset=SubtractVector(track.position,actor.base_origin);
            actor.enabled=true;actor.active_clip="breathe";actor.animation_time=0;
        }
    GroundCutsceneCast(state.intro_cutscene,state.character_draws,state.collision_triangles);
    state.audio.StopDialogue();SaveCheckpoint();
    HPVR_LOGI("[hpvr.quest.story] status=STARTED scene=%s",state.intro_cutscene.object_name.c_str());
}
void QuestScene::StartJumpLesson(bool finish){
    auto& state=*state_;if(state.jump.active||state.climb.active)return;
    if(!finish)state.bean_twins_staged=false;
    MoveTwinsToNextRoom(finish); // Repair saved C30 actors stuck in walls too.
    state.intro_cutscene=finish?state.jump_finish:state.next_room;
    state.intro_cutscene.playing=true;state.frontend.progress.quest_stage=finish?11U:9U;
    for(auto& track:state.intro_cutscene.tracks){
        if(track.actor_reference==1346||track.actor_reference==1477)track.actor_reference=1329;
        if(track.actor_reference==1233||track.actor_reference==1506)track.actor_reference=1326;
        if(track.actor_reference==kHarryActorReference){
            track.position=state.last_player;track.position[1]-=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;
        }
        for(auto& actor:state.character_draws)if(actor.actor_reference==track.actor_reference){
            if(track.actor_reference==1329||track.actor_reference==1326)
                track.position=AddVector(actor.base_origin,actor.cutscene_offset);
            actor.enabled=true;actor.active_clip="breathe";actor.animation_time=0;
            actor.yaw=actor.desired_yaw;
        }
    }
    GroundCutsceneCast(state.intro_cutscene,state.character_draws,state.collision_triangles);
    state.audio.StopDialogue();SaveCheckpoint();
    HPVR_LOGI("[hpvr.quest.jump_lesson] status=STARTED scene=%s",state.intro_cutscene.object_name.c_str());
}
void QuestScene::StartTwinsTransition(bool after_peeves){
    auto& state=*state_;if(state.jump.active||state.climb.active)return;
    if(!after_peeves){
        MoveTwinsToNextRoom(false);state.frontend.progress.twins_departed=true;
        SaveCheckpoint();return;
    }
    if(ApplyFirstPeevesContact(state.frontend.progress))state.health_flash_time=2;
    state.intro_cutscene=after_peeves?state.peeves_departure:state.twins_transfer;
    state.intro_cutscene.playing=true;
    if(after_peeves){state.frontend.progress.peeves_phase=2;state.peeves_time=0;}
    else state.frontend.progress.twins_departed=true;
    for(auto& track:state.intro_cutscene.tracks){
        if(track.actor_reference==1346)track.actor_reference=1329;
        if(track.actor_reference==1233)track.actor_reference=1326;
        for(auto& actor:state.character_draws)if(actor.actor_reference==track.actor_reference){
            if(track.actor_reference!=1329&&track.actor_reference!=1326)
                track.position=AddVector(actor.base_origin,actor.cutscene_offset);
            if(track.actor_reference==kHarryActorReference){track.position=state.last_player;
                track.position[1]-=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;}
            actor.enabled=true;actor.active_clip="breathe";actor.animation_time=0;
        }
    }
    GroundCutsceneCast(state.intro_cutscene,state.character_draws,state.collision_triangles);
    state.audio.StopDialogue();SaveCheckpoint();
    HPVR_LOGI("[hpvr.quest.twins] status=AUTHORED_TRANSITION scene=%s camera=%s",state.intro_cutscene.object_name.c_str(),after_peeves?"THEATRICAL":"UNCHANGED");
}
void QuestScene::MoveTwinsToNextRoom(bool finish){
    auto& state=*state_;
    if(finish)state.bean_twins_staged=true;
    const auto& destination=finish?state.jump_finish:state.next_room;
    for(const auto& track:destination.tracks)for(auto& actor:state.character_draws){
        if(((track.actor_reference==1346||track.actor_reference==1477) && actor.actor_reference==1329) ||
           ((track.actor_reference==1233||track.actor_reference==1506) && actor.actor_reference==1326)){
            auto position=track.position;float floor=position[1];
            if(!finish)for(const auto& loc:state.twins_transfer.locations)
                if(AsciiFold(loc.alias)==(actor.actor_reference==1329?"fredloc":"georgeloc"))position=loc.position;
            if(FindPropGroundBelow(state.collision_triangles,position,&floor))position[1]=floor;
            actor.cutscene_offset=SubtractVector(position,actor.base_origin);
            actor.active_clip="breathe";actor.enabled=true;
            // Stage facing the player approach, not the discarded retail clone yaw.
            const auto facing=SubtractVector(destination.trigger_position,position);
            actor.yaw=actor.desired_yaw=std::atan2(facing[0],facing[2]);
            const auto i=static_cast<std::size_t>(&actor-state.character_draws.data());
            (void)state.spell_targets.SetSceneOffset(i,actor.cutscene_offset);
        }
    }
}
void QuestScene::StartOpening(){
    State& state=*state_;
    state.reward_approach={};state.health_flash_time=0;state.lesson_ghost_time=0;state.card_pickup={};state.pickup_flights.clear();
    state.jump={};state.jump_pending=false;state.climb={};
    state.bean_twins_staged=false;
    state.peeves_time=0;state.lesson_intro_started=false;state.lesson_wait=0;state.peeves_hit=false;state.damage_cooldown=0;
    state.intro_cutscene=state.initial_intro;
    state.intro_cutscene.playing=true;
    state.frontend.progress.quest_stage=0;
    state.children.pending.clear();state.children.actors.clear();state.children.time=0;
    state.bump_states.clear();state.bump_actor=0;state.bump_cooldown=2;
    state.children.spawned=state.children.destroyed=state.children.ground_misses=0;
    for(auto& spawner:state.children.spawners)spawner.next_choice=0;
    for(auto& door:state.doors){door.phase=0;door.opening=false;}
    for(std::size_t i=0;i<state.character_draws.size();++i){
        auto& actor=state.character_draws[i];actor.cutscene_offset={};
        actor.enabled=actor.actor_reference==1672 || actor.actor_reference==kHarryActorReference||IsClassroomActor(actor.actor_reference);
        actor.yaw=actor.desired_yaw=actor.base_yaw;actor.animation_time=0;actor.active_clip="breathe";
        (void)state.spell_targets.SetSceneOffset(i,{});
    }
    GroundCutsceneCast(state.intro_cutscene,state.character_draws,state.collision_triangles);
    state.frontend.BeginGame();state.frontend.progress.phase=1;state.frontend.progress.page=14;
    state.frontend.screen=FrontScreen::Objective;state.frontend.selection=0;
    state.frontend.paused=FrontScreen::Game;
    if(!state.frontend.Save())return;
    state.audio.StopDialogue();state.audio.SelectMusic(2);
    HPVR_LOGI("[hpvr.quest.cutscene] status=STARTED object=CutScene4 trigger=NEW_GAME vr_camera=AUTHORED_TARGET_HEAD_RELATIVE");
}
void QuestScene::RestoreCurrentProgress(){
    auto& state=*state_;auto& front=state.frontend;
    if(state.map_id!=0){RestoreTransferredProgress(front.progress,front.slot);return;}
        state.lesson_finish_pending=false;state.first_step.Reset();state.exit_return={};
        state.reward_approach={};
        state.lesson_ghost_time=0;
        state.jump={};state.jump_pending=false;
        state.climb={};
        state.audio.StopDialogue();
        if(front.progress.phase==0){front.BeginStory(front.progress.page);state.audio.SelectMusic(1);
            state.intro_cutscene.playing=false;}
        else if(front.progress.phase==1)StartOpening();
        else{
            state.intro_cutscene.playing=false;state.intro_cutscene.camera_active=false;
            state.children.pending.clear();state.children.actors.clear();
            for(auto& actor:state.character_draws){
                if(IsClassroomActor(actor.actor_reference))actor.enabled=true;
                if(actor.actor_reference==1348)actor.enabled=front.progress.quest_stage>=2;
                if(actor.actor_reference==1396||actor.actor_reference==1390)actor.enabled=false;
                if(actor.actor_reference==1329||actor.actor_reference==1326)actor.enabled=front.progress.quest_stage>=1;
            }
            const std::array<std::int32_t,11> ids{1672,kHarryActorReference,1348,1329,1326,1510,1627,1538,1618,1296,777};
            for(std::size_t j=0;j<ids.size();++j)for(std::size_t i=0;i<state.character_draws.size();++i){
                auto& actor=state.character_draws[i];if(actor.actor_reference!=ids[j])continue;
                if(j>=5)actor.enabled=true;
                const auto& pose=front.progress.cast[j];
                actor.cutscene_offset={pose[0],pose[1],pose[2]};
                auto feet=AddVector(actor.base_origin,actor.cutscene_offset);
                if(GroundScriptActor(state.collision_triangles,feet,feet,false,&feet))
                    actor.cutscene_offset=SubtractVector(feet,actor.base_origin);
                actor.yaw=actor.desired_yaw=pose[3];actor.active_clip="breathe";actor.animation_time=0;
                (void)state.spell_targets.SetSceneOffset(i,actor.cutscene_offset);
            }
            for(std::size_t j=0;j<2;++j){state.doors[j].phase=front.progress.doors[j];
                state.doors[j].opening=front.progress.doors[j]>0.5F;}
            for(auto& door:state.doors)if(door.tag=="fgsec1"){
                door.opening=front.progress.quest_stage>=6;door.phase=door.opening?1.0F:0.0F;
            }
            for(auto& door:state.doors)if(door.tag=="fgsec2"){
                door.opening=front.progress.card_awarded;door.phase=door.opening?1.0F:0.0F;
            }
            state.bump_states.clear();state.bump_actor=0;state.bump_cooldown=2;
            state.placement=front.progress;
            float floor;
            if(FindPropGroundBelow(state.collision_triangles,state.placement.player,&floor))
                state.placement.player[1]=floor+kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;
            state.restore_pending=true;
            front.BeginGame();state.audio.SelectMusic(3);
            if(front.progress.quest_stage==6){
                // C30 may have saved the first pair halfway into a wall.
                auto waiting=state.twins_intro;
                std::erase_if(waiting.tracks,[](const auto& t){return t.actor_reference!=1329&&t.actor_reference!=1326;});
                for(auto& t:waiting.tracks)for(const auto& loc:waiting.locations)
                    if(AsciiFold(loc.alias)==(t.actor_reference==1329?"fredloc":"georgeloc"))t.position=loc.position;
                PlaceWaitingTwins(waiting,state.character_draws,state.collision_triangles);
                front.progress.twins_departed=false;
            }
            if((front.progress.quest_stage==7||front.progress.quest_stage==8)&&front.progress.twins_departed)MoveTwinsToNextRoom();
            state.bean_twins_staged=false;
            state.peeves_time=0;state.lesson_intro_started=false;state.lesson_wait=0;state.peeves_hit=false;state.damage_cooldown=0;
            for(auto& actor:state.character_draws)if(actor.actor_reference==2968)actor.enabled=false;
            if(front.progress.quest_stage==10&&front.progress.peeves_phase==1)front.progress.peeves_phase=0;
            if(front.progress.quest_stage==10&&front.progress.peeves_phase==2){
                state.last_player=state.placement.player;StartTwinsTransition(true);
            }
            if(front.progress.quest_stage==10&&front.progress.peeves_phase==3)MoveTwinsToNextRoom(true);
            if(front.progress.quest_stage>=1&&front.progress.quest_stage<=4){
                PlaceWaitingTwins(state.twins_intro,state.character_draws,state.collision_triangles);
            }
            if(front.progress.quest_stage==2){
                state.last_player=state.placement.player;StartRonEncounter();
            }
            if(front.progress.quest_stage==5){
                state.last_player=state.placement.player;
                if(RonAtTwins(state.ron_lead,state.character_draws))StartTwinsEncounter();
                else front.progress.quest_stage=4; // Recover older saves with Ron downstairs.
            }
            if(front.progress.quest_stage==9||front.progress.quest_stage==11){
                state.last_player=state.placement.player;StartJumpLesson(front.progress.quest_stage==11);
            }
            if(front.progress.quest_stage>=13&&front.progress.quest_stage<20&&(front.progress.quest_stage%2)==1){
                state.last_player=state.placement.player;StartStoryEncounter((front.progress.quest_stage-13)/2);
            }
            if(front.progress.quest_stage==21||front.progress.quest_stage==22){
                state.last_player=state.placement.player;StartTutorialScene(front.progress.quest_stage==21);
            }
            HPVR_LOGI("[hpvr.quest.save] status=RESTORED slot=%u story=SKIPPED cutscene=SKIPPED",front.slot+1);
            if(front.progress.quest_stage==23)RequestChallengeTravel();
        }
}
void QuestScene::SaveCheckpoint(bool authored){
    State& state=*state_;auto& saved=state.frontend.progress;
    if(state.map_id==2){SaveBroomProgress();return;}
    if(state.death_time>=0)return;
    if(IsWalkingChallenge(state.map_id)){
        if(!state.challenge.graph.healthy())return;
        // A menu save persists the last book/level-start snapshot, never a
        // transient position on a moving platform or in front of a fall.
        if(!authored){
            if(state.challenge_start_checkpoint_valid){
                auto checkpoint=state.challenge_start_checkpoint;
                checkpoint.generation=std::max(checkpoint.generation,saved.generation);
                if(WriteProgress(state.frontend.saves,state.frontend.slot,&checkpoint)){
                    state.challenge_start_checkpoint=checkpoint;saved.generation=checkpoint.generation;
                }
            }
            return;
        }
        saved.map_id=state.map_id;saved.phase=2;saved.page=14;
        if(state.map_id==3)saved.charms_state=SaveCharmsState(state.charms);
        saved.player=SafeCheckpointHead(state.last_player,state.climb,state.jump);saved.yaw=state.last_yaw;
        saved.graph_state=state.challenge.graph.Serialize();saved.challenge_stars=state.challenge.graph.star_count();
        std::ostringstream physical;physical<<"CHALLENGE_WORLD 3 "<<std::setprecision(9)<<state.doors.size()<<' ';
        for(const auto& d:state.doors){physical<<d.actor_reference<<' '<<d.phase<<' '<<d.opening<<' '<<d.completion_sent<<' '<<d.hold<<' '<<d.loop_started<<' ';
            for(float v:d.grid_offset)physical<<v<<' ';for(float v:d.grid_target)physical<<v<<' ';
            physical<<std::quoted(movers::SaveMotion(d.motion))<<' ';}
        physical<<state.character_draws.size()<<' ';
        for(const auto& a:state.character_draws){physical<<a.actor_reference<<' '<<a.enabled<<' '<<a.yaw<<' ';
            for(float v:a.cutscene_offset)physical<<v<<' ';}
        physical<<state.challenge.barrel_stage<<' '<<state.challenge.barrel_time<<' '<<state.challenge.gnome_hits.size()<<' ';
        for(const auto& [ref,hits]:state.challenge.gnome_hits)physical<<ref<<' '<<hits<<' ';
        // A save during a scene restarts that scene with its authored initial commands.
        physical<<state.challenge.active_scene<<' '<<state.challenge.complete<<' '<<state.challenge.pending_scenes.size();
        for(auto ref:state.challenge.pending_scenes)physical<<' '<<ref;
        physical<<' '<<state.challenge.gnome_active.size();for(auto ref:state.challenge.gnome_active)physical<<' '<<ref;
        saved.world_state=physical.str();
        state.challenge_start_checkpoint=saved;state.challenge_start_checkpoint_valid=true;
        const bool committed=state.frontend.Save();
        if(committed)state.challenge_start_checkpoint=saved;
        HPVR_LOGI("[hpvr.quest.challenge.checkpoint] status=%s source=AUTHORED slot=%u",committed?"COMMITTED":"MEMORY_ONLY",state.frontend.slot+1);
        return;
    }
    // Keep inventory durable in mid-air, but resume from supported takeoff.
    if(state.frontend.screen==FrontScreen::Story || state.frontend.paused==FrontScreen::Story){
        if(state.frontend.screen!=FrontScreen::Game){saved.phase=0;saved.page=state.frontend.page;}
    } else if(IsCutscenePlaying() && state.intro_cutscene.object_name=="cutscene4"){saved.phase=1;saved.page=14;}
    else{
        saved.phase=2;saved.page=14;saved.player=SafeCheckpointHead(state.last_player,state.climb,state.jump);saved.yaw=state.last_yaw;
        const std::array<std::int32_t,11> ids{1672,kHarryActorReference,1348,1329,1326,1510,1627,1538,1618,1296,777};
        for(std::size_t j=0;j<ids.size();++j)for(const auto& actor:state.character_draws)
            if(actor.actor_reference==ids[j])saved.cast[j]={actor.cutscene_offset[0],
                actor.cutscene_offset[1],actor.cutscene_offset[2],actor.yaw};
        for(std::size_t j=0;j<std::min<std::size_t>(2,state.doors.size());++j)saved.doors[j]=state.doors[j].phase;
    }
    const bool ok=state.frontend.Save();
    HPVR_LOGI("[hpvr.quest.save] status=%s slot=%u phase=%u page=%u generation=%llu",
        ok?"COMMITTED":"FAILED",state.frontend.slot+1,saved.phase,saved.page,
        static_cast<unsigned long long>(saved.generation));
}
#include "quest_challenge_runtime.inl"
#include "quest_broom_runtime.inl"
#include "quest_charms_runtime.inl"

void QuestScene::SkipOpening(){
    State& state=*state_;
    state.frontend.BeginGame();state.audio.StopDialogue();state.restoring=true;
    for(unsigned i=0;i<8000 && state.intro_cutscene.playing;++i)Advance(0.05F);
    state.restoring=false;
    HPVR_LOGI("[hpvr.quest.cutscene.skip] status=%s",state.intro_cutscene.playing?"FAILED":"COMPLETE");
}
void QuestScene::UpdateFrontEnd(const LocomotionInput& input,bool confirm,bool back,
                                const ViewPose& head,float yaw){
    State& state=*state_;auto& front=state.frontend;
    state.last_player=head.position;state.last_yaw=yaw;
    if(state.map_id==2){
        Matrix4 pose{};
        if(BuildRigidTransform(head,&pose)){
            state.broom.input.planar_forward={-pose[8],0,-pose[10]};
            state.broom.input.forward=input.move_active?input.move_y:0;
            state.broom.input.strafe=input.move_active?input.move_x:0;
            state.broom.input.vertical=input.turn_active?input.turn_y:0;
            state.broom.boost=input.sprint;
        }
    }
    const bool was_visible=front.Visible();
    const auto old_progress=front.progress;
    const auto action=front.Input(input.move_active?input.move_y:0,confirm,back,input.move_active?input.move_x:0);
    switch(action){
    case FrontAction::OpenCommunity:state.community_requested=true;break;
    case FrontAction::StartSelectedLevel:{
        const auto prior=campaign::PerfectPriorProgress(front.selected_map);
        if(!prior){front.message="LEVEL PROGRESS PRESET IS NOT AVAILABLE";break;}
        state.travel_origin=old_progress;state.travel_progress={};
        state.travel_progress.map_id=front.selected_map;
        state.travel_progress.phase=front.selected_map!=0?2:1;state.travel_progress.page=14;
        state.travel_progress.lesson_passes=prior->lesson_passes;
        state.travel_progress.banked_beans=prior->banked_beans;
        state.travel_progress.earned_cards=prior->earned_cards;
        state.travel_progress.completed_maps=prior->completed_maps;
        state.travel_progress.house_points=prior->house_points;
        state.travel_progress.lesson_best=prior->lesson_best;
        state.travel_progress.lesson_points=prior->lesson_points;
        state.travel_progress.card_awarded=prior->card_awarded;
        state.travel_progress.card_taken=prior->card_taken;
        state.travel_slot=front.slot;state.travel_pending=true;state.travel_blocked=false;
        state.audio.StopDialogue();
        break;}
    case FrontAction::BeginLevel:
        front.BeginGame();state.audio.SelectMusic(state.map_id==0?2:front.assets.level_music_index);
        if(state.map_id==1&&front.progress.quest_stage==0&&!ChallengeActivated(front.progress,3606))StartChallengeScene(3606);
        if(state.map_id==2&&state.broom.phase==0&&!state.intro_cutscene.playing)StartBroomScene("intro",0);
        if(state.map_id==3&&front.progress.quest_stage==0){
            front.progress.quest_stage=1;
            for(const auto& [ref,scene]:state.challenge.scenes)if(scene.play_on_load&&!ChallengeActivated(front.progress,ref)){
                StartChallengeScene(ref);break;
            }
        }
        break;
    case FrontAction::NewGame:
        if(state.map_id!=0){
            state.travel_origin=old_progress;state.travel_progress={};state.travel_slot=front.slot;
            state.travel_pending=true;state.travel_blocked=false;break;
        }
        state.lesson_finish_pending=false;state.first_step.Reset();state.exit_return={};
        state.jump={};state.jump_pending=false;
        state.climb={};
        front.progress={};front.progress.player=head.position;front.progress.yaw=yaw;
        if(front.Save()){front.BeginStory(0);state.intro_cutscene.playing=false;
            state.audio.StopDialogue();state.audio.SelectMusic(1);}
        break;
    case FrontAction::Continue:
        if(front.progress.map_id!=state.map_id){
            state.travel_origin=old_progress;
            state.travel_progress=front.progress;state.travel_slot=front.slot;state.travel_pending=true;state.travel_blocked=false;
        }else RestoreCurrentProgress();
        break;
    case FrontAction::SaveMenu:
        SaveCheckpoint();state.audio.StopDialogue();state.audio.SelectMusic(0);break;
    case FrontAction::SkipScene:
        if(front.paused==FrontScreen::Story)StartOpening();
        else if(IsCutscenePlaying())SkipOpening();
        break;
    default:break;
    }
    const bool moving=input.move_active&&std::hypot(input.move_x,input.move_y)>.2F;
    if(state.first_step.Update(state.map_id==0&&state.tracking_active&&!front.vr.welcome_seen&&!front.Visible()&&
        !IsCutscenePlaying()&&!state.restore_pending&&!state.jump.active&&!state.climb.active&&front.progress.phase==2,moving,head.position)){
        front.ShowDemoNotice(false);state.front_anchor_valid=false;
        HPVR_LOGI("[hpvr.quest.demo] notice=WELCOME trigger=FIRST_ACTUAL_STEP");
    }
    if(front.Visible()&&!was_visible)state.front_anchor_valid=false;
    if(!front.Visible()&&!front.debug_pinned)state.front_anchor_valid=false;
    state.audio.SetPresentationAudio(front.WorldVisible(),front.PausesAudio());
    if(state.map_id==3){
        const auto distance=SubtractVector(state.last_player,state.ambient_position);
        const float gain=state.ambient_radius>0?std::clamp(1-std::sqrt(DotVector(distance,distance))/state.ambient_radius,0.0F,1.0F):0;
        state.audio.SetAmbientLoopGain(gain*gain*state.ambient_volume);
    }
}

void QuestScene::UpdateFrontPresentation(const ViewPose& rendered_head,const ViewPose* cinematic_rig,bool first_person,bool recapture){
    auto& state=*state_;
    // Shared center-eye pose, including the scripted rig. Presentation must not
    // replace the physical player position used by gameplay/checkpoints.
    if(state.tracking_active)(void)BuildRigidTransform(rendered_head,&state.effect_view_transform);
    const auto& front=state.frontend;
    if(!state.tracking_active||(!front.Visible()&&!front.debug_pinned))return;
    // Presentation only: never overwrite last_player/yaw with a spectator pose.
    state.front_anchor_valid=state.front_anchor.Update(rendered_head,cinematic_rig,first_person,
        recapture||!state.front_anchor_valid,front.FloatingPanel()||front.debug_pinned?.65F:1.0F,&state.front_transform);
}

void QuestScene::UpdatePlayerPose(const ViewPose& head,float yaw,const std::array<float,3>& capsule_center){
    if(std::isfinite(yaw)&&std::ranges::all_of(head.position,[](float v){return std::isfinite(v);})&&
       std::ranges::all_of(capsule_center,[](float v){return std::isfinite(v);})){
        // This is the post-locomotion pose, not the cutscene presentation pose.
        // Platform transport must add to this frame's walk, not overwrite it.
        state_->last_player=head.position;state_->last_yaw=yaw;
        state_->player_capsule=capsule_center;state_->player_capsule_valid=true;
    }
}
void QuestScene::UpdateHudPose(const ViewPose& head){
    if(BuildRigidTransform(head,&state_->hud_transform))
        for(unsigned i=0;i<3;++i)state_->hud_transform[12+i]-=2.5F*state_->hud_transform[8+i];
}
bool QuestScene::NeedsPhysicsTick() const{return state_->map_id!=0||state_->jump.active||state_->jump_pending||state_->climb.active;}
void QuestScene::UpdateJumpInput(bool held,float seconds){
    auto& state=*state_;state.physics_step=std::clamp(seconds,0.0F,0.05F);
    const bool allowed=state.map_id!=2&&!state.frontend.Visible()&&!IsCutscenePlaying()&&!state.climb.active&&state.death_time<0;
    state.jump_pending=allowed&&held&&!state.jump_down;state.jump_down=held;
}
void QuestScene::ResetMovementContinuity(){
    if(state_->map_id==2){state_->broom.session.previous_valid=false;broom::ResetFlight(state_->broom.motion);}
}
void QuestScene::StartPickupFlight(std::int32_t actor_reference){
    auto& s=*state_;
    const auto bean=std::ranges::find_if(s.beans,[&](const auto& b){return b.actor_reference==actor_reference;});
    if(bean==s.beans.end()||(bean->kind!=0&&bean->kind!=3))return;
    if(s.pickup_flights.size()>=32)s.pickup_flights.erase(s.pickup_flights.begin());
    auto origin=BeanWorldPosition(*bean);
    if(bean->kind==3)for(unsigned axis=0;axis<3;++axis)origin[axis]=s.last_player[axis]-s.hud_transform[8+axis]*3.2F;
    s.pickup_flights.push_back({actor_reference,bean->first,bean->count,origin,bean->kind,0,bean->kind==3?.4F:.25F,
        s.bean_time*1.8F+bean->actor_reference});
}
void QuestScene::UpdateBasicCast(const ViewPose& wand,bool tracked,bool held,float seconds){
    auto& state=*state_;Matrix4 model{};
    if(!MapAllowsSpellInput(state.map_id,SpellInputPath::Basic)){
        state.basic_cast={};state.projectile={};
        state.aim_actor=0;state.wand_lock_actor=0;state.wand_lock_valid=false;
        state.wand_lock_point={};state.wand_was_held=held;state.wand_cast_consumed=held;
        state.voice_repeat.Cancel();state.voice_cast_this_hold=false;
        state.challenge.impact_actor=0;state.audio.SetWandDrawing(false);
        return;
    }
    const bool active=state.death_time<0&&!IsGestureLesson()&&BasicSpellGameplayAllowed(state.map_id,state.frontend.progress.quest_stage,state.challenge.complete)&&tracked&&state.tracking_active&&!state.frontend.Visible()&&!IsCutscenePlaying()&&BuildRigidTransform(wand,&model);
    const std::array<float,3> direction{-model[8],-model[9],-model[10]};
    const auto tip=AddVector({model[12],model[13],model[14]},ScaleVector(direction,0.34F));
    if(state.map_id==3)UpdateCharmsWand(tip,direction,active,held);
    const bool manual=UsesGestureCasting(state.frontend.vr.casting_mode)&&CanCast();
    // Retain a target for the release frame: gesture dispatch follows this update.
    if(!state.wand_was_held&&!held){
        state.wand_lock_valid=false;state.wand_cast_consumed=false;state.voice_cast_this_hold=false;
    }
    if(state.wand_cast_mode!=state.frontend.vr.casting_mode||state.wand_voice_mode!=state.frontend.vr.voice_cast){
        state.wand_lock_valid=false;state.wand_cast_consumed=held;
        state.basic_cast.charging=false;state.basic_cast.require_release=true;
        state.wand_cast_mode=state.frontend.vr.casting_mode;
        state.wand_voice_mode=state.frontend.vr.voice_cast;
        state.voice_repeat.Cancel();state.voice_cast_this_hold=false;
    }
    state.wand_was_held=held;
    if(state.voice_repeat.Advance(active,held,state.audio.SpeechBusy(),state.projectile.flying,seconds))
        state.wand_cast_consumed=false;
    // Speech must survive ordinary hand drift: retain the chosen target for
    // this held trigger, just as gesture casting does. Release selects anew.
    const bool capture_target=manual||(state.frontend.vr.voice_cast&&IsWalkingChallenge(state.map_id)&&CanCast());
    if(state.wand_lock_valid)for(const auto& actor:state.character_draws)if(actor.actor_reference==state.wand_lock_actor){
        if(!actor.enabled||actor.collision_disabled){state.wand_lock_valid=false;state.aim_actor=0;break;}
        FollowTargetOffset(state.wand_lock_point,state.aim_minimum,state.aim_maximum,
                           state.wand_lock_actor_offset,actor.cutscene_offset);
        break;
    }
    const bool locked=capture_target&&state.wand_lock_valid;
    float distance=24;
    if(!active){state.aim_actor=0;state.wand_lock_valid=false;state.wand_cast_consumed=held;}
    if(active&&held&&!locked&&!state.wand_cast_consumed){
        state.aim_actor=0;
        distance=BasicRayDistance(state.collision_triangles,tip,direction);
        if(state.map_id==0)distance=std::min(distance,BasicRayDistance(state.prop_aim_triangles,tip,direction));
        for(const auto& actor:state.character_draws){
            if(!actor.enabled||actor.child_template||actor.player)continue;
            auto center=AddVector(actor.collision_center,actor.cutscene_offset);
            const std::array<float,3> low{center[0]-actor.collision_radius,
                actor.collision_min_y+actor.cutscene_offset[1],center[2]-actor.collision_radius};
            const std::array<float,3> high{center[0]+actor.collision_radius,
                actor.collision_max_y+actor.cutscene_offset[1],center[2]+actor.collision_radius};
            float near=0,far=distance;bool hit=true;
            for(unsigned axis=0;axis<3;++axis){
                if(std::abs(direction[axis])<1e-6F){if(tip[axis]<low[axis]||tip[axis]>high[axis])hit=false;}
                else {float a=(low[axis]-tip[axis])/direction[axis],b=(high[axis]-tip[axis])/direction[axis];
                    if(a>b)std::swap(a,b);near=std::max(near,a);far=std::min(far,b);}
            }
            if(hit&&far>=near&&far>0)distance=std::min(distance,near);
        }
    }
    if(active&&held&&!locked&&!state.wand_cast_consumed&&IsWalkingChallenge(state.map_id)&&!state.charms.held_block){
        float target_distance=24;
        const auto target=FindChallengeSpellTarget(tip,direction,&target_distance,&state.aim_minimum,&state.aim_maximum);
        if(target&&target_distance<=distance+.03F){state.aim_actor=target;distance=target_distance;}
    }
    auto aim=AddVector(tip,ScaleVector(direction,std::max(0.02F,distance-0.025F)));
    if(locked)aim=state.wand_lock_point;
    // Sweeping empty space is ordinary aiming. The stroke starts only on
    // acquisition of an eligible Flipendo object, never on trigger-down alone.
    if(active&&capture_target&&held&&!locked&&!state.wand_cast_consumed&&
       !state.basic_cast.require_release&&state.aim_actor>0){
        state.wand_lock_valid=true;state.wand_lock_actor=state.aim_actor;state.wand_lock_point=aim;
        state.wand_lock_actor_offset={};
        for(const auto& actor:state.character_draws)if(actor.actor_reference==state.aim_actor){
            state.wand_lock_actor_offset=actor.cutscene_offset;break;
        }
    }
    if(state.basic_cast.Observe(active,held,tip,aim,seconds)){
        if(state.wand_cast_consumed||state.voice_cast_this_hold||(manual&&state.wand_lock_valid)){
            state.basic_cast.flying=false;return;
        }
        if(IsWalkingChallenge(state.map_id)&&state.aim_actor){
            std::array<float,3> launch_direction{};
            if(NormalizeVector(SubtractVector(state.basic_cast.destination,tip),&launch_direction))
                LaunchChallengeSpell(tip,launch_direction,tip,++state.automatic_cast_serial);
            return;
        }
        state.audio.PlayBasicCast();
        HPVR_LOGI("[hpvr.quest.basic_cast] status=CAST spell=spellnone sound=spell_dud damage=NONE");
    }
}
void QuestScene::SetWandDrawing(const bool drawing) {
    state_->audio.SetWandDrawing(MapAllowsSpellInput(state_->map_id,SpellInputPath::Basic)&&
        ((drawing && CanCast())||state_->basic_cast.charging));
}

void QuestScene::AdvanceIntroCutscene(const float delta_seconds) {
    State& state = *state_;
    IntroCutscene& scene = state.intro_cutscene;
    if (!scene.playing || !std::isfinite(delta_seconds) ||
        delta_seconds <= 0.0F) return;
    const float step = std::min(delta_seconds, 0.05F);
    const auto location_for = [&scene](const std::string& alias)
        -> const CutsceneLocation* {
        const auto folded = AsciiFold(alias);
        const auto found = std::ranges::find_if(
            scene.locations, [&folded](const auto& location) {
                return AsciiFold(location.alias) == folded;
            });
        return found == scene.locations.end() ? nullptr : &*found;
    };
    const auto character_for = [&state](const std::int32_t actor_reference)
        -> CharacterDraw* {
        const auto found = std::ranges::find_if(
            state.character_draws, [actor_reference](const auto& draw) {
                return draw.actor_reference == actor_reference;
            });
        return found == state.character_draws.end() ? nullptr : &*found;
    };
    const auto set_track_position = [&scene, &state, &character_for](
        CutsceneTrack& track, const std::array<float, 3>& position) {
        track.position = position;
        if (track.camera) {
            scene.camera_position = position;
            scene.camera_position_valid = true;
            return;
        }
        if (auto* character = character_for(track.actor_reference)) {
            auto grounded=AddVector(character->base_origin,character->cutscene_offset);
            if(character->flying||(state.map_id==2&&(AsciiFold(character->class_name)=="harrypotter.broomharry"||AsciiFold(character->class_name)=="tut2.broomhooch")))grounded=position;
            else if(!GroundScriptActor(state.collision_triangles,grounded,position,track.moving,&grounded))
                HPVR_LOGE("[hpvr.quest.cutscene.ground] status=NO_SUPPORT actor_ref=%d",track.actor_reference);
            track.position=grounded;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                character->cutscene_offset[axis] =
                    grounded[axis] - character->base_origin[axis];
            }
            const auto target_index = static_cast<std::size_t>(
                character - state.character_draws.data());
            if (!state.spell_targets.SetSceneOffset(
                    target_index, character->cutscene_offset)) {
                HPVR_LOGE(
                    "[hpvr.quest.cutscene.move] status=TARGET_OFFSET_REJECTED "
                    "actor_ref=%d",
                    character->actor_reference);
            }
        }
    };
    const auto track_position = [&scene](
        const CutsceneTrack& track) {
        if (track.camera && scene.camera_position_valid) {
            return scene.camera_position;
        }
        return track.position;
    };
    constexpr std::array<std::string_view, 5> kDialogueNames{{
        "111dumbledoreinfo1", "111dumbledoreinfo2",
        "111dumbledoreinfo3", "111dumbledoreinfo4", "dumbledore_01"}};
    const auto dialogue_index = [&kDialogueNames,&state](const std::string& value)
        -> std::optional<std::size_t> {
        const auto folded = AsciiFold(value);
        std::size_t audio_index=19;
        for(std::size_t i=0;i<state.frontend.assets.gameplay_audio.size();++i){
            if(i==1)continue;
            if(AsciiFold(state.frontend.assets.gameplay_audio[i].object_name)==folded)return audio_index;
            ++audio_index;
        }
        for (std::size_t index = 0; index < kDialogueNames.size(); ++index) {
            if (folded == kDialogueNames[index]) return index;
        }
        return std::nullopt;
    };

    bool all_finished = true;
    for (auto& track : scene.tracks) {
        if(track.finished&&track.actor_reference==1348&&scene.object_name=="cutscene51"&&!scene.cues.contains("ron_lead_started")){
            auto lead=state.ron_lead;ResumeRonLead(lead,state.character_draws);
            track=lead.tracks.front();scene.cues.insert("ron_lead_started");
            for(const auto& loc:lead.locations)if(std::ranges::none_of(scene.locations,[&](const auto& old){return AsciiFold(old.alias)==AsciiFold(loc.alias);}))scene.locations.push_back(loc);
        }
        if (track.finished) continue;
        all_finished = false;
        if (track.moving) {
            track.move_seconds = std::min(
                track.move_duration_seconds, track.move_seconds + step);
            const float phase = track.move_duration_seconds > 0.0F
                ? track.move_seconds / track.move_duration_seconds : 1.0F;
            const float smooth = track.camera
                ? phase * phase * (3.0F - 2.0F * phase) : phase;
            std::array<float, 3> position{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                position[axis] = track.move_start[axis] +
                    (track.move_target[axis] - track.move_start[axis]) *
                        smooth;
            }
            set_track_position(track, position);
            if (phase < 1.0F) continue;
            track.moving = false;
            // The owned bean conversation leaves George in a waiting state
            // after his approach. Restore that state's attention to Harry.
            if(scene.object_name=="cutscene55"&&track.actor_reference==1326&&!scene.cues.contains("cutend"))
                if(auto* actor=character_for(track.actor_reference))if(const auto* target=location_for("hploc")){
                    const auto d=SubtractVector(target->position,track.position);
                    actor->desired_yaw=std::atan2(d[0],d[2]);
                }
            HPVR_LOGI(
                "[hpvr.quest.cutscene.move] status=ARRIVED cast=%s "
                "command=%zu",
                track.alias.c_str(), track.next_command);
        }
        if(track.dialogue_waiting&&!state.restoring){
            if(!state.audio.DialogueFinished(track.dialogue_index))continue;
            track.delay_seconds=0;
        }
        if (track.delay_seconds > 0.0F) {
            track.delay_seconds = std::max(0.0F,
                                           track.delay_seconds - step);
            if (track.delay_seconds > 0.0F) continue;
        }
        if(track.dialogue_waiting||track.speaking||track.animating){
                if (auto* actor = character_for(track.speaking?track.speaking_actor:track.actor_reference)) {
                    actor->active_clip = actor->clips.contains(track.idle_clip)?track.idle_clip:"breathe";
                    actor->animation_time = 0.0F;
                }
                track.speaking = track.animating = false;
                track.dialogue_waiting=false;
        }
        if (!track.waiting_for.empty()) {
            if (!scene.cues.contains(AsciiFold(track.waiting_for))) continue;
            track.waiting_for.clear();
        }

        constexpr std::size_t kMaximumImmediateCommandsPerFrame = 12;
        for (std::size_t immediate = 0;
             immediate < kMaximumImmediateCommandsPerFrame;
             ++immediate) {
            if (track.next_command >= track.commands.size()) {
                track.finished = true;
                break;
            }
            const std::size_t command_index = track.next_command++;
            const std::string line = track.commands[command_index];
            if (line.empty()) continue;
            const auto separator = line.find(' ');
            const std::string op = AsciiFold(line.substr(0, separator));
            const std::string argument = separator == std::string::npos
                ? std::string{} : line.substr(separator + 1U);
            ++scene.executed_commands;
            HPVR_LOGI(
                "[hpvr.quest.cutscene.command] cast=%s index=%zu op=%s "
                "arg=%s",
                track.alias.c_str(), command_index, op.c_str(),
                argument.c_str());

            if (op == "none") continue;
            if(op=="preface" || (track.camera && (op=="face"||op=="turnto"))){
                scene.camera_target=AsciiFold(argument);continue;
            }
            if (op == "capture") {
                if (track.camera) scene.camera_active = true;
                if(track.actor_reference==state.harry_actor)scene.harry_released=false;
                continue;
            }
            if (op == "release") {
                if(state.map_id==3&&scene.object_name=="cutscene5"&&track.actor_reference==state.harry_actor){
                    const float wait=CloseCharmsEntryDoors(state.doors);
                    if(wait>0){track.next_command=command_index;track.delay_seconds=wait;break;}
                    // The first-person entry performed both one-shot contacts.
                    // Do not replay their toggles when player control returns.
                    RememberChallengeEvent(state.frontend.progress,1539);
                    RememberChallengeEvent(state.frontend.progress,1485);
                }
                if (track.camera) scene.camera_active = false;
                if(track.actor_reference==state.harry_actor)scene.harry_released=true;
                continue;
            }
            if (op == "cue") {
                scene.cues.insert(AsciiFold(argument));
                if(scene.object_name=="cutscene52"&&AsciiFold(argument)=="rongone"){
                    for(auto& door:state.doors)if(door.tag=="fgsec1"&&!door.opening){
                        door.opening=true;
                        if(!state.restoring)if(auto i=GameplayDialogueIndex(state.frontend.assets,"stone_door_long"))
                            (void)state.audio.PlayWorldEffect(*i,.65F);
                        HPVR_LOGI("[hpvr.quest.secret] status=OPENING source=TWINS_DEPART event=FGsec1");
                    }
                }
                continue;
            }
            if (op == "waitfor") {
                if (!scene.cues.contains(AsciiFold(argument))) {
                    track.waiting_for = argument;
                    break;
                }
                continue;
            }
            if (op == "sleep") {
                try {
                    track.delay_seconds = std::max(0.0F,
                                                   std::stof(argument));
                } catch (...) {
                    track.delay_seconds = 0.0F;
                }
                if (track.delay_seconds > 0.0F) break;
                continue;
            }
            if (op == "camspeed") {
                try {
                    scene.camera_speed = std::clamp(
                        std::stof(argument), 0.05F, 2.0F);
                } catch (...) {
                    scene.camera_speed = 0.2F;
                }
                continue;
            }
            if (op == "goto" || op == "teleport") {
                if(state.map_id==1&&scene.object_name=="cutscene50"&&AsciiFold(argument)=="outq"){
                    if(auto* actor=character_for(track.actor_reference))actor->enabled=false;
                    continue;
                }
                if (const auto* target = location_for(argument)) {
                    set_track_position(track, target->position);
                    if(state.map_id==1&&scene.object_name=="cutscene50"&&AsciiFold(argument)=="newqloc")
                        SetBridgeProfessor(state.character_draws,true);
                }
                continue;
            }
            if (op == "moveto") {
                const auto* target = location_for(argument);
                if (target == nullptr) continue;
                track.move_start = track_position(track);
                track.move_target = target->position;
                const auto delta = SubtractVector(track.move_target,
                                                  track.move_start);
                if (auto* actor = character_for(track.actor_reference)) {
                    const bool running=state.map_id==1?AsciiFold(actor->class_name)!="tut1.tut1quirrell":
                        actor->actor_reference!=1672&&actor->actor_reference!=1510&&actor->actor_reference!=777;
                    const std::string requested=track.walk_clip.empty()?(running?"run":"walk"):track.walk_clip;
                    const std::string clip=actor->clips.contains(requested)?requested:
                        actor->clips.contains("walk")?"walk":"breathe";
                    if(actor->active_clip!=clip){actor->active_clip=clip;actor->animation_time=0.0F;}
                    if (std::hypot(delta[0], delta[2]) > 0.001F)
                        actor->desired_yaw = std::atan2(delta[0], delta[2]);
                }
                const float distance = std::sqrt(
                    delta[0] * delta[0] + delta[1] * delta[1] +
                    delta[2] * delta[2]);
                const float speed = track.camera
                    ? std::max(0.6F, scene.camera_speed * 10.0F) :
                    (character_for(track.actor_reference)&&character_for(track.actor_reference)->active_clip=="run"?4.6F:state.map_id==1?3.0F:1.46F);
                track.move_duration_seconds = std::clamp(
                    distance / speed, 0.05F, 60.0F);
                track.move_seconds = 0.0F;
                track.moving = true;
                break;
            }
            if (op == "talk" || op == "say" || (op.size()==5 && op.starts_with("talk") && op[4]>='0' && op[4]<='6')) {
                const auto index = dialogue_index(argument);
                if (index.has_value() && (state.restoring || state.audio.PlayDialogue(*index))) {
                    track.delay_seconds = std::max(
                        0.25F,
                        state.audio.DialogueDurationSeconds(*index));
                    ++scene.spoken_lines;
                    track.dialogue_waiting=true;track.dialogue_index=*index;
                    track.speaking_actor=CutsceneSpeaker(scene,track,op);
                    if (auto* actor = character_for(track.speaking_actor); actor && op!="say") {
                        if(actor->actor_reference==1329||actor->actor_reference==1326){
                            for(const auto& harry:scene.tracks)if(harry.actor_reference==state.harry_actor){
                                const auto d=SubtractVector(harry.position,AddVector(actor->base_origin,actor->cutscene_offset));
                                actor->yaw=actor->desired_yaw=std::atan2(d[0],d[2]);
                            }
                        }
                        actor->active_clip = actor->clips.contains("talk2")?"talk2":
                            actor->clips.contains("talk1")?"talk1":"breathe";
                        actor->animation_time = 0.0F;
                        track.speaking = true;
                    }
                    HPVR_LOGI(
                        "[hpvr.quest.cutscene.dialogue] status=PLAY cast=%s "
                        "line=%s duration_s=%.3f",
                        track.alias.c_str(), argument.c_str(),
                        track.delay_seconds);
                    break;
                }
                continue;
            }
            if (op == "trigger") {
                if(state.map_id==2){DispatchBroomEvent(argument);continue;}
                if(IsWalkingChallenge(state.map_id)){(void)state.challenge.graph.Dispatch(argument);continue;}
                if(AsciiFold(argument)=="spawnwizardcard"){
                    state.frontend.progress.card_awarded=true;SaveCheckpoint();
                    HPVR_LOGI("[hpvr.quest.reward] status=CARD_AWARDED beans_required=25");
                }
                if(AsciiFold(argument)=="fgsec2"&&!state.frontend.progress.card_awarded)continue;
                EmitChildEvent(state.children, state.doors, state.collision_triangles, argument);
                if(!state.restoring && AsciiFold(argument)=="flybymusic")state.audio.SelectMusic(2);
                if(!state.restoring && AsciiFold(argument)=="entry_stairs")state.audio.SelectMusic(3);
                HPVR_LOGI(
                    "[hpvr.quest.cutscene.event] status=BROADCAST event=%s",
                    argument.c_str());
                continue;
            }
            if(op=="changelevel"){
                // This is the boundary to the next owned map, not another waiting cue.
                scene.cues.insert("cutend");scene.harry_released=true;scene.camera_active=false;
                track.finished=true;
                if(auto* actor=character_for(track.actor_reference))actor->active_clip="breathe";
                if(IsWalkingChallenge(state.map_id))state.challenge.complete=true;
                if(state.map_id==2)state.broom.complete=true;
                HPVR_LOGI("[hpvr.quest.lesson] status=LEARNED next_map=%s travel=%s",argument.c_str(),state.map_id==0?"QUEUED_AFTER_SCENE":"NEXT_MAP_BOUNDARY");
                break;
            }
            if (op == "face" || op == "turnto") {
                auto* actor = character_for(track.actor_reference);
                const auto* target = location_for(argument);
                std::optional<std::array<float,3>> target_position;
                if(target)target_position=target->position;
                else for(const auto& cast:scene.tracks)
                    if(AsciiFold(cast.alias)==AsciiFold(argument)||
                       (AsciiFold(argument)=="harry"&&cast.actor_reference==state.harry_actor))target_position=track_position(cast);
                if (actor != nullptr && target_position.has_value()) {
                    const auto delta = SubtractVector(*target_position, track_position(track));
                    actor->desired_yaw = std::atan2(delta[0], delta[2]);
                    if (op == "face") actor->yaw = actor->desired_yaw;
                    else { track.delay_seconds = 0.4F; break; }
                }
                continue;
            }
            if (op == "animate") {
                auto* actor = character_for(track.actor_reference);
                const auto name = AsciiFold(argument);
                if(actor&&actor->actor_reference==1296&&(name=="intro1"||name=="intro2")){
                    for(const auto& harry:scene.tracks)if(harry.actor_reference==state.harry_actor){
                        const auto d=SubtractVector(harry.position,track.position);
                        actor->yaw=actor->desired_yaw=std::atan2(d[0],d[2]);
                    }
                }
                if (actor != nullptr && actor->clips.contains(name)) {
                    actor->active_clip = name;
                    actor->animation_time = 0.0F;
                    // ANIMATE is asynchronous; SLEEP and WAITFOR set pacing.
                }
                continue;
            }
            if(op=="setidle"||op=="setwalk"){
                if(auto* actor=character_for(track.actor_reference)){
                    auto name=AsciiFold(argument);if(name=="breath")name="breathe";
                    if(actor->clips.contains(name)){if(op=="setidle"){track.idle_clip=name;actor->active_clip=name;}else track.walk_clip=name;}
                }continue;
            }
            if (op == "fadein" || op == "fadeout" || op == "preface" || op == "emote" ||
                op == "camprox" || op == "camrestore") {
                continue;
            }
            HPVR_LOGE(
                    "[hpvr.quest.cutscene.command] status=UNSUPPORTED op=%s",
                op.c_str());
        }
        if(!track.moving)if(auto* actor=character_for(track.actor_reference))
            if(actor->active_clip=="run"||actor->active_clip=="walk"){
                actor->active_clip=actor->clips.contains(track.idle_clip)?track.idle_clip:"breathe";actor->animation_time=0;
            }
    }
    if(FinishJumpCameraTour(scene))
        HPVR_LOGI("[hpvr.quest.jump_lesson] status=CAMERA_TAIL_SKIPPED cue=CutEnd dialogue=FINISHED");
    if(ReleaseCutsceneControlIfReady(scene)){
        HPVR_LOGI("[hpvr.quest.cutscene] status=CONTROL_RELEASED object=%s background_tracks=CONTINUE",scene.object_name.c_str());
    }
    // If Ron is the last completed track, extend it before the all-finished
    // check closes the scene. The lead is independent of Harry's approach.
    for(auto& track:scene.tracks)if(track.finished&&track.actor_reference==1348&&scene.object_name=="cutscene51"&&!scene.cues.contains("ron_lead_started")){
        auto lead=state.ron_lead;ResumeRonLead(lead,state.character_draws);
        track=lead.tracks.front();scene.cues.insert("ron_lead_started");all_finished=false;
        for(const auto& loc:lead.locations)if(std::ranges::none_of(scene.locations,[&](const auto& old){return AsciiFold(old.alias)==AsciiFold(loc.alias);}))scene.locations.push_back(loc);
    }
    if (all_finished || std::ranges::all_of(
            scene.tracks, [](const auto& track) { return track.finished; })) {
        scene.playing = false;
        scene.camera_active = false;
        HPVR_LOGI(
            "[hpvr.quest.cutscene] status=FINISHED object=%s "
            "commands=%zu spoken=%zu cues=%zu",
            scene.object_name.c_str(),scene.executed_commands, scene.spoken_lines, scene.cues.size());
    }
}

void QuestScene::Advance(const float delta_seconds) {
    State& state = *state_;
    if(!state.frontend.WorldVisible())state.house_point_hud.Reset(state.frontend.progress.house_points[campaign::kGryffindor]);
    else state.house_point_hud.Advance(state.frontend.progress.house_points[campaign::kGryffindor],delta_seconds,
        !state.tracking_active||state.frontend.PausesWorld());
    // Tick before map-specific early returns. A VR panel over gameplay is live.
    state.effects_clock.Advance(delta_seconds,
        state.tracking_active && !state.frontend.PausesWorld());
    if(!state.tracking_active||!std::isfinite(delta_seconds)||delta_seconds<=0)return;
    if(state.frontend.PausesWorld()){
        if(state.map_id==2){state.broom.session.previous_valid=false;broom::ResetFlight(state.broom.motion);}
        auto& front=state.frontend;
        if(front.screen==FrontScreen::Story){
            const auto index=5U+front.page;
            if(!front.page_voice_started && front.page_time>=1.9F){
                front.page_voice_started=state.audio.PlayDialogue(index);
            }
            const auto old_page=front.page;
            const auto action=front.TickStory(delta_seconds,1.9F+state.audio.DialogueDurationSeconds(index));
            if(front.page!=old_page)state.audio.StopDialogue();
            if(action==FrontAction::StoryDone)StartOpening();
        }
        return;
    }
    if(state.map_id==1){AdvanceChallenge(delta_seconds);return;}
    if(state.map_id==3){AdvanceCharms(delta_seconds);return;}
    if(state.map_id==2){AdvanceBroom(delta_seconds);return;}
    state.spell_targets.Advance(delta_seconds);
    if (std::isfinite(delta_seconds) && delta_seconds > 0.0F) {
        const float step = std::min(delta_seconds, 0.05F);
        for(auto& knight:state.knights){
            knight.time=std::fmod(knight.time+step,380.0F/30);
            const int phase=knight.time<.667F?0:knight.time<2.667F?1:knight.time<3.334F?2:
                knight.time<6.334F?3:knight.time<7.001F?4:knight.time<9.001F?5:knight.time<9.668F?6:7;
            if(phase!=knight.last_sound_phase&&(phase%2)==0){
                const auto d=SubtractVector(state.last_player,knight.origin);
                const float distance=std::sqrt(DotVector(d,d));
                if(distance<5)(void)state.audio.PlayWorldEffect(39U+unsigned(phase/2)%2,.55F*(1-distance/5));
            }
            knight.last_sound_phase=phase;
        }
        state.bean_time=std::fmod(state.bean_time+step,1000.0F);
        state.card_pickup.Advance(step);
        for(auto& flight:state.pickup_flights)flight.elapsed+=step;
        std::erase_if(state.pickup_flights,[](const auto& flight){return flight.elapsed>=flight.duration;});
        state.bean_hud_time=std::max(0.0F,state.bean_hud_time-step);
        state.health_flash_time=std::max(0.0F,state.health_flash_time-step);
        const bool was_cutscene=IsCutscenePlaying();
        if(state.bump_actor&&(!state.audio.DialogueBusy()||state.intro_cutscene.playing)){
            if(!state.intro_cutscene.playing||std::ranges::none_of(state.intro_cutscene.tracks,[&](const auto& t){return t.actor_reference==state.bump_actor;}))
              for(auto& actor:state.character_draws)if(actor.actor_reference==state.bump_actor){
                actor.active_clip="breathe";actor.desired_yaw=state.bump_restore_yaw;
            }
            state.bump_actor=0;
        }
        AdvanceIntroCutscene(step);
        SettleStoppedActors(state.intro_cutscene,state.character_draws);
        // The owned Ghost uses flying Float animation and no world collision.
        // Schedule its classroom pass visibly instead of depending on its
        // original random 120-second patrol arriving during the VR lesson.
        if(state.frontend.progress.quest_stage==19||state.frontend.progress.quest_stage==20){
            if(state.lesson_ghost_time==0){
                auto harry=state.last_player,teacher=state.last_player;
                for(const auto& a:state.character_draws){
                    if(a.actor_reference==kHarryActorReference)harry=AddVector(a.base_origin,a.cutscene_offset);
                    if(a.actor_reference==777)teacher=AddVector(a.base_origin,a.cutscene_offset);
                }
                state.lesson_ghost_center=AddVector(teacher,ScaleVector(SubtractVector(harry,teacher),.35F));
                state.lesson_ghost_center[1]=teacher[1]+1.0F;
                auto direction=SubtractVector(teacher,harry);direction[1]=0;
                if(!NormalizeVector({direction[2],0,-direction[0]},&state.lesson_ghost_direction))state.lesson_ghost_direction={1,0,0};
            }
            state.lesson_ghost_time+=step;
        }
        for(auto& a:state.character_draws)if(a.actor_reference==3148){
            const float t=state.lesson_ghost_time;
            a.enabled=t>1&&t<9&&(state.frontend.progress.quest_stage==19||state.frontend.progress.quest_stage==20);
            const auto position=AddVector(state.lesson_ghost_center,ScaleVector(state.lesson_ghost_direction,(t-5)*2));
            a.cutscene_offset=SubtractVector(position,a.base_origin);
            a.yaw=a.desired_yaw=std::atan2(state.lesson_ghost_direction[0],state.lesson_ghost_direction[2]);
        }
        AdvanceChildren(state.children, state.doors, state.collision_triangles, step);
        for (auto& door : state.doors) {
            door.phase = std::clamp(door.phase + (door.opening ? step : -step) / door.duration, 0.0F, 1.0F);
        }
        for (auto& actor : state.character_draws) {
            actor.animation_time += step;
            const float angle = std::remainder(actor.desired_yaw - actor.yaw, kTau);
            actor.yaw += std::clamp(angle, -step * 8.0F, step * 8.0F);
        }
        if(was_cutscene&&!IsCutscenePlaying()){
            state.frontend.progress.quest_stage=state.intro_cutscene.object_name=="cutscene4"?1U:
                state.intro_cutscene.object_name=="cutscene51"?3U:
                state.intro_cutscene.object_name=="cutscene52"?6U:
                state.intro_cutscene.object_name=="cutscene54"?10U:
                state.intro_cutscene.object_name=="cutscene5"?10U:
                state.intro_cutscene.object_name=="cutscene55"?12U:
                state.intro_cutscene.object_name=="cutscene56"?14U:
                state.intro_cutscene.object_name=="cutscene1"?16U:
                state.intro_cutscene.object_name=="cutscene58"?18U:
                state.intro_cutscene.object_name=="cutscene3"?12U:
                state.intro_cutscene.object_name=="cutscene60"?23U:20U;
            if(state.intro_cutscene.object_name=="cutscene5"){
                state.frontend.progress.peeves_phase=3;MoveTwinsToNextRoom(true);
            }
            if(state.intro_cutscene.object_name=="cutscene56"){
                auto& progress=state.frontend.progress;progress.filch_seen=true;
                progress.quest_stage=std::max(16U,progress.filch_resume_stage);progress.filch_resume_stage=0;
            }
            for(auto& actor:state.character_draws){
                if(actor.actor_reference==1329||actor.actor_reference==1326)actor.enabled=true;
                if(actor.actor_reference==1510||actor.actor_reference==1627||actor.actor_reference==1538||
                   actor.actor_reference==1618||actor.actor_reference==1296||actor.actor_reference==777)actor.enabled=true;
                if(actor.actor_reference==1396||actor.actor_reference==1390)actor.enabled=false;
            }
            for(const auto& actor:state.character_draws)if(actor.actor_reference==kHarryActorReference){
                state.last_player=AddVector(actor.base_origin,actor.cutscene_offset);
                state.last_player[1]+=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;
            }
            if(state.frontend.progress.quest_stage==1){
                PlaceWaitingTwins(state.twins_intro,state.character_draws,state.collision_triangles);
            }
            state.frontend.paused=FrontScreen::Game;
            if(state.exit_return.valid){
                state.last_player=AddVector(state.last_player,state.exit_return.offset);
                state.last_yaw=state.exit_return.yaw;
            }
            state.exit_return.valid=false;
            SaveCheckpoint();state.placement=state.frontend.progress;state.restore_pending=true;
            state.audio.SelectMusic(3);state.save_clock=0;
            if(state.frontend.progress.quest_stage==23){
                RequestChallengeTravel();
            }
        } else if(!IsCutscenePlaying()){
            auto& stage=state.frontend.progress.quest_stage;
            if(stage==1){
                const auto d=SubtractVector(state.last_player,state.ron_intro.trigger_position);
                if(std::hypot(d[0],d[2])<state.ron_intro.trigger_radius &&
                   std::abs(d[1]-kPlayerEyeHeightMeters)<2.0F)StartRonEncounter();
            } else if(stage==3){
                for(const auto& loc:state.ron_intro.locations)if(AsciiFold(loc.alias)=="ronwait"){
                    const auto d=SubtractVector(state.last_player,loc.position);
                    if(std::hypot(d[0],d[2])<2.0F && std::abs(d[1])<2.5F){
                        stage=4;SaveCheckpoint();
                        HPVR_LOGI("[hpvr.quest.first] status=RON_REACHED next=TWINS");
                    }
                }
            }
            if(stage==3||stage==4){
                if(!state.intro_cutscene.playing&&!RonAtTwins(state.ron_lead,state.character_draws)){
                    state.intro_cutscene=state.ron_lead;
                    ResumeRonLead(state.intro_cutscene,state.character_draws);
                    HPVR_LOGI("[hpvr.quest.ron] status=LEADING source=CutScene0 camera=UNCHANGED");
                }
                const auto d=SubtractVector(state.last_player,state.twins_intro.trigger_position);
                if(RonAtTwins(state.ron_lead,state.character_draws)&&
                   std::hypot(d[0],d[2])<state.twins_intro.trigger_radius&&std::abs(d[1]-kPlayerEyeHeightMeters)<2)
                    StartTwinsEncounter();
            }
            if(stage>=6&&!state.climb.active){
                auto& collected=state.frontend.progress.collected_beans;
                for(const auto& bean:state.beans){
                    if(bean.kind==1&&state.frontend.progress.frog_taken)continue;
                    if(bean.kind==2&&(!state.frontend.progress.card_awarded||state.frontend.progress.card_taken))continue;
                    if(std::ranges::binary_search(collected,bean.actor_reference))continue;
                    auto body=state.last_player;body[1]-=kPlayerEyeHeightMeters;
                    // Vertical gating prevents collecting a bean through its shelf.
                    if(!CanCollectBean(state.collision_triangles,body,bean.position))continue;
                    if(bean.kind){
                        if(bean.kind==1){state.frontend.progress.frog_taken=true;state.frontend.progress.health=std::min(100U,state.frontend.progress.health+20);
                            (void)state.audio.PlayWorldEffect(state.frog_sound,.8F);
                        }else{
                            state.frontend.progress.card_taken=true;
                            state.frontend.progress.earned_cards|=campaign::CardMask(101);
                            state.card_pickup={bean.actor_reference,0,
                                std::clamp(state.audio.DialogueDurationSeconds(state.card_sound),.5F,8.0F),
                                state.bean_time*1.8F+bean.actor_reference};
                            (void)state.audio.PlayWorldEffect(state.card_sound,.9F);
                            HPVR_LOGI("[hpvr.quest.reward] status=CARD_COLLECTED sound=pickup_wizardcard2");
                        }
                        SaveCheckpoint();continue;
                    }
                    StartPickupFlight(bean.actor_reference);
                    collected.insert(std::lower_bound(collected.begin(),collected.end(),bean.actor_reference),bean.actor_reference);
                    state.bean_hud_time=4.0F;
                    state.audio.PlayBeanPickup();
                    if(stage==6)stage=7;
                    SaveCheckpoint();
                    HPVR_LOGI("[hpvr.quest.beans] status=COLLECTED actor_ref=%d count=%zu",bean.actor_reference,collected.size());
                }
            }
            if(stage==6||stage==7){
                const auto transfer=SubtractVector(state.last_player,state.twins_transfer.trigger_position);
                const auto next=SubtractVector(state.last_player,state.next_room.trigger_position);
                if(!state.frontend.progress.twins_departed&&!state.intro_cutscene.playing&&
                   ((std::hypot(transfer[0],transfer[2])<state.twins_transfer.trigger_radius&&std::abs(transfer[1]-kPlayerEyeHeightMeters)<2)||
                    (std::hypot(next[0],next[2])<state.next_room.trigger_radius+4&&std::abs(next[1]-kPlayerEyeHeightMeters)<2))){
                    StartTwinsTransition(false);
                }
                const auto d=SubtractVector(state.last_player,state.next_room.trigger_position);
                if(stage==7&&state.frontend.progress.twins_departed&&!state.intro_cutscene.playing&&std::hypot(d[0],d[2])<state.next_room.trigger_radius&&std::abs(d[1]-kPlayerEyeHeightMeters)<2){
                    stage=8;SaveCheckpoint();HPVR_LOGI("[hpvr.quest.climb] status=LESSON_COMPLETE");
                }
            }
            if(stage==8){
                const auto d=SubtractVector(state.last_player,state.next_room.trigger_position);
                if(std::hypot(d[0],d[2])<state.next_room.trigger_radius&&std::abs(d[1]-kPlayerEyeHeightMeters)<2)
                    StartJumpLesson(false);
            }
            if(stage==10){
                auto& phase=state.frontend.progress.peeves_phase;
                const auto trigger=SubtractVector(state.last_player,state.peeves_trigger);
                for(auto& ghost:state.character_draws)if(ghost.actor_reference==2968){
                    ghost.enabled=phase==1;
                    auto position=state.peeves_home;
                    if(phase==0){position[1]+=.12F*std::sin(state.bean_time*1.4F);
                        ghost.active_clip="run";
                        if(std::hypot(trigger[0],trigger[2])<4.6F&&std::abs(trigger[1]-kPlayerEyeHeightMeters)<3){
                            phase=1;state.peeves_time=0;state.peeves_hit=false;state.peeves_from=position;ghost.enabled=true;
                            state.peeves_to=state.last_player;state.peeves_to[1]-=1.4F;
                            if(auto clip=GameplayDialogueIndex(state.frontend.assets,"111Peeves1"))(void)state.audio.PlayDialogue(*clip);
                            SaveCheckpoint();HPVR_LOGI("[hpvr.quest.peeves] status=FLY_AT_PLAYER camera=UNCHANGED");
                        }
                    }
                    if(phase==1){
                        state.peeves_time+=step;
                        float duration=3;
                        if(auto clip=GameplayDialogueIndex(state.frontend.assets,"111Peeves1"))duration=std::max(duration,state.audio.DialogueDurationSeconds(*clip));
                        const float t=state.peeves_time;
                        const float blend=std::clamp(t/.9F,0.0F,1.0F);
                        if(t<.9F){state.peeves_to=state.last_player;state.peeves_to[1]-=1.0F;}
                        position=AddVector(state.peeves_from,ScaleVector(SubtractVector(state.peeves_to,state.peeves_from),blend));
                        if(t>=.9F&&!state.peeves_hit){
                            state.peeves_hit=true;
                            const auto before=state.frontend.progress.health;
                            if(ApplyFirstPeevesContact(state.frontend.progress))state.health_flash_time=3;
                            state.damage_cooldown=1;
                            SaveCheckpoint();HPVR_LOGI("[hpvr.quest.peeves] status=CONTACT_DAMAGE raw=5 max_raw=50 before=%u after=%u camera=UNCHANGED",before,state.frontend.progress.health);
                        }
                        if(t>duration)position=AddVector(state.peeves_to,ScaleVector(SubtractVector(state.peeves_retreat,state.peeves_to),std::clamp(t-duration,0.0F,1.0F)));
                        const std::string clip=t<.9F||t>duration?"run":"grab";
                        if(ghost.active_clip!=clip){ghost.active_clip=clip;ghost.animation_time=0;}
                        if(t>duration+1.0F)StartTwinsTransition(true);
                    }
                    ghost.cutscene_offset=SubtractVector(position,ghost.base_origin);
                    const auto facing=SubtractVector(state.last_player,position);ghost.desired_yaw=std::atan2(facing[0],facing[2]);
                    if(phase>=2){ghost.enabled=true;ghost.cutscene_offset=SubtractVector(state.peeves_retreat,ghost.base_origin);}
                }
                const auto d=SubtractVector(state.last_player,state.jump_finish.trigger_position);
                if(phase==3&&std::hypot(d[0],d[2])<state.jump_finish.trigger_radius&&std::abs(d[1]-kPlayerEyeHeightMeters)<2)
                    StartJumpLesson(true);
            }
            state.damage_cooldown=std::max(0.0F,state.damage_cooldown-step);
            if(stage>=10&&state.frontend.progress.peeves_phase>=2){
                state.peeves_time+=step;
                for(auto& ghost:state.character_draws)if(ghost.actor_reference==2968){
                    const float cycle=std::fmod(std::max(0.0F,state.peeves_time-2)*.16F,2.0F),t=cycle<1?cycle:2-cycle;
                    auto p=AddVector(state.peeves_path_a,ScaleVector(SubtractVector(state.peeves_path_b,state.peeves_path_a),t));
                    if(state.peeves_time<2)p=AddVector(state.peeves_retreat,ScaleVector(SubtractVector(state.peeves_path_a,state.peeves_retreat),state.peeves_time*.5F));
                    ghost.enabled=stage<=12;ghost.active_clip="run";ghost.cutscene_offset=SubtractVector(p,ghost.base_origin);
                    const auto d=SubtractVector(state.last_player,AddVector(p,{0,1.0F,0}));
                    ghost.desired_yaw=std::atan2(d[0],d[2]);
                    if(ghost.enabled&&DotVector(d,d)<1.2F*1.2F&&state.damage_cooldown==0){
                        ApplyTutorialDamage(state.frontend.progress);state.health_flash_time=2;state.damage_cooldown=1;SaveCheckpoint();
                    }
                }
            }
            if(!state.intro_cutscene.playing){
                if(TutorialRewardReady(state.frontend.progress)){
                    for(const auto& fred:state.character_draws)if(fred.actor_reference==1329&&fred.enabled){
                        const auto target=AddVector(AddVector(fred.base_origin,fred.cutscene_offset),{0,1.2F,0});
                        const auto delta=SubtractVector(target,state.last_player);
                        const float distance=std::sqrt(DotVector(delta,delta));
                        const bool visible=distance<.01F||BasicRayDistance(state.collision_triangles,state.last_player,ScaleVector(delta,1/distance))>=distance-.1F;
                        if(state.reward_approach.Update(state.frontend.progress,distance,visible))StartTutorialScene(true);
                    }
                }else if((stage!=12||state.frontend.progress.card_awarded)){
                    if(const auto encounter=SelectStoryEncounter(stage,state.story_encounters,state.last_player,state.frontend.progress.filch_seen))
                        StartStoryEncounter(*encounter);
                }
            }
            if(stage==20){
                state.lesson_wait=std::max(0.0F,state.lesson_wait-step);
                if(!state.lesson_intro_started&&!state.audio.DialogueBusy()){
                    state.lesson_intro_started=true;
                    if(auto i=GameplayDialogueIndex(state.frontend.assets,"quirrell_lesson_99")){
                        (void)state.audio.PlayDialogue(*i);state.lesson_wait=state.audio.DialogueDurationSeconds(*i);
                    }
                }
                if((state.frontend.progress.lesson_passes==4||state.lesson_finish_pending)&&state.lesson_wait==0&&!state.audio.DialogueBusy())StartTutorialScene(false);
            }
            state.bump_cooldown=std::max(0.0F,state.bump_cooldown-step);
            for(auto& [id,bump]:state.bump_states)bump.cooldown=std::max(0.0F,bump.cooldown-step);
            // Leaving while the previous line is still playing also rearms it.
            for(const auto& actor:state.character_draws){
                const auto id=BumpProfileActor(actor.actor_reference,stage,state.bean_twins_staged);
                if(auto bump=state.bump_states.find(id);bump!=state.bump_states.end()){
                    auto chest=AddVector(actor.base_origin,actor.cutscene_offset);chest[1]+=1.2F;
                    const auto d=SubtractVector(chest,state.last_player);
                    if(DotVector(d,d)>2.3F*2.3F)bump->second.near=false;
                }
            }
            if(!state.intro_cutscene.playing&&!state.audio.DialogueBusy()){
                for(auto& actor:state.character_draws){
                    if(!actor.enabled||actor.child_template||actor.actor_reference==kHarryActorReference)continue;
                    if((actor.actor_reference==1329||actor.actor_reference==1326)&&
                       (state.frontend.progress.card_awarded||(stage>=12&&state.frontend.progress.collected_beans.size()>=25)))continue;
                    const auto id=BumpProfileActor(actor.actor_reference,stage,state.bean_twins_staged);
                    const auto profile=std::ranges::find_if(state.frontend.assets.bump_speech,[&](const auto& p){return p.actor_reference==id;});
                    if(profile==state.frontend.assets.bump_speech.end())continue;
                    auto& bump=state.bump_states[id];auto chest=AddVector(actor.base_origin,actor.cutscene_offset);chest[1]+=1.2F;
                    const auto delta=SubtractVector(chest,state.last_player);const float distance=std::sqrt(DotVector(delta,delta));
                    if(distance>2.3F)bump.near=false;
                    if(distance>1.9F||distance<.01F||bump.near||bump.cooldown>0||state.bump_cooldown>0)continue;
                    if(BasicRayDistance(state.collision_triangles,state.last_player,ScaleVector(delta,1/distance))<distance-.1F)continue;
                    const auto index=GameplayDialogueIndex(state.frontend.assets,profile->lines[bump.next%profile->lines.size()]);
                    if(!index||!state.audio.PlayDialogue(*index))continue;
                    bump.near=true;bump.cooldown=0;++bump.next;state.bump_cooldown=.25F;
                    state.bump_actor=actor.actor_reference;state.bump_restore_yaw=actor.desired_yaw;
                    actor.desired_yaw=std::atan2(-delta[0],-delta[2]);
                    actor.active_clip=actor.clips.contains("talk2")?"talk2":actor.clips.contains("talk1")?"talk1":"breathe";
                    actor.animation_time=0;
                    HPVR_LOGI("[hpvr.quest.bump] status=SPEAK actor=%d source=%d clip=%zu camera=UNCHANGED",actor.actor_reference,id,*index);
                    break;
                }
            }
            state.save_clock+=step;
            if(state.save_clock>=15.0F){SaveCheckpoint();state.save_clock=0;}
        }
        if (state.projectile.flying) {
            state.projectile.distance_m = std::min(
                state.projectile.terminal_distance_m,
                state.projectile.distance_m +
                    step * kFlipendoSpeedMetersPerSecond);
            if (state.projectile.distance_m >=
                state.projectile.terminal_distance_m) {
                state.projectile.flying = false;
                state.projectile.impacting = true;
                state.projectile.impact_seconds = 0.0F;
                state.projectile.impact_position = AddVector(
                    state.projectile.origin,
                    ScaleVector(state.projectile.direction,
                                state.projectile.terminal_distance_m));
                if (state.projectile.target.hit &&
                    state.spell_targets.ApplyHit(
                        state.projectile.target,
                        state.projectile.direction)) {
                    state.audio.PlaySpellHit();
                    HPVR_LOGI(
                        "[hpvr.quest.spell.projectile] status=IMPACT "
                        "serial=%llu actor_ref=%d distance_m=%.3f",
                        static_cast<unsigned long long>(
                            state.projectile.target.serial),
                        state.projectile.target.actor_reference,
                        state.projectile.terminal_distance_m);
                } else {
                    HPVR_LOGI(
                        "[hpvr.quest.spell.projectile] status=MISS_END "
                        "serial=%llu distance_m=%.3f",
                        static_cast<unsigned long long>(
                            state.projectile.target.serial),
                        state.projectile.terminal_distance_m);
                }
            }
        } else if (state.projectile.impacting) {
            state.projectile.impact_seconds += step;
            if (state.projectile.impact_seconds >= kFlipendoImpactSeconds) {
                state.projectile.impacting = false;
            }
        }
    }
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0F ||
        state.animation_frame_count == 0) {
        return;
    }
    const float loop_seconds =
        static_cast<float>(state.animation_frame_count) /
        kCharacterAnimationFramesPerSecond;
    state.animation_elapsed_seconds = std::fmod(
        state.animation_elapsed_seconds + std::min(delta_seconds, 0.05F),
        loop_seconds);
    state.current_animation_frame =
        static_cast<std::uint32_t>(
            state.animation_elapsed_seconds *
            kCharacterAnimationFramesPerSecond) %
        state.animation_frame_count;
}

bool QuestScene::ResolvePlayerMovement(
    const std::array<float, 3>& capsule_center,
    const std::array<float, 3>& requested_displacement,
    LocomotionMove* const output) const {
    State& state = *state_;
    if(state.death_time>=0){*output={};return true;}
    if(state.map_id==2)return ResolveBroomMovement(capsule_center,output);
    if(state.map_id==3&&state.charms.active_lesson>=0){*output={};return true;}
    const float climb_distance=std::hypot(requested_displacement[0],requested_displacement[2])*0.65F;
    const float mantle_step=std::clamp(state.physics_step,0.0F,.05F)*1.6F;
    if(state.jump_pending){state.jump_pending=false;
        if(StartJump(state.collision_triangles,capsule_center,state.jump))HPVR_LOGI("[hpvr.quest.jump] status=STARTED button=A");}
    if(IsWalkingChallenge(state.map_id)&&!state.jump.active&&!state.climb.active){
        const auto next=AddVector(capsule_center,{requested_displacement[0],0,requested_displacement[2]});float ground=0;
        if(!FindCollisionGroundHeight(state.collision_triangles,next[0],next[2],capsule_center[1]-kPlayerCapsuleHalfHeightMeters,&ground)&&
            ClearCapsuleSegment(state.collision_triangles,capsule_center,next)){
            auto safe=state.frontend.progress.player;safe[1]-=kPlayerEyeHeightMeters;
            state.jump={true,0,0,safe};*output={};output->displacement=SubtractVector(next,capsule_center);return true;
        }
    }
    if(state.jump.active){
        if(state.jump.elapsed>6.0F){
            if(IsWalkingChallenge(state.map_id)){BeginChallengeDeath("FALL_WATCHDOG");*output={};return true;}
            state.placement=state.frontend.progress;state.placement.player=state.jump.safe_origin;
            state.placement.player[1]+=kPlayerEyeHeightMeters;state.placement.yaw=state.last_yaw;
            state.restore_pending=true;state.jump={};*output={};return true;
        }
        const bool ok=StepJump(state.collision_triangles,state.jump,capsule_center,requested_displacement,state.physics_step,output);
        if(ok&&state.jump.active&&output->blocked_substeps&&
           std::hypot(requested_displacement[0],requested_displacement[2])>.0001F&&
           BeginClimb(state.collision_triangles,AddVector(capsule_center,output->displacement),requested_displacement,&state.climb)){
            // An airborne ledge contact becomes a completed mantle, not a
            // six-second stuck jump. Grabbing no longer requires a held stick.
            state.climb.start=state.jump.safe_origin;state.jump={};
            HPVR_LOGI("[hpvr.quest.climb] status=AIRBORNE_LEDGE_GRAB");
        }
        if(ok)ResolveCharacterMovement(state.character_draws,state.spell_targets,capsule_center,output);
        if(ok)for(const auto& door:state.doors)if((door.tag=="fgsec1"||door.tag=="fgsec2")&&door.phase<.98F)
            ResolveMoverContacts(door.tag=="fgsec1"?state.closed_secret_wall:state.closed_reward_wall,capsule_center,*output);
        return ok;
    }
    if(state.climb.active){
        if(StepClimb(state.collision_triangles,state.climb,capsule_center,mantle_step,output))return true;
        if(state.map_id==3){
            SceneLoadTrace::Event(state.frontend.saves,state.map_id,"MANTLE_CANCELLED resume=FALL_IN_PLACE");
            state.climb={};state.jump={true,0,0,capsule_center};*output={};return true;
        }
        // A moving column can interrupt a captured mantle. Its start may be
        // the last book, so rewinding only Harry would retain closed doors and
        // consumed triggers on the far side of that checkpoint.
        if(IsWalkingChallenge(state.map_id)){BeginChallengeDeath("MANTLE_INTERRUPTED");*output={};return true;}
        state.placement=state.frontend.progress;state.placement.player=state.climb.start;
        state.placement.player[1]+=kPlayerEyeHeightMeters;state.placement.yaw=state.last_yaw;
        state.restore_pending=true;*output={};return true;
    }
    if (!ResolveCollisionMovement(
            state.collision_triangles, capsule_center,
            requested_displacement, output)) {
        return false;
    }
    const auto stage=state.frontend.progress.quest_stage;
    if(IsWalkingChallenge(state.map_id)&&output->blocked_substeps&&std::hypot(requested_displacement[0],requested_displacement[2])>.0001F){
        for(std::size_t i=0;i<state.doors.size();++i){const auto& d=state.doors[i];
            if(!d.grid||std::hypot(d.grid_target[0]-d.grid_offset[0],d.grid_target[2]-d.grid_offset[2])>.005F)continue;
            std::array<float,3> low{1e9F,1e9F,1e9F},high{-1e9F,-1e9F,-1e9F};
            for(const auto& t:state.challenge.mover_triangles[i])for(const auto& v:t.vertices){const auto q=MoverPoint(d,v);
                for(unsigned axis=0;axis<3;++axis){low[axis]=std::min(low[axis],q[axis]);high[axis]=std::max(high[axis],q[axis]);}}
            const auto next=AddVector(capsule_center,requested_displacement);
            if(next[0]<low[0]-kPlayerCapsuleRadiusMeters-.03F||next[0]>high[0]+kPlayerCapsuleRadiusMeters+.03F||
               next[2]<low[2]-kPlayerCapsuleRadiusMeters-.03F||next[2]>high[2]+kPlayerCapsuleRadiusMeters+.03F||
               next[1]-kPlayerCapsuleHalfHeightMeters>=high[1]-.02F||next[1]+kPlayerCapsuleHalfHeightMeters<low[1])continue;
            (void)state.challenge.graph.Spell(d.actor_reference);return true;
        }
    }
    const bool lesson_room=InClimbLesson(state.twins_intro,state.next_room,capsule_center)||
        (stage>=10&&InClimbLesson(state.next_room,state.jump_finish,capsule_center))||
        (stage>=12&&InClimbLesson(state.jump_finish,state.story_encounters[3],capsule_center));
    if((IsWalkingChallenge(state.map_id)||(stage>=6&&lesson_room))&&output->blocked_substeps&&
        std::hypot(output->displacement[0],output->displacement[2])<climb_distance*0.25F&&
        BeginClimb(state.collision_triangles,capsule_center,requested_displacement,&state.climb))
        return StepClimb(state.collision_triangles,state.climb,capsule_center,mantle_step,output);
    ResolveCharacterMovement(state.character_draws, state.spell_targets,
                             capsule_center, output);
    for(const auto& door:state.doors)if((door.tag=="fgsec1"||door.tag=="fgsec2")&&door.phase<.98F){
        ResolveMoverContacts(door.tag=="fgsec1"?state.closed_secret_wall:state.closed_reward_wall,capsule_center,*output);
    }
    const auto final_position = AddVector(capsule_center, output->displacement);
    for (auto& trigger : state.script_triggers) {
        const float dx = final_position[0] - trigger.position[0];
        const float dz = final_position[2] - trigger.position[2];
        const bool inside = dx * dx + dz * dz <=
                                trigger.radius * trigger.radius &&
                            std::abs(final_position[1] - trigger.position[1]) <=
                                trigger.half_height +
                                    kPlayerCapsuleHalfHeightMeters;
        if (inside && !trigger.inside) {
            HPVR_LOGI(
                "[hpvr.quest.script.trigger] status=ENTER actor_ref=%d "
                "object=%s tag=%s event=%s",
                trigger.actor_reference, trigger.object_name.c_str(),
                trigger.tag.c_str(), trigger.event.c_str());
        } else if (!inside && trigger.inside) {
            HPVR_LOGI(
                "[hpvr.quest.script.trigger] status=EXIT actor_ref=%d "
                "object=%s",
                trigger.actor_reference, trigger.object_name.c_str());
        }
        trigger.inside = inside;
    }
    return true;
}

std::int32_t QuestScene::FindChallengeSpellTarget(const std::array<float,3>& origin,
    const std::array<float,3>& direction,float* distance,
    std::array<float,3>* bounds_min,std::array<float,3>* bounds_max) const{
    const auto& state=*state_;
    float nearest=BasicRayDistance(state.collision_triangles,origin,direction);
    std::int32_t target=0;
    const auto test=[&](std::int32_t reference,const std::array<float,3>& low,const std::array<float,3>& high){
        float a=0,b=nearest+.025F;
        for(unsigned axis=0;axis<3;++axis){
        if(std::abs(direction[axis])<1e-6F){if(origin[axis]<low[axis]||origin[axis]>high[axis])return;}
        else {float l=(low[axis]-origin[axis])/direction[axis],h=(high[axis]-origin[axis])/direction[axis];
            if(l>h)std::swap(l,h);a=std::max(a,l);b=std::min(b,h);}
        }
        if(b>=a&&b>0&&a<=nearest+.025F){nearest=a;target=reference;
            if(bounds_min)*bounds_min=low;if(bounds_max)*bounds_max=high;}
    };
    for(const auto& zone:state.challenge.spatial)if(zone.spell){
        if(state.map_id==3&&zone.spell_name=="spellaloho"&&!state.frontend.progress.lesson_best[1])continue;
        const auto* node=state.challenge.graph.Find(zone.reference);if(!node||!node->active||node->consumed)continue;
        test(zone.reference,AddVector(zone.position,{-zone.radius,-zone.height,-zone.radius}),
        AddVector(zone.position,{zone.radius,zone.height,zone.radius}));
    }
    for(const auto& prop:state.challenge.props)if(prop.spell_target&&!ChallengeActivated(state.frontend.progress,prop.reference)){
        if(state.map_id==3&&chest::IsChest(prop.name)&&!state.frontend.progress.lesson_best[1])continue;
        test(prop.reference,prop.minimum,prop.maximum);
    }
    if(state.map_id==3&&state.frontend.progress.lesson_best[2])for(const auto& [ref,block]:state.charms.blocks)
        if(!block.plate)test(ref,AddVector(block.minimum,block.offset),AddVector(block.maximum,block.offset));
    for(const auto& a:state.character_draws)if(a.enabled&&!a.player&&!a.flying){
        const auto cls=AsciiFold(a.class_name);if(cls!="tut1.tut1gnome"&&cls!="tut1.flipbarrel")continue;
        if(cls=="tut1.tut1gnome"&&state.challenge.gnome_hits.contains(a.actor_reference)&&state.challenge.gnome_hits.at(a.actor_reference)>0)continue;
        if(cls=="tut1.flipbarrel"&&state.challenge.barrel_stage>=4)continue;
        const auto center=AddVector(a.collision_center,a.cutscene_offset);
        test(a.actor_reference,{center[0]-a.collision_radius,a.collision_min_y+a.cutscene_offset[1],center[2]-a.collision_radius},
        {center[0]+a.collision_radius,a.collision_max_y+a.cutscene_offset[1],center[2]+a.collision_radius});
        if(target==a.actor_reference&&bounds_min&&bounds_max){
            // Walking capsules are deliberately narrower than rendered models.
            // In particular the barrel must not use the NPC's 0.42m radius cap
            // as its marker plane: that would still bury the effect in its mesh.
            *bounds_min={1e9F,1e9F,1e9F};*bounds_max={-1e9F,-1e9F,-1e9F};
            for(unsigned corner=0;corner<8;++corner){
                std::array<float,3> p{};
                for(unsigned axis=0;axis<3;++axis)p[axis]=(corner&(1U<<axis))?a.visual_maximum[axis]:a.visual_minimum[axis];
                p=AddVector(AddVector(a.base_origin,RotateYaw(SubtractVector(p,a.base_origin),a.yaw-a.base_yaw)),a.cutscene_offset);
                for(unsigned axis=0;axis<3;++axis){(*bounds_min)[axis]=std::min((*bounds_min)[axis],p[axis]);(*bounds_max)[axis]=std::max((*bounds_max)[axis],p[axis]);}
            }
        }
    }
    for(std::size_t i=0;i<state.doors.size();++i)if(state.doors[i].grid){
        auto low=std::array<float,3>{1e9F,1e9F,1e9F},high=std::array<float,3>{-1e9F,-1e9F,-1e9F};
        for(const auto& t:state.challenge.mover_triangles[i])for(const auto& v:t.vertices){const auto q=MoverPoint(state.doors[i],v);
        for(unsigned axis=0;axis<3;++axis){low[axis]=std::min(low[axis],q[axis]);high[axis]=std::max(high[axis],q[axis]);}}
        test(state.doors[i].actor_reference,low,high);
    }
    *distance=nearest;return target;
}
void QuestScene::LaunchChallengeSpell(const std::array<float,3>& origin,const std::array<float,3>& direction,
    const std::array<float,3>& tip,std::uint64_t serial){
    auto& state=*state_;
    if(!IsWalkingChallenge(state.map_id)||!CanCast())return;
    float distance=24;
    auto target=FindChallengeSpellTarget(origin,direction,&distance);
    auto destination=AddVector(origin,ScaleVector(direction,distance));
    if(state.wand_lock_valid){
        target=state.wand_lock_actor;destination=state.wand_lock_point;state.wand_lock_valid=false;
    }
    if(state.map_id==3)if(const auto block=state.charms.blocks.find(target);block!=state.charms.blocks.end()){
        state.charms.held_block=target;state.charms.hold_release_armed=state.wand_was_held;
        const auto center=AddVector(ScaleVector(AddVector(block->second.minimum,block->second.maximum),.5F),block->second.offset);
        const auto delta=SubtractVector(center,tip);state.charms.hold_distance=std::clamp(std::sqrt(DotVector(delta,delta)),1.2F,8.0F);
        // Keep the initial grab point: aiming at a low face must not pin the
        // centre into the floor. Start with a small, collision-swept lift.
        state.charms.hold_offset=SubtractVector(AddVector(center,{0,.45F,0}),
            AddVector(state.charms.wand_tip,ScaleVector(state.charms.wand_direction,state.charms.hold_distance)));
        HPVR_LOGI("[hpvr.quest.charms.block] status=GRABBED ref=%d plate=%d distance=%.3f",target,block->second.plate,state.charms.hold_distance);
        state.wand_cast_consumed=true;state.basic_cast.flying=false;state.aim_actor=0;state.audio.PlaySpellCast(false);return;
    }
    std::array<float,3> flight{};const auto delta=SubtractVector(destination,tip);
    if(!NormalizeVector(delta,&flight))return;
    state.challenge.impact_actor=target;state.projectile={};state.projectile.origin=tip;
    state.projectile.direction=flight;state.projectile.target.serial=serial;
    state.projectile.terminal_distance_m=std::max(.02F,std::sqrt(DotVector(delta,delta)));
    state.projectile.flying=true;state.basic_cast.flying=false;state.wand_cast_consumed=true;state.audio.PlaySpellCast(ActiveGestureSpell()==GestureSpell::Flipendo);
    HPVR_LOGI("[hpvr.quest.challenge.spell] serial=%llu target=%d distance=%.3f",static_cast<unsigned long long>(serial),target,state.projectile.terminal_distance_m);
}
bool QuestScene::DispatchFlipendo(const FlipendoEvent& event) {
    if(!CanCast())return true;
    auto& state=*state_;
    if(state.map_id==3&&state.charms.active_lesson>=0){SubmitCharmsLesson(event.score);return true;}
    if(IsWalkingChallenge(state.map_id)){
        if(event.spell!=ActiveGestureSpell())return true;
        SpellTargetResult validation{};
        if(!state.spell_targets.Consume(event,&validation))return false;
        std::array<float,3> direction{};if(!NormalizeVector(event.locked_direction,&direction))return false;
        LaunchChallengeSpell(event.locked_origin,direction,event.release_tip,event.serial);
        return true;
    }
    if(state.frontend.progress.quest_stage==20){
        if(state.lesson_wait>0||state.audio.DialogueBusy())return true;
        auto& passes=state.frontend.progress.lesson_passes;
        if(passes>=4)return true;
        ++passes;
        auto& progress=state.frontend.progress;
        (void)campaign::RaiseLessonPoints(progress.lesson_points,progress.house_points,
            campaign::kFlipendoLesson,campaign::LessonPoints(passes),static_cast<std::uint32_t>(event.serial));
        if(std::isfinite(event.score))progress.lesson_best[campaign::kFlipendoLesson]=std::max(
            progress.lesson_best[campaign::kFlipendoLesson],static_cast<unsigned>(std::clamp(event.score*100.0F,0.0F,100.0F)));
        SaveCheckpoint();state.audio.PlaySpellCast();
        const std::array<const char*,4> lines{"quirrell_lesson_118","quirrell_lesson_154","quirrell_lesson_171","quirrell_lesson_76"};
        if(auto i=GameplayDialogueIndex(state.frontend.assets,lines[passes-1])){
            (void)state.audio.PlayDialogue(*i);state.lesson_wait=state.audio.DialogueDurationSeconds(*i)+.4F;
        }
        HPVR_LOGI("[hpvr.quest.lesson] status=ROUND_PASSED round=%u total=4 score=%.3f",passes,event.score);
        return true;
    }
    SpellTargetResult result{};
    if (!state_->spell_targets.Consume(event, &result)) {
        HPVR_LOGE(
            "[hpvr.quest.npc.spell] status=EVENT_REJECTED serial=%llu",
            static_cast<unsigned long long>(event.serial));
        return false;
    }
    std::array<float, 3> direction{};
    if (!NormalizeVector(event.locked_direction, &direction)) return false;
    state_->projectile = {};
    state_->projectile.origin = event.locked_origin;
    state_->projectile.direction = direction;
    state_->projectile.target = result;
    state_->projectile.terminal_distance_m =
        result.hit ? result.distance_m : kFlipendoMissDistanceMeters;
    state_->projectile.flying = true;
    state_->audio.PlaySpellCast();
    if (!result.hit) {
        HPVR_LOGI(
            "[hpvr.quest.spell.projectile] status=LAUNCHED_MISS "
            "serial=%llu targets=%zu distance_m=%.3f score=%.6f",
            static_cast<unsigned long long>(event.serial),
            state_->character_draws.size(),
            state_->projectile.terminal_distance_m, event.score);
        return true;
    }
    const CharacterDraw& target =
        state_->character_draws[result.target_index];
    HPVR_LOGI(
        "[hpvr.quest.spell.projectile] status=LAUNCHED_TARGET "
        "serial=%llu actor_ref=%d "
        "class=%s object=%s staged=%s distance_m=%.3f score=%.6f",
        static_cast<unsigned long long>(event.serial), target.actor_reference,
        target.class_name.c_str(), target.object_name.c_str(),
        target.staged ? "YES" : "NO", result.distance_m, event.score);
    return true;
}

bool QuestScene::IsLoaded() const {
    const State& state = *state_;
    std::uint64_t expected_vertices =
        static_cast<std::uint64_t>(state.map_vertex_count) +
        static_cast<std::uint64_t>(state.fixture_vertex_count);
    for (const auto& door : state.doors) {
        if (door.first_vertex != expected_vertices) return false;
        expected_vertices += door.vertex_count;
    }
    if(state.front_vertex_count>state.vertices.size()||
       !ValidateAnimatedVertexLayout(state.character_draws,state.beans,expected_vertices,
           state.vertices.size()-state.front_vertex_count))return false;
    expected_vertices=state.vertices.size();
    const bool map_specific=state.map_id==3?
        ValidateCharmsSceneLayout(state.challenge,state.charms,state.doors.size(),state.fixture_actor_count,state.character_draws):state.map_id==2?
        (state.broom.lesson.valid&&state.challenge.graph.healthy()&&state.challenge.scenes.size()==14&&
         state.fixture_actor_count>0&&state.character_draws.size()==7):state.map_id==1?
        (state.challenge.graph.healthy()&&state.doors.size()==67&&state.challenge.scenes.size()==15&&
         state.fixture_actor_count>0&&state.character_draws.size()>=10):
        (state.fixture_actor_count==60&&state.flames.size()==86&&state.glows.size()==14&&
         state.glow_vertices.size()==216&&state.character_draws.size()==22+state.children.prototypes.size()&&
         !state.children.prototypes.empty()&&state.doors.size()==5&&state.initial_intro.tracks.size()==5&&
         state.initial_intro.locations.size()==21&&state.ron_intro.available&&state.ron_intro.trigger_radius>0&&
         state.twins_intro.available&&state.twins_intro.tracks.size()==6);
    return map_specific && !state.vertices.empty() && !state.texture_rgba8.empty() &&
           state.map_vertex_count > 0 &&
           state.fixture_vertex_count > 0 &&
           state.fire_texture_layer < state.texture_layers &&
           state.character_frame_vertex_count > 0 &&
           state.animation_frame_count == kCharacterAnimationFrameCount &&
           state.vertices.size() == expected_vertices &&
           !state.collision_triangles.empty() &&
           !state.wand_vertices.empty() &&
           !state.script_triggers.empty() &&
           state.script_actor_count > 0 &&
           state.authored_light_count > 0 &&
           state.intro_cutscene.available &&
           state.audio.DialogueClipCount() == 19+state.frontend.assets.gameplay_audio.size() &&
           state.audio.IsConfigured() &&
           state.spell_targets.TargetCount() ==
               state.character_draws.size();
}

bool QuestScene::IsCutscenePlaying() const {
    return state_->intro_cutscene.playing&&!state_->intro_cutscene.control_released;
}

bool QuestScene::GetCinematicCameraPosition(
    std::array<float, 3>* const output) const {
    if (output == nullptr || !IsCutscenePlaying() ||
        state_->frontend.PausesWorld() ||
        !state_->intro_cutscene.camera_active ||
        !state_->intro_cutscene.camera_position_valid) return false;
    *output = state_->intro_cutscene.camera_position;
    return true;
}

bool QuestScene::GetCinematicCameraPose(ViewPose* output,bool* first_person) const {
    if(first_person)*first_person=false;
    if(!output)return false;
    const auto& scene=state_->intro_cutscene;
    if(!IsCutscenePlaying()||state_->frontend.PausesWorld())return false;
    // Camera selection leaves authored tracks/cues and actor animation intact.
    // The actor root avoids the exaggerated vertical motion of a head bone.
    if(state_->frontend.vr.first_person_cutscenes){
        const auto camera_actor=state_->map_id==2?BroomCameraActor(scene,state_->harry_actor):state_->harry_actor;
        for(const auto& actor:state_->character_draws)if(actor.actor_reference==camera_actor&&actor.enabled){
            auto position=AddVector(actor.base_origin,actor.cutscene_offset);
            if(state_->map_id==2)position=BroomCameraPosition(scene,camera_actor,position);
            if(BuildDemoActorEye(position,actor.yaw,
                kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters,output)){
                if(first_person)*first_person=true;
                return true;
            }
        }
    }
    // Keep the final exit moving with Harry in both camera modes. The twins'
    // follow scene temporarily releases its spectator camera during this walk.
    if((!scene.camera_active&&scene.object_name=="cutscene52")||scene.object_name=="cutscene60"){
        for(const auto& actor:state_->character_draws)if(actor.actor_reference==state_->harry_actor){
            auto focus=AddVector(actor.base_origin,actor.cutscene_offset);focus[1]+=1.3F;
            std::array<float,3> offset{-std::sin(actor.yaw)*3.0F,0.8F,-std::cos(actor.yaw)*3.0F},direction{};
            if(!NormalizeVector(offset,&direction))return false;
            const float length=std::min(3.1F,std::max(0.2F,BasicRayDistance(state_->collision_triangles,focus,direction)-0.2F));
            output->position=AddVector(focus,ScaleVector(direction,length));
            return BuildLookOrientation(output->position,focus,&output->orientation);
        }
    }
    if(!GetCinematicCameraPosition(&output->position))return false;
    if(const auto target=CinematicTarget(scene,state_->character_draws))
        return BuildLookOrientation(output->position,*target,&output->orientation);
    output->orientation={0,0,0,1};return true;
}

bool QuestScene::IsGpuReady() const {
    return state_->pipeline != VK_NULL_HANDLE && state_->frontend_pipeline!=VK_NULL_HANDLE && state_->ghost_pipeline!=VK_NULL_HANDLE &&
           state_->vertex_buffer != VK_NULL_HANDLE &&
           state_->descriptor_set != VK_NULL_HANDLE &&
           state_->wand_pipeline != VK_NULL_HANDLE &&
           state_->effect_pipeline != VK_NULL_HANDLE &&
           state_->particle_pipeline != VK_NULL_HANDLE &&
           state_->particle_pipeline_layout != VK_NULL_HANDLE &&
           state_->wand_vertex_buffer != VK_NULL_HANDLE &&
           state_->guide_vertex_buffer != VK_NULL_HANDLE &&
           state_->glow_vertex_buffer != VK_NULL_HANDLE &&
           state_->particle_vertex_buffer != VK_NULL_HANDLE;
}

std::uint32_t QuestScene::VertexCount() const {
    return static_cast<std::uint32_t>(state_->vertices.size());
}

std::uint32_t QuestScene::TextureLayerCount() const {
    return state_->texture_layers;
}

std::uint32_t QuestScene::WandVertexCount() const {
    return static_cast<std::uint32_t>(state_->wand_vertices.size());
}

std::uint32_t QuestScene::AnimationFrameCount() const {
    return state_->animation_frame_count;
}

std::uint32_t QuestScene::CurrentAnimationFrame() const {
    return state_->current_animation_frame;
}

std::uint32_t QuestScene::CharacterCount() const {
    return static_cast<std::uint32_t>(state_->character_draws.size());
}

}  // namespace hpvr::quest
#else
}  // namespace
}  // namespace hpvr::quest
#endif
