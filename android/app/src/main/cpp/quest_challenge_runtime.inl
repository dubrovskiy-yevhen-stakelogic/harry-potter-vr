void QuestScene::Swap(QuestScene& other) noexcept {state_.swap(other.state_);}
bool QuestScene::ConsumeMapTransition(unsigned* map,ProgressSave* progress,unsigned* slot){
    auto& s=*state_;if(!s.travel_pending||!map||!progress||!slot)return false;
    s.travel_pending=false;*map=s.travel_progress.map_id;*progress=s.travel_progress;*slot=s.travel_slot;return true;
}
void QuestScene::AbortMapTransition(const char* message){
    auto& s=*state_;s.travel_pending=false;s.travel_blocked=true;
    s.frontend.progress=s.travel_origin;s.frontend.screen=FrontScreen::Main;s.frontend.selection=0;
    s.frontend.message=message?message:"MAP LOAD FAILED";s.front_anchor_valid=false;
    s.audio.StopDialogue();s.audio.SelectMusic(0);
    HPVR_LOGE("[hpvr.quest.travel] status=FAILED old_map=%u reason=%s saves=UNCHANGED",s.map_id,s.frontend.message.c_str());
}
void QuestScene::RequestChallengeTravel(){
    auto& s=*state_;if(s.map_id!=0||s.travel_pending||s.travel_blocked)return;
    s.travel_origin=s.frontend.progress;auto next=s.frontend.progress;
    next.completed_maps|=1U<<kIntroductionMapId;
    next.map_id=1;next.phase=2;next.page=14;next.quest_stage=0;next.lesson_passes=4;
    next.banked_beans+=static_cast<unsigned>(next.collected_beans.size());next.collected_beans.clear();
    next.activated_events.clear();next.challenge_stars=0;next.graph_state.clear();next.world_state.clear();
    next.player={0,kPlayerEyeHeightMeters,0};next.yaw=0;
    s.travel_progress=std::move(next);s.travel_slot=s.frontend.slot;s.travel_pending=true;
    HPVR_LOGI("[hpvr.quest.travel] status=REQUESTED from=Lev_Tut1 to=Lev_Tut1b");
}
void QuestScene::RebuildChallengeCollision(){
    auto& s=*state_;if(s.map_id==0)return;
    s.collision_triangles.resize(s.challenge.collision_base);
    for(std::size_t i=0;i<std::min(s.doors.size(),s.challenge.mover_triangles.size());++i){
        auto& door=s.doors[i];
        door.collision_first=s.collision_triangles.size();
        door.collision_count=0;
        if(door.grid&&door.grid_physics.retired)continue;
        auto transform=movers::BuildTransform(door.placement,door.motion.pose);
        transform.translation=AddVector(transform.translation,door.grid_offset);
        for(auto t:s.challenge.mover_triangles[i]){
            for(auto& p:t.vertices)p=movers::TransformPoint(transform,p);
            // The broad-phase bounds belong to the transformed triangle, not
            // its closed-door location. Rebuild the normal for pitch/roll too.
            if(!NormalizeVector(CrossVector(SubtractVector(t.vertices[1],t.vertices[0]),
                SubtractVector(t.vertices[2],t.vertices[0])),&t.normal))continue;
            t.minimum=t.maximum=t.vertices[0];
            for(unsigned j=1;j<3;++j)for(unsigned axis=0;axis<3;++axis){
                t.minimum[axis]=std::min(t.minimum[axis],t.vertices[j][axis]);
                t.maximum[axis]=std::max(t.maximum[axis],t.vertices[j][axis]);
            }
            s.collision_triangles.push_back(t);
        }
        door.collision_count=s.collision_triangles.size()-door.collision_first;
    }
    if(s.map_id==3)AppendCharmsCollision(s.charms,s.collision_triangles);
    s.challenge.collision_dirty=false;
}
template<class SceneState>
std::string InitialChallengeWorldSnapshot(const SceneState& state){
    std::ostringstream physical;physical<<"CHALLENGE_WORLD 3 "<<std::setprecision(9)<<state.doors.size()<<' ';
    for(const auto& d:state.doors){
        physical<<d.actor_reference<<' '<<d.phase<<' '<<d.opening<<' '<<d.completion_sent<<' '<<d.hold<<' '<<d.loop_started<<' ';
        for(float v:d.grid_offset)physical<<v<<' ';for(float v:d.grid_target)physical<<v<<' ';
        physical<<std::quoted(movers::SaveMotion(d.motion))<<' ';
    }
    physical<<state.character_draws.size()<<' ';
    for(const auto& actor:state.character_draws){
        physical<<actor.actor_reference<<' '<<actor.enabled<<' '<<actor.yaw<<' ';
        for(float v:actor.cutscene_offset)physical<<v<<' ';
    }
    physical<<state.challenge.barrel_stage<<' '<<state.challenge.barrel_time<<' '<<state.challenge.gnome_hits.size()<<' ';
    for(const auto& [ref,hits]:state.challenge.gnome_hits)physical<<ref<<' '<<hits<<' ';
    physical<<state.challenge.active_scene<<' '<<state.challenge.complete<<' '<<state.challenge.pending_scenes.size();
    for(auto ref:state.challenge.pending_scenes)physical<<' '<<ref;
    physical<<' '<<state.challenge.gnome_active.size();for(auto ref:state.challenge.gnome_active)physical<<' '<<ref;
    return physical.str();
}
void QuestScene::RestoreTransferredProgress(const ProgressSave& progress,unsigned slot){
    state_->house_point_hud.Reset(progress.house_points[campaign::kGryffindor]);
    auto& s=*state_;
    if(progress.map_id!=s.map_id||slot>=3)return;
    s.frontend.progress=progress;s.frontend.slot=slot;
    s.frontend.paused=FrontScreen::Game;s.frontend.BeginGame();
    if(s.map_id==0){RestoreCurrentProgress();return;}
    if(s.map_id==2){RestoreBroomProgress();return;}
    if(s.map_id==3&&!RestoreCharmsState(s.charms,progress.charms_state)){
        s.frontend.screen=FrontScreen::Main;s.frontend.message="INVALID CHARMS CHECKPOINT";return;
    }
    auto& p=s.frontend.progress;
    if(!s.challenge_initial_checkpoint_valid){
        auto initial=p;initial.health=100;initial.quest_stage=0;initial.challenge_stars=0;
        initial.collected_beans.clear();initial.activated_events.clear();initial.charms_state.clear();
        initial.graph_state=s.challenge.graph.Serialize();initial.world_state=InitialChallengeWorldSnapshot(s);
        float floor=0;(void)FindPropGroundBelow(s.collision_triangles,{0,0,0},&floor);
        initial.player={0,floor+kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters,0};initial.yaw=0;
        s.challenge_initial_checkpoint=initial;s.challenge_initial_checkpoint_valid=true;
    }
    // Rebuild the fallback for this slot from immutable fresh-map state. A
    // legacy Continue in another slot must not inherit the previous slot's book.
    auto initial=p;initial.health=100;initial.quest_stage=0;initial.challenge_stars=0;
    initial.collected_beans.clear();initial.activated_events.clear();initial.charms_state.clear();
    initial.graph_state=s.challenge_initial_checkpoint.graph_state;
    initial.world_state=s.challenge_initial_checkpoint.world_state;
    initial.player=s.challenge_initial_checkpoint.player;initial.yaw=0;
    s.challenge_start_checkpoint=std::move(initial);s.challenge_start_checkpoint_valid=true;
    auto graph=s.challenge.graph;
    if(!p.graph_state.empty()&&!graph.Restore(p.graph_state)){
        s.frontend.screen=FrontScreen::Main;s.frontend.message="SAVE EVENT DATA DOES NOT MATCH THIS MAP";return;
    }
    std::int32_t resume_scene=0;
    if(!p.world_state.empty()){
        // Parse into copies so an invalid bank never partially mutates the world.
        auto doors=s.doors;auto actors=s.character_draws;std::map<std::int32_t,unsigned> hits;
        std::vector<std::int32_t> pending;std::set<std::int32_t> active_gnomes;
        std::istringstream in(p.world_state);std::string magic;unsigned version=0,count=0,barrel=0;float barrel_time=0;bool complete=false;
        bool valid=bool(in>>magic>>version>>count)&&magic=="CHALLENGE_WORLD"&&(version>=1&&version<=3)&&count==doors.size();
        const auto finite=[](float v){return std::isfinite(v)&&std::abs(v)<2000;};
        for(auto& d:doors){std::int32_t ref=0;in>>ref>>d.phase>>d.opening>>d.completion_sent>>d.hold;
            if(version>=2)in>>d.loop_started;
            for(auto& v:d.grid_offset)in>>v;for(auto& v:d.grid_target)in>>v;
            if(version>=2){std::string motion;in>>std::quoted(motion);valid=valid&&movers::RestoreMotion(d.motion,motion);}
            else{
                if(!std::isfinite(d.phase)||d.phase<0||d.phase>1){valid=false;continue;}
                d.loop_started=d.looping&&(d.opening||d.phase>0||d.hold>0);
                const float position=d.phase*static_cast<float>(d.motion.count-1);
                const auto key=static_cast<unsigned>(std::clamp(position,0.0F,static_cast<float>(d.motion.count-1)));
                (void)movers::Settle(d.motion,key);
                d.motion.seconds=d.opening?d.open_seconds:d.close_seconds;
                if((d.opening&&d.phase<1)||(!d.opening&&d.phase>0)||d.loop_started){
                    (void)movers::Start(d.motion,d.opening,d.loop_started);
                    d.motion.phase=std::clamp(position-static_cast<float>(key),0.0F,1.0F);
                    if(!d.opening)d.motion.phase=1-d.motion.phase;
                    d.motion.pose=movers::Interpolate(d.motion.source,d.motion.keys[d.motion.target],d.motion.phase);
                }
            }
            valid=valid&&ref==d.actor_reference&&std::isfinite(d.phase)&&d.phase>=0&&d.phase<=1&&finite(d.hold)&&d.hold>=0&&
                std::ranges::all_of(d.grid_offset,finite)&&std::ranges::all_of(d.grid_target,finite);}
        const bool characters_valid=ReadCheckpointCharacters(in,actors,s.map_id==3);
        valid=valid&&characters_valid;
        const unsigned maximum_gnomes=s.map_id==3?4U:3U;
        in>>barrel>>barrel_time>>count;valid=valid&&barrel<=4&&finite(barrel_time)&&barrel_time>=0&&count<=maximum_gnomes;
        if(count>maximum_gnomes)valid=false;
        else for(unsigned i=0;i<count;++i){std::int32_t ref=0;unsigned n=0;in>>ref>>n;
            valid=valid&&n<=3&&!hits.contains(ref)&&std::ranges::any_of(actors,[&](const auto& a){return a.actor_reference==ref&&AsciiFold(a.class_name)=="tut1.tut1gnome";});hits[ref]=n;}
        in>>resume_scene>>complete;valid=valid&&bool(in)&&(!resume_scene||s.challenge.scenes.contains(resume_scene));
        if(version>=2){
            in>>count;valid=valid&&count<=s.challenge.scenes.size();
            if(count<=s.challenge.scenes.size())for(unsigned i=0;i<count;++i){std::int32_t ref=0;in>>ref;
                valid=valid&&s.challenge.scenes.contains(ref)&&ref!=resume_scene&&
                    std::ranges::find(pending,ref)==pending.end();pending.push_back(ref);}
            in>>count;valid=valid&&count<=maximum_gnomes;
            if(count<=maximum_gnomes)for(unsigned i=0;i<count;++i){std::int32_t ref=0;in>>ref;
                valid=valid&&active_gnomes.insert(ref).second&&std::ranges::any_of(actors,[&](const auto& a){
                    return a.actor_reference==ref&&a.enabled&&AsciiFold(a.class_name)=="tut1.tut1gnome";});}
        }
        valid=valid&&bool(in);
        in>>std::ws;valid=valid&&in.eof();
        if(!valid){s.frontend.screen=FrontScreen::Main;s.frontend.message="INVALID MAP CHECKPOINT";return;}
        s.doors=std::move(doors);s.character_draws=std::move(actors);s.challenge.gnome_hits=std::move(hits);
        s.challenge.barrel_stage=barrel;s.challenge.barrel_time=barrel_time;s.challenge.complete=complete;
        s.challenge.pending_scenes=std::move(pending);s.challenge.gnome_active=std::move(active_gnomes);
    }
    s.challenge.graph=std::move(graph);s.intro_cutscene.playing=false;s.challenge.active_scene=0;
    if(s.map_id==1)SetBridgeProfessor(s.character_draws,ChallengeActivated(p,4807)&&resume_scene!=4807);
    s.challenge.checkpoint_pending=false;
    s.challenge.authored_checkpoint_pending=p.world_state.empty();
    if(p.world_state.empty()){
        s.challenge.pending_scenes.clear();s.challenge.complete=false;s.challenge.gnome_active.clear();s.challenge.gnome_hits.clear();
    }
    s.challenge.gnome_motion.clear();s.challenge.grid_hit_positions.clear();
    for(auto& door:s.doors){
        door.grid_physics={};
        door.grid_physics.finish_pending=door.grid&&grid_motion::RestoredCompletionPending(
            door.completion_sent,door.grid_offset,door.grid_target);
        door.grid_target[1]=door.grid_offset[1];
    }
    for(auto& actor:s.character_draws)if(AsciiFold(actor.class_name)=="tut1.tut1gnome"){
        const auto ref=actor.actor_reference;auto& motion=s.challenge.gnome_motion[ref];
        const auto* node=s.challenge.graph.Find(ref);
        const bool completed=(node&&node->signaled)||ChallengeActivated(p,ref);
        const auto hit=s.challenge.gnome_hits.find(ref);
        if(completed||(hit!=s.challenge.gnome_hits.end()&&hit->second>0)){
            // C48 hid defeated gnomes. Recover their authored seated model;
            // a newer save mid-fall still owes its one counter notification.
            gnome::RestoreHit(motion,completed);s.challenge.gnome_hits[ref]=1;
            s.challenge.gnome_active.erase(ref);actor.enabled=true;
        }else if(s.challenge.gnome_active.contains(ref))(void)gnome::Activate(motion);
        const auto pose=gnome::Presentation(motion);
        actor.active_clip=pose.clip;actor.animation_time=pose.clip_seconds;
        actor.animation_loop=pose.loop;actor.collision_disabled=!pose.targetable;
    }
    for(auto& zone:s.challenge.spatial){zone.inside=false;zone.arm_time=1;zone.armed=false;}
    RebuildChallengeCollision();
    for(auto& prop:s.challenge.props){
        const bool activated=ChallengeActivated(p,prop.reference);
        prop.activation_time=activated?prop.animation_duration:-1;
        for(auto& bean:s.beans)if(bean.source_actor==prop.reference){
            if(activated)PrepareChallengeBeanEmission(bean,prop,s.collision_triangles);
            bean.emission_time=activated?1.0F:0.0F;
        }
    }
    if(s.map_id==3)for(auto& bean:s.beans)if(bean.source_actor&&
        std::ranges::none_of(s.challenge.props,[&](const auto& prop){return prop.reference==bean.source_actor;})){
        const bool activated=ChallengeActivated(p,bean.source_actor);
        if(activated)PrepareChallengeBeanEmission(bean,BeanSpawnerSource(bean,s.challenge.props),s.collision_triangles,&p.player);
        bean.emission_time=activated?1.0F:0.0F;
    }
    s.last_player=p.player;s.last_yaw=p.yaw;
    if(p.world_state.empty()){
        float floor=0;if(FindPropGroundBelow(s.collision_triangles,{0,0,0},&floor))s.last_player={0,floor+kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters,0};
    }
    for(std::size_t i=0;i<s.character_draws.size();++i)(void)s.spell_targets.SetSceneOffset(i,s.character_draws[i].cutscene_offset);
    s.placement=p;s.placement.player=s.last_player;s.restore_pending=true;
    s.death_time=-1;s.death_duration=0;s.death_checkpoint_valid=false;
    if(p.health>0&&(p.world_state.empty()||p.world_state.starts_with("CHALLENGE_WORLD 3 "))){
        s.challenge_start_checkpoint=p;
        s.challenge_start_checkpoint.player=s.last_player;
        s.challenge_start_checkpoint.yaw=s.last_yaw;
        if(s.challenge_start_checkpoint.graph_state.empty())s.challenge_start_checkpoint.graph_state=s.challenge.graph.Serialize();
        if(s.challenge_start_checkpoint.world_state.empty())s.challenge_start_checkpoint.world_state=InitialChallengeWorldSnapshot(s);
        s.challenge_start_checkpoint_valid=true;
    }else if(!p.world_state.empty()&&!p.world_state.starts_with("CHALLENGE_WORLD 3 ")){
        // Older versions saved arbitrary frame positions. Their exact historical
        // book snapshot cannot be reconstructed. Keep the old bank unchanged;
        // recover at a touched book with its existing world progress, or at the
        // clean level start if no book was touched. Never loop on the old pose.
        const ChallengeSpatial* book=nullptr;float nearest=std::numeric_limits<float>::max();
        for(const auto& zone:s.challenge.spatial)if(zone.checkpoint&&ChallengeActivated(p,zone.reference)){
            const auto d=SubtractVector(zone.position,p.player);const float distance=DotVector(d,d);
            if(distance<nearest){nearest=distance;book=&zone;}
        }
        if(book){
            auto checkpoint=p;float floor=0;
            if(FindPropGroundBelow(s.collision_triangles,AddVector(book->position,{0,.5F,0}),&floor)){
                checkpoint.player={book->position[0],floor+kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters,book->position[2]};
                checkpoint.health=100;
                // Only the marker changes: physical fields are identical in v2/v3.
                if(checkpoint.world_state.starts_with("CHALLENGE_WORLD 2 "))checkpoint.world_state[16]='3';
                s.challenge_start_checkpoint=std::move(checkpoint);
                HPVR_LOGI("[hpvr.quest.challenge.checkpoint] status=LEGACY_BOOK_RECOVERY ref=%d old_save=UNCHANGED",book->reference);
            }
        }
    }
    s.jump={};s.climb={};s.projectile={};s.basic_cast={};s.first_step.Reset();s.exit_return={};s.pickup_flights.clear();
    s.audio.SelectMusic(s.frontend.assets.level_music_index);s.audio.SetPresentationAudio(true,false);
    if(s.challenge.barrel_time>0)for(auto& a:s.character_draws)
        if(AsciiFold(a.class_name)=="tut1.flipbarrel"&&a.clips.contains("roll"))a.active_clip="roll";
    if(s.map_id==1&&s.challenge.complete&&!resume_scene)RequestBroomTravel();
    else if(resume_scene)StartChallengeScene(resume_scene);
    else if(p.quest_stage==0){s.frontend.screen=FrontScreen::Objective;s.frontend.selection=0;}
    // Commit only after XR has adopted the scene, not from the background loader.
}
void QuestScene::BeginChallengeDeath(const char* reason) const {
    auto& s=*state_;
    if(!IsWalkingChallenge(s.map_id)||s.death_time>=0)return;
    SceneLoadTrace::Event(s.frontend.saves,s.map_id,std::string("DEATH reason=")+reason+
        " player="+std::to_string(s.last_player[0])+","+std::to_string(s.last_player[1])+","+std::to_string(s.last_player[2]));
    ProgressSave checkpoint;
    auto checked_graph=s.challenge.graph;
    const bool memory=s.challenge_start_checkpoint_valid&&s.challenge_start_checkpoint.health>0&&
        !s.challenge_start_checkpoint.world_state.empty()&&checked_graph.Restore(s.challenge_start_checkpoint.graph_state);
    const bool disk=!memory&&ReadProgress(s.frontend.saves,s.frontend.slot,&checkpoint)&&checkpoint.map_id==s.map_id&&checkpoint.health>0&&
        checkpoint.world_state.starts_with("CHALLENGE_WORLD 3 ")&&
        !checkpoint.graph_state.empty()&&!checkpoint.world_state.empty()&&checked_graph.Restore(checkpoint.graph_state);
    if(!disk){
        if(!memory){
            s.audio.StopDialogue();s.frontend.screen=FrontScreen::Main;
            s.frontend.message="NO VALID LEVEL CHECKPOINT";s.front_anchor_valid=false;return;
        }
        checkpoint=s.challenge_start_checkpoint;
    }
    s.death_checkpoint=std::move(checkpoint);s.death_checkpoint_valid=true;
    s.death_time=0;s.death_duration=.5F;
    s.frontend.progress.health=0;s.health_flash_time=1;
    s.challenge.checkpoint_pending=false;s.intro_cutscene.playing=false;
    s.challenge.authored_checkpoint_pending=false;
    s.challenge.active_scene=0;s.audio.StopDialogue();
    s.jump={};s.climb={};s.jump_pending=false;s.projectile={};s.basic_cast={};
    for(auto& actor:s.character_draws)if(actor.player){
        auto feet=s.last_player;feet[1]-=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;
        actor.cutscene_offset=SubtractVector(feet,actor.base_origin);
        actor.enabled=true;actor.animation_time=0;
        if(const auto clip=actor.clips.find("faint");clip!=actor.clips.end()){
            actor.active_clip="faint";s.death_duration=death::Duration(clip->second.duration);
        }else actor.active_clip="breathe";
    }
    HPVR_LOGI("[hpvr.quest.challenge.death] status=FAINT reason=%s duration=%.3f recovery=%s",reason?reason:"UNKNOWN",s.death_duration,disk?"SAVED_SLOT":"SESSION_CHECKPOINT");
}
void QuestScene::AdvanceChallengeDeath(float seconds){
    auto& s=*state_;if(s.death_time<0)return;
    s.death_time=death::Advance(s.death_time,seconds,s.death_duration);
    s.health_flash_time=std::max(0.0F,s.health_flash_time-seconds);
    for(auto& actor:s.character_draws)if(actor.player&&actor.active_clip=="faint")
        actor.animation_time=death::FaintTime(s.death_time,s.death_duration);
    if(s.death_time<s.death_duration)return;
    if(!s.death_checkpoint_valid)return;
    const auto checkpoint=s.death_checkpoint;const auto slot=s.frontend.slot;
    const auto fallback=s.challenge_start_checkpoint;const bool fallback_valid=s.challenge_start_checkpoint_valid;
    RestoreTransferredProgress(checkpoint,slot);
    // ReadProgress verifies bank integrity; the map-local restore also checks
    // every actor/mover reference. An incompatible physical bank must not
    // strand the death sequence or mutate the fallback into the failed save.
    if(s.death_time>=0&&fallback_valid)RestoreTransferredProgress(fallback,slot);
    if(s.death_time<0)HPVR_LOGI("[hpvr.quest.challenge.death] status=CHECKPOINT_RESTORED slot=%u health=%u",slot+1,s.frontend.progress.health);
    else HPVR_LOGE("[hpvr.quest.challenge.death] status=CHECKPOINT_REJECTED slot=%u saves=UNCHANGED",slot+1);
}
void QuestScene::StartChallengeScene(std::int32_t reference){
    auto& s=*state_;const auto found=s.challenge.scenes.find(reference);
    if(found==s.challenge.scenes.end()||s.intro_cutscene.playing)return;
    s.challenge.active_scene=reference;s.intro_cutscene=found->second;s.intro_cutscene.playing=true;
    SceneLoadTrace::Event(s.frontend.saves,s.map_id,"CUTSCENE_START ref="+std::to_string(reference));
    RememberChallengeEvent(s.frontend.progress,reference);s.challenge.checkpoint_pending=true;
    auto& scene=s.intro_cutscene;
    // A saved cutscene restarts its own authored marks rather than inheriting a
    // camera or a character's pose from another room.
    for(auto& track:scene.tracks){
        for(auto& a:s.character_draws)if(a.actor_reference==track.actor_reference){
            a.enabled=true;track.position=AddVector(a.base_origin,a.cutscene_offset);
            if(a.player){s.harry_actor=a.actor_reference;track.position=s.last_player;track.position[1]-=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;}
            a.cutscene_offset=SubtractVector(track.position,a.base_origin);a.active_clip="breathe";a.animation_time=0;a.animation_loop=true;
        }
        // Apply only initial placement/facing before the first submitted frame.
        for(const auto& line:track.commands){
            const auto split=line.find(' ');const auto op=AsciiFold(line.substr(0,split));
            if(op=="sleep"||op=="say"||op=="talk"||op=="waitfor"||op=="moveto")break;
            if(split==std::string::npos)continue;
            const auto arg=AsciiFold(line.substr(split+1));
            if(track.camera&&(op=="preface"||op=="turnto"||op=="face"))scene.camera_target=arg;
            if(op=="teleport"||op=="goto")for(const auto& loc:scene.locations)if(AsciiFold(loc.alias)==arg){
                track.position=loc.position;
                if(track.camera){scene.camera_position=loc.position;scene.camera_position_valid=true;}
                else for(auto& a:s.character_draws)if(a.actor_reference==track.actor_reference)
                    a.cutscene_offset=SubtractVector(track.position,a.base_origin);
            }
        }
    }
    if(s.map_id==3){
        const float wait=OpenCharmsCutsceneDoors(reference,s.doors);
        for(auto& track:scene.tracks)track.delay_seconds=std::max(track.delay_seconds,wait);
    }
    if(s.map_id==1&&reference==4807)SetBridgeProfessor(s.character_draws,false);
    GroundCutsceneCast(scene,s.character_draws,s.collision_triangles);
    if(s.map_id==3&&reference==1526){
        for(auto& track:scene.tracks)for(auto& actor:s.character_draws)if(actor.player&&actor.actor_reference==track.actor_reference)
            for(const auto& location:scene.locations)if(AsciiFold(location.alias)=="locname1"){
                const auto direction=SubtractVector(location.position,track.position);
                if(std::hypot(direction[0],direction[2])>.01F)actor.yaw=actor.desired_yaw=std::atan2(direction[0],direction[2]);
            }
    }
    if(s.map_id==1)(void)award::RestoreProfessorFacing(reference,scene.locations,s.character_draws);
    if(s.map_id==3&&s.frontend.vr.first_person_cutscenes)FaceCharmsCinematicTarget(scene,s.character_draws);
    s.audio.StopDialogue();s.front_anchor_valid=false;
    HPVR_LOGI("[hpvr.quest.challenge.scene] status=STARTED ref=%d name=%s",reference,scene.object_name.c_str());
}
void QuestScene::AdvanceChallenge(float seconds){
    auto& s=*state_;auto& c=s.challenge;auto& p=s.frontend.progress;
    if(!std::isfinite(seconds)||seconds<=0||!c.graph.healthy())return;
    const float step=std::clamp(seconds,0.0F,.05F);
    if(s.map_id==3)UpdateCharmsPickupAttachments(s.charms,s.doors,s.beans);
    if(s.death_time>=0){AdvanceChallengeDeath(step);return;}
    if(p.health==0){BeginChallengeDeath("HEALTH_DEPLETED");return;}
    for(auto& flight:s.pickup_flights)flight.elapsed+=step;
    std::erase_if(s.pickup_flights,[](const auto& flight){return flight.elapsed>=flight.duration;});
    s.bean_time=std::fmod(s.bean_time+step,1000.0F);s.bean_hud_time=std::max(0.0F,s.bean_hud_time-step);
    s.star_hud_time=std::max(0.0F,s.star_hud_time-step);
    s.card_pickup.Advance(step);
    for(auto& prop:c.props)if(prop.activation_time>=0){
        const bool opening=prop.activation_time<prop.animation_duration;
        prop.activation_time=std::min(prop.animation_duration,prop.activation_time+step);
        if(opening&&prop.activation_time>=prop.animation_duration&&chest::IsChest(prop.name))
            if(const auto sound=GameplayDialogueIndex(s.frontend.assets,"chest_landing"))
                (void)s.audio.PlayWorldEffect(*sound,.85F);
    }
    for(auto& bean:s.beans)if(bean.source_actor&&ChallengeRewardsReady(c.props,bean.source_actor,p))
        bean.emission_time=std::min(1.0F,bean.emission_time+step);
    c.hurt_time=std::max(0.0F,c.hurt_time-step);s.health_flash_time=std::max(0.0F,s.health_flash_time-step);
    const bool playing=s.intro_cutscene.playing;
    const bool controlled=IsCutscenePlaying();
    AdvanceIntroCutscene(step);
    if(s.map_id==3&&s.frontend.vr.first_person_cutscenes&&IsCutscenePlaying())
        FaceCharmsCinematicTarget(s.intro_cutscene,s.character_draws);
    for(auto& a:s.character_draws){a.animation_time+=step;a.yaw+=std::clamp(std::remainder(a.desired_yaw-a.yaw,kTau),-8*step,8*step);}
    if(controlled&&!IsCutscenePlaying()){
        SceneLoadTrace::Event(s.frontend.saves,s.map_id,"CUTSCENE_RELEASE ref="+std::to_string(c.active_scene));
        // Restore player control once, not again when a background exit track
        // eventually ends after the player has already walked into another room.
        c.active_scene=0;p.quest_stage=std::min(63U,std::max(1U,p.quest_stage+1));
        for(const auto& a:s.character_draws)if(a.actor_reference==s.harry_actor){
            s.last_player=AddVector(a.base_origin,a.cutscene_offset);s.last_player[1]+=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;
        }
        if(s.exit_return.valid){s.last_player=AddVector(s.last_player,s.exit_return.offset);s.last_yaw=s.exit_return.yaw;}
        s.exit_return.valid=false;c.checkpoint_pending=true;s.placement=p;
        s.placement.player=s.last_player;s.placement.yaw=s.last_yaw;s.restore_pending=true;
    }
    if(playing&&!s.intro_cutscene.playing){c.active_scene=0;c.checkpoint_pending=true;}
    auto body=s.last_player;body[1]-=kPlayerEyeHeightMeters;
    // Collision and platform support must agree on the same physical feet.
    // The presentation head changes height when the user crouches or stands.
    if(s.player_capsule_valid&&!s.restore_pending)body=s.player_capsule;
    // Scripted actors still touch class-proximity triggers during cinematics.
    // Player triggers remain gated below until control is returned.
    for(auto& zone:c.spatial){
        if(s.map_id==3&&zone.event=="aloroom2"&&ChallengeActivated(p,zone.reference))continue;
        if(zone.proximity_class.empty()||zone.proximity_class=="harry"||zone.proximity_class=="wingardiumblock")continue;
        bool inside=false;
        for(const auto& actor:s.character_draws){
            if(!actor.enabled)continue;
            const auto cls=AsciiFold(actor.class_name);
            if(cls!=zone.proximity_class&&!cls.ends_with("."+zone.proximity_class))continue;
            const auto d=SubtractVector(AddVector(actor.base_origin,actor.cutscene_offset),zone.position);
            if(std::hypot(d[0],d[2])<=zone.radius&&std::abs(d[1])<=zone.height+kPlayerCapsuleHalfHeightMeters){inside=true;break;}
        }
        if(inside&&!zone.inside)(void)c.graph.Touch(zone.reference);
        zone.inside=inside;
    }
    if(!IsCutscenePlaying()&&!(s.map_id==3&&s.charms.active_lesson>=0)){
        const auto world=AddVector(RotateYaw(body,-c.source_yaw),c.source_origin);
        const std::array<float,3> unreal{-world[2]/kMetersPerUnrealUnit,world[0]/kMetersPerUnrealUnit,world[1]/kMetersPerUnrealUnit};
        if(c.zones.IsLethal(unreal)){
            BeginChallengeDeath("AUTHORED_BSP_KILLZONE");return;
        }
        for(auto& zone:c.spatial){
            if(!zone.proximity_class.empty()&&zone.proximity_class!="harry")continue;
            if(s.map_id==3&&(zone.event=="openclosedoor"||zone.event=="aloroom2")&&ChallengeActivated(p,zone.reference))continue;
            auto position=zone.position;
            if(zone.checkpoint)position[1]+=.10F+.03F*std::sin(8*s.bean_time);
            const auto d=SubtractVector(body,position);
            const bool inside=std::hypot(d[0],d[2])<=zone.radius&&std::abs(d[1])<=zone.height+kPlayerCapsuleHalfHeightMeters;
            if(zone.checkpoint){
                zone.arm_time=std::max(0.0F,zone.arm_time-step);
                if(zone.arm_time==0&&std::hypot(d[0],d[2])>2)zone.armed=true;
                if(inside&&!zone.inside&&zone.armed&&!ChallengeActivated(p,zone.reference)){
                    RememberChallengeEvent(p,zone.reference);(void)c.graph.Touch(zone.reference);c.authored_checkpoint_pending=true;
                    if(const auto sound=GameplayDialogueIndex(s.frontend.assets,"save_game"))(void)s.audio.PlayWorldEffect(*sound,.85F);
                    HPVR_LOGI("[hpvr.quest.challenge.savebook] ref=%d status=TOUCHED",zone.reference);
                }
                zone.inside=inside;continue;
            }
            if(inside&&!zone.inside&&!zone.spell)(void)c.graph.Touch(zone.reference);
            zone.inside=inside;
        }
        for(const auto& pickup:s.beans){
            if(pickup.source_actor&&(!ChallengeRewardsReady(c.props,pickup.source_actor,p)||pickup.emission_time<.25F))continue;
            if(ChallengeCollected(p,pickup.actor_reference)||!CanCollectBean(s.collision_triangles,body,BeanWorldPosition(pickup)))continue;
            if(pickup.kind==4){
                if(TakeBroomCard(p,s.card_pickup,pickup,s.bean_time,s.audio.DialogueDurationSeconds(s.card_sound))){
                    p.earned_cards|=campaign::CardMask(chest::CardId(pickup.actor_reference));
                    (void)s.audio.PlayWorldEffect(s.card_sound,.9F);s.bean_hud_time=4;
                    c.checkpoint_pending=true;
                }
                continue;
            }
            p.collected_beans.insert(std::lower_bound(p.collected_beans.begin(),p.collected_beans.end(),pickup.actor_reference),pickup.actor_reference);
            if(pickup.kind==1){
                p.health=std::min(100U,p.health+20);(void)s.audio.PlayWorldEffect(s.frog_sound,.8F);
                c.checkpoint_pending=true;continue;
            }
            if(pickup.kind==3){(void)c.graph.CollectStar(pickup.actor_reference);p.challenge_stars=c.graph.star_count();s.star_hud_time=4.0F;}
            StartPickupFlight(pickup.actor_reference);
            s.bean_hud_time=4;
            if(pickup.kind==3)(void)s.audio.PlayWorldEffect(s.star_sound,.85F);
            else s.audio.PlayBeanPickup();
            c.checkpoint_pending=true;
        }
    }
    (void)c.graph.Advance(step);
    const auto reduce_effects=[&](){for(unsigned pass=0;pass<16&&c.graph.healthy();++pass){
        const auto effects=c.graph.DrainEffects();if(effects.empty())break;
        for(const auto& effect:effects){
            const auto ref=effect.actor_reference;
            switch(effect.kind){
            case MapEventKind::mover_trigger:
                for(auto& d:s.doors)if(d.actor_reference==ref){
                    if(d.cutscene_hold)break;
                    if(d.grid){
                        const auto push_origin=grid_push::ConsumeOrigin(c.grid_hit_positions,ref,s.last_player);
                        const auto remaining=SubtractVector(d.grid_target,d.grid_offset);
                        if(d.grid_physics.retired||DotVector(remaining,remaining)>.0001F||
                            (d.grid_physics.initialized&&!d.grid_physics.grounded))break;
                        const auto object=movers::ToUnreal(movers::Yaw(AddVector(d.pivot,d.grid_offset),-d.placement.player_yaw));
                        const auto pusher=movers::ToUnreal(movers::Yaw(push_origin,-d.placement.player_yaw));
                        const auto offset=movers::Yaw(movers::FromUnreal(movers::GridStepUnreal(object,pusher,d.grid_increment)),d.placement.player_yaw);
                        d.grid_target=AddVector(d.grid_offset,offset);d.completion_sent=false;d.grid_physics.finish_pending=true;
                        c.checkpoint_pending=true;break;
                    }
                    d.opening=d.initial_state.find("toggle")!=std::string::npos?!d.opening:effect.enabled;
                    d.motion.seconds=d.opening?d.open_seconds:d.close_seconds;
                    if(d.looping)d.loop_started=effect.enabled;
                    const bool started=movers::Start(d.motion,d.opening,d.looping&&d.loop_started);
                    if(started){d.completion_sent=false;d.hold=0;c.collision_dirty=true;c.checkpoint_pending=true;}
                    else if(d.looping&&d.loop_started&&d.motion.moving)d.completion_sent=false;
                    else if(!d.motion.moving&&((d.opening&&d.motion.current+1==d.motion.count)||(!d.opening&&d.motion.current==0))){
                        // A retrigger at the requested endpoint has no travel to
                        // wait for. Complete it now instead of stranding a counter.
                        d.completion_sent=true;(void)c.graph.Signal(d.actor_reference);c.checkpoint_pending=true;
                    }
                }
                break;
            case MapEventKind::cutscene_start:
                if(!ChallengeActivated(p,ref)&&std::ranges::find(c.pending_scenes,ref)==c.pending_scenes.end())c.pending_scenes.push_back(ref);
                break;
            case MapEventKind::music:
                for(const auto& music:s.frontend.assets.music_cues)if(music.actor_reference==ref)
                    s.audio.SelectMusic(effect.enabled?music.music_index:-1);
                break;
            case MapEventKind::sound:{
                const auto split=effect.asset_path.find_last_of('.');const auto name=effect.asset_path.substr(split==std::string::npos?0:split+1);
                if(const auto i=GameplayDialogueIndex(s.frontend.assets,name))(void)s.audio.PlayWorldEffect(*i,.75F);
                break;}
            case MapEventKind::checkpoint:
                c.authored_checkpoint_pending=true;HPVR_LOGI("[hpvr.quest.challenge.checkpoint] ref=%d",ref);break;
            case MapEventKind::actor_spell:
                for(auto& a:s.character_draws)if(a.actor_reference==ref){
                    const auto cls=AsciiFold(a.class_name);
                    if(cls=="tut1.flipbarrel"&&c.barrel_time==0&&c.barrel_stage<4){c.barrel_time=.001F;a.active_clip=a.clips.contains("roll")?"roll":"breathe";c.checkpoint_pending=true;}
                    if(cls=="tut1.tut1gnome"&&a.enabled&&gnome::Hit(c.gnome_motion[ref])){
                        c.gnome_hits[ref]=1;c.gnome_active.erase(ref);a.collision_disabled=true;
                        a.active_clip="knockback";a.animation_time=0;a.animation_loop=false;
                        c.checkpoint_pending=true;
                        HPVR_LOGI("[hpvr.quest.challenge.gnome] ref=%d status=KNOCKBACK visible=1 completion=PENDING",ref);
                    }
                }
                for(auto& prop:c.props)if(prop.reference==ref&&prop.spell_target&&!ChallengeActivated(p,ref)){
                    RememberChallengeEvent(p,ref);(void)c.graph.Signal(ref);c.checkpoint_pending=true;
                    prop.activation_time=0;
                    constexpr std::array<const char*,4> chest_sounds{
                        "METAL_CHEST_OPEN_2","METAL_CHEST_OPEN_4","WOOD_CHEST_OPEN_1","WOOD_CHEST_OPEN_2"};
                    const auto effect_name=chest::IsChest(prop.name)?chest_sounds[static_cast<unsigned>(ref)%chest_sounds.size()]:
                        (prop.cauldron?"cauldron_flip":"vase_breaking");
                    if(const auto sound=GameplayDialogueIndex(s.frontend.assets,effect_name))
                        (void)s.audio.PlayWorldEffect(*sound,.85F);
                    for(auto& bean:s.beans)if(bean.source_actor==ref){
                        PrepareChallengeBeanEmission(bean,prop,s.collision_triangles,&body);bean.emission_time=0;
                    }
                }
                break;
            case MapEventKind::actor_trigger:
                if(s.map_id==3&&effect.enabled)for(const auto& prop:c.props)
                    if(prop.reference==ref&&prop.name=="hprops.padlock"){
                        RememberChallengeEvent(p,ref);c.checkpoint_pending=true;
                    }
                if(s.map_id==3)StartCharmsLesson(ref);
                if(s.map_id==3&&effect.enabled&&!ChallengeActivated(p,ref))
                    if(std::ranges::any_of(s.beans,[&](const auto& bean){return bean.source_actor==ref;})){
                        RememberChallengeEvent(p,ref);c.checkpoint_pending=true;
                        for(auto& bean:s.beans)if(bean.source_actor==ref){
                            PrepareChallengeBeanEmission(bean,BeanSpawnerSource(bean,c.props),s.collision_triangles,&body);
                            bean.emission_time=0;
                        }
                    }
                for(auto& a:s.character_draws)if(a.actor_reference==ref&&!ChallengeActivated(p,ref)){
                    if(AsciiFold(a.class_name)=="tut1.tut1gnome"){
                        auto& motion=c.gnome_motion[ref];
                        if(effect.enabled&&gnome::Targetable(motion)){
                            a.enabled=true;c.gnome_active.insert(ref);(void)gnome::Activate(motion);
                        }else c.gnome_active.erase(ref);
                    }else a.enabled=effect.enabled;
                    c.checkpoint_pending=true;
                }
                break;
            case MapEventKind::star_collected:break;
            }
        }
    }};
    reduce_effects();
    if(!c.pending_scenes.empty()&&s.intro_cutscene.playing&&s.intro_cutscene.control_released){
        // Nick's exit keeps a sleeping background track after releasing Harry.
        // Do not make the next room wait for that decorative tail to finish.
        for(const auto& track:s.intro_cutscene.tracks)for(auto& actor:s.character_draws)
            if(actor.actor_reference==track.actor_reference&&AsciiFold(actor.class_name)=="harrypotter.nhnick")
                actor.enabled=false;
        s.intro_cutscene.playing=false;c.active_scene=0;c.checkpoint_pending=true;
        s.audio.StopDialogue();
    }
    if(!s.intro_cutscene.playing&&!c.pending_scenes.empty()){
        const auto ref=c.pending_scenes.front();c.pending_scenes.erase(c.pending_scenes.begin());StartChallengeScene(ref);
    }
    bool moved=false;
    for(std::size_t i=0;i<std::min(s.doors.size(),c.mover_triangles.size());++i){
        auto& d=s.doors[i];const auto old_pose=d.motion.pose;const auto old_grid=d.grid_offset;
        const bool released_cutscene_hold=d.cutscene_hold&&(!s.intro_cutscene.playing||d.tag!=CharmsCutsceneDoorTag(c.active_scene));
        if(released_cutscene_hold)d.cutscene_hold=false;
        auto old_transform=movers::BuildTransform(d.placement,old_pose);
        old_transform.translation=AddVector(old_transform.translation,old_grid);
        bool supported=false;
        if(!s.jump.active&&!s.climb.active&&!IsCutscenePlaying()){
            const float foot=body[1]-kPlayerCapsuleHalfHeightMeters;
            for(const auto& t:c.mover_triangles[i]){
                auto world=t;
                for(unsigned j=0;j<3;++j)world.vertices[j]=movers::TransformPoint(old_transform,t.vertices[j]);
                if(!NormalizeVector(CrossVector(SubtractVector(world.vertices[1],world.vertices[0]),
                    SubtractVector(world.vertices[2],world.vertices[0])),&world.normal))continue;
                float floor=0;
                if(CollisionTriangleSupportHeightAtXZ(world,body[0],body[2],&floor)&&std::abs(floor-foot)<.06F){supported=true;break;}
            }
        }
        movers::StepResult arrival;
        if(d.hold>0){
            d.hold=std::max(0.0F,d.hold-step);
            if(d.hold==0){d.motion.seconds=d.close_seconds;(void)movers::Start(d.motion,false);}
        }else if(!d.grid&&(!d.looping||d.loop_started))arrival=movers::Advance(d.motion,step);
        if(d.motion.count>1){
            const float key=static_cast<float>(d.motion.current)+
                (static_cast<float>(d.motion.target)-static_cast<float>(d.motion.current))*d.motion.phase;
            d.phase=std::clamp(key/static_cast<float>(d.motion.count-1),0.0F,1.0F);
        }
        if(d.grid&&!d.grid_physics.retired&&!c.mover_triangles[i].empty()){
            const std::array<float,3> delta{d.grid_target[0]-d.grid_offset[0],0,d.grid_target[2]-d.grid_offset[2]};
            const float distance=std::hypot(delta[0],delta[2]);
            if(!d.grid_physics.initialized||!d.grid_physics.grounded||d.grid_physics.dynamic_support||distance>.00001F){
                const float frame_yaw=s.map_id==3?d.placement.player_yaw:0;
                auto minimum=grid_motion::Yaw(movers::TransformPoint(old_transform,c.mover_triangles[i][0].vertices[0]),-frame_yaw);auto maximum=minimum;
                for(const auto& triangle:c.mover_triangles[i])for(const auto& vertex:triangle.vertices){
                    const auto point=grid_motion::Yaw(movers::TransformPoint(old_transform,vertex),-frame_yaw);
                    for(unsigned axis=0;axis<3;++axis){minimum[axis]=std::min(minimum[axis],point[axis]);maximum[axis]=std::max(maximum[axis],point[axis]);}
                }
                // One millimetre covers BSP/mesh rounding without lifting the
                // lower trim out of its authored recessed track.
                if(s.map_id==3)for(const unsigned axis:{0U,2U}){minimum[axis]+=.001F;maximum[axis]-=.001F;}
                const auto horizontal=grid_motion::Yaw(distance>.00001F?ScaleVector(delta,std::min(1.0F,step*2.5F/distance)):std::array<float,3>{},-frame_yaw);
                const auto physics=grid_motion::Advance(d.grid_physics,{minimum,maximum},horizontal,step,
                    s.collision_triangles,c.collision_base,d.collision_first,d.collision_count,0,frame_yaw);
                d.grid_offset=AddVector(d.grid_offset,grid_motion::Yaw(physics.offset,frame_yaw));
                if(physics.blocked){d.grid_target=d.grid_offset;c.checkpoint_pending=true;}
                d.grid_target[1]=d.grid_offset[1];
                if(physics.left_support||physics.landed)c.checkpoint_pending=true;
                if(d.grid_physics.retired){d.grid_target=d.grid_offset;c.collision_dirty=true;c.checkpoint_pending=true;}
            }
        }
        const bool grid_arrived=d.grid&&!d.completion_sent&&d.grid_physics.finish_pending&&d.grid_physics.grounded&&
            std::hypot(d.grid_target[0]-d.grid_offset[0],d.grid_target[2]-d.grid_offset[2])<.001F;
        if(grid_arrived)d.grid_physics.finish_pending=false;
        if((arrival.finished||grid_arrived||(d.looping&&arrival.arrivals))&&!d.completion_sent){
            d.completion_sent=true;(void)c.graph.Signal(d.actor_reference);c.checkpoint_pending=true;
        }
        if((arrival.finished||released_cutscene_hold)&&!d.cutscene_hold&&d.opening&&d.initial_state.find("opentimed")!=std::string::npos){
            d.opening=false;d.hold=d.stay_open;
            if(d.hold==0){d.motion.seconds=d.close_seconds;(void)movers::Start(d.motion,false);}
        }
        const bool changed=d.motion.pose.offset_unreal!=old_pose.offset_unreal||
            d.motion.pose.rotation_units!=old_pose.rotation_units||d.grid_offset!=old_grid;moved|=changed;
        if(supported&&changed){
            // Keep the same point on a platform through translation AND rotation.
            // Undo the previous pose, then apply the next pose to the foot point.
            auto foot=body;foot[1]-=kPlayerCapsuleHalfHeightMeters;
            auto old_local=movers::ToUnreal(movers::Yaw(SubtractVector(SubtractVector(foot,old_grid),d.placement.pivot_scene),-d.placement.player_yaw));
            for(unsigned axis=0;axis<3;++axis)old_local[axis]-=old_pose.offset_unreal[axis]*d.placement.meters_per_unit;
            const auto base_local=movers::InverseApply(movers::Rotation(movers::Add(d.placement.base_rotation_units,old_pose.rotation_units)),old_local);
            const auto base_point=AddVector(d.placement.pivot_scene,movers::Yaw(movers::FromUnreal(
                movers::Apply(movers::Rotation(d.placement.base_rotation_units),base_local)),d.placement.player_yaw));
            const auto move=SubtractVector(MoverPoint(d,base_point),foot);
            s.last_player=AddVector(s.last_player,move);
            s.player_capsule=AddVector(s.player_capsule,move);
            s.platform_transport=AddVector(s.platform_transport,move);
            body=AddVector(body,move);
        }
    }
    if(moved||c.collision_dirty)RebuildChallengeCollision();
    if(s.map_id==3)UpdateCharmsPickupAttachments(s.charms,s.doors,s.beans);
    for(auto& a:s.character_draws){
        const auto cls=AsciiFold(a.class_name);auto feet=AddVector(a.base_origin,a.cutscene_offset);
        if(cls=="tut1.flipbarrel"&&c.barrel_time>0&&c.barrel_stage<4){
            auto target=c.barrel_route[c.barrel_stage];float floor=feet[1];
            if(FindPropGroundBelow(s.collision_triangles,AddVector(target,{0,1,0}),&floor))target[1]=floor;
            const auto delta=SubtractVector(target,feet);const float distance=std::sqrt(DotVector(delta,delta));
            if(distance<.08F){a.cutscene_offset=SubtractVector(target,a.base_origin);++c.barrel_stage;
                if(c.barrel_stage%2==0){c.barrel_time=0;a.active_clip="breathe";c.checkpoint_pending=true;}}
            else {const auto move=ScaleVector(delta,std::min(1.0F,step*4.0F/distance));a.cutscene_offset=AddVector(a.cutscene_offset,move);a.desired_yaw=std::atan2(delta[0],delta[2]);}
        }
        if(cls=="tut1.tut1gnome"&&a.enabled&&!IsCutscenePlaying()){
            const auto ref=a.actor_reference;auto& motion=c.gnome_motion[ref];
            auto d=SubtractVector(body,feet);d[1]=0;const float distance=std::hypot(d[0],d[2]);
            const auto duration=[&](const char* name){const auto clip=a.clips.find(name);return clip==a.clips.end()?1.0F:clip->second.duration;};
            const gnome::Timings timings{duration("runattack"),duration("knockback"),duration("downdizzy")};
            auto origin=feet;origin[1]+=gnome::kCollisionHalfHeightMeters;
            const bool active=c.gnome_active.contains(ref)&&gnome::Targetable(motion);
            const bool visible=active&&gnome::CanSee(s.collision_triangles,origin,body);
            const bool waiting=gnome::Presentation(motion).chasing&&!visible;
            const auto pose=gnome::Advance(motion,waiting?0:step,timings,distance<gnome::kDizzyProximityMeters);
            a.active_clip=pose.clip;a.animation_time=pose.clip_seconds;a.animation_loop=pose.loop;
            a.collision_disabled=!pose.targetable;
            if(pose.completed){
                (void)c.graph.Signal(ref);RememberChallengeEvent(p,ref);c.checkpoint_pending=true;
                HPVR_LOGI("[hpvr.quest.challenge.gnome] ref=%d status=STUNNED visible=1 completion=SENT",ref);
            }
            if(pose.chasing&&active){
                if(visible){
                    a.desired_yaw=std::atan2(d[0],d[2]);
                    if(distance>gnome::kCollisionRadiusMeters+kPlayerCapsuleRadiusMeters){
                        const auto requested=ScaleVector(d,std::min(1.0F,step*gnome::kRunSpeedMetersPerSecond/distance));
                        const auto next=MoveChallengeGnome(s.collision_triangles,feet,requested);
                        a.cutscene_offset=SubtractVector(next,a.base_origin);
                        if(std::hypot(next[0]-feet[0],next[2]-feet[2])<.0001F){a.active_clip="breathe";a.animation_loop=true;}
                    }else if(c.hurt_time==0){
                        p.health=p.health>gnome::kContactDamage?p.health-gnome::kContactDamage:0;
                        c.hurt_time=1.2F;s.health_flash_time=1;c.checkpoint_pending=true;
                        if(p.health==0){BeginChallengeDeath("GNOME_CONTACT");return;}
                    }
                }else {a.active_clip="breathe";a.animation_loop=true;}
            }
        }
    }
    for(std::size_t i=0;i<s.character_draws.size();++i)(void)s.spell_targets.SetSceneOffset(i,s.character_draws[i].cutscene_offset);
    if(s.projectile.flying){
        for(const auto& actor:s.character_draws)if(actor.actor_reference==c.impact_actor&&AsciiFold(actor.class_name)=="tut1.tut1gnome"){
            if(!actor.enabled||actor.collision_disabled){c.impact_actor=0;s.projectile.flying=false;break;}
            const auto current=AddVector(s.projectile.origin,ScaleVector(s.projectile.direction,s.projectile.distance_m));
            auto destination=AddVector(actor.collision_center,actor.cutscene_offset);
            destination[1]=(actor.collision_min_y+actor.collision_max_y)*.5F+actor.cutscene_offset[1];
            s.projectile.terminal_distance_m=RetargetProjectile(s.projectile.origin,s.projectile.direction,
                s.projectile.distance_m,destination);
            const float length=s.projectile.terminal_distance_m-s.projectile.distance_m;
            const float obstruction=BasicRayDistance(s.collision_triangles,current,s.projectile.direction);
            if(obstruction+.025F<length){s.projectile.terminal_distance_m=s.projectile.distance_m+obstruction;c.impact_actor=0;}
            break;
        }
    }
    if(s.projectile.flying){
        const auto current=AddVector(s.projectile.origin,ScaleVector(s.projectile.direction,s.projectile.distance_m));
        const float obstruction=BasicRayDistance(s.collision_triangles,current,s.projectile.direction);
        if(obstruction+.025F<s.projectile.terminal_distance_m-s.projectile.distance_m){
            s.projectile.terminal_distance_m=s.projectile.distance_m+obstruction;c.impact_actor=0;
        }
        s.projectile.distance_m=std::min(s.projectile.terminal_distance_m,s.projectile.distance_m+step*kFlipendoSpeedMetersPerSecond);
        if(s.projectile.distance_m>=s.projectile.terminal_distance_m){
            s.projectile.flying=false;s.projectile.impacting=true;s.projectile.impact_seconds=0;
            s.projectile.impact_position=AddVector(s.projectile.origin,ScaleVector(s.projectile.direction,s.projectile.terminal_distance_m));
            if(c.impact_actor){
                if(std::ranges::any_of(s.doors,[&](const auto& door){return door.actor_reference==c.impact_actor&&door.grid;}))
                    grid_push::RememberImpact(c.grid_hit_positions,c.impact_actor,s.projectile.impact_position);
                (void)c.graph.Spell(c.impact_actor);s.audio.PlaySpellHit();c.impact_actor=0;
            }
        }
    }else if(s.projectile.impacting){s.projectile.impact_seconds+=step;if(s.projectile.impact_seconds>=kFlipendoImpactSeconds)s.projectile.impacting=false;}
    // Arrival/impact can enqueue another authored action in the same frame.
    // Save only after those reducers have materialized their world state; ME2
    // also persists any bounded queue that must continue on the next frame.
    reduce_effects();
    c.grid_hit_positions.clear();
    if(!c.graph.healthy()){
        s.frontend.screen=FrontScreen::Main;s.frontend.message="CHALLENGE EVENT ERROR - CHECKPOINT PRESERVED";
        s.front_anchor_valid=false;s.audio.StopDialogue();c.checkpoint_pending=false;
        HPVR_LOGE("[hpvr.quest.challenge] event_graph=FAILED save=UNCHANGED");return;
    }
    // Only the level entrance and original SavePoint events create checkpoints.
    // Collectibles, movers, damage and elapsed time never move the respawn point.
    if(c.authored_checkpoint_pending&&!s.restoring&&!IsCutscenePlaying()&&!s.jump.active&&!s.climb.active){
        SaveCheckpoint(true);c.authored_checkpoint_pending=false;
    }
    c.checkpoint_pending=false;
    if(s.map_id==1&&c.complete&&!s.intro_cutscene.playing)RequestBroomTravel();
}
