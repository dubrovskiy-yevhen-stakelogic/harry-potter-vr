// CPU-only, immutable scene preparation shared by the installer and Quest.
// Private game-derived data is written only to the user's preparation folder.
struct PreparedGeometry {
    bool prop_orientation_restored=false; // Runtime-only; never serialized into prepared caches.
    std::vector<GpuVertex> vertices;
    std::vector<std::uint8_t> textures,lightmaps;
    std::vector<CollisionTriangle> collision,prop_aim;
    std::vector<DoorDraw> doors;
    std::vector<CharacterDraw> characters;
    std::vector<BeanDraw> beans;
    std::vector<KnightDraw> knights;
    std::vector<ChallengeProp> challenge_props;
    std::vector<FlameEmitter> flames;
    std::vector<GlowEmitter> glows;
    std::vector<SpellTargetDescriptor> targets;
    std::uint32_t texture_width=256,texture_height=256,texture_layers=0;
    std::uint32_t lightmap_width=0,lightmap_height=0;
    std::uint32_t map_vertices=0,fixture_vertices=0,fixture_actors=0;
    std::uint32_t character_frame_vertices=0,animation_frames=0;
    std::uint32_t decoded_lightmaps=0,decoded_textures=0,fallback_materials=0;
};

bool PrepareGeometryFromOwnedData(const std::filesystem::path& root,unsigned map_id,
    const hpvr_hp1_player_start_report& start,float yaw,const WorldMetadata& world,
    PreparedGeometry& output,const std::function<void(const char*)>& progress={}){
    const auto stage=[&](const char* name){if(progress)progress(name);};
    const auto* descriptor=FindQuestMap(map_id);
    if(!descriptor)return false;
    const auto map=root/descriptor->package_path;
    PreparedGeometry g;g.flames=world.flames;g.glows=world.glows;
    auto scene=wand::build_hp1_textured_bsp_scene(root,map,kMetersPerUnrealUnit,kMaximumTriangles);
    if(scene.status!=wand::Hp1ProfileStatus::ok||scene.vertices.empty()||scene.vertices.size()%3||
        scene.texture_layer_width!=256||scene.texture_layer_height!=256||!scene.texture_layer_count||
        !scene.lightmap_width||!scene.lightmap_height||!scene.decoded_lightmap_count||
        scene.vertices.size()>std::numeric_limits<std::uint32_t>::max()||
        scene.texture_rgba8.size()!=std::uint64_t(256)*256*scene.texture_layer_count*4||
        scene.lightmap_rgba8.size()!=std::uint64_t(scene.lightmap_width)*scene.lightmap_height*4)return false;
    std::optional<std::array<float,3>> courtyard_ambient;
    if(map_id==2||map_id==3) {
        const auto topology=wand::load_hp1_bsp_topology(map);
        const auto actors=wand::inspect_hp1_actor_visuals(map);
        if(topology.status!=wand::Hp1ProfileStatus::ok || actors.status!=wand::Hp1ProfileStatus::ok ||
           !RestoreBroomSky(map,topology,actors,scene))return false;
        if(map_id==2)for(const auto& actor:actors.actors)
            if(AsciiFold(actor.qualified_class_name)=="engine.zoneinfo") {
                const auto ambient=wand::Hp1SerializedZoneAmbient(actor);
                if(ambient) {
                    if(courtyard_ambient)return false;
                    courtyard_ambient=ambient;
                }
            }
        if((map_id==2&&!courtyard_ambient) || scene.fallback_material_count!=0)return false;
    }
    g.texture_layers=scene.texture_layer_count;
    g.lightmap_width=scene.lightmap_width;g.lightmap_height=scene.lightmap_height;
    g.decoded_lightmaps=static_cast<std::uint32_t>(scene.decoded_lightmap_count);
    g.decoded_textures=static_cast<std::uint32_t>(scene.decoded_texture_count);
    g.fallback_materials=static_cast<std::uint32_t>(scene.fallback_material_count);
    g.vertices.reserve(scene.vertices.size());
    for(const auto& source:scene.vertices){
        const bool sky=(source.polygon_flags&kBroomSkyFlag)!=0;
        const auto p=RotateYaw({source.position_m.x-(sky?0:start.position_m[0]),source.position_m.y-(sky?0:start.position_m[1]),
            source.position_m.z-(sky?0:start.position_m[2])},yaw);
        g.vertices.push_back({{p[0],p[1],p[2]},
            {source.texture_uv[0],source.texture_uv[1]},{source.lightmap_uv[0],source.lightmap_uv[1]},
            source.texture_layer,(source.polygon_flags&~((map_id==3||map_id==4)?0x04000000U:0U))|(((map_id==3||map_id==4)&&source.texture_layer<scene.texture_layer_names.size()&&
                AsciiFold(scene.texture_layer_names[source.texture_layer]).find("mirrorblur")!=std::string::npos)?0x04000000U:0U)|((source.texture_layer<scene.texture_layer_names.size()&&
                IsReflectiveWoodFloor(scene.texture_layer_names[source.texture_layer],source.normal.y))?0x10000000U:0U),
            source.has_lightmap,PackAuthoredLighting(p,world.lights)});
    }
    g.map_vertices=static_cast<std::uint32_t>(g.vertices.size());
    g.collision=BuildCollisionTriangles(g.vertices,g.map_vertices);
    if(g.collision.empty())return false;
    g.textures=std::move(scene.texture_rgba8);g.lightmaps=std::move(scene.lightmap_rgba8);
    stage("BSP_READY");
    if(map_id==0){
        std::size_t actor_count=0;
        if(!LoadOwnedSceneProps(root,map,start,yaw,world.lights,g.collision,&g.vertices,&g.textures,
            &g.texture_layers,&g.fixture_vertices,&actor_count,&g.glows,&g.flames,&g.knights))return false;
        g.fixture_actors=static_cast<std::uint32_t>(actor_count);
    }else{
        const auto first=g.vertices.size();
        if(!LoadChallengeProps(root,map,start,yaw,world.lights,g.vertices,g.textures,g.texture_layers,g.collision,
            g.challenge_props,g.flames,g.glows))return false;
        g.fixture_vertices=static_cast<std::uint32_t>(g.vertices.size()-first);
        g.fixture_actors=static_cast<std::uint32_t>(g.challenge_props.size());
    }
    g.prop_aim=BuildPropAimTriangles(g.vertices,g.map_vertices,g.knights);
    stage("PROPS_READY");
    if(!LoadIntroDoors(root,map,start,yaw,world.lights,256,256,&g.vertices,&g.textures,&g.texture_layers,&g.doors))return false;
    stage("MOVERS_READY");
    QuestSpellTargets targets;
    if(!LoadOwnedCharacters(root,map,start,yaw,g.texture_layers,g.vertices,g.map_vertices,world.lights,
        &g.vertices,&g.textures,&g.texture_layers,&g.character_frame_vertices,&g.animation_frames,&g.characters,
        &targets,&g.targets))return false;
    stage("CHARACTERS_READY");
    if(!LoadOwnedBeans(root,map,start,yaw,g.vertices,g.textures,g.texture_layers,g.beans,&g.collision))return false;
    if((map_id==1||map_id==3)&&!LoadChallengeStars(root,map,start,yaw,g.vertices,g.textures,g.texture_layers,g.beans,map_id==3?6:8))return false;
    if(courtyard_ambient) {
        for(auto& vertex:g.vertices)
            if(!vertex.has_lightmap && !(vertex.polygon_flags&kBroomSkyFlag))
                vertex.packed_light=BroomAmbientLighting(vertex.packed_light,*courtyard_ambient);
    }
    stage("GEOMETRY_PREPARED");
    output=std::move(g);return true;
}
