bool BroomCapsuleBlocked(const std::vector<CollisionTriangle>& triangles,
                         const std::array<float,3>& center) {
    const float radius=kPlayerCapsuleRadiusMeters-kGroundContactEpsilonMeters;
    const float half=kPlayerCapsuleHalfHeightMeters-kPlayerCapsuleRadiusMeters;
    const unsigned samples=std::max(1U,static_cast<unsigned>(std::ceil(2*half/radius)));
    for(const auto& triangle:triangles){
        if(center[0]+radius<triangle.minimum[0]||center[0]-radius>triangle.maximum[0]||
           center[1]+kPlayerCapsuleHalfHeightMeters<triangle.minimum[1]||
           center[1]-kPlayerCapsuleHalfHeightMeters>triangle.maximum[1]||
           center[2]+radius<triangle.minimum[2]||center[2]-radius>triangle.maximum[2])continue;
        for(unsigned i=0;i<=samples;++i){
            auto point=center;point[1]+=-half+2*half*static_cast<float>(i)/static_cast<float>(samples);
            const auto distance=SubtractVector(point,ClosestPointOnTriangle(point,triangle));
            if(DotVector(distance,distance)<radius*radius)return true;
        }
    }
    return false;
}

bool QuestScene::ResolveBroomMovement(const std::array<float,3>& center,LocomotionMove* output) const {
    if(!output)return false;
    *output={};auto& s=*state_;auto& b=s.broom;
    if(!b.active||s.restore_pending||!s.tracking_active||s.frontend.PausesWorld()||IsCutscenePlaying())return true;
    if(!std::ranges::all_of(center,[](float v){return std::isfinite(v);}))return false;
    const float normal=b.lesson.normal_speed_unreal*kMetersPerUnrealUnit;
    const float speed=(b.boost?b.lesson.boost_speed_unreal:b.lesson.normal_speed_unreal)*kMetersPerUnrealUnit;
    const broom::FlightConfig config{speed,normal,normal*3,normal*4,.05F};
    const auto requested=broom::AdvanceFlight(b.motion,b.input,config,s.physics_step);
    const float length=std::sqrt(DotVector(requested,requested));
    if(!std::isfinite(length)||length>2)return false;
    const unsigned steps=std::max(1U,static_cast<unsigned>(std::ceil(length/.04F)));
    const auto delta=ScaleVector(requested,1.0F/static_cast<float>(steps));auto position=center;
    for(unsigned step=0;step<steps;++step){
        const auto candidate=AddVector(position,delta);
        if(!BroomCapsuleBlocked(s.collision_triangles,candidate)){position=candidate;continue;}
        ++output->blocked_substeps;
        for(unsigned axis=0;axis<3;++axis){
            if(std::abs(delta[axis])<.000001F)continue;
            auto slide=position;slide[axis]+=delta[axis];
            if(!BroomCapsuleBlocked(s.collision_triangles,slide))position=slide;
            else b.motion.velocity[axis]=0;
        }
    }
    output->displacement=SubtractVector(position,center);
    return true;
}

