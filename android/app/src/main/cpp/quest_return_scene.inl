struct ReturnFlightLeg {
    std::int32_t first_path=0,station=0;
    std::array<float,3> first_position{},station_position{};
    peeves::Flight waypoints;
};
struct ReturnMetadata {
    std::int32_t opening_scene=0,peeves_actor=0,harry_actor=0,malfoy_actor=0;
    std::string peeves_trigger,peeves_defeated;
    std::vector<ReturnFlightLeg> patrol;
    ReturnFlightLeg entrance,departure;
    std::int32_t merchant=0;
    unsigned sale_price=0;
    std::string sale_scene;
};

bool LoadReturnMetadata(const wand::Hp1ActorVisualCensus& census,
    const hpvr_hp1_player_start_report& start,float yaw,ReturnMetadata& output){
    if(census.status!=wand::Hp1ProfileStatus::ok)return false;
    ReturnMetadata result;
    const wand::Hp1ActorVisual* peeves_actor=nullptr;
    std::map<std::string,const wand::Hp1ActorVisual*> named;
    for(const auto& actor:census.actors){
        if(!named.emplace(AsciiFold(actor.object_name),&actor).second)return false;
        const auto cls=AsciiFold(actor.qualified_class_name);
        if(cls=="tut3.tut3peeves"){
            if(peeves_actor)return false;
            peeves_actor=&actor;result.peeves_actor=actor.actor_reference;
            result.peeves_trigger=actor.tag;result.peeves_defeated=actor.event;
        }
        if(cls=="harrypotter.harry")result.harry_actor=actor.actor_reference;
        if(cls=="harrypotter.bossrailmove")result.malfoy_actor=actor.actor_reference;
        if(cls=="tut1.tut1fred")for(const auto& p:actor.serialized_properties){
            if(AsciiFold(p.name)=="merchant"&&p.boolean_value_serialized&&p.boolean_value)result.merchant=actor.actor_reference;
            if(AsciiFold(p.name)=="saleprice"&&p.value.size()==4){
                std::int32_t price=0;std::memcpy(&price,p.value.data(),4);
                if(price>0&&price<=1000)result.sale_price=static_cast<unsigned>(price);
            }
            if(AsciiFold(p.name)=="salescene")result.sale_scene=AsciiFold(p.text_value);
        }
        if(cls=="hpbase.cutscene")for(const auto& p:actor.serialized_properties)
            if(AsciiFold(p.name)=="blevelloadstarts"&&p.boolean_value_serialized&&p.boolean_value){
                if(result.opening_scene)return false;
                result.opening_scene=actor.actor_reference;
            }
    }
    if(!peeves_actor||!result.opening_scene||!result.harry_actor||!result.malfoy_actor||
        result.peeves_trigger.empty()||result.peeves_defeated.empty())return false;
    const auto leg=[&](const std::string& first,const std::string& destination,ReturnFlightLeg& out){
        const auto path=named.find(AsciiFold(first)),station=named.find(AsciiFold(destination));
        if(path==named.end()||station==named.end()||!path->second->location_serialized||!station->second->location_serialized||
            !AsciiFold(path->second->qualified_class_name).starts_with("harrypotter.hpath_")||
            AsciiFold(station->second->qualified_class_name)!="hpbase.basestation")return false;
        out={path->second->actor_reference,station->second->actor_reference,
            ActorLocalPosition(*path->second,start,yaw),ActorLocalPosition(*station->second,start,yaw),{}};
        const auto finite=[](float v){return std::isfinite(v)&&std::abs(v)<2000;};
        return std::ranges::all_of(out.first_position,finite)&&std::ranges::all_of(out.station_position,finite);
    };
    auto first=SerializedName(*peeves_actor,"firstpath"),destination=SerializedName(*peeves_actor,"stationdestination");
    if(!leg(first,destination,result.entrance))return false;
    std::set<std::pair<std::string,int>> visited;
    int group=0;
    while(result.patrol.size()<16){
        const auto key=std::make_pair(destination,group);
        if(!visited.insert(key).second){
            if(destination!=SerializedName(*peeves_actor,"stationdestination")||group!=0)return false;
            break;
        }
        const auto* station=named.at(destination);
        const wand::Hp1StationRoute* route=nullptr;
        for(const auto& p:station->serialized_properties)
            if(AsciiFold(p.name)=="aidata"&&std::max<std::int64_t>(0,p.array_index)==group&&p.station_route){
                if(route)return false;
                route=&*p.station_route;
            }
        if(!route||route->behavior==4)return false;
        first=AsciiFold(route->first_path);destination=AsciiFold(route->destination);group=route->next_group;
        ReturnFlightLeg segment;
        if(!leg(first,destination,segment))return false;
        result.patrol.push_back(segment);
    }
    if(result.patrol.empty()||result.patrol.size()>=16||
       !leg(SerializedName(*peeves_actor,"exitfirstpath"),SerializedName(*peeves_actor,"exitstationdestination"),result.departure))return false;
    output=std::move(result);return true;
}

