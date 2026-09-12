struct BroomRuntime {
    broom::LessonMetadata lesson;
    std::array<std::array<std::vector<broom::Hoop>,5>,2> routes;
    std::map<std::int32_t,std::size_t> hoop_indices;
    std::map<std::int32_t,std::array<float,3>> hoop_centers;
    broom::RouteState route;
    broom::SessionState session;
    broom::FlightMotion motion;
    broom::FlightInput input;
    unsigned path=0,stage=0,hits=0;
    bool active=false,pending_start=false,complete=false,boost=false;
    // 0 introduction, 1 trial, 2 assessment, 3 retry, 4 departure, 5 finished.
    unsigned phase=0;
    std::array<float,3> previous{};
    bool previous_valid=false;
    float seconds=0,vertical=0;
    std::array<float,3> start_head{};
    float start_yaw=0;
    std::vector<CutsceneLocation> camera_locations;
    std::map<std::int32_t,std::array<float,3>> prop_offsets;
};

bool TakeBroomCard(ProgressSave& progress,CardPickupEffect& effect,const BeanDraw& pickup,
    float scene_time,float sound_duration){
    if(pickup.kind!=4||pickup.actor_reference<=0||ChallengeCollected(progress,pickup.actor_reference)||
       !std::isfinite(scene_time))return false;
    auto& collected=progress.collected_beans;
    collected.insert(std::lower_bound(collected.begin(),collected.end(),pickup.actor_reference),pickup.actor_reference);
    const float duration=std::isfinite(sound_duration)?std::clamp(sound_duration,.5F,8.0F):1.0F;
    effect={pickup.actor_reference,0,duration,scene_time*1.8F+pickup.actor_reference};
    return true;
}

bool BroomExecutedTeleport(const IntroCutscene& scene,std::int32_t reference,std::string_view destination){
    for(const auto& track:scene.tracks)if(track.actor_reference==reference)
        for(std::size_t i=0;i<std::min(track.next_command,track.commands.size());++i){
            const auto command=AsciiFold(track.commands[i]);
            if(command=="teleport "+std::string(destination)||command=="goto "+std::string(destination))return true;
        }
    return false;
}

std::int32_t BroomCameraActor(const IntroCutscene& scene,std::int32_t player_reference){
    if(scene.object_name=="cutscene10"&&!BroomExecutedTeleport(scene,player_reference,"cutmark10"))return 156;
    if(scene.object_name=="cutscene14"&&BroomExecutedTeleport(scene,player_reference,"cutmark7"))return 156;
    return player_reference;
}

std::array<float,3> BroomCameraPosition(const IntroCutscene& scene,std::int32_t actor,std::array<float,3> position){
    if(scene.object_name=="cutscene14"&&actor==156&&!BroomExecutedTeleport(scene,156,"cutmark10"))
        for(const auto& mark:scene.locations)if(AsciiFold(mark.alias)=="cutmark10")return mark.position;
    // The original swaps walking/mounted actors with a 0.2-second gap.
    if(scene.object_name=="cutscene10"&&actor==156&&BroomExecutedTeleport(scene,156,"cutmark17"))
        for(const auto& mark:scene.locations)if(AsciiFold(mark.alias)=="cutmark10")return mark.position;
    return position;
}

broom::SessionConfig BroomSessionConfig(const BroomRuntime& state){
    broom::SessionConfig config;
    for(unsigned i=0;i<5;++i)config.stages[i]=state.routes[state.path][i];
    config.stage_time_bonuses=state.lesson.stage_seconds[state.path];
    config.minimum_hits=state.lesson.minimum_hits;
    config.hoop_assist_radius=.18F;
    return config;
}

bool LoadBroomMetadata(const std::filesystem::path& root,const wand::Hp1ActorVisualCensus& census,
    const hpvr_hp1_player_start_report& start,float yaw,BroomRuntime& output){
    BroomRuntime next;next.lesson=broom::LoadLessonMetadata(root,census);
    if(!next.lesson.valid){HPVR_LOGE("[hpvr.quest.broom] metadata=REJECTED reason=%s",next.lesson.error.c_str());return false;}
    next.path=next.lesson.first_path-1;
    for(std::size_t i=0;i<next.lesson.hoops.size();++i){
        const auto& authored=next.lesson.hoops[i];const auto& p=authored.center_unreal;const auto& n=authored.normal_unreal;
        const auto center=RotateYaw({p[1]*kMetersPerUnrealUnit-start.position_m[0],
            p[2]*kMetersPerUnrealUnit-start.position_m[1],-p[0]*kMetersPerUnrealUnit-start.position_m[2]},yaw);
        const auto normal=RotateYaw({n[1],n[2],-n[0]},yaw);
        next.routes[authored.path-1][authored.stage-1].push_back({authored.id,center,normal,
            std::min(authored.collision_radius_unreal,authored.collision_height_unreal)*kMetersPerUnrealUnit});
        next.hoop_indices.emplace(authored.id,i);next.hoop_centers.emplace(authored.id,center);
    }
    for(auto& path:next.routes)for(auto& route:path){
        broom::RouteState test;if(!broom::BeginRoute(test,1,route,0))return false;
    }
    output=std::move(next);return true;
}

std::array<float,3> BroomHoopOffset(const BroomRuntime& state,std::int32_t ref){
    const auto index=state.hoop_indices.find(ref);if(index==state.hoop_indices.end())return {};
    const auto& hoop=state.lesson.hoops[index->second];
    return hoop.bobbing?std::array<float,3>{0,hoop.bob_amount_unreal*kMetersPerUnrealUnit*std::sin(state.seconds*2.0F),0}:std::array<float,3>{};
}