void QuestScene::StartBroomScene(const std::string& tag,unsigned phase) {
    auto& s=*state_;auto& b=s.broom;
    if(s.map_id!=2||s.intro_cutscene.playing)return;
    const auto name=AsciiFold(tag);const auto reference=b.lesson.scenes_by_tag.find(name);
    if(reference==b.lesson.scenes_by_tag.end()||!s.challenge.scenes.contains(reference->second)){
        b.active=false;s.frontend.screen=FrontScreen::Main;s.frontend.message="FLYING LESSON SCENE IS MISSING";return;
    }
    if(!s.challenge_initial_checkpoint_valid){
        s.challenge_initial_checkpoint=s.frontend.progress;
        s.challenge_initial_checkpoint.graph_state=s.challenge.graph.Serialize();
        s.challenge_initial_checkpoint_valid=true;
    }
    b.phase=phase;b.active=false;b.pending_start=false;b.previous_valid=false;
    b.session.previous_valid=false;broom::ResetFlight(b.motion);
    s.jump={};s.climb={};s.jump_pending=false;s.projectile={};s.basic_cast={};
    s.challenge.active_scene=reference->second;
    s.intro_cutscene=s.challenge.scenes.at(reference->second);auto& scene=s.intro_cutscene;
    scene.playing=true;b.camera_locations=scene.locations;
    for(auto& track:scene.tracks){
        if(const auto offset=b.prop_offsets.find(track.actor_reference);offset!=b.prop_offsets.end())
            track.position=AddVector(track.position,offset->second);
        for(auto& actor:s.character_draws)if(actor.actor_reference==track.actor_reference){
            actor.enabled=true;track.position=AddVector(actor.base_origin,actor.cutscene_offset);
            if(actor.player){
                s.harry_actor=actor.actor_reference;
                track.position=s.last_player;
                track.position[1]-=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;
            }
            actor.cutscene_offset=SubtractVector(track.position,actor.base_origin);
            actor.active_clip=actor.clips.contains("hover")?"hover":"breathe";
            actor.animation_time=0;actor.animation_loop=true;
        }
        for(const auto& command:track.commands){
            const auto split=command.find(' ');const auto op=AsciiFold(command.substr(0,split));
            if(op=="sleep"||op=="say"||op=="talk"||op=="waitfor"||op=="moveto")break;
            if(split==std::string::npos)continue;
            const auto argument=AsciiFold(command.substr(split+1));
            if(track.camera&&(op=="preface"||op=="face"||op=="turnto"))scene.camera_target=argument;
            if(op!="goto"&&op!="teleport")continue;
            for(const auto& location:scene.locations)if(AsciiFold(location.alias)==argument){
                track.position=location.position;
                if(track.camera){scene.camera_position=location.position;scene.camera_position_valid=true;}
                else for(auto& actor:s.character_draws)if(actor.actor_reference==track.actor_reference)
                    actor.cutscene_offset=SubtractVector(location.position,actor.base_origin);
            }
        }
    }
    s.audio.StopDialogue();s.front_anchor_valid=false;s.exit_return={};
    SaveBroomProgress();
    HPVR_LOGI("[hpvr.quest.broom.scene] status=STARTED tag=%s phase=%u",name.c_str(),phase);
}

void QuestScene::BeginBroomTrial() {
    auto& s=*state_;auto& b=s.broom;
    if(s.map_id!=2||s.intro_cutscene.playing||b.path>=b.routes.size())return;
    if(!broom::BeginSession(b.session,BroomSessionConfig(b))){
        b.active=false;s.frontend.screen=FrontScreen::Main;s.frontend.message="INVALID FLYING LESSON ROUTE";return;
    }
    for(const auto& actor:s.character_draws)if(actor.actor_reference==b.lesson.player_reference){
        s.last_player=AddVector(actor.base_origin,actor.cutscene_offset);
        s.last_player[1]+=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;
        break;
    }
    b.stage=0;b.hits=0;b.route=b.session.route;b.phase=1;b.active=true;b.pending_start=false;b.complete=false;
    b.seconds=0;b.previous_valid=false;broom::ResetFlight(b.motion);
    s.challenge.active_scene=0;s.intro_cutscene.camera_active=false;
    s.placement=s.frontend.progress;s.placement.player=s.last_player;s.placement.yaw=s.last_yaw;
    s.restore_pending=true;s.player_capsule_valid=false;
    s.frontend.BeginGame();s.front_anchor_valid=false;
    DispatchBroomEvent("RememberallChaseMusic");
    if(const auto sound=GameplayDialogueIndex(s.frontend.assets,"Q_Whistle_Short"))
        (void)s.audio.PlayWorldEffect(*sound,.8F);
    SaveBroomProgress();
    HPVR_LOGI("[hpvr.quest.broom] status=TRIAL_STARTED path=%u time=%.1f",b.path+1,broom::SessionTimeRemaining(b.session));
}