// Breadth-first walk over same-class path nodes from first_path to station.
// allow_nearest ends at the node nearest the station when none links to it.
bool ResolveReturnLeg(const std::map<std::int32_t,const wand::Hp1ActorVisual*>& actors,const wand::Hp1Navigation& navigation,
    const hpvr_hp1_player_start_report& start,float yaw,ReturnFlightLeg& leg,bool allow_nearest){
    if(!actors.contains(leg.first_path)||!actors.contains(leg.station))return false;
    const auto type=AsciiFold(actors.at(leg.first_path)->qualified_class_name);
    std::map<std::int32_t,std::int32_t> previous{{leg.first_path,0}};
    std::vector<std::int32_t> queue{leg.first_path};
    for(std::size_t i=0;i<queue.size();++i){
        if(queue.size()>128)return false;
        for(const auto& edge:navigation.paths){
            if(edge.pruned||edge.start!=queue[i]||!actors.contains(edge.end)||previous.contains(edge.end))continue;
            const auto& actor=*actors.at(edge.end);
            if(!actor.location_serialized||(edge.end!=leg.station&&AsciiFold(actor.qualified_class_name)!=type))continue;
            previous.emplace(edge.end,edge.start);queue.push_back(edge.end);
        }
    }
    auto end=leg.station;
    if(!previous.contains(end)){
        if(!allow_nearest)return false;
        float nearest=std::numeric_limits<float>::max();end=leg.first_path;
        for(const auto ref:queue){
            const auto delta=SubtractVector(ActorLocalPosition(*actors.at(ref),start,yaw),leg.station_position);
            const float squared=DotVector(delta,delta);
            if(squared<nearest){nearest=squared;end=ref;}
        }
    }
    std::vector<std::int32_t> path;
    for(auto ref=end;ref;ref=previous.at(ref))path.push_back(ref);
    std::reverse(path.begin(),path.end());leg.waypoints.clear();
    for(const auto ref:path)leg.waypoints.push_back(ActorLocalPosition(*actors.at(ref),start,yaw));
    if(end!=leg.station)leg.waypoints.push_back(leg.station_position);
    return peeves::ValidFlight(leg.waypoints);
}

bool ResolveReturnRoutes(const wand::Hp1ActorVisualCensus& census,const wand::Hp1Navigation& navigation,
    const hpvr_hp1_player_start_report& start,float yaw,ReturnMetadata& metadata){
    if(census.status!=wand::Hp1ProfileStatus::ok||navigation.status!=wand::Hp1ProfileStatus::ok)return false;
    std::map<std::int32_t,const wand::Hp1ActorVisual*> actors;
    for(const auto& actor:census.actors)actors.emplace(actor.actor_reference,&actor);
    auto candidate=metadata;
    const auto resolve=[&](ReturnFlightLeg& leg,bool departure){
        return ResolveReturnLeg(actors,navigation,start,yaw,leg,departure);
    };
    if(!resolve(candidate.entrance,false)||!resolve(candidate.departure,true))return false;
    for(auto& leg:candidate.patrol)if(!resolve(leg,false))return false;
    metadata=std::move(candidate);return true;
}

// Hedwig delivers the scroll: CutScene0/CutScene2 send "trigger hedwig1/2",
// which in the original starts her flight along firstPath to her station.
struct ReturnOwl {
    std::int32_t actor=0;
    std::string tag;
    ReturnFlightLeg leg;
    peeves::Flight departure;
    std::array<float,3> scroll{};
    float pause=0,wait=0,fall_speed=0;
    bool departing=false,drop_pending=false,scroll_visible=false;
    peeves::Battle flight; // position and waypoint cursor for peeves::Move
    bool flying=false,arrived=false;
};

bool LoadReturnOwls(const wand::Hp1ActorVisualCensus& census,const wand::Hp1Navigation& navigation,
    const hpvr_hp1_player_start_report& start,float yaw,std::vector<ReturnOwl>& owls){
    owls.clear();
    if(census.status!=wand::Hp1ProfileStatus::ok||navigation.status!=wand::Hp1ProfileStatus::ok)return false;
    std::map<std::int32_t,const wand::Hp1ActorVisual*> actors;
    std::map<std::string,const wand::Hp1ActorVisual*> named;
    for(const auto& actor:census.actors){actors.emplace(actor.actor_reference,&actor);named.emplace(AsciiFold(actor.object_name),&actor);}
    for(const auto& actor:census.actors){
        if(AsciiFold(actor.qualified_class_name)!="harrypotter.hedwig"||actor.tag.empty())continue;
        const auto path=named.find(AsciiFold(SerializedName(actor,"firstpath")));
        const auto station=named.find(AsciiFold(SerializedName(actor,"stationdestination")));
        if(path==named.end()||station==named.end()||!path->second->location_serialized||!station->second->location_serialized)continue;
        ReturnOwl owl;owl.actor=actor.actor_reference;owl.tag=AsciiFold(actor.tag);
        owl.leg={path->second->actor_reference,station->second->actor_reference,
            ActorLocalPosition(*path->second,start,yaw),ActorLocalPosition(*station->second,start,yaw),{}};
        if(!ResolveReturnLeg(actors,navigation,start,yaw,owl.leg,true))return false;
        for(const auto& p:station->second->serialized_properties)if(p.station_route){
            const auto& route=*p.station_route;
            owl.pause=std::clamp(route.pause_seconds,0.F,30.F);
            const auto next=named.find(AsciiFold(route.first_path)),end=named.find(AsciiFold(route.destination));
            if(next!=named.end()&&end!=named.end()){
                ReturnFlightLeg exit_leg{next->second->actor_reference,end->second->actor_reference,
                    ActorLocalPosition(*next->second,start,yaw),ActorLocalPosition(*end->second,start,yaw),{}};
                if(!ResolveReturnLeg(actors,navigation,start,yaw,exit_leg,true))return false;
                owl.departure=std::move(exit_leg.waypoints);
            }
            break;
        }
        owls.push_back(std::move(owl));
    }
    return true;
}

struct ReturnRuntime {
    ReturnMetadata metadata;
    peeves::Route route;
    peeves::Battle battle;
    peeves::Timings timings;
    std::array<peeves::Apple,peeves::kMaximumApples> apples{};
    peeves::Phase voiced_phase=peeves::Phase::Dormant;
    unsigned voiced_hits=peeves::kMaximumHits;
    std::string voice;
    bool completion_shown=false; // Session-only: the end panel is shown once per load.
    std::vector<ReturnOwl> owls;
};

