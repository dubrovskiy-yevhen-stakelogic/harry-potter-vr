// First-person geometry reuses the prepared mounted-Harry frames and materials.
struct BroomAvatar {
    std::vector<GpuVertex> vertices;
    std::uint32_t vertex_count=0,frame_count=0,removed_triangles=0,broom_triangles=0;
    float duration=0;
    std::array<float,3> source_eye{};
    bool broom_only=false;
};

struct BroomAvatarMask {
    std::vector<bool> hidden_points,head_points;
    bool valid=false;
};

BroomAvatarMask BuildBroomAvatarMask(const wand::Hp1SkeletalSkin& skin){
    BroomAvatarMask result;
    if(skin.status!=wand::Hp1ProfileStatus::ok||skin.points.empty()||skin.bones.empty()||
       skin.bones.size()!=skin.bone_weight_spans.size())return result;
    result.hidden_points.resize(skin.points.size());result.head_points.resize(skin.points.size());
    bool has_head=false,has_neck=false;
    for(std::size_t bone=0;bone<skin.bones.size();++bone){
        bool head=false,neck=false;
        auto ancestor=static_cast<std::int32_t>(bone);
        for(std::size_t depth=0;depth<=skin.bones.size();++depth){
            if(ancestor<0||static_cast<std::size_t>(ancestor)>=skin.bones.size()||depth==skin.bones.size())return {};
            const auto& entry=skin.bones[static_cast<std::size_t>(ancestor)];
            const auto name=AsciiFold(entry.name);
            head|=name.find("head")!=std::string::npos;
            neck|=name.find("neck")!=std::string::npos;
            if(entry.parent_index==ancestor||entry.parent_index<0)break;
            ancestor=entry.parent_index;
        }
        has_head|=head;has_neck|=neck;
        const auto& span=skin.bone_weight_spans[bone];
        if(static_cast<std::size_t>(span.weight_offset)+span.weight_count>skin.bone_weights.size())return {};
        for(std::size_t i=span.weight_offset;i<static_cast<std::size_t>(span.weight_offset)+span.weight_count;++i){
            const auto& weight=skin.bone_weights[i];
            if(weight.point_index>=skin.points.size())return {};
            if(weight.encoded_weight==0)continue;
            result.hidden_points[weight.point_index]=result.hidden_points[weight.point_index]||head||neck;
            result.head_points[weight.point_index]=result.head_points[weight.point_index]||head;
        }
    }
    result.valid=has_head&&has_neck&&std::ranges::any_of(result.head_points,[](bool hidden){return hidden;});
    return result;
}

