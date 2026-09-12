#pragma once
// Runtime-only augmentation of older prepared maps. Include after the prepared
// codec; call before vector adoption and GPU upload. No textures or cache files
// are changed, and existing vertices/ranges are never rewritten.
inline constexpr std::size_t kPreparedGnomeVertexReserve = 600000;
struct GnomeClipRestoreStats {
    bool valid=false;
    const char* error="";
    std::size_t actors=0,clips=0,vertices=0,root_tracks_frozen=0;
};

GnomeClipRestoreStats RestorePreparedGnomeClips(PreparedGeometry& geometry,
                                               const std::filesystem::path& root) {
    GnomeClipRestoreStats result;
    constexpr std::array<const char*,5> names{"runattack","runattackbite","knockback","downbreath","downdizzy"};
    std::vector<std::size_t> actors;
    for(std::size_t i=0;i<geometry.characters.size();++i)
        if(AsciiFold(geometry.characters[i].class_name)=="tut1.tut1gnome" &&
           std::ranges::any_of(names,[&](const auto* name){return !geometry.characters[i].clips.contains(name);}))actors.push_back(i);
    if(actors.empty()){result.valid=true;return result;}
    if(actors.size()>3){result.error="GNOME_COUNT";return result;}
    const auto package=root/"system/HPModels.u";
    const auto mesh=wand::build_hp1_skeletal_triangle_mesh(package,230,kMetersPerUnrealUnit);
    const auto skin=wand::load_hp1_skeletal_skin(package,230);
    if(mesh.status!=wand::Hp1ProfileStatus::ok||skin.status!=wand::Hp1ProfileStatus::ok||mesh.vertices.empty()||skin.census.animation_reference!=234){
        result.error="GNOME_OWNED_MESH";return result;
    }
    const auto animation=wand::load_hp1_animation(package,skin.census.animation_reference);
    if(animation.status!=wand::Hp1ProfileStatus::ok){result.error="GNOME_OWNED_ANIMATION";return result;}
    const auto idle=std::ranges::find_if(animation.sequences,[](const auto& clip){return AsciiFold(clip.name)=="breath";});
    if(idle==animation.sequences.end()){result.error="GNOME_IDLE";return result;}
    const auto rest=wand::sample_hp1_skeletal_animation(skin,animation,static_cast<std::size_t>(idle-animation.sequences.begin()),0);
    if(rest.status!=wand::Hp1ProfileStatus::ok){result.error="GNOME_REST";return result;}
    constexpr float scale=kMetersPerUnrealUnit*1.25F;
    float rest_floor=std::numeric_limits<float>::infinity();
    for(const auto& vertex:mesh.vertices){
        if(vertex.point_index>=rest.points.size()){result.error="GNOME_POINT";return result;}
        rest_floor=std::min(rest_floor,rest.points[vertex.point_index].z*scale);
    }
    struct ClipPlan {const char* name;std::size_t sequence;std::uint32_t frames;float duration;};
    std::vector<ClipPlan> plans;
    std::size_t required=0;
    for(const auto* name:names){
        const auto found=std::ranges::find_if(animation.sequences,[&](const auto& clip){return AsciiFold(clip.name)==name;});
        if(found==animation.sequences.end()){result.error="GNOME_CLIP_MISSING";return result;}
        const auto index=static_cast<std::size_t>(found-animation.sequences.begin());
        if(index>=animation.moves.size()){result.error="GNOME_MOVE";return result;}
        const float duration=animation.moves[index].track_time;
        if(!std::isfinite(duration)||duration<=0||duration>10){result.error="GNOME_DURATION";return result;}
        const auto frames=static_cast<std::uint32_t>(std::clamp(std::ceil(duration*30),2.F,512.F));
        plans.push_back({name,index,frames,duration});
        for(auto actor:actors)if(!geometry.characters[actor].clips.contains(name))required+=mesh.vertices.size()*frames;
    }
    const auto old_size=geometry.vertices.size();
    if(required>kPreparedGnomeVertexReserve || old_size+required>std::numeric_limits<std::uint32_t>::max()){
        result.error="GNOME_VERTEX_BUDGET";return result;
    }
    // The caller reserves this together with frontend capacity while reading
    // the cache, avoiding another copy of the entire scene on the headset.
    if(geometry.vertices.capacity()<old_size+required){result.error="GNOME_RESERVE";return result;}
    for(auto index:actors){
        const auto& actor=geometry.characters[index];
        const auto idle_clip=actor.clips.find("breathe");
        if(actor.vertex_count!=mesh.vertices.size()||idle_clip==actor.clips.end()||
           std::uint64_t(idle_clip->second.first_vertex)+actor.vertex_count>old_size){result.error="GNOME_CACHED_MESH";return result;}
        // The cached first frame supplies the exact material, mask, UV and
        // authored lighting; the owned mesh supplies only point remapping.
        for(std::size_t v=0;v<mesh.vertices.size();++v){
            const auto& cached=geometry.vertices[idle_clip->second.first_vertex+v];
            if(cached.texture_layer>=geometry.texture_layers ||
               std::abs(cached.texture_uv[0]-mesh.vertices[v].texture_uv[0])>1e-5F ||
               std::abs(cached.texture_uv[1]-mesh.vertices[v].texture_uv[1])>1e-5F){result.error="GNOME_CACHED_MATERIAL";return result;}
        }
    }
    for(const auto& plan:plans){
        auto local_animation=animation;
        auto& move=local_animation.moves[plan.sequence];
        // Scene AI owns all horizontal translation. Freeze root XY to this
        // clip's first key, retaining its authored Z fall/sitting trajectory.
        // Run root height is stabilized by the sampler, exactly like Harry.
        for(std::size_t bone=0;bone<skin.bones.size();++bone){
            if(skin.bones[bone].parent_index!=static_cast<std::int32_t>(bone))continue;
            if(bone>=move.bone_indices.size()){result.error="GNOME_ROOT_MAP";return result;}
            const auto track_index=move.bone_indices[bone];
            if(track_index<0)continue;
            if(static_cast<std::size_t>(track_index)>=move.tracks.size()){result.error="GNOME_ROOT_TRACK";return result;}
            const auto& track=move.tracks[static_cast<std::size_t>(track_index)];
            if(track.position_offset+track.position_count>local_animation.compressed_position_keys.size()){result.error="GNOME_ROOT_KEYS";return result;}
            if(track.position_count){
                const auto first=local_animation.compressed_position_keys[track.position_offset];
                for(std::size_t key=0;key<track.position_count;++key){
                    auto& target=local_animation.compressed_position_keys[track.position_offset+key];
                    target[0]=first[0];target[1]=first[1];
                }
                ++result.root_tracks_frozen;
            }
        }
        struct Destination {std::size_t actor;std::uint32_t first,rest;};
        std::vector<Destination> destinations;
        for(auto actor:actors)if(!geometry.characters[actor].clips.contains(plan.name)){
            const auto first=static_cast<std::uint32_t>(geometry.vertices.size());
            destinations.push_back({actor,first,geometry.characters[actor].clips.at("breathe").first_vertex});
            geometry.vertices.resize(geometry.vertices.size()+mesh.vertices.size()*plan.frames);
            geometry.characters[actor].clips.emplace(plan.name,CharacterClip{first,plan.duration,plan.frames});
            ++result.clips;
        }
        if(destinations.empty())continue;
        const bool running=std::string_view(plan.name).starts_with("run");
        const bool looping=running||std::string_view(plan.name)=="downbreath";
        for(std::uint32_t frame=0;frame<plan.frames;++frame){
            const float time=plan.duration*static_cast<float>(frame)/static_cast<float>(looping?plan.frames:plan.frames-1);
            const auto pose=wand::sample_hp1_skeletal_animation(skin,local_animation,plan.sequence,time,looping,running);
            if(pose.status!=wand::Hp1ProfileStatus::ok||pose.points.size()!=rest.points.size()){result.error="GNOME_POSE";return result;}
            for(const auto& destination:destinations){
                const auto& actor=geometry.characters[destination.actor];
                for(std::size_t v=0;v<mesh.vertices.size();++v){
                    const auto point=pose.points[mesh.vertices[v].point_index];
                    const std::array<float,3> model{point.y*scale,point.z*scale-rest_floor,point.x*scale};
                    const auto world=AddVector(actor.base_origin,RotateYaw(model,actor.base_yaw));
                    if(!std::ranges::all_of(world,[](float value){return std::isfinite(value);})){result.error="GNOME_POSITION";return result;}
                    auto vertex=geometry.vertices[destination.rest+v];
                    for(unsigned axis=0;axis<3;++axis)vertex.position[axis]=world[axis];
                    geometry.vertices[destination.first+std::size_t(frame)*mesh.vertices.size()+v]=vertex;
                }
            }
        }
    }
    result.actors=actors.size();result.vertices=geometry.vertices.size()-old_size;result.valid=true;return result;
}