bool LaunchReturnOwl(ReturnRuntime& state,std::vector<CharacterDraw>& actors,const std::string& tag){
    for(auto& owl:state.owls){
        if(owl.tag!=tag)continue;
        const auto actor=std::ranges::find_if(actors,[&](const auto& a){return a.actor_reference==owl.actor;});
        if(actor==actors.end())return false;
        if(owl.flying||owl.arrived)return true;
        owl.flight=peeves::Battle{};owl.flight.position=AddVector(actor->base_origin,actor->cutscene_offset);
        owl.flying=true;actor->enabled=true;actor->collision_disabled=true;
        actor->active_clip=actor->clips.contains("fly")?"fly":"breathe";
        actor->animation_time=0;actor->animation_loop=true;
        return true;
    }
    return false;
}

void AdvanceReturnOwls(ReturnRuntime& state,std::vector<CharacterDraw>& actors,float seconds){
    constexpr float kOwlSpeedMetersPerSecond=5.0F;
    if(!std::isfinite(seconds)||seconds<=0)return;
    for(auto& owl:state.owls){
        if(!owl.flying&&!owl.arrived)continue;
        const auto actor=std::ranges::find_if(actors,[&](const auto& a){return a.actor_reference==owl.actor;});
        if(actor==actors.end()){owl.flying=false;continue;}
        if(owl.arrived&&!owl.flying){
            owl.wait+=seconds;
            if(owl.wait<std::max(.4F,owl.pause))continue;
            if(owl.departure.empty()){actor->enabled=false;continue;}
            owl.flying=true;owl.departing=true;owl.flight.waypoint=0;
            actor->active_clip=actor->clips.contains("fly")?"fly":"breathe";actor->animation_loop=true;actor->animation_time=0;
        }
        const auto from=owl.flight.position;
        const auto& route=owl.departing?owl.departure:owl.leg.waypoints;
        const bool arrived=peeves::Move(owl.flight,route,kOwlSpeedMetersPerSecond*seconds);
        const auto delta=SubtractVector(owl.flight.position,from);
        actor->cutscene_offset=SubtractVector(owl.flight.position,actor->base_origin);
        if(std::hypot(delta[0],delta[2])>.00001F)actor->desired_yaw=std::atan2(delta[0],delta[2]);
        if(arrived||owl.flight.waypoint>=route.size()){
            if(owl.departing){owl.flying=false;owl.departure.clear();actor->enabled=false;continue;}
            owl.flying=false;owl.arrived=true;
            owl.drop_pending=true;owl.scroll_visible=true;owl.scroll=owl.flight.position;owl.wait=0;
            actor->active_clip=actor->clips.contains("drop")?"drop":"breathe";
            actor->animation_time=0;actor->animation_loop=actor->active_clip=="breathe";
        }
    }
}

struct ReturnMalfoyRuntime {
    malfoy::Config config;
    malfoy::Timings timings;
    malfoy::State motion;
    std::int32_t actor=0;
    std::array<std::int32_t,2> rails{};
    std::array<float,3> player_boundary{};
    std::string first_throw,defeated,victorious;
    std::array<cracker::Cracker,cracker::kCapacity> crackers{};
    std::array<float,3> hand{},aim{0,0,-1};
    bool hand_valid=false,trigger_held=false,throw_armed=false;
};
[[maybe_unused]] bool ConfigureReturnMalfoy(const wand::Hp1ActorVisualCensus& census,
    const hpvr_hp1_player_start_report& start,float yaw,const std::vector<CharacterDraw>& actors,ReturnMalfoyRuntime& output){
    if(census.status!=wand::Hp1ProfileStatus::ok)return false;
    ReturnMalfoyRuntime result;const wand::Hp1ActorVisual* boss=nullptr;
    for(const auto& a:census.actors)if(AsciiFold(a.qualified_class_name)=="harrypotter.bossrailmove"){
        if(boss)return false;boss=&a;
    }
    if(!boss)return false;
    result.actor=boss->actor_reference;
    const auto move_tag=SerializedName(*boss,"navpoint_movetagname"),player_tag=SerializedName(*boss,"navpoint_harrydistancetagname");
    result.first_throw=SerializedName(*boss,"triggertosendonfirstthrow");
    result.defeated=SerializedName(*boss,"trigeventwhendefeated");result.victorious=SerializedName(*boss,"trigeventwhenvictor");
    if(move_tag.empty()||player_tag.empty()||result.first_throw.empty()||result.defeated.empty()||result.victorious.empty())return false;
    unsigned rails=0,boundaries=0;
    for(const auto& a:census.actors){
        const auto tag=AsciiFold(a.tag);
        if(tag!=move_tag&&tag!=player_tag)continue;
        if(!a.location_serialized||AsciiFold(a.qualified_class_name)!="engine.navigationpoint")return false;
        const auto p=ActorLocalPosition(a,start,yaw);if(!malfoy::Finite(p))return false;
        if(tag==move_tag){
            if(rails>=2)return false;result.rails[rails]=a.actor_reference;
            (rails++==0?result.config.rail_start:result.config.rail_end)=p;
        }else{if(boundaries++)return false;result.player_boundary=p;}
    }
    if(rails!=2||boundaries!=1)return false;
    result.config.start_speed=SerializedFloat(*boss,"groundspeed",0,200)*kMetersPerUnrealUnit;
    result.config.end_speed=SerializedFloat(*boss,"groundspeedend",0,275)*kMetersPerUnrealUnit;
    for(const auto& p:boss->serialized_properties){
        const auto name=AsciiFold(p.name);
        if(name=="groundspeed"||name=="groundspeedend"){
            float value=0;if(p.value.size()!=4)return false;std::memcpy(&value,p.value.data(),4);if(!std::isfinite(value))return false;
        }
        if(name=="inumhitstobeat"){
            std::int32_t value=0;if(p.value.size()!=4)return false;std::memcpy(&value,p.value.data(),4);
            if(value<=0)return false;result.config.maximum_hits=static_cast<unsigned>(value);
        }
    }
    if(!malfoy::Valid(result.config))return false;
    const auto draw=std::ranges::find_if(actors,[&](const auto& a){return a.actor_reference==result.actor;});
    if(draw==actors.end())return false;
    for(const auto* name:{"lookdownhall","strafeleft","straferight","breathe","throw","knockback","knockdown"}){
        const auto clip=draw->clips.find(name);
        if(clip==draw->clips.end()||!std::isfinite(clip->second.duration)||clip->second.duration<=0)return false;
    }
    result.timings={draw->clips.at("breathe").duration,draw->clips.at("throw").duration,
        draw->clips.at("knockback").duration,draw->clips.at("knockdown").duration};
    output=std::move(result);return true;
}