bool BuildBroomAvatarFromPrepared(const wand::Hp1SkeletalSkin& skin,
    const wand::Hp1SkeletalTriangleMesh& topology,const std::vector<GpuVertex>& prepared,
    const CharacterDraw& player,BroomAvatar& output){
    auto found=player.clips.find("hover");
    // The scene cooker normalizes the first mounted idle (hover) to breathe.
    if(found==player.clips.end())found=player.clips.find("breathe");
    if(found==player.clips.end()||topology.status!=wand::Hp1ProfileStatus::ok||
       topology.vertices.empty()||topology.vertices.size()%3||topology.vertices.size()!=player.vertex_count||
       !std::isfinite(player.base_yaw))return false;
    const auto& clip=found->second;
    if(!clip.frame_count||clip.frame_count>512||!std::isfinite(clip.duration)||clip.duration<=0||
       clip.first_vertex>prepared.size()||
       std::size_t(clip.frame_count)*player.vertex_count>prepared.size()-clip.first_vertex)return false;
    for(float axis:player.base_origin)if(!std::isfinite(axis))return false;
    const auto mask=BuildBroomAvatarMask(skin);
    std::vector<std::uint32_t> kept;
    BroomAvatar next;next.broom_only=!mask.valid;
    std::array<float,3> low{1e9F,1e9F,1e9F},high{-1e9F,-1e9F,-1e9F};
    std::array<float,3> all_low=low,all_high=high;
    bool head_bounds=false;
    for(std::size_t triangle=0;triangle<topology.vertices.size();triangle+=3){
        bool hidden=false,broom=true;
        for(unsigned corner=0;corner<3;++corner){
            const auto& vertex=topology.vertices[triangle+corner];
            if(vertex.point_index>=skin.points.size()&&mask.valid)return false;
            hidden|=mask.valid&&mask.hidden_points[vertex.point_index];
            broom&=vertex.material_index==4;
            const auto& cooked=prepared[clip.first_vertex+triangle+corner];
            const auto position=RotateYaw(SubtractVector({cooked.position[0],cooked.position[1],cooked.position[2]},player.base_origin),-player.base_yaw);
            for(unsigned axis=0;axis<3;++axis){
                if(!std::isfinite(position[axis]))return false;
                all_low[axis]=std::min(all_low[axis],position[axis]);all_high[axis]=std::max(all_high[axis],position[axis]);
                if(mask.valid&&mask.head_points[vertex.point_index]){
                    head_bounds=true;low[axis]=std::min(low[axis],position[axis]);high[axis]=std::max(high[axis],position[axis]);
                }
            }
        }
        if(next.broom_only?!broom:hidden){++next.removed_triangles;continue;}
        if(broom)++next.broom_triangles;
        kept.push_back(static_cast<std::uint32_t>(triangle));
    }
    if(!next.broom_only&&(kept.empty()||!next.broom_triangles||!head_bounds||!next.removed_triangles)){
        kept.clear();next.broom_only=true;next.removed_triangles=0;next.broom_triangles=0;
        for(std::size_t triangle=0;triangle<topology.vertices.size();triangle+=3){
            bool broom=true;
            for(unsigned corner=0;corner<3;++corner)broom&=topology.vertices[triangle+corner].material_index==4;
            if(!broom){++next.removed_triangles;continue;}
            kept.push_back(static_cast<std::uint32_t>(triangle));++next.broom_triangles;
        }
    }
    if(kept.empty()||!next.broom_triangles)return false;
    // One reference eye, never animated per frame: original body motion remains
    // visible, but its head animation cannot shake the player's camera.
    if(head_bounds)next.source_eye={(low[0]+high[0])*.5F,low[1]+(high[1]-low[1])*.70F,high[2]-.04F};
    else next.source_eye={(all_low[0]+all_high[0])*.5F,all_high[1]-.12F,0};
    next.frame_count=clip.frame_count;next.duration=clip.duration;
    next.vertex_count=static_cast<std::uint32_t>(kept.size()*3);
    next.vertices.reserve(std::size_t(next.vertex_count)*next.frame_count);
    for(unsigned frame=0;frame<next.frame_count;++frame)for(const auto triangle:kept)for(unsigned corner=0;corner<3;++corner){
        auto vertex=prepared[clip.first_vertex+std::size_t(frame)*player.vertex_count+triangle+corner];
        const auto position=SubtractVector(RotateYaw(SubtractVector(
            {vertex.position[0],vertex.position[1],vertex.position[2]},player.base_origin),-player.base_yaw),next.source_eye);
        for(unsigned axis=0;axis<3;++axis){if(!std::isfinite(position[axis]))return false;vertex.position[axis]=position[axis];}
        next.vertices.push_back(vertex);
    }
    output=std::move(next);return true;
}

bool BuildBroomAvatar(const std::filesystem::path& root,const std::vector<GpuVertex>& prepared,
    const CharacterDraw& player,BroomAvatar& output){
    if(AsciiFold(player.class_name)!="harrypotter.broomharry")return false;
    const auto package=root/"System/HarryPotter.u";
    const auto topology=wand::build_hp1_skeletal_triangle_mesh(package,497,kMetersPerUnrealUnit);
    const auto skin=wand::load_hp1_skeletal_skin(package,497);
    if(topology.census.object_name!="skremharryMesh"||skin.census.animation_reference!=1647)return false;
    return BuildBroomAvatarFromPrepared(skin,topology,prepared,player,output);
}
