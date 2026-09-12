// Immutable outdoor-scene fixes. Sky triangles retain the owned room's UVs and
// become eye-relative directions; no extra draw pass or dynamic light is needed.
constexpr std::uint32_t kBroomSkyFlag=0x08000000U;

bool RestoreBroomSky(const std::filesystem::path& map,
                     const wand::Hp1BspTopology& topology,
                     const wand::Hp1ActorVisualCensus& actors,
                     wand::Hp1TexturedBspScene& scene) {
    static_cast<void>(map);
    const auto sky=std::ranges::find_if(actors.actors,[](const auto& actor){
        return AsciiFold(actor.qualified_class_name)=="engine.skyzoneinfo" && actor.location_serialized;});
    if(sky==actors.actors.end())return false;
    const auto zone=std::ranges::find(topology.zone_actor_references,sky->actor_reference);
    if(zone==topology.zone_actor_references.end())return false;
    const auto sky_zone=static_cast<unsigned>(zone-topology.zone_actor_references.begin());
    const std::array<float,3> origin{sky->location_unreal.y*kMetersPerUnrealUnit,
        sky->location_unreal.z*kMetersPerUnrealUnit,-sky->location_unreal.x*kMetersPerUnrealUnit};
    std::size_t sky_vertices=0,backdrop_vertices=0;
    std::set<std::uint32_t> sky_layers;
    for(auto& vertex:scene.vertices) {
        if(vertex.node_index>=topology.nodes.size())return false;
        const auto& node=topology.nodes[vertex.node_index];
        if(node.zone_indices[1]==sky_zone) {
            if(vertex.texture_layer==0 || (vertex.polygon_flags&kBroomSkyFlag))return false;
            vertex.position_m.x-=origin[0];vertex.position_m.y-=origin[1];vertex.position_m.z-=origin[2];
            vertex.polygon_flags=(vertex.polygon_flags&~(1U|128U))|kBroomSkyFlag|kPolyNotSolid;
            vertex.has_lightmap=0;
            sky_layers.insert(vertex.texture_layer);++sky_vertices;
        } else if((vertex.polygon_flags&128U)!=0) {
            // Invisible, not non-solid: keep the original flight boundary.
            vertex.polygon_flags|=1U;++backdrop_vertices;
        }
    }
    HPVR_LOGI("[hpvr.quest.broom.environment] sky_vertices=%zu sky_faces=%zu backdrop_vertices=%zu",
        sky_vertices,sky_layers.size(),backdrop_vertices);
    return sky_vertices==36 && sky_layers.size()==6 && backdrop_vertices>0;
}

std::uint32_t BroomAmbientLighting(std::uint32_t packed,const std::array<float,3>& ambient) {
    constexpr std::array<float,3> old_ambient{.16F,.17F,.20F};
    std::uint32_t result=packed&0xFF000000U;
    for(unsigned c=0;c<3;++c) {
        const float previous=float((packed>>(c*8U))&255U)*(1.25F/255.0F);
        const auto channel=static_cast<std::uint32_t>(std::lround(
            std::clamp(previous-old_ambient[c]+ambient[c],0.0F,1.25F)*(255.0F/1.25F)));
        result|=channel<<(c*8U);
    }
    return result;
}