[[maybe_unused]] malfoy::Step AdvanceReturnMalfoy(ReturnMalfoyRuntime& state,std::vector<CharacterDraw>& actors,
    MapEventGraph& graph,float seconds,const std::array<float,3>& player){
    const auto actor=std::ranges::find_if(actors,[&](const auto& a){return a.actor_reference==state.actor;});
    if(actor==actors.end()||state.motion.phase==malfoy::Phase::Idle||
       state.motion.phase==malfoy::Phase::Complete||state.motion.phase==malfoy::Phase::Lost)return {};
    const auto out=malfoy::Advance(state.motion,state.config,state.timings,seconds);
    actor->cutscene_offset=SubtractVector(state.motion.position,actor->base_origin);
    actor->active_clip=out.clip;actor->animation_time=out.clip_seconds;actor->animation_loop=out.loop;
    const auto delta=SubtractVector(player,state.motion.position);
    if(std::hypot(delta[0],delta[2])>.00001F)actor->desired_yaw=std::atan2(delta[0],delta[2]);
    if(out.first_throw)(void)graph.Dispatch(state.first_throw);
    if(out.defeated)(void)graph.Dispatch(state.defeated);
    return out;
}

struct ReturnAppleVisual { std::uint32_t first=0,count=0; };
bool LoadReturnScroll(const std::filesystem::path& root,std::vector<GpuVertex>& vertices,
    std::vector<std::uint8_t>& pixels,std::uint32_t& layers,ReturnAppleVisual& output){
    const auto package=root/"system/HarryPotter.u";
    const auto table=wand::inspect_hp1_package_link_table(package);
    if(table.status!=wand::Hp1ProfileStatus::ok)return false;
    const auto entry=std::ranges::find_if(table.exports,[](const auto& e){return
        e.qualified_class_name=="Engine.SkeletalMesh"&&!e.object_path.empty()&&AsciiFold(e.object_path.back())=="hedwigsscrollmesh";});
    if(entry==table.exports.end())return false;
    LoadedStaticMesh mesh;if(!LoadStaticMesh(package.string(),entry->reference,layers,&mesh))return false;
    if(layers+mesh.report.texture_layer_count>kMaximumCombinedTextureLayers)return false;
    output={static_cast<std::uint32_t>(vertices.size()),static_cast<std::uint32_t>(mesh.vertices.size())};
    for(const auto& v:mesh.vertices)vertices.push_back({{v.position_m[0],v.position_m[1],v.position_m[2]},
        {v.texture_uv[0],v.texture_uv[1]},{0,0},layers+v.texture_layer,v.polygon_flags,0,0x00c0c0c0U});
    pixels.insert(pixels.end(),mesh.textures.begin(),mesh.textures.end());layers+=mesh.report.texture_layer_count;
    return true;
}
bool ReturnCarrying(const ReturnMalfoyRuntime& state){return std::ranges::any_of(state.crackers,[](const auto& c){return c.phase==cracker::Phase::Carried;});}
bool UpdateReturnHand(ReturnMalfoyRuntime& state,const std::array<float,3>& hand,const std::array<float,3>& aim,bool active,bool held){
    state.hand_valid=active;state.hand=hand;state.aim=aim;
    const bool carrying=ReturnCarrying(state);
    bool released=false;
    if(active&&carrying){
        if(held&&!state.trigger_held)state.throw_armed=true;
        if(!held&&state.trigger_held&&state.throw_armed)for(auto& c:state.crackers)if(c.phase==cracker::Phase::Carried){
            released=cracker::Release(c,hand,aim);
            const auto target=AddVector(state.motion.position,{0,.7F,0});
            const auto delta=SubtractVector(target,hand);
            const float distance=std::sqrt(DotVector(delta,delta)),aim_length=std::sqrt(DotVector(aim,aim));
            if(released&&distance>.1F&&aim_length>.001F&&DotVector(delta,aim)>distance*aim_length*.94F)
                c.velocity=peeves::BallisticVelocity(hand,target,11.F,cracker::kGravity);
            break;
        }
    }
    if(!active||!carrying||!held)state.throw_armed=false;
    state.trigger_held=held;return released;
}
struct ReturnDuelStep { unsigned damage=0;bool thrown=false,picked_up=false,exploded=false,landed=false; };
ReturnDuelStep AdvanceReturnDuel(ReturnMalfoyRuntime& state,std::vector<CharacterDraw>& actors,MapEventGraph& graph,
    float seconds,const std::vector<CollisionTriangle>& triangles,const std::array<float,3>& player){
    ReturnDuelStep result;
    const auto actor=std::ranges::find_if(actors,[&](const auto& a){return a.actor_reference==state.actor;});
    if(actor==actors.end()||state.motion.phase==malfoy::Phase::Idle||state.motion.phase==malfoy::Phase::Lost)return result;
    const auto step=AdvanceReturnMalfoy(state,actors,graph,seconds,player);
    if(state.motion.phase==malfoy::Phase::Complete){state.crackers={};return result;}
    auto boss=AddVector(actor->collision_center,actor->cutscene_offset);
    boss[1]=(actor->collision_min_y+actor->collision_max_y)*.5F+actor->cutscene_offset[1];
    if(step.throw_projectile){for(auto& c:state.crackers)if(!cracker::Active(c)){
        result.thrown=cracker::Spawn(c,boss,player,step.projectile_fuse);break;
    }}
    bool carrying=ReturnCarrying(state);
    for(auto& c:state.crackers){
        if(c.phase==cracker::Phase::Carried){
            if(state.hand_valid)c.position=state.hand;
            else {c.position=player;c.position[1]+=.3F;}
        }
        if(c.phase==cracker::Phase::Ground&&!carrying){
            const auto d=SubtractVector(c.position,player);
            if(std::hypot(d[0],d[2])<kPlayerCapsuleRadiusMeters+.15F&&
               std::abs(d[1])<kPlayerCapsuleHalfHeightMeters+.15F&&
               !grid_motion::Sweep(triangles,{c.position,c.position},ScaleVector(d,-1),0,0).found){
                carrying=cracker::PickUp(c);result.picked_up=carrying;state.throw_armed=false;
            }
        }
        const auto impact=cracker::Advance(c,seconds,triangles,player,boss,kPlayerCapsuleRadiusMeters,kPlayerCapsuleHalfHeightMeters);
        result.damage+=impact.player_damage;result.exploded|=impact.exploded;result.landed|=impact.landed;
        if(impact.boss_hit)(void)malfoy::Hit(state.motion,state.config);
    }
    return result;
}
std::string SaveReturnDuel(const ReturnMalfoyRuntime& state){
    const auto& m=state.motion;std::ostringstream out;
    out<<"MALFOY 1 "<<std::setprecision(9)<<static_cast<unsigned>(m.phase)<<' '<<m.elapsed<<' '<<m.hits<<' '<<m.volley<<' '
        <<m.projectile_count<<' '<<m.random<<' '<<m.moving_left<<' '<<m.direction_change<<' '<<m.first_throw<<' '<<m.emitted;
    for(float v:m.position)out<<' '<<v;for(float v:m.target)out<<' '<<v;
    unsigned count=0;for(const auto& c:state.crackers)count+=cracker::Active(c);out<<' '<<count;
    for(const auto& c:state.crackers)if(cracker::Active(c)){
        out<<' '<<static_cast<unsigned>(c.phase)<<' '<<c.fuse<<' '<<c.age<<' '<<c.returned;
        for(float v:c.position)out<<' '<<v;for(float v:c.velocity)out<<' '<<v;
    }
    return out.str();
}
bool RestoreReturnDuel(ReturnMalfoyRuntime& state,const std::string& text){
    if(text.size()>8192||!malfoy::Valid(state.config))return false;
    auto candidate=state;candidate.motion={};candidate.crackers={};candidate.hand_valid=false;candidate.throw_armed=false;candidate.trigger_held=false;
    auto& m=candidate.motion;std::istringstream in(text);std::string magic;unsigned version=0,phase=0,count=0,carried=0;
    in>>magic>>version>>phase>>m.elapsed>>m.hits>>m.volley>>m.projectile_count>>m.random>>m.moving_left>>m.direction_change>>m.first_throw>>m.emitted;
    for(auto& v:m.position)in>>v;for(auto& v:m.target)in>>v;
    if(!in||magic!="MALFOY"||version!=1||phase>static_cast<unsigned>(malfoy::Phase::Lost)||
       !std::isfinite(m.elapsed)||m.elapsed<0||m.elapsed>3600||m.hits>candidate.config.maximum_hits||m.volley>5||
       m.projectile_count>2||!m.random||!malfoy::Finite(m.position)||!malfoy::Finite(m.target))return false;
    m.phase=static_cast<malfoy::Phase>(phase);
    if((m.phase==malfoy::Phase::Complete||m.phase==malfoy::Phase::Knockdown||m.phase==malfoy::Phase::DefeatPause)&&m.hits!=candidate.config.maximum_hits)return false;
    if(malfoy::Active(m)&&m.hits>=candidate.config.maximum_hits)return false;
    in>>count;if(!in||count>cracker::kCapacity||((m.phase==malfoy::Phase::Idle||m.phase==malfoy::Phase::Complete)&&count))return false;
    for(unsigned i=0;i<count;++i){auto& c=candidate.crackers[i];unsigned p=0;
        in>>p>>c.fuse>>c.age>>c.returned;for(auto& v:c.position)in>>v;for(auto& v:c.velocity)in>>v;
        if(!in||p<1||p>3||!std::isfinite(c.fuse)||c.fuse<=0||c.fuse>5||!std::isfinite(c.age)||c.age<0||c.age>36000||
           !malfoy::Finite(c.position)||!malfoy::Finite(c.velocity))return false;
        c.phase=static_cast<cracker::Phase>(p);carried+=c.phase==cracker::Phase::Carried;
        if(c.phase==cracker::Phase::Carried&&c.fuse<1)return false;
    }
    in>>std::ws;if(!in.eof()||carried>1)return false;
    state=std::move(candidate);return true;
}
struct ReturnCrackerVisual {
    std::uint32_t first=0,count=0,total=0;
    std::map<std::string,CharacterClip> clips;
};
bool LoadReturnCracker(const std::filesystem::path& root,std::vector<GpuVertex>& vertices,
    std::vector<std::uint8_t>& pixels,std::uint32_t& layers,ReturnCrackerVisual& output){
    const auto package=root/"system/HPModels.u";
    const auto table=wand::inspect_hp1_package_link_table(package);
    if(table.status!=wand::Hp1ProfileStatus::ok)return false;
    const auto entry=std::ranges::find_if(table.exports,[](const auto& e){return
        e.qualified_class_name=="Engine.SkeletalMesh"&&!e.object_path.empty()&&AsciiFold(e.object_path.back())=="skwizardcrackermesh";});
    if(entry==table.exports.end())return false;
    LoadedStaticMesh mesh;if(!LoadStaticMesh(package.string(),entry->reference,layers,&mesh))return false;
    const auto skin=wand::load_hp1_skeletal_skin(package,entry->reference);
    if(skin.status!=wand::Hp1ProfileStatus::ok)return false;
    const auto animation=wand::load_hp1_animation(package,skin.census.animation_reference);
    if(animation.status!=wand::Hp1ProfileStatus::ok||layers+mesh.report.texture_layer_count>kMaximumCombinedTextureLayers)return false;
    ReturnCrackerVisual result;result.first=vertices.size();result.count=mesh.vertices.size();
    for(const auto* name:{"flying","hit","swell","shake"}){
        const std::string_view source=(std::string_view(name)=="flying"||std::string_view(name)=="hit")?"idle":name;
        const auto sequence=std::ranges::find_if(animation.sequences,[&](const auto& s){return AsciiFold(s.name)==source;});
        if(sequence==animation.sequences.end())return false;
        const auto index=static_cast<std::size_t>(sequence-animation.sequences.begin());
        if(index>=animation.moves.size())return false;
        const float duration=animation.moves[index].track_time;
        if(!std::isfinite(duration)||duration<=0)return false;
        const unsigned frames=static_cast<unsigned>(std::clamp(std::ceil(duration*30),2.F,128.F));
        CharacterClip clip{static_cast<std::uint32_t>(vertices.size()),duration,frames};
        for(unsigned frame=0;frame<frames;++frame){
            const auto pose=wand::sample_hp1_skeletal_animation(skin,animation,index,duration*frame/frames);
            if(pose.status!=wand::Hp1ProfileStatus::ok)return false;
            for(const auto& v:mesh.vertices){
                if(v.point_index>=pose.points.size())return false;const auto& p=pose.points[v.point_index];
                vertices.push_back({{p.y*.0075F,p.z*.0075F,p.x*.0075F},{v.texture_uv[0],v.texture_uv[1]},
                    {0,0},layers+v.texture_layer,v.polygon_flags,0,0x00a0a0a0U});
            }
        }
        result.clips.emplace(name,clip);
    }
    result.total=vertices.size()-result.first;
    pixels.insert(pixels.end(),mesh.textures.begin(),mesh.textures.end());layers+=mesh.report.texture_layer_count;
    output=std::move(result);return true;
}
std::string SaveReturnBattle(const ReturnRuntime& runtime){
    const auto& b=runtime.battle;const auto& m=b.motion;
    std::ostringstream out;out<<"PEEVES 1 "<<std::setprecision(9)<<static_cast<unsigned>(m.phase)<<' '
        <<m.hits_left<<' '<<m.taunt<<' '<<m.elapsed<<' '<<m.contact_cooldown<<' '<<m.completion_sent<<' ';
    for(float v:b.position)out<<v<<' ';
    out<<b.next_patrol<<' '<<b.waypoint<<' '<<b.entrance_flown<<' '<<b.flight_started<<' ';
    unsigned count=0;for(const auto& a:runtime.apples)count+=a.active;out<<count;
    for(const auto& a:runtime.apples)if(a.active){
        for(float v:a.position)out<<' '<<v;for(float v:a.velocity)out<<' '<<v;
        out<<' '<<a.age<<' '<<a.fuse<<' '<<a.settled;
    }
    return out.str();
}
bool RestoreReturnBattle(ReturnRuntime& runtime,const std::string& saved){
    if(saved.size()>8192||!peeves::ValidRoute(runtime.route))return false;
    auto candidate=runtime;candidate.battle={};candidate.apples={};
    auto& b=candidate.battle;auto& m=b.motion;
    std::istringstream in(saved);std::string magic;unsigned version=0,phase=0,count=0;
    in>>magic>>version>>phase>>m.hits_left>>m.taunt>>m.elapsed>>m.contact_cooldown>>m.completion_sent;
    for(float& v:b.position)in>>v;
    in>>b.next_patrol>>b.waypoint>>b.entrance_flown>>b.flight_started>>count;
    if(!in||magic!="PEEVES"||version!=1||phase>static_cast<unsigned>(peeves::Phase::Complete)||
       m.hits_left>peeves::kMaximumHits||m.taunt>3||!std::isfinite(m.elapsed)||m.elapsed<0||m.elapsed>=3600||
       !std::isfinite(m.contact_cooldown)||m.contact_cooldown<0||m.contact_cooldown>peeves::kContactCooldown||
       !peeves::FinitePoint(b.position)||b.next_patrol>=runtime.route.patrol.size()||count>candidate.apples.size())return false;
    m.phase=static_cast<peeves::Phase>(phase);
    const bool ending=m.phase>=peeves::Phase::DepartureDelay;
    const bool sent=m.phase>=peeves::Phase::DeparturePause;
    const bool flying=m.phase==peeves::Phase::Patrol||m.phase==peeves::Phase::Departing;
    if(m.completion_sent!=sent||(ending&&m.hits_left!=0)||
       (!ending&&m.phase!=peeves::Phase::Hit&&m.hits_left==0)||
       (m.phase==peeves::Phase::Dormant&&(m.hits_left!=peeves::kMaximumHits||m.elapsed!=0||count||b.entrance_flown))||
       (b.flight_started&&!flying)||(!b.flight_started&&b.waypoint))return false;
    const auto& path=m.phase==peeves::Phase::Departing?runtime.route.departure:
        !b.entrance_flown?runtime.route.entrance:runtime.route.patrol[b.next_patrol];
    if(b.flight_started&&b.waypoint>=path.size())return false;
    for(unsigned i=0;i<count;++i){
        auto& a=candidate.apples[i];a.active=true;
        for(float& v:a.position)in>>v;for(float& v:a.velocity)in>>v;
        in>>a.age>>a.fuse>>a.settled;
        if(!in||!peeves::FinitePoint(a.position)||!peeves::FinitePoint(a.velocity)||
           !std::isfinite(a.age)||a.age<0||a.age>4||!std::isfinite(a.fuse)||a.fuse<=0||a.fuse>3||
           (a.settled&&a.velocity!=peeves::Point{}))return false;
    }
    in>>std::ws;if(!in.eof())return false;
    candidate.voice.clear();candidate.voiced_phase=m.phase;candidate.voiced_hits=m.hits_left;
    runtime=std::move(candidate);return true;
}