void QuestScene::DispatchBroomEvent(const std::string& event) {
    auto& s=*state_;if(s.map_id!=2)return;
    const auto tag=AsciiFold(event);
    if(tag=="gamereferee")s.broom.pending_start=true;
    (void)s.challenge.graph.Dispatch(tag);
}

void QuestScene::SaveBroomProgress() {
    auto& s=*state_;auto& b=s.broom;
    if(s.map_id!=2||!b.lesson.valid||s.restoring||b.path>=2)return;
    auto& progress=s.frontend.progress;
    const unsigned resume=b.phase==1?3:b.phase==2?4:b.phase;
    progress.map_id=2;progress.phase=2;progress.page=14;progress.quest_stage=resume;
    progress.player=b.start_head;progress.yaw=b.start_yaw;progress.health=100;
    progress.graph_state.clear();
    std::ostringstream out;out<<"BROOM_WORLD 1 "<<b.path<<' '<<resume;
    progress.world_state=out.str();
    const bool saved=s.frontend.Save();
    HPVR_LOGI("[hpvr.quest.broom.save] status=%s path=%u resume_phase=%u",saved?"COMMITTED":"FAILED",b.path+1,resume);
}

void QuestScene::RestoreBroomProgress() {
    auto& s=*state_;auto& b=s.broom;auto& progress=s.frontend.progress;
    unsigned path=b.lesson.first_path-1,phase=0;
    const bool fresh=progress.world_state.empty();
    if(!fresh){
        std::istringstream in(progress.world_state);std::string magic;unsigned version=0;
        bool valid=bool(in>>magic>>version>>path>>phase)&&magic=="BROOM_WORLD"&&version==1&&path<2&&phase<=5;
        in>>std::ws;valid=valid&&in.eof();
        if(!valid){s.frontend.screen=FrontScreen::Main;s.frontend.message="INVALID FLYING LESSON CHECKPOINT";return;}
    }
    if(!s.challenge_initial_checkpoint_valid){
        s.challenge_initial_checkpoint=progress;
        s.challenge_initial_checkpoint.graph_state=s.challenge.graph.Serialize();
        s.challenge_initial_checkpoint_valid=true;
    }
    if(!s.challenge.graph.Restore(s.challenge_initial_checkpoint.graph_state)){
        s.frontend.screen=FrontScreen::Main;s.frontend.message="FLYING LESSON EVENT RESET FAILED";return;
    }
    b.path=path;b.phase=phase==1?3:phase==2?4:phase;b.active=false;b.pending_start=false;
    b.complete=b.phase==5;b.stage=0;b.hits=0;b.seconds=0;b.previous_valid=false;b.camera_locations.clear();
    b.prop_offsets.clear();
    broom::ResetSession(b.session);broom::ResetFlight(b.motion);b.route={};
    s.intro_cutscene.playing=false;s.intro_cutscene.camera_active=false;s.challenge.active_scene=0;
    s.challenge.pending_scenes.clear();s.challenge.complete=false;
    for(auto& door:s.doors){
        (void)movers::Settle(door.motion,0);door.phase=0;door.opening=false;door.completion_sent=false;
        door.loop_started=false;door.hold=0;door.grid_offset={};door.grid_target={};
    }
    for(auto& actor:s.character_draws){
        actor.cutscene_offset={};actor.yaw=actor.desired_yaw=actor.base_yaw;actor.enabled=true;
        actor.animation_time=0;actor.animation_loop=true;
        actor.active_clip=actor.clips.contains("hover")?"hover":"breathe";
    }
    for(auto& zone:s.challenge.spatial)zone.inside=false;
    RebuildChallengeCollision();s.last_player=b.start_head;s.last_yaw=b.start_yaw;
    s.placement=progress;s.placement.player=b.start_head;s.placement.yaw=b.start_yaw;
    s.restore_pending=true;s.player_capsule_valid=false;s.jump={};s.climb={};s.jump_pending=false;
    s.projectile={};s.basic_cast={};s.card_pickup={};s.pickup_flights.clear();s.exit_return={};
    s.audio.StopDialogue();s.audio.SelectMusic(s.frontend.assets.level_music_index);
    s.audio.SetPresentationAudio(true,false);s.front_anchor_valid=false;
    if(b.phase==5){s.frontend.ShowDemoNotice(true);return;}
    if(fresh){s.frontend.screen=FrontScreen::Objective;s.frontend.selection=0;return;}
    s.frontend.BeginGame();
    const bool restoring=s.restoring;s.restoring=true;
    StartBroomScene(b.phase==0?"intro":b.phase==3?"redo":"exit",b.phase);
    s.restoring=restoring;
}

