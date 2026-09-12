#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"

#include <iostream>
#include <stdexcept>

using namespace hpvr::quest;

namespace {
using Point = std::array<float, 3>;
using Property = hpvr::wand::Hp1ClassDefaultProperty;
using Census = hpvr::wand::Hp1ActorVisualCensus;
std::size_t checks = 0;

void Check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

Property Reference(std::string name, int index, std::int32_t actor, std::string alias) {
    Property property;
    property.name = std::move(name);
    property.array_index = index;
    property.object_reference_serialized = true;
    property.object_reference = actor;
    property.text_value_serialized = true;
    property.text_value = std::move(alias);
    return property;
}

Property Command(unsigned cast, int index, std::string text) {
    Property property;
    property.name = "Cast" + std::to_string(cast) + "Script";
    property.array_index = index;
    property.text_value_serialized = true;
    property.text_value = std::move(text);
    return property;
}

Census SceneActors(std::string camera_class, std::string camera_alias) {
    Census census;
    census.status = hpvr::wand::Hp1ProfileStatus::ok;
    hpvr::wand::Hp1ActorVisual scene;
    scene.actor_reference = 100;
    scene.object_name = "CutScene10";
    scene.qualified_class_name = "HPBase.CutScene";
    for (int cast = 0; cast < 9; ++cast) {
        scene.serialized_properties.push_back(Reference("Cast", cast, cast + 1,
            cast == 0 ? camera_alias : cast == 4 ? "" : "Actor" + std::to_string(cast)));
        scene.serialized_properties.push_back(Command(static_cast<unsigned>(cast), 0, "Sleep 0.1"));
        hpvr::wand::Hp1ActorVisual actor;
        actor.actor_reference = cast + 1;
        actor.qualified_class_name = cast == 0 ? camera_class : "HarryPotter.BroomHarry";
        actor.location_serialized = true;
        actor.location_unreal = {float(cast * 10), float(cast * 20), float(cast * 30)};
        census.actors.push_back(std::move(actor));
    }
    scene.serialized_properties.push_back(Reference("Cast", 9, 0, "Unused"));
    scene.serialized_properties.push_back(Reference("Locs", 0, 2, "Start"));
    scene.serialized_properties.push_back(Reference("Locs", 1, 0, "Unused"));
    scene.serialized_properties.push_back(Command(8, 28, "Release"));
    census.actors.push_back(std::move(scene));
    return census;
}

void CameraClassification() {
    const std::array<std::pair<const char*, const char*>, 3> camera_types{{
        {"HPBase.BaseCam", "View"}, {"HarryPotter.PotCam", "View"}, {"HPBase.Actor", "Camera"}}};
    hpvr_hp1_player_start_report start{};
    for (const auto& [class_name, alias] : camera_types) {
        auto census = SceneActors(class_name, alias);
        IntroCutscene scene;
        Check(LoadIntroCutscene(census, start, .3F, &scene, "CUTSCENE10", false),
              "nine-cast scene loads without tutorial-only limits");
        Check(scene.object_name == "cutscene10" && scene.tracks.size() == 9 && scene.locations.size() == 1,
              "all cast slots survive; null locations and cast references are ignored");
        Check(std::ranges::count_if(scene.tracks, [](const auto& track) { return track.camera; }) == 1 &&
              scene.tracks.front().camera, "BaseCam, PotCam and authored camera aliases retain their camera role");
        Check(scene.tracks[4].actor_reference == 5 && scene.tracks[4].alias.empty(),
              "an empty serialized alias does not discard a valid cast actor");
        Check(scene.tracks[8].commands[28] == "Release", "late commands in the ninth cast are retained");
        Check(scene.tracks[1].position == ActorLocalPosition(census.actors[1], start, .3F),
              "camera and character tracks use the same world-to-scene transform");

        auto invalid = census;
        invalid.actors.back().serialized_properties.push_back(Reference("Cast", 10, 1, "Invalid"));
        Check(!LoadIntroCutscene(invalid, start, 0, &scene, "CutScene10", false), "out-of-range cast rejected");
        Check(scene.tracks.size() == 9, "rejected scene leaves the previous scene intact");
        invalid = census;
        invalid.actors.back().serialized_properties.push_back(Reference("Cast", 0, 2, "Duplicate"));
        Check(!LoadIntroCutscene(invalid, start, 0, &scene, "CutScene10", false), "duplicate cast slot rejected");
        invalid = census;
        invalid.actors.back().serialized_properties.push_back(Command(8, 40, "Release"));
        Check(!LoadIntroCutscene(invalid, start, 0, &scene, "CutScene10", false), "out-of-range command rejected");
    }
}

CutsceneTrack Track(std::int32_t actor, std::string command) {
    CutsceneTrack track;
    track.actor_reference = actor;
    track.commands[0] = std::move(command);
    return track;
}

void MountedActorHandoff() {
    constexpr std::int32_t mounted = 75, walking = 156;
    const Point visible{1, 2, 3}, walking_position{2, 2, 3}, offstage{40, -10, -50};
    IntroCutscene scene;
    scene.object_name = "cutscene10";
    scene.locations.push_back({10, "CutMark10", visible});
    scene.tracks = {Track(mounted, "Teleport CutMark10"), Track(walking, "Teleport CutMark17")};
    Check(BroomCameraActor(scene, mounted) == walking, "intro follows walking Harry before mount handoff");
    Check(BroomCameraPosition(scene, walking, walking_position) == walking_position,
          "intro preserves the active walking actor position");
    scene.tracks[1].next_command = 1;
    Check(BroomCameraActor(scene, mounted) == walking &&
          BroomCameraPosition(scene, walking, offstage) == visible,
          "intro bridges the offstage swap gap at the visible mount mark");
    scene.tracks[0].next_command = 1;
    Check(BroomCameraActor(scene, mounted) == mounted &&
          BroomCameraPosition(scene, mounted, visible) == visible,
          "intro switches to mounted Harry only after his teleport executes");

    scene.object_name = "cutscene14";
    scene.tracks = {Track(mounted, "Teleport CutMark7"), Track(walking, "Teleport CutMark10")};
    Check(BroomCameraActor(scene, mounted) == mounted, "departure starts on mounted Harry");
    scene.tracks[0].next_command = 1;
    Check(BroomCameraActor(scene, mounted) == walking &&
          BroomCameraPosition(scene, walking, offstage) == visible,
          "departure bridges the dismount gap without following a storage actor");
    scene.tracks[1].next_command = 1;
    Check(BroomCameraPosition(scene, walking, walking_position) == walking_position,
          "departure resumes walking Harry's actual movement after the swap");
    scene.tracks[1].next_command = 0;
    Check(!BroomExecutedTeleport(scene, walking, "cutmark10"), "queued teleport is not already executed");
    scene.tracks[1].commands[0] = "GOTO CUTMARK10";
    scene.tracks[1].next_command = scene.tracks[1].commands.size() + 7;
    Check(BroomExecutedTeleport(scene, walking, "cutmark10"), "case-folded goto is recognized with a bounded cursor");
    Check(!BroomExecutedTeleport(scene, 999, "cutmark10"), "another actor cannot satisfy the camera handoff");
    scene.object_name = "cutscene8";
    Check(BroomCameraActor(scene, mounted) == mounted &&
          BroomCameraPosition(scene, walking, walking_position) == walking_position,
          "retry and assessment scenes keep their normal actor positions");
}

BroomVisuals SyntheticVisuals() {
    BroomVisuals visuals;
    visuals.valid = true;
    visuals.target_points = {{0, 80, 0}, {0, 0, 80}, {0, -80, 0}, {0, 0, -80}};
    visuals.standby_points = {{0, 20, 0}, {0, 0, 20}, {0, -20, 0}, {0, 0, -20}};
    for (auto& style : visuals.styles) {
        style.lifetime = {.5F, .1F};
        style.width = {6, 1};
        style.length = {8, 1};
        style.end_scale = {.4F, .1F};
        style.color_start = {1, .5F, .2F};
        style.color_end = {.5F, .2F, 0};
        style.rate = 200;
    }
    return visuals;
}

void SharedHoopPose(BroomRuntime state, const BroomVisuals& visuals) {
    const std::array<float, 16> view{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    std::array<ParticleGpuVertex, kBroomHoopVertexCapacity> before{}, after{};
    state.active = true;
    for (unsigned path = 0; path < 2; ++path) for (unsigned stage = 0; stage < 5; ++stage) {
        state.path = path;
        state.stage = stage;
        state.route.next_index = 0;
        auto& route = state.routes[path][stage];
        Check(!route.empty(), "hoop pose fixture contains every stage and path");
        state.seconds = .7F;
        for (auto& hoop : route) hoop.center = AddVector(state.hoop_centers.at(hoop.id), BroomHoopOffset(state, hoop.id));
        const auto count = BuildBroomHoopVertices(state, visuals, .25F, view, before.data(), before.size());
        Check(count > 0 && count % 6 == 0 && count <= kBroomHoopVertexCapacity, "hoop particles are bounded");
        const auto repeated = BuildBroomHoopVertices(state, visuals, .25F, view, after.data(), after.size());
        Check(repeated == count && std::memcmp(before.data(), after.data(), count * sizeof(before[0])) == 0,
              "both eye passes produce identical world-space hoop particles");

        state.seconds = 1.1F;
        const auto same_pose = BuildBroomHoopVertices(state, visuals, .25F, view, after.data(), after.size());
        Check(same_pose == count && std::memcmp(before.data(), after.data(), count * sizeof(before[0])) == 0,
              "visuals do not apply bobbing again after the collision route pose is updated");
        const Point translation{.17F, .31F, -.23F};
        for (auto& hoop : route) hoop.center = AddVector(hoop.center, translation);
        const auto translated = BuildBroomHoopVertices(state, visuals, .25F, view, after.data(), after.size());
        Check(translated == count, "moving the shared route does not change particle allocation");
        for (std::size_t i = 0; i < count; ++i) for (unsigned axis = 0; axis < 3; ++axis)
            Check(std::abs(after[i].position[axis] - before[i].position[axis] - translation[axis]) < .00003F,
                  "every particle follows exactly the same translation as its collision hoop");
    }
    Check(BuildBroomHoopVertices(state, visuals, 0, view, before.data(), 5) == 0, "incomplete quad buffer rejected");
    Check(BuildBroomHoopVertices(state, visuals, 0, view, nullptr, before.size()) == 0, "null particle buffer rejected");
    state.active = false;
    Check(BuildBroomHoopVertices(state, visuals, 0, view, before.data(), before.size()) == 0, "inactive lesson draws no hoops");
}

void SyntheticHoopPoses() {
    BroomRuntime state;
    for (unsigned path = 1; path <= 2; ++path) for (unsigned stage = 1; stage <= 5; ++stage)
        for (unsigned order = 1; order <= 4; ++order) {
            broom::LessonHoop authored;
            authored.id = static_cast<std::int32_t>(path * 100 + stage * 10 + order);
            authored.path = path;
            authored.stage = stage;
            authored.order = order;
            authored.play_scale = 1;
            authored.bobbing = stage == 5;
            authored.bob_amount_unreal = 24;
            const Point center{float(order * 2), float(stage), float(path * 4)};
            state.hoop_indices.emplace(authored.id, state.lesson.hoops.size());
            state.hoop_centers.emplace(authored.id, center);
            state.lesson.hoops.push_back(authored);
            state.routes[path - 1][stage - 1].push_back({authored.id, center, {0,0,1}, 1});
        }
    SharedHoopPose(std::move(state), SyntheticVisuals());
}

void SecretCardPickup() {
    ProgressSave progress;
    progress.collected_beans={10,400};
    progress.card_awarded=true;progress.card_taken=true;
    BeanDraw card;card.actor_reference=265;card.kind=4;card.position={0,0,0};
    CardPickupEffect effect;
    Check(CanCollectBean({}, {.5F,0,0},card.position), "unobstructed nearby secret card is touchable");
    std::vector<GpuVertex> wall(3);
    const std::array<Point,3> corners{{{.25F,-2,-2},{.25F,2,-2},{.25F,0,2}}};
    for(unsigned vertex=0;vertex<3;++vertex)for(unsigned axis=0;axis<3;++axis)
        wall[vertex].position[axis]=corners[vertex][axis];
    const auto collision=BuildCollisionTriangles(wall,3);
    Check(!CanCollectBean(collision,{.5F,0,0},card.position),
          "a closed secret door still blocks card collection through its surface");
    Check(TakeBroomCard(progress,effect,card,2,1.5F)&&effect.active()&&effect.actor==265,
          "secret card starts the original grow-spin pickup effect");
    Check(progress.collected_beans==std::vector<std::int32_t>({10,265,400})&&
          progress.card_awarded&&progress.card_taken,
          "level card is recorded independently without changing the first lesson card flags");
    Check(!TakeBroomCard(progress,effect,card,4,1), "a collected card cannot award itself twice");
    effect.Advance(.75F);
    Check(effect.scale()>1&&effect.angle()>2*1.8F+265, "pickup grows and rotates before disappearing");
    effect.Advance(2);
    Check(!effect.active()&&effect.scale()==0, "the pickup finishes without leaving a solid card behind");
    card.actor_reference=300;card.kind=0;
    Check(!TakeBroomCard(progress,effect,card,2,1), "ordinary beans cannot enter the wizard-card path");
}

void OwnedScene(const std::filesystem::path& root) {
    const auto map = root / "Maps/Lev_Tut2.unr";
    const auto census = hpvr::wand::inspect_hp1_actor_visuals(map);
    Check(census.status == hpvr::wand::Hp1ProfileStatus::ok, "owned broom map census loads");
    hpvr_hp1_player_start_report start{};
    Check(hpvr_hp1_load_player_start_utf8(map.string().c_str(), kMetersPerUnrealUnit, 0, &start) == HPVR_HP1_PROFILE_OK,
          "owned broom player start loads");
    const float yaw = start.rotation_units[1] * kTau / 65536.0F;
    unsigned scenes = 0;
    for (const auto& actor : census.actors) {
        if (AsciiFold(actor.qualified_class_name) != "hpbase.cutscene") continue;
        IntroCutscene scene;
        Check(LoadIntroCutscene(census, start, yaw, &scene, actor.object_name, false), "every authored broom scene loads");
        for (const auto& track : scene.tracks) if (track.actor_reference == 78)
            Check(track.camera, "the owned BaseCam stays a camera in every scene");
        if (scene.object_name == "cutscene10") Check(scene.tracks.size() == 9, "owned introduction retains all nine cast tracks");
        ++scenes;
    }
    Check(scenes == 14, "owned lesson contains fourteen restored scenes");
    BroomRuntime runtime;
    Check(LoadBroomMetadata(root, census, start, yaw, runtime), "owned lesson runtime routes load");
    BroomVisuals visuals;
    std::vector<std::uint8_t> texture;
    std::uint32_t layers = 0;
    Check(LoadBroomVisuals(root, 256, 256, texture, layers, visuals), "owned hoop emission mesh and particle styles load");
    SharedHoopPose(std::move(runtime), visuals);

    WorldMetadata world;
    Check(LoadWorldMetadata(root, map, start, yaw, &world), "owned world metadata loads");
    PreparedGeometry geometry;
    Check(PrepareGeometryFromOwnedData(root, 2, start, yaw, world, geometry), "owned broom geometry prepares");
    const auto mounted=std::ranges::find_if(geometry.characters,[](const auto& draw){return draw.player;});
    BroomAvatar avatar;
    Check(mounted!=geometry.characters.end()&&BuildBroomAvatar(root,geometry.vertices,*mounted,avatar)&&!avatar.broom_only,
          "actual cooked mounted idle retains the first-person body and broom");
    std::set<std::uint32_t> sky_layers;
    unsigned sky_vertices=0,backdrop_vertices=0;
    for(const auto& vertex:geometry.vertices){
        if(vertex.polygon_flags&0x08000000U){
            ++sky_vertices;sky_layers.insert(vertex.texture_layer);
            Check((vertex.polygon_flags&8U)!=0,"sky geometry cannot become a flight obstacle");
        }
        if((vertex.polygon_flags&128U)&&!(vertex.polygon_flags&0x08000000U)){
            ++backdrop_vertices;
            Check((vertex.polygon_flags&1U)!=0&&(vertex.polygon_flags&8U)==0,
                  "authored fake backdrops are invisible but retain their flight boundary");
        }
    }
    Check(sky_vertices==36&&sky_layers.size()==6&&backdrop_vertices>0,
          "owned flying courtyard restores the six original sky faces without visible backdrop bricks");
    for(std::size_t first=0;first+2<geometry.vertices.size();first+=3){
        if(!(geometry.vertices[first].polygon_flags&0x08000000U))continue;
        Check(std::ranges::none_of(geometry.collision,[&](const auto& triangle){
            for(unsigned corner=0;corner<3;++corner)for(unsigned axis=0;axis<3;++axis)
                if(triangle.vertices[corner][axis]!=geometry.vertices[first+corner].position[axis])return false;
            return true;
        }),"render-only sky faces never enter the collision triangle set");
    }
    Check(geometry.fallback_materials==0,"the restored fountain uses no missing-material checkerboard");
    const auto card=std::ranges::find_if(geometry.beans,[](const auto& pickup){return pickup.actor_reference==265;});
    Check(card!=geometry.beans.end()&&card->kind==4&&card->count>0,
          "owned secret Merlin card is a collectible rather than scenery");
    Check(std::ranges::none_of(geometry.challenge_props,[](const auto& prop){return prop.reference==265;}),
          "the secret card has no duplicate static prop or prop collision");
    const auto authored_card=std::ranges::find_if(census.actors,[](const auto& actor){return actor.actor_reference==265;});
    Check(authored_card!=census.actors.end()&&card->position==ActorLocalPosition(*authored_card,start,yaw),
          "secret card remains at its authored floating position rather than snapping to the floor");
    const std::set<std::int32_t> expected{73,75,156,157,161,163,164};
    std::set<std::int32_t> actual;
    for (const auto& character : geometry.characters) actual.insert(character.actor_reference);
    Check(actual == expected && geometry.characters.size() == 7, "runtime keeps seven lesson actors, not the unused potion actor");
    Check(std::ranges::count_if(geometry.characters, [](const auto& character) { return character.player; }) == 1,
          "only BroomHarry owns the gameplay player role");
    Check(std::ranges::any_of(geometry.characters, [](const auto& character) { return character.clips.contains("look"); }),
          "authored setidle look clips survive character preparation");
    std::cout << "OWNED_BROOM_SCENE=PASS scenes=" << scenes << " characters=" << geometry.characters.size() << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 2) throw std::runtime_error("usage: hpvr_quest_broom_scene_tests [owned-game-root]");
        CameraClassification();
        MountedActorHandoff();
        SyntheticHoopPoses();
        SecretCardPickup();
        if (argc == 2) OwnedScene(std::filesystem::path(argv[1]));
        std::cout << "BROOM_SCENE_TESTS=PASS checks=" << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "BROOM_SCENE_TESTS=FAIL " << error.what() << '\n';
        return 1;
    }
}