bool LoadReturnApple(const std::filesystem::path& root,std::vector<GpuVertex>& vertices,
    std::vector<std::uint8_t>& pixels,std::uint32_t& layers,ReturnAppleVisual& output){
    const auto package=root/"system/HPBase.u";
    const auto table=wand::inspect_hp1_package_link_table(package);
    if(table.status!=wand::Hp1ProfileStatus::ok)return false;
    const auto type=std::ranges::find_if(table.exports,[](const auto& e){
        return e.qualified_class_name=="Core.Class"&&!e.object_path.empty()&&AsciiFold(e.object_path.back())=="spellpeevesthrow";
    });
    if(type==table.exports.end())return false;
    const auto defaults=wand::inspect_hp1_class_visual_defaults(package,type->reference);
    LoadedStaticMesh mesh;
    if(defaults.status!=wand::Hp1ProfileStatus::ok||defaults.mesh_reference<=0)return false;
    if(!LoadStaticMesh(package.string(),defaults.mesh_reference,layers,&mesh)){
        HPVR_LOGE("[hpvr.quest.peeves.apple] status=MESH_FAILED ref=%d",defaults.mesh_reference);return false;
    }
    if(layers+mesh.report.texture_layer_count>kMaximumCombinedTextureLayers)return false;
    ReturnAppleVisual result{static_cast<std::uint32_t>(vertices.size()),static_cast<std::uint32_t>(mesh.vertices.size())};
    for(const auto& v:mesh.vertices)vertices.push_back({
        {v.position_m[0]*2,v.position_m[1]*2,v.position_m[2]*2},
        {v.texture_uv[0],v.texture_uv[1]},{0,0},layers+v.texture_layer,v.polygon_flags,0,0x00a0a0a0U});
    pixels.insert(pixels.end(),mesh.textures.begin(),mesh.textures.end());layers+=mesh.report.texture_layer_count;
    output=result;return true;
}

