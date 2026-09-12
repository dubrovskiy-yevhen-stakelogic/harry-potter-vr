#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"

#include <iostream>
#include <stdexcept>

using namespace hpvr::quest;

namespace {
std::size_t checks=0;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
bool Near(float a,float b){return std::abs(a-b)<.00001F;}

void SyntheticMask(){
    hpvr::wand::Hp1SkeletalSkin skin;
    skin.status=hpvr::wand::Hp1ProfileStatus::ok;skin.points.resize(12);skin.bones.resize(5);
    skin.bones[0].name="Root";skin.bones[0].parent_index=0;
    skin.bones[1].name="Spine";skin.bones[1].parent_index=0;
    skin.bones[2].name="Harry Neck";skin.bones[2].parent_index=1;
    skin.bones[3].name="Harry Head";skin.bones[3].parent_index=2;
    skin.bones[4].name="Hair";skin.bones[4].parent_index=3;
    for(std::uint16_t point:{0,1,2,6,8,9,10,11,7,3,4,5})skin.bone_weights.push_back({point,65535});
    skin.bone_weight_spans={{0,8,0,0},{8,0,0,0},{8,1,0,0},{9,2,0,0},{11,1,0,0}};
    auto mask=BuildBroomAvatarMask(skin);
    Check(mask.valid&&mask.hidden_points[3]&&mask.hidden_points[7]&&mask.hidden_points[5],
          "head, neck and a head-child hair bone are excluded");
    Check(!mask.hidden_points[0]&&!mask.hidden_points[9]&&!mask.head_points[7],
          "body and broom stay visible; neck points do not set the eye anchor");

    hpvr::wand::Hp1SkeletalTriangleMesh topology;
    topology.status=hpvr::wand::Hp1ProfileStatus::ok;topology.vertices.resize(12);
    CharacterDraw draw;draw.vertex_count=12;draw.base_origin={4,5,6};draw.base_yaw=.6F;
    draw.clips.emplace("hover",CharacterClip{0,.8F,2});
    std::vector<GpuVertex> prepared;
    for(unsigned i=0;i<12;++i){topology.vertices[i].point_index=i;topology.vertices[i].material_index=i>=9?4U:i>=3?1U:0U;}
    for(unsigned frame=0;frame<2;++frame)for(unsigned i=0;i<12;++i){
        const std::array<float,3> local{float(i%3)*.1F,float(i/3)*.3F+float(frame)*.02F,float(i%2)*.2F};
        const auto p=AddVector(draw.base_origin,RotateYaw(local,draw.base_yaw));
        prepared.push_back({{p[0],p[1],p[2]},{.2F,.3F},{0,0},20+topology.vertices[i].material_index,7,0,0x123456});
    }
    BroomAvatar avatar;
    Check(BuildBroomAvatarFromPrepared(skin,topology,prepared,draw,avatar)&&!avatar.broom_only,
          "prepared hover becomes an original-material headless avatar");
    Check(avatar.vertex_count==6&&avatar.vertices.size()==12&&avatar.frame_count==2&&
          avatar.removed_triangles==2&&avatar.broom_triangles==1,
          "whole head and mixed neck triangles are removed, body and broom retained in every frame");
    const std::array<unsigned,6> expected{0,1,2,9,10,11};
    for(unsigned frame=0;frame<2;++frame)for(unsigned i=0;i<6;++i){
        const auto& output=avatar.vertices[frame*6+i];const auto& source=prepared[frame*12+expected[i]];
        Check(output.texture_layer==source.texture_layer&&output.polygon_flags==source.polygon_flags&&
              output.packed_light==source.packed_light&&output.texture_uv[0]==source.texture_uv[0]&&
              output.texture_uv[1]==source.texture_uv[1], "avatar preserves original prepared materials and lighting");
        const auto reconstructed=AddVector(draw.base_origin,RotateYaw(AddVector(
            {output.position[0],output.position[1],output.position[2]},avatar.source_eye),draw.base_yaw));
        for(unsigned axis=0;axis<3;++axis)Check(Near(reconstructed[axis],source.position[axis]),
            "eye-relative avatar geometry reversibly removes only actor origin and yaw");
        if(frame)Check(Near(output.position[1]-avatar.vertices[i].position[1],.02F),
            "body animation remains intact under one stable eye anchor");
    }
    auto invalid=skin;invalid.bones[2].name="Unknown";invalid.bones[3].name="Unknown";
    Check(BuildBroomAvatarFromPrepared(invalid,topology,prepared,draw,avatar)&&avatar.broom_only&&
          avatar.vertex_count==3&&avatar.removed_triangles==3,
          "unrecognized head rig falls back to the original broom only, never a visible head");
    invalid=skin;invalid.bone_weight_spans[3].weight_offset=65000;
    Check(!BuildBroomAvatarMask(invalid).valid,"out-of-range influence spans fail closed");
    invalid=skin;invalid.bones[3].parent_index=4;
    Check(!BuildBroomAvatarMask(invalid).valid,"a cyclic hierarchy cannot loop forever");
    auto bad_draw=draw;bad_draw.clips.at("hover").first_vertex=1000;
    const auto previous_count=avatar.vertices.size();
    Check(!BuildBroomAvatarFromPrepared(skin,topology,prepared,bad_draw,avatar)&&avatar.vertices.size()==previous_count,
          "invalid prepared ranges do not replace an existing avatar");
}

void OwnedAvatar(const std::filesystem::path& root){
    const auto package=root/"System/HarryPotter.u";
    const auto skin=hpvr::wand::load_hp1_skeletal_skin(package,497);
    const auto mesh=hpvr::wand::build_hp1_skeletal_triangle_mesh(package,497,kMetersPerUnrealUnit);
    const auto animation=hpvr::wand::load_hp1_animation(package,1647);
    Check(skin.status==hpvr::wand::Hp1ProfileStatus::ok&&mesh.status==hpvr::wand::Hp1ProfileStatus::ok&&
          animation.status==hpvr::wand::Hp1ProfileStatus::ok,"owned mounted Harry mesh and animation load read-only");
    const auto sequence=std::ranges::find_if(animation.sequences,[](const auto& clip){return AsciiFold(clip.name)=="hover";});
    Check(sequence!=animation.sequences.end(),"owned mounted Harry has the hover sequence");
    const auto index=static_cast<std::size_t>(sequence-animation.sequences.begin());
    CharacterDraw draw;draw.class_name="HarryPotter.BroomHarry";draw.vertex_count=static_cast<std::uint32_t>(mesh.vertices.size());
    draw.clips.emplace("hover",CharacterClip{0,animation.moves[index].track_time,24});
    std::vector<GpuVertex> prepared;
    for(unsigned frame=0;frame<24;++frame){
        const auto pose=hpvr::wand::sample_hp1_skeletal_animation(skin,animation,index,draw.clips.at("hover").duration*float(frame)/24,true,false);
        Check(pose.status==hpvr::wand::Hp1ProfileStatus::ok,"owned hover frames sample without moving the camera");
        float floor=1e9F;
        for(const auto& vertex:mesh.vertices)floor=std::min(floor,pose.points.at(vertex.point_index).z*kMetersPerUnrealUnit);
        for(const auto& vertex:mesh.vertices){
            const auto& p=pose.points.at(vertex.point_index);
            prepared.push_back({{p.y*kMetersPerUnrealUnit,p.z*kMetersPerUnrealUnit-floor,p.x*kMetersPerUnrealUnit},
                {vertex.texture_uv[0],vertex.texture_uv[1]},{0,0},10+vertex.material_index,vertex.polygon_flags,0,0xffffff});
        }
    }
    const auto mask=BuildBroomAvatarMask(skin);
    Check(mask.valid,"owned neck/head bone hierarchy is recognized without material guesses");
    std::size_t body=0,broom=0,hidden=0;
    for(std::size_t i=0;i<mesh.vertices.size();i+=3){
        bool removed=false;
        for(unsigned corner=0;corner<3;++corner)removed|=mask.hidden_points[mesh.vertices[i+corner].point_index];
        if(removed)++hidden;
        else if(mesh.vertices[i].material_index==4)++broom;
        else ++body;
    }
    BroomAvatar avatar;
    Check(BuildBroomAvatar(root,prepared,draw,avatar)&&!avatar.broom_only,
          "owned original body and broom are both restored without a head");
    Check(body>0&&broom>0&&hidden>0&&avatar.broom_triangles==broom&&avatar.removed_triangles==hidden&&
          avatar.vertex_count==(body+broom)*3,"every retained owned triangle passes the neck/head mask");
    Check(avatar.frame_count==24&&Near(avatar.duration,.8F),"owned hover timing is retained");
    std::cout<<"OWNED_BROOM_AVATAR=PASS body_triangles="<<body<<" broom_triangles="<<broom<<" removed="<<hidden
             <<" vertices="<<avatar.vertices.size()<<" eye="<<avatar.source_eye[0]<<','<<avatar.source_eye[1]<<','<<avatar.source_eye[2]<<'\n';
}
} // namespace

int main(int argc,char** argv){
    try{
        if(argc>2)throw std::runtime_error("usage: hpvr_quest_broom_avatar_tests [owned-game-root]");
        SyntheticMask();if(argc==2)OwnedAvatar(argv[1]);
        std::cout<<"BROOM_AVATAR_TESTS=PASS checks="<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"BROOM_AVATAR_TESTS=FAIL "<<error.what()<<'\n';return 1;}
}
