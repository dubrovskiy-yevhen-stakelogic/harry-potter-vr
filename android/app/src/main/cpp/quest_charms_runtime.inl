void QuestScene::StartCharmsLesson(std::int32_t reference){
    auto& s=*state_;auto& c=s.charms;
    if(s.map_id!=kCharmsTrainingMapId)return;
    for(unsigned i=0;i<c.lessons.size();++i)if(c.lessons[i].actor_reference==reference){
        if(ChallengeActivated(s.frontend.progress,reference)){(void)s.challenge.graph.Signal(reference);return;}
        if(c.active_lesson>=0)return;
        c.active_lesson=static_cast<int>(i);c.lesson.Begin();c.finish_pending=false;c.lesson_wait=.25F;
        c.lesson_attempt=0;c.lesson_speech.Reset({charms::LessonLine(c.lessons[i],"LessonIntro")});
        c.held_block=0;s.wand_lock_valid=false;s.projectile={};s.basic_cast={};
        HPVR_LOGI("[hpvr.quest.charms.lesson] status=START spell=%s",c.lessons[i].spell_class.c_str());
    }
}
void QuestScene::SubmitCharmsLesson(float score){
    auto& s=*state_;auto& c=s.charms;
    if(c.active_lesson<0||c.lesson_wait>0||s.audio.DialogueBusy()||c.finish_pending)return;
    const auto& metadata=c.lessons[static_cast<unsigned>(c.active_lesson)];
    const auto previous=c.lesson.round;
    if(s.frontend.vr.relaxed_lesson&&score>0&&previous<metadata.pass_marks.size())
        score=std::max(score,metadata.pass_marks[previous]);
    auto result=c.lesson.Submit(metadata,score);
    if(result==charms::AttemptResult::ignored)return;
    if(c.lesson.round>previous){
        const auto slot=1U+static_cast<unsigned>(c.active_lesson);
        auto& p=s.frontend.progress;
        p.lesson_best[slot]=std::max(p.lesson_best[slot],static_cast<unsigned>(std::clamp(score*100.0F,0.0F,100.0F)));
        campaign::RaiseLessonPoints(p.lesson_points,p.house_points,slot,c.lesson.points,
            static_cast<std::uint32_t>(p.generation));
    }
    c.finish_pending=result==charms::AttemptResult::complete;
    c.lesson_speech.Reset(charms::LessonFeedback(metadata,previous,c.lesson.round,c.finish_pending,score,c.lesson_attempt++));
    c.lesson_wait=.25F;
    HPVR_LOGI("[hpvr.quest.charms.lesson] round=%u points=%u score=%.3f finish=%d",c.lesson.round,c.lesson.points,score,c.finish_pending);
}
GestureSpell QuestScene::ActiveGestureSpell() const{
    const auto& s=*state_;if(s.map_id!=kCharmsTrainingMapId)return GestureSpell::Flipendo;
    if(s.charms.active_lesson>=0)return s.charms.active_lesson==0?GestureSpell::Alohomora:GestureSpell::Wingardium;
    const auto ref=s.wand_lock_valid?s.wand_lock_actor:s.aim_actor;
    if(s.charms.blocks.contains(ref))return GestureSpell::Wingardium;
    for(const auto& prop:s.challenge.props)if(prop.reference==ref&&chest::IsChest(prop.name))return GestureSpell::Alohomora;
    for(const auto& zone:s.challenge.spatial)if(zone.reference==ref&&zone.spell_name=="spellaloho")return GestureSpell::Alohomora;
    return GestureSpell::Flipendo;
}
void QuestScene::UpdateCharmsWand(const std::array<float,3>& tip,const std::array<float,3>& direction,bool tracked,bool held){
    auto& s=*state_;auto& c=s.charms;c.wand_tip=tip;c.wand_direction=direction;
    if(!c.held_block)return;
    if(!tracked||IsCutscenePlaying()||IsFrontEndVisible())c.held_block=0;
    else if(held)c.hold_release_armed=true;
    else if(c.hold_release_armed)c.held_block=0;
}
void QuestScene::AdvanceCharms(float seconds){
    auto& s=*state_;auto& c=s.charms;const float step=std::clamp(seconds,0.0F,.05F);
    AdvanceChallenge(step);
    if(s.death_time>=0||s.frontend.PausesWorld())return;
    if(c.active_lesson>=0&&!IsCutscenePlaying()){
        const auto speech=c.lesson_speech.Advance(step,false,s.audio.DialogueBusy());
        if(speech.stop)s.audio.StopDialogue();
        if(speech.timed_out)HPVR_LOGI("[hpvr.quest.charms.lesson] speech=WAIT_TIMEOUT");
        if(!speech.line.empty())if(const auto audio=GameplayDialogueIndex(s.frontend.assets,speech.line))
            if(s.audio.PlayDialogue(*audio))c.lesson_speech.Started(s.audio.DialogueDurationSeconds(*audio));
        c.lesson_wait=c.lesson_speech.Pending()?.01F:0;
        if(c.finish_pending&&c.lesson_wait==0&&!s.audio.DialogueBusy()){
            const auto ref=c.lessons[static_cast<unsigned>(c.active_lesson)].actor_reference;
            RememberChallengeEvent(s.frontend.progress,ref);c.active_lesson=-1;c.finish_pending=false;
            (void)s.challenge.graph.Signal(ref);s.wand_lock_valid=false;s.wand_cast_consumed=true;
            HPVR_LOGI("[hpvr.quest.charms.lesson] status=LEARNED ref=%d",ref);
        }
    }
    if(IsCutscenePlaying())return;
    const auto body=s.player_capsule_valid?s.player_capsule:s.last_player;
    const auto displacement=SubtractVector(body,c.reflected_player_position);
    const float distance=std::hypot(displacement[0],displacement[2]);
    c.reflected_walk_time=std::max(0.0F,c.reflected_walk_time-step);
    if(c.reflected_player_valid&&distance>step*.2F&&distance<.5F)c.reflected_walk_time=.15F;
    c.reflected_player_position=body;c.reflected_player_valid=true;
    for(auto& actor:s.character_draws)if(actor.player){
        const std::string clip=c.reflected_walk_time>0&&actor.clips.contains("run")?"run":"breathe";
        if(actor.active_clip!=clip){actor.active_clip=clip;actor.animation_time=0;}
        actor.animation_loop=true;
    }
    s.bump_cooldown=std::max(0.0F,s.bump_cooldown-step);
    if(s.bump_actor&&!s.audio.DialogueBusy()){
        for(auto& actor:s.character_draws)if(actor.actor_reference==s.bump_actor){
            actor.active_clip="breathe";actor.desired_yaw=s.bump_restore_yaw;actor.animation_time=0;
        }
        s.bump_actor=0;
    }
    for(auto& actor:s.character_draws){
        const auto cls=AsciiFold(actor.class_name);
        if(!actor.enabled||(!cls.starts_with("harrypotter.gen_fem_")&&!cls.starts_with("harrypotter.gen_male_")))continue;
        if(s.bump_actor!=actor.actor_reference){
            actor.active_clip="breathe";actor.animation_loop=true;
        }
        const auto profile=std::ranges::find_if(s.frontend.assets.bump_speech,[&](const auto& p){return p.actor_reference==actor.actor_reference;});
        if(profile==s.frontend.assets.bump_speech.end()||profile->lines.empty())continue;
        auto& contact=s.bump_states[actor.actor_reference];
        auto position=AddVector(actor.base_origin,actor.cutscene_offset);position[1]+=1.2F;
        const auto delta=SubtractVector(position,s.last_player);const float distance=std::sqrt(DotVector(delta,delta));
        if(distance>2.3F)contact.near=false;
        if(c.active_lesson>=0||c.lesson_speech.Pending()||s.audio.DialogueBusy()||s.bump_cooldown>0||
            contact.near||distance>1.9F||distance<.01F)continue;
        if(BasicRayDistance(s.collision_triangles,s.last_player,ScaleVector(delta,1/distance))<distance-.1F)continue;
        const auto index=GameplayDialogueIndex(s.frontend.assets,profile->lines[contact.next%profile->lines.size()]);
        if(!index||!s.audio.PlayDialogue(*index))continue;
        contact.near=true;++contact.next;s.bump_cooldown=.25F;
        s.bump_actor=actor.actor_reference;s.bump_restore_yaw=actor.desired_yaw;
        actor.desired_yaw=std::atan2(-delta[0],-delta[2]);
        actor.active_clip=actor.clips.contains("talk2")?"talk2":actor.clips.contains("talk1")?"talk1":"breathe";
        actor.animation_time=0;actor.animation_loop=true;
        HPVR_LOGI("[hpvr.quest.charms.student] status=SPEAK actor=%d clip=%zu",actor.actor_reference,*index);
    }
    bool moved=false;
    for(auto& [ref,block]:c.blocks){
        const auto old=block.offset;
        const auto bounds=CharmsBlockBounds(block);
        const auto desired=AddVector(AddVector(c.wand_tip,ScaleVector(c.wand_direction,c.hold_distance)),c.hold_offset);
        const auto physics=charms_block::Advance(block.motion,bounds,RotateYaw(desired,-block.collision_yaw),c.held_block==ref&&!block.plate,
            step,block.maximum_hold_seconds,s.collision_triangles,s.challenge.collision_base,
            block.collision_first,block.collision_count,block.collision_yaw);
        block.offset=AddVector(block.offset,RotateYaw(physics.offset,block.collision_yaw));
        if(physics.released&&c.held_block==ref)c.held_block=0;
        if(!block.plate){
            const auto current=grid_motion::Translate({block.minimum,block.maximum},block.offset);
            for(const auto& plate:s.challenge.spatial){
                if(plate.proximity_class!="wingardiumblock")continue;
                const auto* node=s.challenge.graph.Find(plate.reference);
                if(!node||!node->active||node->consumed||ChallengeActivated(s.frontend.progress,plate.reference)||
                   !charms_block::TouchesPlate(current,plate.position,plate.radius,plate.height))continue;
                std::array<float,3> alignment{};
                if(!charms_block::SettleOnPlate(s.collision_triangles,CharmsBlockBounds(block),RotateYaw(plate.position,-block.collision_yaw),
                    block.collision_first,block.collision_count,&alignment,block.collision_yaw))continue;
                block.offset=AddVector(block.offset,RotateYaw(alignment,block.collision_yaw));block.plate=plate.reference;
                block.motion={};if(c.held_block==ref)c.held_block=0;
                RememberChallengeEvent(s.frontend.progress,plate.reference);
                (void)s.challenge.graph.Touch(plate.reference);break;
            }
            if(!block.plate&&charms_block::NeedsReset(block.offset,kMetersPerUnrealUnit)){
                block.offset={};block.motion={};if(c.held_block==ref)c.held_block=0;
            }
        }
        const auto translation=SubtractVector(block.offset,old);
        if(block.offset!=old){
            // Later blocks in this frame see the current shape, not last frame's
            // pose. The range remains available for exact self-exclusion.
            for(std::size_t index=block.collision_first;index<s.collision_triangles.size()&&
                index-block.collision_first<block.collision_count;++index){
                auto& triangle=s.collision_triangles[index];
                for(auto& point:triangle.vertices)point=AddVector(point,translation);
                triangle.minimum=AddVector(triangle.minimum,translation);
                triangle.maximum=AddVector(triangle.maximum,translation);
            }
            moved=true;
        }
    }
    if(moved)s.challenge.collision_dirty=true;
    if(s.challenge.complete&&!s.intro_cutscene.playing){
        auto& progress=s.frontend.progress;
        if((progress.completed_maps&(1U<<kCharmsTrainingMapId))==0){
            campaign::AddHousePoints(progress.house_points,progress.challenge_stars>=6?20U:progress.challenge_stars>=3?10U:5U,
                static_cast<std::uint32_t>(progress.generation));
            progress.completed_maps|=1U<<kCharmsTrainingMapId;
        }
        SaveCheckpoint(true);s.frontend.ShowDemoNotice(true);s.front_anchor_valid=false;
    }
}