bool ConfigureReturnPeeves(ReturnRuntime& output,const ReturnMetadata& metadata,
    const std::vector<CharacterDraw>& actors){
    const auto actor=std::ranges::find_if(actors,[&](const auto& a){return a.actor_reference==metadata.peeves_actor;});
    if(actor==actors.end())return false;
    ReturnRuntime result;result.metadata=metadata;
    const auto path=[](const ReturnFlightLeg& leg){return leg.waypoints.empty()?peeves::Flight{leg.first_position,leg.station_position}:leg.waypoints;};
    result.route.entrance=path(metadata.entrance);result.route.departure=path(metadata.departure);
    for(const auto& leg:metadata.patrol)result.route.patrol.push_back(path(leg));
    if(!peeves::ValidRoute(result.route))return false;
    for(const auto* name:{"breathe","grab","float","attackfloat","scheming","look","throwobject1","throwobject2","hit"}){
        const auto clip=actor->clips.find(name);
        if(clip==actor->clips.end()||!std::isfinite(clip->second.duration)||clip->second.duration<=0)return false;
    }
    const auto duration=[&](const char* name){return actor->clips.at(name).duration;};
    result.timings={duration("grab"),duration("scheming"),duration("look"),duration("throwobject1"),duration("throwobject2"),duration("hit")};
    output=std::move(result);return true;
}

