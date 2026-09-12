// Read authored non-fire Fire01 overrides once at load. Prepared cache data
// remains immutable on disk; only the former fire approximation is removed.
using AmbientParticleEmitter=hpvr::quest::ambient::Emitter;
using AmbientParticle=hpvr::quest::ambient::Particle;
using hpvr::quest::ambient::BuildAmbientParticles;

std::vector<AmbientParticleEmitter> RestorePreparedAmbientParticles(PreparedGeometry& geometry,
    const wand::Hp1ActorVisualCensus& census,const hpvr_hp1_player_start_report& start,float yaw){
    std::vector<AmbientParticleEmitter> output;
    if(census.status!=wand::Hp1ProfileStatus::ok)return output;
    for(const auto& actor:census.actors){
        if(!actor.location_serialized||AsciiFold(actor.qualified_class_name)!="hpparticle.fire01")continue;
        bool glow_texture=false;
        for(const auto& p:actor.serialized_properties){
            if(AsciiFold(p.name)!="textures"||std::max<std::int64_t>(0,p.array_index)!=0||!p.object_reference_serialized)continue;
            glow_texture=p.object_path.size()==3&&AsciiFold(p.object_path[0])=="hpparticle"&&
                AsciiFold(p.object_path[1])=="particle_fx"&&AsciiFold(p.object_path[2])=="glow00";
        }
        if(!glow_texture)continue; // Keep genuine Fire14/Fire8 and all unknown textures.
        AmbientParticleEmitter e;e.actor_reference=actor.actor_reference;
        e.position=ActorLocalPosition(actor,start,yaw);
        if(!std::all_of(e.position.begin(),e.position.end(),[](float v){return std::isfinite(v);}))continue;
        const auto range=[&](std::string_view name,float base,float variation,float minimum,float maximum){
            for(const auto& p:actor.serialized_properties){
                if(AsciiFold(p.name)!=name||p.value.size()!=8||AsciiFold(p.structure_name)!="floatparams")continue;
                float b=0,r=0;std::memcpy(&b,p.value.data(),4);std::memcpy(&r,p.value.data()+4,4);
                if(std::isfinite(b)&&std::isfinite(r)){base=b;variation=r;}break;
            }
            return std::array<float,2>{std::clamp(base,minimum,maximum),std::clamp(variation,0.0F,maximum-minimum)};
        };
        const auto color=[&](std::string_view name,std::array<float,3> fallback){
            for(const auto& p:actor.serialized_properties)
                if(AsciiFold(p.name)==name&&p.value.size()==8&&AsciiFold(p.structure_name)=="colorparams")
                    return std::array<float,3>{float(p.value[0])/255,float(p.value[1])/255,float(p.value[2])/255};
            return fallback;
        };
        const auto width=range("sourcewidth",0,0,0,1000),height=range("sourceheight",0,0,0,1000);
        const auto speed=range("speed",0,0,-150,150),life=range("lifetime",1,0,.05F,10);
        const auto size=range("sizewidth",4,0,.25F,50),end=range("sizeendscale",0,0,-1,8);
        const auto rate=range("particlespersec",20,0,0,100),alpha=range("alphastart",1,0,0,1);
        e.source_width_m=width[0]*kMetersPerUnrealUnit;e.source_height_m=height[0]*kMetersPerUnrealUnit;
        e.speed_mps=speed[0]*kMetersPerUnrealUnit;e.speed_range_mps=speed[1]*kMetersPerUnrealUnit;
        e.lifetime=life[0];e.lifetime_range=life[1];e.size_m=size[0]*kMetersPerUnrealUnit;
        e.size_range_m=size[1]*kMetersPerUnrealUnit;e.size_end_scale=end[0];e.size_end_range=end[1];
        e.rate=rate[0];e.rate_range=rate[1];e.alpha_start=alpha[0];e.alpha_range=alpha[1];
        e.color_start=color("colorstart",{1,1,1});e.color_end=color("colorend",e.color_start);
        const float pitch=actor.rotation_units[0]*kTau/65536.0F,heading=actor.rotation_units[1]*kTau/65536.0F;
        const float cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(heading),sy=std::sin(heading);
        e.direction=RotateYaw({cp*sy,sp,-cp*cy},yaw);
        e.source_right=RotateYaw({cy,0,sy},yaw);e.source_up=RotateYaw({sp*sy,-cp,-sp*cy},yaw);
        e.phase=float(actor.actor_reference&255)*.071F;
        const auto same_position=[&](const auto& item){
            float squared=0;for(unsigned axis=0;axis<3;++axis){const float d=item.position[axis]-e.position[axis];squared+=d*d;}
            return squared<.000001F;
        };
        std::erase_if(geometry.flames,[&](const auto& f){return same_position(f)&&f.scale==2.4F&&f.phase==e.phase;});
        const float glow_phase=float(actor.actor_reference&255)*.043F;
        std::erase_if(geometry.glows,[&](const auto& g){return same_position(g)&&g.radius==.34F&&g.intensity==.55F&&g.phase==glow_phase;});
        if(!actor.hidden&&output.size()<hpvr::quest::ambient::kMaximumAmbientEmitters)output.push_back(e);
    }
    return output;
}