void QuestScene::RequestBroomTravel() {
    auto& s=*state_;auto& b=s.broom;
    if(s.map_id==1){
        if(s.travel_pending||s.travel_blocked)return;
        s.travel_origin=s.frontend.progress;auto next=s.frontend.progress;
        next.map_id=2;next.phase=2;next.page=14;next.quest_stage=0;
        next.banked_beans+=static_cast<unsigned>(next.collected_beans.size())-std::min<unsigned>(next.challenge_stars,static_cast<unsigned>(next.collected_beans.size()));
        next.collected_beans.clear();next.activated_events.clear();next.challenge_stars=0;
        next.graph_state.clear();next.world_state.clear();next.player={0,kPlayerEyeHeightMeters,0};next.yaw=0;
        s.travel_progress=std::move(next);s.travel_slot=s.frontend.slot;s.travel_pending=true;
        HPVR_LOGI("[hpvr.quest.travel] status=REQUESTED from=Lev_Tut1b to=Lev_Tut2");return;
    }
    if(s.map_id!=2||b.phase==5)return;
    b.active=false;b.complete=true;b.phase=5;broom::ResetFlight(b.motion);
    s.intro_cutscene.playing=false;s.intro_cutscene.camera_active=false;s.challenge.active_scene=0;
    s.audio.StopDialogue();SaveBroomProgress();s.frontend.ShowDemoNotice(true);s.front_anchor_valid=false;
    HPVR_LOGI("[hpvr.quest.broom] status=COMPLETE next_map=Lev_Tut3 boundary=NOT_PREPARED");
}