bool TriggerReturnPeeves(ReturnRuntime& state,std::vector<CharacterDraw>& actors){
    const auto actor=std::ranges::find_if(actors,[&](const auto& a){return a.actor_reference==state.metadata.peeves_actor;});
    if(actor==actors.end()||!peeves::Activate(state.battle,AddVector(actor->base_origin,actor->cutscene_offset),state.route))return false;
    actor->enabled=true;actor->active_clip="grab";actor->animation_time=0;actor->animation_loop=false;
    return true;
}

void ApproachReturnPeevesCamera(ReturnRuntime& state,const std::array<float,3>& eye,float yaw,float seconds){
    if(state.battle.motion.phase!=peeves::Phase::Grab)return;
    auto destination=AddVector(eye,RotateYaw({0,-.3F,-.6F},yaw));
    const auto delta=SubtractVector(destination,state.battle.position);
    const float distance=std::sqrt(DotVector(delta,delta));
    if(distance>.08F){
        state.battle.position=AddVector(state.battle.position,ScaleVector(delta,std::min(distance,10.F*seconds)/distance));
        if(distance>10.F*seconds+.08F)state.battle.motion.elapsed=0;
    }
}

void OrientReturnStudentDoors(std::vector<DoorDraw>& doors){
    // These leaves must swing into the corridor, away from the approaching pupil.
    for(auto& door:doors)if(door.tag=="stageleft"&&door.motion.count==2){
        if(door.actor_reference==2279)door.motion.keys[1].rotation_units[1]=16384;
        else if(door.actor_reference==1603)door.motion.keys[1].rotation_units[1]=-16384;
        else continue;
        // Saved intermediate poses use the same swing direction as the new keys.
        for(auto* pose:{&door.motion.source,&door.motion.pose})
            pose->rotation_units[1]=std::copysign(std::abs(pose->rotation_units[1]),door.motion.keys[1].rotation_units[1]);
    }
}
bool UpdateMerchantApproach(bool& armed,bool controlled,float distance){
    if(controlled||!std::isfinite(distance)){armed=false;return false;}
    if(distance>2.3F)armed=true;
    return armed&&distance>=.01F&&distance<=1.9F;
}
std::vector<std::size_t> ReturnContactOrder(const std::vector<CharacterDraw>& actors,const std::array<float,3>& player,
                                         std::int32_t merchant,bool can_purchase){
    std::vector<std::size_t> order;
    for(std::size_t i=0;i<actors.size();++i)order.push_back(i);
    const auto rank=[&](std::size_t i){
        auto position=AddVector(actors[i].base_origin,actors[i].cutscene_offset);position[1]+=1.2F;
        const auto delta=SubtractVector(position,player);
        return std::pair{can_purchase&&actors[i].actor_reference==merchant?0:1,DotVector(delta,delta)};
    };
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return rank(a)<rank(b);});return order;
}
bool ReleaseReturnCameraTail(IntroCutscene& scene){
    if(scene.object_name!="cutscene4"||!scene.harry_released||!scene.cues.contains("cutscenedone")||!scene.camera_active)return false;
    if(std::ranges::any_of(scene.tracks,[](const auto& t){return t.dialogue_waiting;}))return false;
    // Off-screen camera travel must not retain first-person control after
    // the authored departure cue. Background scripts still finish normally.
    scene.camera_active=false;return true;
}
void FaceReturnCinematicTarget(const IntroCutscene& scene,std::vector<CharacterDraw>& actors){
    auto focus=CinematicTarget(scene,actors);
    if(scene.object_name=="cutscene8"){
        // The first mark aligns Harry with the doorway. Keep his first-person
        // gaze on the exit even while that short alignment move runs.
        for(const auto& loc:scene.locations)if(AsciiFold(loc.alias)=="locname1")focus=loc.position;
    }
    for(const auto& track:scene.tracks)if(!track.camera&&track.dialogue_waiting)
        for(const auto& actor:actors)if(actor.actor_reference==track.speaking_actor&&!actor.player)
            focus=AddVector(AddVector(actor.base_origin,actor.cutscene_offset),{0,.9F,0});
    for(auto& actor:actors)if(actor.player){
        const auto origin=AddVector(actor.base_origin,actor.cutscene_offset);
        if(!focus||std::hypot((*focus)[0]-origin[0],(*focus)[2]-origin[2])<.4F){
            for(const auto& track:scene.tracks)if(!track.camera&&track.actor_reference!=actor.actor_reference&&!track.finished&&track.moving){
                focus=track.position;break;
            }
        }
        if(focus){const auto d=SubtractVector(*focus,origin);
            if(std::hypot(d[0],d[2])>.4F)actor.yaw=actor.desired_yaw=std::atan2(d[0],d[2]);
        }
    }
}

