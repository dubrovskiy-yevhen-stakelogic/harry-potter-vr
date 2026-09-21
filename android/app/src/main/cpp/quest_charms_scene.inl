struct CharmsBlock {
    std::array<float,3> minimum{},maximum{},offset{};
    std::int32_t plate=0;
    float fall_speed=0;
    float maximum_hold_seconds=15;
    charms_block::Motion motion;
    std::size_t collision_first=0,collision_count=0;
    grid_motion::Bounds collision_bounds{};
    float collision_yaw=0;
};
grid_motion::Bounds CharmsBlockBounds(const CharmsBlock& block){
    const auto bounds=charms_block::Valid(block.collision_bounds)?block.collision_bounds:grid_motion::Bounds{block.minimum,block.maximum};
    return grid_motion::Translate(bounds,RotateYaw(block.offset,-block.collision_yaw));
}
std::string_view CharmsCutsceneDoorTag(std::int32_t scene){
    if(scene==1526)return "openclosedoor";
    if(scene==2771||scene==1878)return "aloroom2";
    return {};
}
float OpenCharmsCutsceneDoors(std::int32_t scene,std::vector<DoorDraw>& doors){
    const auto tag=CharmsCutsceneDoorTag(scene);float wait=0;
    if(tag.empty())return wait;
    for(auto& door:doors)if(door.tag==tag&&!door.grid){
        door.cutscene_hold=true;door.hold=0;door.opening=true;door.motion.seconds=door.open_seconds;
        if(movers::Start(door.motion,true)){door.completion_sent=false;wait=std::max(wait,door.open_seconds);}
    }
    return wait;
}
float CloseCharmsDoors(std::vector<DoorDraw>& doors,std::string_view tag,bool retain_hold=false){
    float wait=0;
    for(auto& door:doors)if(door.tag==tag&&!door.grid){
        door.cutscene_hold=retain_hold;door.hold=0;door.opening=false;door.motion.seconds=door.close_seconds;
        if(movers::Start(door.motion,false))door.completion_sent=false;
        if(door.motion.moving)wait=std::max(wait,door.close_seconds+.05F);
    }
    return wait;
}
float CloseCharmsEntryDoors(std::vector<DoorDraw>& doors){return CloseCharmsDoors(doors,"openclosedoor");}
struct CharmsRuntime {
    std::array<charms::LessonMetadata,2> lessons;
    charms::LessonSession lesson;
    int active_lesson=-1;
    float lesson_wait=0;
    bool finish_pending=false;
    charms::LessonSpeechQueue lesson_speech;
    unsigned lesson_attempt=0;
    std::map<std::int32_t,CharmsBlock> blocks;
    std::map<std::int32_t,std::int32_t> prop_attachments;
    std::set<std::int32_t> valid_plates;
    std::int32_t held_block=0;
    std::array<float,3> wand_tip{},wand_direction{0,0,-1},hold_offset{};
    float hold_distance=2;
    bool hold_release_armed=false;
    std::array<float,3> reflected_player_position{};
    bool reflected_player_valid=false;
    float reflected_walk_time=0;
};
void UpdateReflectedPlayerAnimation(CharmsRuntime& state,std::vector<CharacterDraw>& actors,
                                    const std::array<float,3>& body,float seconds,bool controlled){
    if(controlled){state.reflected_player_valid=false;state.reflected_walk_time=0;return;}
    const float step=std::clamp(seconds,0.F,.05F);
    const auto displacement=SubtractVector(body,state.reflected_player_position);
    const float distance=std::hypot(displacement[0],displacement[2]);
    state.reflected_walk_time=std::max(0.F,state.reflected_walk_time-step);
    if(state.reflected_player_valid&&distance>step*.2F&&distance<.5F)state.reflected_walk_time=.15F;
    state.reflected_player_position=body;state.reflected_player_valid=true;
    for(auto& actor:actors)if(actor.player){
        const std::string clip=state.reflected_walk_time>0&&actor.clips.contains("run")?"run":"breathe";
        if(actor.active_clip!=clip){actor.active_clip=clip;actor.animation_time=0;}
        actor.animation_loop=true;
    }
}
void UpdateCharmsPickupAttachments(const CharmsRuntime& charms,const std::vector<DoorDraw>& doors,std::vector<BeanDraw>& pickups){
    for(auto& pickup:pickups){
        pickup.attachment_offset={};
        const auto parent=charms.prop_attachments.find(pickup.actor_reference);
        if(parent==charms.prop_attachments.end()||pickup.source_actor)continue;
        for(const auto& door:doors)if(door.actor_reference==parent->second){
            pickup.attachment_offset=SubtractVector(MoverPoint(door,pickup.position),pickup.position);break;
        }
    }
}
bool ValidateCharmsSceneLayout(const ChallengeRuntime& challenge,const CharmsRuntime& charms,
    std::size_t doors,std::size_t fixtures,const std::vector<CharacterDraw>& characters){
    if(!challenge.graph.healthy()||doors!=58||challenge.scenes.size()!=23||
        charms.blocks.size()!=7||charms.valid_plates.size()!=9||!fixtures||characters.size()!=12||
        !std::ranges::all_of(charms.lessons,[](const auto& lesson){return lesson.valid;}))return false;
    for(const auto reference:{1117,1196,1225,2237})
        if(std::ranges::count_if(characters,[&](const auto& actor){
            return actor.actor_reference==reference&&!actor.clips.empty();})!=1)return false;
    return true;
}
void BindCharmsKnightTargets(ChallengeRuntime& challenge,const wand::Hp1ActorVisualCensus& census,
    const hpvr_hp1_player_start_report& start,float yaw){
    for(auto& zone:challenge.spatial){
        if(!zone.spell||zone.spell_name!="spellflip")continue;
        const ChallengeProp* nearest=nullptr;float best=.8F*.8F;
        for(const auto& actor:census.actors){
            if(AsciiFold(actor.qualified_class_name)!="hprops.knight")continue;
            const auto delta=SubtractVector(ActorLocalPosition(actor,start,yaw),zone.position);
            const float distance=delta[0]*delta[0]+delta[2]*delta[2];
            if(distance>=best||std::abs(delta[1])>2)continue;
            const auto prop=std::ranges::find_if(challenge.props,[&](const auto& p){return p.reference==actor.actor_reference;});
            if(prop==challenge.props.end())continue;
            nearest=&*prop;best=distance;
        }
        if(!nearest)continue;
        zone.position=ScaleVector(AddVector(nearest->minimum,nearest->maximum),.5F);
        zone.radius=.08F+.5F*std::max(nearest->maximum[0]-nearest->minimum[0],nearest->maximum[2]-nearest->minimum[2]);
        zone.height=.08F+.5F*(nearest->maximum[1]-nearest->minimum[1]);
    }
}
std::string SaveCharmsState(const CharmsRuntime& c){
    std::ostringstream out;out<<"CHARMS 1 "<<std::setprecision(9)<<c.active_lesson<<' '
        <<c.lesson.round<<' '<<c.lesson.points<<' '<<c.lesson.started<<' '<<c.lesson.finished<<' '
        <<c.lesson.learned<<' '<<c.finish_pending<<' '<<c.blocks.size();
    for(const auto& [ref,b]:c.blocks)out<<' '<<ref<<' '<<b.plate<<' '<<b.offset[0]<<' '<<b.offset[1]<<' '<<b.offset[2];
    return out.str();
}
bool RestoreCharmsState(CharmsRuntime& c,const std::string& text){
    auto next=c;next.held_block=0;next.active_lesson=-1;next.lesson={};next.lesson_wait=0;next.finish_pending=false;
    next.lesson_speech={};next.lesson_attempt=0;
    for(auto& [ref,b]:next.blocks){(void)ref;b.offset={};b.plate=0;b.fall_speed=0;b.motion={};}
    if(text.empty()){c=std::move(next);return true;}
    std::istringstream in(text);std::string magic;unsigned version=0;std::size_t count=0;
    in>>magic>>version>>next.active_lesson>>next.lesson.round>>next.lesson.points>>next.lesson.started
      >>next.lesson.finished>>next.lesson.learned>>next.finish_pending>>count;
    if(!in||magic!="CHARMS"||version!=1||count!=next.blocks.size()||
       !charms::ValidSession(next.lessons,next.lesson,next.active_lesson,next.finish_pending))return false;
    std::set<std::int32_t> occupied_plates;
    for(auto& [ref,b]:next.blocks){std::int32_t saved=0;in>>saved>>b.plate>>b.offset[0]>>b.offset[1]>>b.offset[2];
        if(!in||saved!=ref||(b.plate!=0&&(!next.valid_plates.contains(b.plate)||!occupied_plates.insert(b.plate).second))||
           !std::ranges::all_of(b.offset,[](float v){return std::isfinite(v)&&std::abs(v)<200;}))return false;
    }
    in>>std::ws;if(!in.eof())return false;c=std::move(next);return true;
}
void AppendCharmsCollision(CharmsRuntime& charms,std::vector<CollisionTriangle>& triangles){
    for(auto& [reference,block]:charms.blocks){
        (void)reference;
        block.collision_first=triangles.size();
        charms_block::AppendBox(triangles,CharmsBlockBounds(block));
        for(std::size_t i=block.collision_first;i<triangles.size();++i){
            auto& t=triangles[i];
            for(auto& p:t.vertices)p=RotateYaw(p,block.collision_yaw);
            t.normal=RotateYaw(t.normal,block.collision_yaw);t.minimum=t.maximum=t.vertices[0];
            for(const auto& p:t.vertices)for(unsigned a=0;a<3;++a){t.minimum[a]=std::min(t.minimum[a],p[a]);t.maximum[a]=std::max(t.maximum[a],p[a]);}
        }
        block.collision_count=triangles.size()-block.collision_first;
    }
}
bool LoadCharmsMetadata(const std::filesystem::path& root,const wand::Hp1ActorVisualCensus& census,
    const hpvr_hp1_player_start_report&,float yaw,const std::vector<ChallengeProp>& props,CharmsRuntime& result,
    const std::vector<GpuVertex>* vertices=nullptr){
    result.lessons=charms::LoadLessons(root,census);
    for(const auto& lesson:result.lessons)if(!lesson.valid){
        HPVR_LOGE("[hpvr.quest.charms] lesson=REJECTED reason=%s",lesson.error.c_str());return false;
    }
    for(const auto& actor:census.actors){
        if(AsciiFold(actor.qualified_class_name)=="engine.attachmover"){
            const auto* tag=charms::detail::Property(actor.serialized_properties,"AttachTag");
            if(tag&&tag->text_value_serialized&&!tag->text_value.empty())
                for(const auto& attached:census.actors)if(AsciiFold(attached.tag)==AsciiFold(tag->text_value))
                    result.prop_attachments.emplace(attached.actor_reference,actor.actor_reference);
        }
        if(AsciiFold(actor.qualified_class_name)!="engine.trigger")continue;
        const auto* proximity=charms::detail::Property(actor.serialized_properties,"ClassProximityType");
        const auto* type=charms::detail::Property(actor.serialized_properties,"TriggerType");
        if(proximity&&proximity->object_reference_serialized&&!proximity->object_path.empty()&&
           AsciiFold(proximity->object_path.back())=="wingardiumblock"&&type&&type->value.size()==1&&type->value[0]==2)
            result.valid_plates.insert(actor.actor_reference);
    }
    for(const auto& prop:props)if(prop.name=="hprops.wingardiumblock"){
        CharmsBlock block;block.minimum=prop.minimum;block.maximum=prop.maximum;
        const auto actor=std::ranges::find_if(census.actors,[&](const auto& a){return a.actor_reference==prop.reference;});
        if(actor==census.actors.end()||!charms_block::Valid({block.minimum,block.maximum}))return false;
        if(vertices){
            if(!prop.count||prop.first>=vertices->size()||prop.count>vertices->size()-prop.first)return false;
            block.collision_yaw=yaw+actor->rotation_units[1]*kTau/65536.0F;
            auto& box=block.collision_bounds;box.minimum={1e9F,1e9F,1e9F};box.maximum={-1e9F,-1e9F,-1e9F};
            for(std::size_t i=prop.first;i<std::size_t(prop.first)+prop.count;++i){
                const auto& v=(*vertices)[i];const auto p=RotateYaw({v.position[0],v.position[1],v.position[2]},-block.collision_yaw);
                for(unsigned a=0;a<3;++a){box.minimum[a]=std::min(box.minimum[a],p[a]);box.maximum[a]=std::max(box.maximum[a],p[a]);}
            }
            if(!charms_block::Valid(box))return false;
        }
        try{block.maximum_hold_seconds=charms::detail::Number(actor->serialized_properties,"levTime");}
        catch(const std::exception&){return false;}
        if(block.maximum_hold_seconds<=0||block.maximum_hold_seconds>120)return false;
        result.blocks.emplace(prop.reference,std::move(block));
    }
    HPVR_LOGI("[hpvr.quest.charms] lessons=2 blocks=%zu",result.blocks.size());
    return result.blocks.size()==7;
}
