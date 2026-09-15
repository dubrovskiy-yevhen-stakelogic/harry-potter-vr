bool LoadWingFeatherTexture(const std::filesystem::path& root,unsigned width,unsigned height,
    std::vector<std::uint8_t>& pixels,unsigned& layers,unsigned& feather_layer){
    if(!width||!height||layers>=kMaximumCombinedTextureLayers||pixels.size()!=std::size_t(width)*height*4*layers)return false;
    const auto package=root/"system/HPParticle.u";
    const auto table=wand::inspect_hp1_package_link_table(package);
    if(table.status!=wand::Hp1ProfileStatus::ok)return false;
    const auto style=broom::lesson_detail::Class(package,table,"Wing_fly");
    const auto* texture=broom::lesson_detail::Property(style,"Textures");
    if(!texture||texture->object_reference<=0||texture->object_path.empty()||texture->object_path.back()!="White_Feather")return false;
    const auto image=wand::load_hp1_p8_texture(package,texture->object_reference);
    if(image.status!=wand::Hp1ProfileStatus::ok||image.mips.empty())return false;
    const auto& mip=image.mips.front();
    if(!mip.width||!mip.height||image.rgba8.size()<std::size_t(mip.width)*mip.height*4)return false;
    feather_layer=layers++;
    const auto first=pixels.size();pixels.resize(first+std::size_t(width)*height*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const auto source=(std::size_t(y*mip.height/height)*mip.width+x*mip.width/width)*4;
        std::copy_n(image.rgba8.data()+source,4,pixels.data()+first+(std::size_t(y)*width+x)*4);
    }
    return true;
}
bool LoadLockParticles(const std::filesystem::path& root,
    const wand::Hp1ActorVisualCensus& census,const hpvr_hp1_player_start_report& start,float yaw,
    std::uint32_t width,std::uint32_t height,std::vector<std::uint8_t>& pixels,
    std::uint32_t& layers,std::uint32_t& texture_layer,std::vector<AmbientParticleEmitter>& emitters){
    try{
        using namespace broom_visual_detail;
        if(!width||!height||layers>=kMaximumCombinedTextureLayers||
            pixels.size()!=std::size_t(width)*height*4*layers)return false;
        const auto package=root/"system/HPParticle.u";
        const auto table=wand::inspect_hp1_package_link_table(package);
        if(table.status!=wand::Hp1ProfileStatus::ok)return false;
        const auto properties=broom::lesson_detail::Class(package,table,"Lock");
        const auto* texture=broom::lesson_detail::Property(properties,"Textures");
        if(!texture||texture->object_path!=std::vector<std::string>{"hp_fx","Particles","Les_Sparkle_01"})return false;
        const auto texture_package=root/"textures/hp_fx.utx";
        const auto textures=wand::inspect_hp1_package_link_table(texture_package);
        if(textures.status!=wand::Hp1ProfileStatus::ok)return false;
        const auto found=std::ranges::find_if(textures.exports,[](const auto& item){return !item.object_path.empty()&&item.object_path.back()=="Les_Sparkle_01";});
        if(found==textures.exports.end())return false;
        const auto image=wand::load_hp1_p8_texture(texture_package,found->reference);
        if(image.status!=wand::Hp1ProfileStatus::ok||image.mips.empty())return false;
        const auto& mip=image.mips.front();
        if(!mip.width||!mip.height||image.rgba8.size()<std::size_t(mip.width)*mip.height*4)return false;
        AmbientParticleEmitter style;
        const auto source_width=Range(properties,"SourceWidth"),source_height=Range(properties,"SourceHeight"),depth=Range(properties,"SourceDepth");
        const auto speed=Range(properties,"Speed"),life=Range(properties,"Lifetime"),size=Range(properties,"SizeWidth"),end=Range(properties,"SizeEndScale");
        style.source_width_m=source_width[0]*kMetersPerUnrealUnit;style.source_height_m=source_height[0]*kMetersPerUnrealUnit;
        style.source_depth_m=depth[0]*kMetersPerUnrealUnit;style.direction={0,1,0};style.source_up={0,0,1};style.radial=true;
        style.speed_mps=speed[0]*kMetersPerUnrealUnit;style.speed_range_mps=speed[1]*kMetersPerUnrealUnit;
        style.lifetime=life[0];style.lifetime_range=life[1];style.size_m=size[0]*kMetersPerUnrealUnit;style.size_range_m=size[1]*kMetersPerUnrealUnit;
        style.size_end_scale=end[0];style.size_end_range=end[1];style.rate=Range(properties,"ParticlesPerSec")[0];
        style.color_start=Color(properties,"ColorStart");style.color_end=Color(properties,"ColorEnd");
        std::vector<AmbientParticleEmitter> next;
        for(const auto& actor:census.actors)if(actor.location_serialized&&AsciiFold(actor.qualified_class_name)=="hprops.padlock"){
            if(next.size()>=ambient::kMaximumAmbientEmitters)return false;
            auto e=style;e.actor_reference=actor.actor_reference;e.enabled=!actor.hidden;e.position=ActorLocalPosition(actor,start,yaw);next.push_back(e);
        }
        std::vector<std::uint8_t> layer(std::size_t(width)*height*4);
        for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x){
            const auto source=(std::size_t(y*mip.height/height)*mip.width+x*mip.width/width)*4;
            std::copy_n(image.rgba8.data()+source,4,layer.data()+(std::size_t(y)*width+x)*4);
        }
        texture_layer=layers++;pixels.insert(pixels.end(),layer.begin(),layer.end());emitters=std::move(next);return true;
    }catch(const std::exception& e){HPVR_LOGE("[hpvr.quest.lock.particles] error=%s",e.what());return false;}
}