peeves::BattleStep AdvanceReturnPeeves(ReturnRuntime& state,std::vector<CharacterDraw>& actors,
    MapEventGraph& graph,float seconds,const std::array<float,3>& player){
    const auto actor=std::ranges::find_if(actors,[&](const auto& a){return a.actor_reference==state.metadata.peeves_actor;});
    if(actor==actors.end()||state.battle.motion.phase==peeves::Phase::Dormant)return {};
    // Flight coordinates are actor origins; the player's capsule overlaps the
    // boss vertically above that origin, not at the rendered model's feet.
    auto contact_center=player;
    contact_center[1]-=(actor->collision_min_y+actor->collision_max_y)*.5F-actor->base_origin[1];
    auto out=peeves::Advance(state.battle,state.route,seconds,state.timings,contact_center,
        actor->collision_radius+kPlayerCapsuleRadiusMeters);
    state.voice.clear();
    const auto& motion=state.battle.motion;
    if(seconds>0&&(motion.phase!=state.voiced_phase||motion.hits_left!=state.voiced_hits)){
        if(motion.phase==peeves::Phase::Taunt)
            state.voice=std::array<const char*,4>{"111Peeves3","111Peeves2","111Peeves4","111Peeves5"}[motion.taunt%4];
        else if(motion.phase==peeves::Phase::Patrol)state.voice="EmotivePeeves"+std::to_string(19+state.battle.next_patrol%4);
        else if(motion.phase==peeves::Phase::Hit)state.voice="EmotivePeeves"+std::to_string(31+(peeves::kMaximumHits-motion.hits_left-1)%4);
        else if(out.presentation.completed)state.voice="111Peeves7";
        state.voiced_phase=motion.phase;state.voiced_hits=motion.hits_left;
    }
    const auto delta=SubtractVector(state.battle.position,AddVector(actor->base_origin,actor->cutscene_offset));
    actor->cutscene_offset=SubtractVector(state.battle.position,actor->base_origin);
    actor->active_clip=out.presentation.clip;actor->animation_time=out.presentation.clip_seconds;
    actor->animation_loop=out.presentation.loop;
    actor->enabled=state.battle.motion.phase!=peeves::Phase::Complete;
    actor->collision_disabled=!peeves::Vulnerable(state.battle.motion);
    const auto facing=out.presentation.moving?delta:SubtractVector(player,state.battle.position);
    if(std::hypot(facing[0],facing[2])>.00001F)actor->desired_yaw=std::atan2(facing[0],facing[2]);
    if(out.presentation.completed)(void)graph.Signal(state.metadata.peeves_actor);
    if(out.presentation.throw_projectile){
        auto origin=AddVector(actor->collision_center,actor->cutscene_offset);
        origin[1]=(actor->collision_min_y+actor->collision_max_y)*.5F+actor->cutscene_offset[1];
        for(auto& apple:state.apples)if(!apple.active){(void)peeves::Throw(apple,origin,player);break;}
    }
    return out;
}
