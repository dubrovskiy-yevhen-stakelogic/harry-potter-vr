struct MirrorPush {
    Matrix4 projection{};
    std::array<float,4> plane{},object{},params{},padding{};
};
static_assert(sizeof(MirrorPush)==128);
void QuestScene::RecordMirrorCapture(VkCommandBuffer command,const Matrix4& projection){
    auto& s=*state_;s.captured_mirrors.assign(s.mirrors.size(),false);
    if(!IsGpuReady()||(s.map_id!=3&&s.map_id!=kHogwartsReturnMapId)||!s.frontend.WorldVisible()||s.mirrors.empty())return;
    Matrix4 inverse{};
    if(!InvertReflectionMatrix(projection,inverse)||std::abs(inverse[11])<1e-7F)return;
    const std::array<float,3> eye{inverse[8]/inverse[11],inverse[9]/inverse[11],inverse[10]/inverse[11]};
    for(unsigned i=0;i<s.mirrors.size();++i){
        const auto& m=s.mirrors[i];
        s.mirror_target.Begin(command,i);
        const auto mover=std::ranges::find_if(s.doors,[&](const auto& d){return d.actor_reference==m.mover_reference;});
        if((m.mover_reference&&(mover==s.doors.end()||!MirrorClosed(*mover)))||
           std::abs(DotVector(m.normal,SubtractVector(eye,m.center)))<.03F){vkCmdEndRenderPass(command);continue;}
        std::array<float,4> r{1,1,-1,-1};bool visible=false,clipped=false;
        for(unsigned corner=0;corner<8;++corner){
            std::array<float,4> p{};
            for(unsigned row=0;row<4;++row){p[row]=projection[12+row];for(unsigned axis=0;axis<3;++axis)p[row]+=projection[axis*4+row]*((corner&(1U<<axis))?m.maximum[axis]:m.minimum[axis]);}
            if(p[3]<=.01F){clipped=true;continue;}
            visible=true;r[0]=std::min(r[0],p[0]/p[3]);r[1]=std::min(r[1],p[1]/p[3]);r[2]=std::max(r[2],p[0]/p[3]);r[3]=std::max(r[3],p[1]/p[3]);
        }
        for(auto& x:r)x=std::clamp(x,-1.0F,1.0F);
        // A near-plane crossing can cover the eye even when all projected
        // front corners fall outside it. Keep a conservative capture there.
        if(visible&&clipped)r={-1,-1,1,1};
        if(!visible||r[2]<=r[0]||r[3]<=r[1]){vkCmdEndRenderPass(command);continue;}
        const auto rectangle=r;s.captured_mirrors[i]=true;
    const auto& mirror=m;
    auto normal=mirror.normal;
    if(DotVector(normal,SubtractVector(eye,mirror.center))<0)normal=ScaleVector(normal,-1);
    MirrorPush push;push.projection=projection;push.plane={normal[0],normal[1],normal[2],-DotVector(normal,mirror.center)};
    const auto w=s.mirror_target.Width(),h=s.mirror_target.Height();
    VkViewport viewport{0,float(h),float(w),-float(h),0,1};vkCmdSetViewport(command,0,1,&viewport);
    const int x=std::max(0,int((rectangle[0]+1)*.5F*float(w))-12),y=std::max(0,int((1-rectangle[3])*.5F*float(h))-12);
    const unsigned right=std::min(w,unsigned((rectangle[2]+1)*.5F*float(w))+12),bottom=std::min(h,unsigned((1-rectangle[1])*.5F*float(h))+12);
    VkRect2D scissor{{x,y},{right-unsigned(x),bottom-unsigned(y)}};vkCmdSetScissor(command,0,1,&scissor);
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,s.mirror_capture_pipeline);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,s.mirror_layout,0,1,&s.descriptor_set,1,&s.abyss_disabled_offset);
    const VkDeviceSize zero=0;vkCmdBindVertexBuffers(command,0,1,&s.vertex_buffer,&zero);
    const auto draw=[&](unsigned first,unsigned count,const std::array<float,3>& offset,float yaw){
        push.object={offset[0],offset[1],offset[2],yaw};
        vkCmdPushConstants(command,s.mirror_layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);
        if(count)vkCmdDraw(command,count,1,first,0);
    };
    draw(0,s.map_vertex_count,{},0);
    for(const auto& prop:s.challenge.props){
        const auto range=ChallengePropDrawRange(prop,ChallengeActivated(s.frontend.progress,prop.reference),s.bean_time);
        auto offset=ChallengePropOffset(prop,s.bean_time);
        if(const auto b=s.charms.blocks.find(prop.reference);b!=s.charms.blocks.end())offset=AddVector(offset,b->second.offset);
        draw(range.first,range.second,offset,0);
    }
    for(const auto& door:s.doors){
        if(door.collision_only||IsMirrorBlocker(door,s.mirrors))continue;
        const auto origin=MoverPoint(door,{0,0,0});
        const auto xaxis=SubtractVector(MoverPoint(door,{1,0,0}),origin);
        const auto up=SubtractVector(MoverPoint(door,{0,1,0}),origin);
        if(std::abs(up[1]-1)<.001F)draw(door.first_vertex,door.vertex_count,origin,std::atan2(-xaxis[2],xaxis[0]));
    }
    for(const auto& actor:s.character_draws){
        if(actor.child_template||!actor.enabled||actor.flying)continue;
        auto destination=AddVector(actor.base_origin,actor.cutscene_offset);float yaw=actor.yaw;
        if(actor.player&&!IsCutscenePlaying()){
            destination=s.last_player;destination[1]-=kPlayerCapsuleHalfHeightMeters+kPlayerEyeHeightMeters;
            if(s.player_capsule_valid){destination=s.player_capsule;destination[1]-=kPlayerCapsuleHalfHeightMeters;}
            yaw=s.last_yaw+kTau*.5F;
        }
        const auto& clip=actor.clips.at(actor.active_clip);
        const float time=actor.animation_loop?std::fmod(actor.animation_time,clip.duration):std::min(actor.animation_time,clip.duration);
        const unsigned frame=std::min(clip.frame_count-1,unsigned(time/clip.duration*float(clip.frame_count)));
        const float angle=yaw-actor.base_yaw;
        draw(clip.first_vertex+frame*actor.vertex_count,actor.vertex_count,SubtractVector(destination,RotateYaw(actor.base_origin,angle)),angle);
    }
    vkCmdEndRenderPass(command);
    }
}
void QuestScene::RecordMirrorComposite(VkCommandBuffer command,const Matrix4& projection,unsigned width,unsigned height)const{
    const auto& s=*state_;
    if(s.mirrors.empty())return;
    MirrorPush push;push.projection=projection;
    const VkDeviceSize zero=0;vkCmdBindVertexBuffers(command,0,1,&s.vertex_buffer,&zero);
    for(unsigned i=0;i<s.mirrors.size();++i){
        const auto& mirror=s.mirrors[i];
        const auto door=std::ranges::find_if(s.doors,[&](const auto& d){return d.actor_reference==mirror.mover_reference;});
        if(mirror.mover_reference&&door==s.doors.end())continue;
        const bool closed=!mirror.mover_reference||MirrorClosed(*door);
        if(closed&&(i>=s.captured_mirrors.size()||!s.captured_mirrors[i]))continue;
        const auto extent=SubtractVector(mirror.maximum,mirror.minimum);
        push.plane={mirror.normal[0],mirror.normal[1],mirror.normal[2],0};
        push.object={mirror.center[0],mirror.center[1],mirror.center[2],
            std::max(.01F,std::abs(mirror.normal[2])*extent[0]+std::abs(mirror.normal[0])*extent[2])};
        push.padding={std::max(.01F,extent[1]),std::abs(mirror.normal[1])>.9F?1.0F:0.0F,float(i),0};
        push.params={closed?1.0F:2.0F,float(width),float(height),s.effects_clock.Seconds()};
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,closed?s.mirror_composite_pipeline:s.mirror_veil_pipeline);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,s.mirror_layout,0,1,&s.descriptor_set,1,&s.abyss_disabled_offset);
        vkCmdPushConstants(command,s.mirror_layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);
        if(mirror.water_draw.second)vkCmdDraw(command,mirror.water_draw.second,1,mirror.water_draw.first,0);
        else for(const auto& range:mirror.ranges)vkCmdDraw(command,range.second,1,range.first,0);
    }
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,s.pipeline);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,s.pipeline_layout,0,1,&s.descriptor_set,1,&s.abyss_disabled_offset);
    vkCmdPushConstants(command,s.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(projection),projection.data());
    const AuthoredDarkLightPush no_dark_lights{};
    vkCmdPushConstants(command,s.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,sizeof(projection),sizeof(no_dark_lights),&no_dark_lights);
}