void QuestScene::AdvanceBroom(float seconds) {
    auto& s=*state_;auto& b=s.broom;auto& c=s.challenge;
    if(s.map_id!=2||!std::isfinite(seconds)||seconds<=0||!c.graph.healthy())return;
    const float step=std::min(seconds,.05F);
    if(!s.tracking_active||s.frontend.PausesWorld()){
        b.session.previous_valid=false;b.previous_valid=false;return;
    }
    b.seconds+=step;s.bean_time=std::fmod(s.bean_time+step,1000.0F);s.bean_hud_time=std::max(0.0F,s.bean_hud_time-step);
    s.card_pickup.Advance(step);
    for(auto& flight:s.pickup_flights)flight.elapsed+=step;
    std::erase_if(s.pickup_flights,[](const auto& flight){return flight.elapsed>=flight.duration;});
    const bool was_playing=s.intro_cutscene.playing;
    AdvanceIntroCutscene(step);
    if(was_playing&&c.scenes.contains(c.active_scene)){
        const auto& authored=c.scenes.at(c.active_scene);
        for(const auto& track:s.intro_cutscene.tracks){
            if(std::ranges::none_of(c.props,[&](const auto& prop){return prop.reference==track.actor_reference;}))continue;
            const auto original=std::ranges::find_if(authored.tracks,[&](const auto& candidate){return candidate.actor_reference==track.actor_reference;});
            if(original!=authored.tracks.end())b.prop_offsets[track.actor_reference]=SubtractVector(track.position,original->position);
        }
    }
    for(auto& actor:s.character_draws){
        actor.animation_time+=step;
        actor.yaw+=std::clamp(std::remainder(actor.desired_yaw-actor.yaw,kTau),-8*step,8*step);
    }
    (void)c.graph.Advance(step);
    for(unsigned pass=0;pass<16&&c.graph.healthy();++pass){
        const auto effects=c.graph.DrainEffects();if(effects.empty())break;
        for(const auto& effect:effects){
            if(effect.kind==MapEventKind::mover_trigger){
                for(auto& door:s.doors)if(door.actor_reference==effect.actor_reference){
                    door.opening=door.initial_state.find("toggle")!=std::string::npos?!door.opening:effect.enabled;
                    door.motion.seconds=door.opening?door.open_seconds:door.close_seconds;
                    if(movers::Start(door.motion,door.opening)){door.completion_sent=false;door.hold=0;c.collision_dirty=true;}
                }
            }else if(effect.kind==MapEventKind::music){
                for(const auto& music:s.frontend.assets.music_cues)if(music.actor_reference==effect.actor_reference)
                    s.audio.SelectMusic(effect.enabled?music.music_index:-1);
            }else if(effect.kind==MapEventKind::sound){
                const auto split=effect.asset_path.find_last_of('.');
                const auto name=effect.asset_path.substr(split==std::string::npos?0:split+1);
                if(const auto sound=GameplayDialogueIndex(s.frontend.assets,name))(void)s.audio.PlayWorldEffect(*sound,.75F);
            }else if(effect.kind==MapEventKind::actor_trigger&&effect.actor_reference==b.lesson.referee_reference){
                b.pending_start=true;
            }else if(effect.kind==MapEventKind::cutscene_start&&effect.actor_reference!=c.active_scene){
                if(std::ranges::find(c.pending_scenes,effect.actor_reference)==c.pending_scenes.end())c.pending_scenes.push_back(effect.actor_reference);
            }
        }
    }
    bool moved=false;
    for(auto& door:s.doors){
        const auto previous=door.motion.pose;
        if(door.hold>0){
            door.hold=std::max(0.0F,door.hold-step);
            if(door.hold==0){door.motion.seconds=door.close_seconds;(void)movers::Start(door.motion,false);}
        }else{
            const auto arrival=movers::Advance(door.motion,step);
            if(arrival.finished&&!door.completion_sent){door.completion_sent=true;(void)c.graph.Signal(door.actor_reference);}
        }
        if(door.motion.count>1){
            const float key=static_cast<float>(door.motion.current)+
                (static_cast<float>(door.motion.target)-static_cast<float>(door.motion.current))*door.motion.phase;
            door.phase=std::clamp(key/static_cast<float>(door.motion.count-1),0.0F,1.0F);
        }
        moved|=previous.offset_unreal!=door.motion.pose.offset_unreal||previous.rotation_units!=door.motion.pose.rotation_units;
        if(door.tag=="cammover"&&s.intro_cutscene.playing){
            for(const auto& authored:b.camera_locations)if(authored.actor_reference==62){
                const auto position=MoverPoint(door,authored.position);
                for(auto& location:s.intro_cutscene.locations)if(location.actor_reference==authored.actor_reference)location.position=position;
                for(auto& track:s.intro_cutscene.tracks)if(track.camera){
                    std::string target;
                    for(std::size_t i=0;i<std::min(track.next_command,track.commands.size());++i){
                        const auto split=track.commands[i].find(' ');const auto op=AsciiFold(track.commands[i].substr(0,split));
                        if(split!=std::string::npos&&(op=="moveto"||op=="teleport"||op=="goto"))target=AsciiFold(track.commands[i].substr(split+1));
                    }
                    if(target==AsciiFold(authored.alias)){
                        if(track.moving)track.move_target=position;
                        else{s.intro_cutscene.camera_position=position;s.intro_cutscene.camera_position_valid=true;track.position=position;}
                    }
                }
            }
        }
    }
    if(moved||c.collision_dirty)RebuildChallengeCollision();
    if(b.complete&&b.phase==4){RequestBroomTravel();return;}
    if(was_playing&&!s.intro_cutscene.playing){
        c.active_scene=0;
        if(b.phase==0||b.phase==3){BeginBroomTrial();return;}
        if(b.phase==2){StartBroomScene("exit",4);return;}
        if(b.phase==4){RequestBroomTravel();return;}
    }
    if(!s.intro_cutscene.playing&&!c.pending_scenes.empty()){
        const auto ref=c.pending_scenes.front();c.pending_scenes.erase(c.pending_scenes.begin());
        for(const auto& [tag,id]:b.lesson.scenes_by_tag)if(id==ref){
            StartBroomScene(tag,tag=="intro"?0:tag=="redo"?3:tag=="exit"?4:2);return;
        }
    }
    if(!b.active||s.intro_cutscene.playing)return;
    auto body=s.last_player;body[1]-=kPlayerEyeHeightMeters;
    if(s.player_capsule_valid&&!s.restore_pending)body=s.player_capsule;
    for(auto& path:b.routes)for(auto& route:path)for(auto& hoop:route)
        hoop.center=AddVector(b.hoop_centers.at(hoop.id),BroomHoopOffset(b,hoop.id));
    const auto update=broom::AdvanceSession(b.session,BroomSessionConfig(b),body,seconds,s.restore_pending);
    b.route=b.session.route;b.stage=static_cast<unsigned>(b.session.stage);b.hits=static_cast<unsigned>(b.session.hits);
    if(update.hits_added){
        auto last_order=b.session.hits;
        for(const auto& stage:b.routes[b.path]){
            if(last_order<=stage.size())break;
            last_order-=stage.size();
        }
        const auto number=std::clamp<std::size_t>(last_order,1,15);
        const auto name="Q_Through_Hoop"+std::string(number<10?"0":"")+std::to_string(number);
        if(const auto sound=GameplayDialogueIndex(s.frontend.assets,name))(void)s.audio.PlayWorldEffect(*sound,.8F);
        HPVR_LOGI("[hpvr.quest.broom] hit=%u stage=%u remaining=%.2f",b.hits,b.stage+1,broom::SessionTimeRemaining(b.session));
    }
    if(b.session.status==broom::SessionStatus::Invalid){
        b.active=false;StartBroomScene("redo",3);return;
    }
    if(update.finished){
        b.active=false;broom::ResetFlight(b.motion);
        const auto result=b.session.result;
        HPVR_LOGI("[hpvr.quest.broom] result=%s hits=%u points=%u",broom::SessionResultTag(result.grade),b.hits,result.house_points);
        StartBroomScene(broom::SessionResultTag(result.grade),result.passed?2:3);return;
    }
    for(auto& zone:c.spatial){
        if(zone.spell||zone.checkpoint||zone.name=="hpbase.cutscene")continue;
        const auto distance=SubtractVector(body,zone.position);
        const bool inside=std::hypot(distance[0],distance[2])<=zone.radius&&std::abs(distance[1])<=zone.height+kPlayerCapsuleHalfHeightMeters;
        if(inside&&!zone.inside)(void)c.graph.Touch(zone.reference);
        zone.inside=inside;
    }
    for(const auto& pickup:s.beans){
        if(ChallengeCollected(s.frontend.progress,pickup.actor_reference)||
           !CanCollectBean(s.collision_triangles,body,BeanWorldPosition(pickup)))continue;
        if(pickup.kind==4){
            if(!TakeBroomCard(s.frontend.progress,s.card_pickup,pickup,s.bean_time,
                s.audio.DialogueDurationSeconds(s.card_sound)))continue;
            (void)s.audio.PlayWorldEffect(s.card_sound,.9F);
            SaveBroomProgress();
            HPVR_LOGI("[hpvr.quest.broom.reward] status=CARD_COLLECTED actor=%d",pickup.actor_reference);
            continue;
        }
        auto& collected=s.frontend.progress.collected_beans;
        collected.insert(std::lower_bound(collected.begin(),collected.end(),pickup.actor_reference),pickup.actor_reference);
        StartPickupFlight(pickup.actor_reference);s.audio.PlayBeanPickup();s.bean_hud_time=4;
    }
}
