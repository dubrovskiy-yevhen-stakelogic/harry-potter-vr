// Shared CPU scene preparation for the owned Flipendo challenge map.
// Included inside quest_scene.cpp's private namespace; also exercised by host probes.
struct ChallengeProp {
    std::int32_t reference=0;
    std::string name;
    std::uint32_t first=0,count=0;
    std::array<float,3> minimum{},maximum{};
    bool breakable=false;
    bool spell_target=false;
    std::uint32_t animation_first=0,animation_frames=0,settled_first=0,settled_frames=0,broken_first=0,broken_count=0;
    float animation_duration=1,settled_duration=1;
    bool cauldron=false,savebook=false;
    bool cauldron_tip_valid=false; // Derived after loading; never serialized in the prepared cache.
    std::array<float,3> cauldron_mouth{},cauldron_direction{};
    float activation_time=-1; // Transient. Activated checkpoints restore the completed visual state.
};
std::pair<std::uint32_t,std::uint32_t> ChallengePropDrawRange(const ChallengeProp& prop,bool activated,float scene_time){
    if(prop.savebook&&activated)return {0,0};
    if(prop.breakable&&activated)return {prop.broken_first,prop.broken_count};
    if(prop.cauldron&&activated){
        if(prop.activation_time>=0&&prop.activation_time<prop.animation_duration&&prop.animation_frames){
            const auto frame=std::min(prop.animation_frames-1,static_cast<unsigned>(prop.activation_time/prop.animation_duration*prop.animation_frames));
            return {prop.animation_first+frame*prop.count,prop.count};
        }
        if(prop.settled_frames){
            const float elapsed=std::max(0.0F,scene_time);
            const auto frame=static_cast<unsigned>(std::fmod(elapsed,prop.settled_duration)/prop.settled_duration*prop.settled_frames);
            return {prop.settled_first+std::min(frame,prop.settled_frames-1)*prop.count,prop.count};
        }
    }
    return {prop.first,prop.count};
}
std::array<float,3> ChallengePropOffset(const ChallengeProp& prop,float scene_time){
    // HPBase.baseProps: authored Z + fBobAmount + 3.5 + fBobAmount*sin(8*t), in Unreal units.
    return prop.savebook?std::array<float,3>{0,.10F+.03F*std::sin(8*scene_time),0}:std::array<float,3>{};
}
struct ChallengeSpatial {
    std::int32_t reference=0;
    std::string name,tag,event;
    std::array<float,3> position{};
    float radius=.6F,height=.7F;
    bool spell=false,inside=false,checkpoint=false;
    float arm_time=1;bool armed=false; // SavePoint's initial proximity guard; not checkpoint data.
};
// CutScene50 swaps Quirrell4 into the offstage OutQ mark and places
// AfterBridgeQ (Quirrell1) at NewQLoc. Do not ground the retired clone
// back into the room, or expose the waiting replacement through the gate.
void SetBridgeProfessor(std::vector<CharacterDraw>& actors,bool swapped){
    for(auto& actor:actors){
        if(actor.actor_reference==2047)actor.enabled=!swapped;
        if(actor.actor_reference==2386)actor.enabled=swapped;
    }
}
struct ChallengeRuntime {
    MapEventGraph graph;
    zones::Query zones;
    std::array<float,3> source_origin{};
    float source_yaw=0;
    std::map<std::int32_t,IntroCutscene> scenes;
    std::vector<ChallengeSpatial> spatial;
    std::vector<ChallengeProp> props;
    std::vector<std::vector<CollisionTriangle>> mover_triangles;
    std::vector<std::int32_t> pending_scenes;
    std::map<std::int32_t,unsigned> gnome_hits;
    std::map<std::int32_t,gnome::Motion> gnome_motion;
    grid_push::PendingHits grid_hit_positions;
    std::set<std::int32_t> gnome_active;
    std::array<std::array<float,3>,4> barrel_route{};
    unsigned barrel_stage=0;
    float barrel_time=0,hurt_time=0;
    std::int32_t active_scene=0,impact_actor=0;
    std::size_t collision_base=0;
    bool complete=false,collision_dirty=false,checkpoint_pending=false;
    bool authored_checkpoint_pending=false;
};
bool ChallengeCollected(const ProgressSave& p,std::int32_t ref){
    return std::ranges::binary_search(p.collected_beans,ref);
}
void RememberChallengeEvent(ProgressSave& p,std::int32_t ref){
    if(ref>0&&!std::ranges::binary_search(p.activated_events,ref))
        p.activated_events.insert(std::lower_bound(p.activated_events.begin(),p.activated_events.end(),ref),ref);
}
bool ChallengeActivated(const ProgressSave& p,std::int32_t ref){
    return std::ranges::binary_search(p.activated_events,ref);
}
bool ChallengeRewardsReady(const std::vector<ChallengeProp>& props,std::int32_t source,const ProgressSave& progress){
    if(!source)return true;
    if(!ChallengeActivated(progress,source))return false;
    const auto prop=std::ranges::find_if(props,[&](const auto& p){return p.reference==source;});
    return prop==props.end()||!prop->cauldron||prop->activation_time>=prop->animation_duration;
}
float ChallengeBeanSweepFraction(const std::vector<CollisionTriangle>& collision,const ChallengeProp& source,
    const std::array<float,3>& from,const std::array<float,3>& to){
    const auto delta=SubtractVector(to,from);const float length=std::sqrt(DotVector(delta,delta));
    if(length<.00001F)return 1;
    float fraction=1;
    constexpr float radius=.065F;
    constexpr std::array<std::array<float,3>,7> offsets{{{0,0,0},{radius,0,0},{-radius,0,0},
        {0,radius,0},{0,-radius,0},{0,0,radius},{0,0,-radius}}};
    for(const auto& triangle:collision){
        bool overlap=true,source_triangle=true;
        for(unsigned axis=0;axis<3;++axis){
            if(triangle.maximum[axis]<std::min(from[axis],to[axis])-radius||
               triangle.minimum[axis]>std::max(from[axis],to[axis])+radius)overlap=false;
            if(triangle.minimum[axis]<source.minimum[axis]-.002F||triangle.maximum[axis]>source.maximum[axis]+.002F)source_triangle=false;
        }
        // A bean is created inside its source. Ignore only triangles wholly
        // contained by that source's bounds, not nearby floor/wall surfaces.
        if(!overlap||source_triangle)continue;
        const auto e1=SubtractVector(triangle.vertices[1],triangle.vertices[0]);
        const auto e2=SubtractVector(triangle.vertices[2],triangle.vertices[0]);
        const auto cross=CrossVector(delta,e2);const float determinant=DotVector(e1,cross);
        if(std::abs(determinant)<1e-8F)continue;
        for(const auto& offset:offsets){
            const auto relative=SubtractVector(AddVector(from,offset),triangle.vertices[0]);
            const float u=DotVector(relative,cross)/determinant;if(u<0||u>1)continue;
            const auto q=CrossVector(relative,e1);const float v=DotVector(delta,q)/determinant;if(v<0||u+v>1)continue;
            const float hit=DotVector(e2,q)/determinant;
            if(hit>=-.00001F&&hit<=1)fraction=std::min(fraction,std::max(0.0F,hit-.012F/length));
        }
    }
    return fraction;
}
void PrepareChallengeBeanEmission(BeanDraw& bean,const ChallengeProp& source,const std::vector<CollisionTriangle>& collision,
    const std::array<float,3>* approach=nullptr){
    auto anchor=ScaleVector(AddVector(source.minimum,source.maximum),.5F);
    anchor[1]=std::clamp(bean.emission[1],source.minimum[1]+.12F,std::max(source.minimum[1]+.12F,source.maximum[1]));
    auto desired_origin=bean.emission,desired_landing=bean.position;
    if(source.cauldron&&source.cauldron_tip_valid){
        anchor=source.cauldron_mouth;
        auto lateral=SubtractVector(bean.emission,anchor);lateral[1]=0;
        lateral=SubtractVector(lateral,ScaleVector(source.cauldron_direction,DotVector(lateral,source.cauldron_direction)));
        const float width=std::hypot(lateral[0],lateral[2]);
        if(width>.30F)lateral=ScaleVector(lateral,.30F/width);
        desired_origin=AddVector(AddVector(anchor,lateral),ScaleVector(source.cauldron_direction,.07F));
        auto travel=SubtractVector(bean.position,bean.emission);travel[1]=0;
        desired_landing=AddVector(desired_origin,travel);
    }
    const auto sweep=[&](const auto& a,const auto& b){return ChallengeBeanSweepFraction(collision,source,a,b);};
    if(approach){
        const auto toward=SubtractVector(anchor,*approach);
        if(DotVector(toward,toward)<64){
            const float fraction=sweep(*approach,anchor);
            if(fraction<1)anchor=AddVector(*approach,ScaleVector(toward,fraction));
        }
    }
    const auto origin=AddVector(anchor,ScaleVector(SubtractVector(desired_origin,anchor),sweep(anchor,desired_origin)));
    const auto ground=[&](const auto& point){
        auto result=point;float floor=0;
        if(FindPropGroundBelow(collision,AddVector(point,{0,.05F,0}),&floor)&&floor<=point[1]&&floor>=point[1]-4)
            result[1]=std::min(point[1],floor+.18F);
        return result;
    };
    // Cached reward Y may have been grounded on the old wall-facing side.
    // Re-query at the restored destination for vases as well as cauldrons.
    desired_landing=ground(desired_landing);
    bean.emission_path=props::BuildEmissionPath(origin,desired_landing,sweep,ground);
    bean.emission_points=static_cast<unsigned>(bean.emission_path.size());
    bean.emission=bean.emission_path.front();bean.position=bean.emission_path.back();
}
std::array<float,3> MoverPoint(const DoorDraw& d,const std::array<float,3>& p){
    if(d.challenge)return AddVector(movers::TransformPoint(p,d.placement,d.motion.pose),d.grid_offset);
    return AddVector(AddVector(d.pivot,RotateYaw(SubtractVector(p,d.pivot),d.open_yaw*d.phase)),
        AddVector(ScaleVector(d.open_offset,d.phase),d.grid_offset));
}
bool LoadChallengeProps(const std::filesystem::path& root,const std::filesystem::path& map,
    const hpvr_hp1_player_start_report& start,float yaw,const std::vector<SceneLight>& lights,
    std::vector<GpuVertex>& vertices,std::vector<std::uint8_t>& pixels,std::uint32_t& layers,
    std::vector<CollisionTriangle>& collision,std::vector<ChallengeProp>& props,
    std::vector<FlameEmitter>& flames,std::vector<GlowEmitter>& glows){
    const auto manifest=wand::build_hp1_character_manifest(root,map,0,{},true);
    if(manifest.status!=wand::Hp1ProfileStatus::ok)return false;
    broom::LessonMetadata flight;
    if(AsciiFold(map.stem().string())=="lev_tut2"){
        flight=broom::LoadLessonMetadata(root,wand::inspect_hp1_actor_visuals(map));
        if(!flight.valid)return false;
    }
    std::map<std::vector<std::string>,LoadedStaticMesh> meshes;
    std::map<std::vector<std::string>,std::pair<wand::Hp1SkeletalSkin,wand::Hp1Animation>> animations;
    auto ordered=manifest.actors;
    const auto support_rank=[](const auto& actor){
        const auto cls=AsciiFold(actor.qualified_class_name);
        if(cls.ends_with("table"))return 0;
        if(cls.find("candle")!=std::string::npos||cls.find("book")!=std::string::npos)return 2;
        return 1;
    };
    std::stable_sort(ordered.begin(),ordered.end(),[&](const auto& a,const auto& b){return support_rank(a)<support_rank(b);});
    for(const auto& a:ordered){
        const auto cls=AsciiFold(a.qualified_class_name);
        const bool savebook=cls=="harrypotter.savepoint";
        if((!cls.starts_with("hprops.")&&!savebook)||cls.ends_with("bean")||cls=="hprops.star"||
           (flight.valid&&cls=="hprops.wcmerlin"))continue;
        std::vector<std::string> key{a.mesh_package.string(),std::to_string(a.mesh_reference)};
        for(const auto& skin:a.skins){
            key.push_back(std::to_string(skin.material_slot));
            key.push_back(skin.package.string());key.push_back(std::to_string(skin.reference));
        }
        if(!meshes.contains(key)){
            LoadedStaticMesh mesh;if(!LoadStaticMesh(a.mesh_package.string(),a.mesh_reference,layers,&mesh))return false;
            for(const auto& skin:a.skins){
                if(skin.material_slot>=mesh.report.texture_layer_count)return false;
                const bool masked=std::ranges::any_of(mesh.vertices,[&](const auto& v){
                    return v.texture_layer==skin.material_slot&&(v.polygon_flags&2U)!=0;
                });
                const auto texture=wand::load_hp1_p8_texture(skin.package,skin.reference,masked);
                if(texture.status!=wand::Hp1ProfileStatus::ok||texture.mips.empty())return false;
                const auto& mip=texture.mips.front();
                constexpr std::size_t size=HPVR_HP1_SKELETAL_TEXTURE_SIZE;
                for(std::size_t y=0;y<size;++y)for(std::size_t x=0;x<size;++x){
                    const auto source=((y*mip.height/size)*mip.width+x*mip.width/size)*4;
                    const auto target=(skin.material_slot*size*size+y*size+x)*4;
                    std::copy_n(texture.rgba8.begin()+source,4,mesh.textures.begin()+target);
                }
            }
            if(layers+mesh.report.texture_layer_count>kMaximumCombinedTextureLayers)return false;
            pixels.insert(pixels.end(),mesh.textures.begin(),mesh.textures.end());
            layers+=mesh.report.texture_layer_count;meshes.emplace(key,std::move(mesh));
        }
        const auto& mesh=meshes.at(key);
        auto origin=RotateYaw({a.location_unreal.y*kMetersPerUnrealUnit-start.position_m[0],
            a.location_unreal.z*kMetersPerUnrealUnit-start.position_m[1],
            -a.location_unreal.x*kMetersPerUnrealUnit-start.position_m[2]},yaw);
        const bool chandelier=cls.find("chandalier")!=std::string::npos;
        const auto hoop=std::ranges::find_if(flight.hoops,[&](const auto& h){return h.id==a.actor_reference;});
        const bool flying_hoop=hoop!=flight.hoops.end();
        const float draw_scale=flying_hoop?hoop->play_scale:a.draw_scale;
        float ground=0;
        if(!chandelier&&!savebook&&!flying_hoop&&FindPropGroundBelow(collision,origin,&ground))
            origin[1]=ground-mesh.report.bounds_min_m[1]*a.draw_scale;
        const float angle=yaw+a.rotation_units[1]*kTau/65536.0F;
        const auto transform=[&](const std::array<float,3>& p){return AddVector(origin,RotateYaw(ScaleVector(p,draw_scale),angle));};
        ChallengeProp prop;prop.reference=a.actor_reference;prop.name=cls;
        prop.breakable=cls.starts_with("hprops.flipendovase");
        prop.cauldron=cls=="hprops.bronzecauldron";prop.savebook=savebook;
        prop.spell_target=prop.breakable||prop.cauldron;
        prop.first=static_cast<std::uint32_t>(vertices.size());
        prop.minimum={10000,10000,10000};prop.maximum={-10000,-10000,-10000};
        for(const auto& v:mesh.vertices){
            const auto p=transform({v.position_m[0],v.position_m[1],v.position_m[2]});
            for(unsigned axis=0;axis<3;++axis){prop.minimum[axis]=std::min(prop.minimum[axis],p[axis]);prop.maximum[axis]=std::max(prop.maximum[axis],p[axis]);}
            vertices.push_back({{p[0],p[1],p[2]},{v.texture_uv[0],v.texture_uv[1]},{0,0},
                mesh.layer_base+v.texture_layer,v.polygon_flags,0,PackAuthoredLighting(p,lights)});
        }
        prop.count=static_cast<std::uint32_t>(vertices.size())-prop.first;
        if(!chandelier&&!prop.breakable&&!savebook&&!flying_hoop&&
           !(flight.valid&&cls=="hprops.rememberallbroom")){
            std::vector<GpuVertex> solid(vertices.begin()+prop.first,vertices.begin()+prop.first+prop.count);
            auto extra=BuildCollisionTriangles(solid,prop.count);collision.insert(collision.end(),extra.begin(),extra.end());
        }
        if(prop.cauldron){
            if(!animations.contains(key)){
                auto skin=wand::load_hp1_skeletal_skin(a.mesh_package,a.mesh_reference);
                if(skin.status!=wand::Hp1ProfileStatus::ok)return false;
                auto animation=wand::load_hp1_animation(a.mesh_package,skin.census.animation_reference);
                if(animation.status!=wand::Hp1ProfileStatus::ok)return false;
                animations.emplace(key,std::make_pair(std::move(skin),std::move(animation)));
            }
            const auto& [skin,animation]=animations.at(key);
            for(const bool settled:{false,true}){
                const auto found=std::ranges::find_if(animation.sequences,[&](const auto& sequence){return AsciiFold(sequence.name)==(settled?"tipped":"tipover");});
                if(found==animation.sequences.end())return false;
                const auto sequence=static_cast<std::size_t>(found-animation.sequences.begin());
                if(sequence>=animation.moves.size()||found->frame_count<=0||found->frame_count>256)return false;
                const float duration=animation.moves[sequence].track_time;
                if(!std::isfinite(duration)||duration<=0)return false;
                auto& first=settled?prop.settled_first:prop.animation_first;
                auto& frames=settled?prop.settled_frames:prop.animation_frames;
                auto& seconds=settled?prop.settled_duration:prop.animation_duration;
                first=static_cast<std::uint32_t>(vertices.size());frames=static_cast<unsigned>(found->frame_count);seconds=duration;
                for(unsigned frame=0;frame<frames;++frame){
                    const float elapsed=duration*float(frame)/float(settled?frames:std::max(1U,frames-1));
                    const auto pose=wand::sample_hp1_skeletal_animation(skin,animation,sequence,elapsed,settled,false);
                    if(pose.status!=wand::Hp1ProfileStatus::ok)return false;
                    for(const auto& v:mesh.vertices){
                        if(v.point_index>=pose.points.size())return false;
                        const auto& point=pose.points[v.point_index];
                        const auto p=transform({point.y*kMetersPerUnrealUnit,point.z*kMetersPerUnrealUnit,point.x*kMetersPerUnrealUnit});
                        vertices.push_back({{p[0],p[1],p[2]},{v.texture_uv[0],v.texture_uv[1]},{0,0},
                            mesh.layer_base+v.texture_layer,v.polygon_flags,0,PackAuthoredLighting(p,lights)});
                    }
                }
            }
        }
        if(prop.breakable){
            const std::int32_t reference=cls=="hprops.flipendovasebronze"?985:
                cls=="hprops.flipendovasegreen"?1007:cls=="hprops.flipendovaseming"?1010:0;
            if(!reference)return false;
            const auto package=root/"System/HProps.u";
            const std::vector<std::string> broken_key{package.string(),std::to_string(reference)};
            if(!meshes.contains(broken_key)){
                LoadedStaticMesh broken;
                if(!LoadStaticMesh(package.string(),reference,layers,&broken)||layers+broken.report.texture_layer_count>kMaximumCombinedTextureLayers)return false;
                pixels.insert(pixels.end(),broken.textures.begin(),broken.textures.end());
                layers+=broken.report.texture_layer_count;meshes.emplace(broken_key,std::move(broken));
            }
            const auto& broken=meshes.at(broken_key);
            auto broken_origin=origin;
            if(FindPropGroundBelow(collision,origin,&ground))broken_origin[1]=ground-broken.report.bounds_min_m[1]*a.draw_scale;
            prop.broken_first=static_cast<std::uint32_t>(vertices.size());prop.broken_count=static_cast<std::uint32_t>(broken.vertices.size());
            for(const auto& v:broken.vertices){
                const auto p=AddVector(broken_origin,RotateYaw(ScaleVector({v.position_m[0],v.position_m[1],v.position_m[2]},a.draw_scale),angle+32000*kTau/65536.0F));
                vertices.push_back({{p[0],p[1],p[2]},{v.texture_uv[0],v.texture_uv[1]},{0,0},
                    broken.layer_base+v.texture_layer,v.polygon_flags,0,PackAuthoredLighting(p,lights)});
            }
        }
        props.push_back(prop);
        if(cls=="hprops.singlecandlestick"||cls=="hprops.threearmfloorcandlestick"||cls=="hprops.plaincandle"){
            const bool three=cls=="hprops.threearmfloorcandlestick";
            for(unsigned wick=0;wick<(three?3U:1U);++wick){
                float high=-1000,min_x=1000,max_x=-1000,min_z=1000,max_z=-1000;
                for(const auto& v:mesh.vertices){
                    const unsigned group=v.position_m[0]<-.12F?0U:v.position_m[0]>.12F?2U:1U;
                    if(!three||group==wick)high=std::max(high,v.position_m[1]);
                }
                for(const auto& v:mesh.vertices){
                    const unsigned group=v.position_m[0]<-.12F?0U:v.position_m[0]>.12F?2U:1U;
                    if((three&&group!=wick)||v.position_m[1]<high-(mesh.report.bounds_max_m[1]-mesh.report.bounds_min_m[1])*.12F)continue;
                    min_x=std::min(min_x,v.position_m[0]);max_x=std::max(max_x,v.position_m[0]);
                    min_z=std::min(min_z,v.position_m[2]);max_z=std::max(max_z,v.position_m[2]);
                }
                if(high>-100)flames.push_back({transform({(min_x+max_x)*.5F,high,(min_z+max_z)*.5F}),float(a.actor_reference%71)+wick,.32F});
            }
        }else if(chandelier){
            const auto wicks=fixtures::ChandelierWickPositions(mesh.vertices);
            for(std::size_t wick=0;wick<wicks.size();++wick)
                flames.push_back({transform(wicks[wick]),float(a.actor_reference%71)+float(wick),.32F});
            glows.push_back({origin,.45F*a.draw_scale,.35F,float(a.actor_reference%41)});
        }
    }
    HPVR_LOGI("[hpvr.quest.challenge.props] actors=%zu meshes=%zu",props.size(),meshes.size());
    return !props.empty();
}
bool LoadChallengeStars(const std::filesystem::path& root,const std::filesystem::path& map,
    const hpvr_hp1_player_start_report& start,float yaw,
    std::vector<GpuVertex>& vertices,std::vector<std::uint8_t>& pixels,std::uint32_t& layers,std::vector<BeanDraw>& pickups){
    const auto manifest=wand::build_hp1_character_manifest(root,map,0,{},true);
    if(manifest.status!=wand::Hp1ProfileStatus::ok)return false;
    LoadedStaticMesh mesh;bool loaded=false;unsigned stars=0;
    for(const auto& a:manifest.actors)if(AsciiFold(a.qualified_class_name)=="hprops.star"){
        if(!loaded){if(!LoadStaticMesh(a.mesh_package.string(),a.mesh_reference,layers,&mesh))return false;
            if(layers+mesh.report.texture_layer_count>kMaximumCombinedTextureLayers)return false;
            pixels.insert(pixels.end(),mesh.textures.begin(),mesh.textures.end());layers+=mesh.report.texture_layer_count;loaded=true;}
        const auto origin=RotateYaw({a.location_unreal.y*kMetersPerUnrealUnit-start.position_m[0],a.location_unreal.z*kMetersPerUnrealUnit-start.position_m[1],
            -a.location_unreal.x*kMetersPerUnrealUnit-start.position_m[2]},yaw);
        BeanDraw star{a.actor_reference,static_cast<std::uint32_t>(vertices.size()),static_cast<std::uint32_t>(mesh.vertices.size()),origin,3};
        for(const auto& v:mesh.vertices)vertices.push_back({{v.position_m[0]*a.draw_scale,v.position_m[1]*a.draw_scale,v.position_m[2]*a.draw_scale},
            {v.texture_uv[0],v.texture_uv[1]},{0,0},mesh.layer_base+v.texture_layer,v.polygon_flags,0,0xffffff});
        pickups.push_back(star);++stars;
    }
    HPVR_LOGI("[hpvr.quest.challenge.stars] count=%u",stars);return stars==8;
}
bool LoadChallengeMetadata(const wand::Hp1ActorVisualCensus& census,
    const hpvr_hp1_player_start_report& start,float yaw,ChallengeRuntime& challenge,unsigned expected_scenes=15){
    if(!challenge.graph.Load(census))return false;
    for(const auto& a:census.actors){
        const auto cls=AsciiFold(a.qualified_class_name);
        if(cls=="hpbase.cutscene"){
            IntroCutscene scene;
            if(!LoadIntroCutscene(census,start,yaw,&scene,a.object_name,false))return false;
            scene.playing=false;challenge.scenes.emplace(a.actor_reference,std::move(scene));
        }
        const bool spell=cls=="hpbase.spelltrigger";
        if(spell||cls=="engine.trigger"||cls=="harrypotter.savepoint"||cls=="hpbase.starstrigger"||cls=="hpbase.cutscene"){
            const bool checkpoint=cls=="harrypotter.savepoint";
            const float radius=a.collision_radius_serialized?a.collision_radius*kMetersPerUnrealUnit:
                checkpoint?.6F:cls=="hpbase.cutscene"?.8F:spell?.64F:.8F;
            if(radius<=0)continue;
            challenge.spatial.push_back({a.actor_reference,cls,AsciiFold(a.tag),AsciiFold(a.event),ActorLocalPosition(a,start,yaw),radius,
                a.collision_height_serialized?a.collision_height*kMetersPerUnrealUnit:checkpoint?.6F:.8F,spell,false,checkpoint});
        }
        const auto object=AsciiFold(a.object_name);
        constexpr std::array<const char*,4> route{"hpath_a0","basestation0","hpath_b0","basestation3"};
        for(unsigned i=0;i<route.size();++i)if(object==route[i])challenge.barrel_route[i]=ActorLocalPosition(a,start,yaw);
    }
    HPVR_LOGI("[hpvr.quest.challenge.graph] scenes=%zu spatial=%zu",challenge.scenes.size(),challenge.spatial.size());
    return challenge.scenes.size()==expected_scenes;
}
