// Installer-only CPU preparation. Input game files are always read-only.
#define HPVR_QUEST_CPU_ONLY
#include "../../../android/app/src/main/cpp/quest_scene.cpp"
#include <iostream>
#include <iomanip>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
using namespace hpvr::quest;
namespace fs=std::filesystem;
bool Within(const fs::path& child,const fs::path& parent){
    auto c=child.begin();
    for(auto p=parent.begin();p!=parent.end();++p,++c)
        if(c==child.end()||AsciiFold(c->generic_string())!=AsciiFold(p->generic_string()))return false;
    return true;
}
void RejectRedirects(const fs::path& path){
    fs::path current;
    for(const auto& part:fs::absolute(path).lexically_normal()){
        current/=part;
#ifdef _WIN32
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("preparation paths must not contain junctions or symbolic links");
#else
        if(fs::is_symlink(fs::symlink_status(current)))
            throw std::runtime_error("preparation paths must not contain symbolic links");
#endif
    }
}
int Run(const std::vector<fs::path>& args){
    if(args.size()!=6&&args.size()!=7){
        std::cerr<<"usage: hpvr_quest_prepare_assets <owned-root> --output <private-output-outside-root> --map <0|1|2|3> [--verify]\n";
        return 2;
    }
    try{
        const auto map_argument=args[5].string();
        const auto map=std::ranges::find_if(kQuestMaps,[&](const auto& item){return std::to_string(item.id)==map_argument;});
        if(args[2]!="--output"||args[4]!="--map"||
            map==kQuestMaps.end()||(args.size()==7&&args[6]!="--verify"))
            throw std::runtime_error("invalid preparation arguments");
        RejectRedirects(args[1]);RejectRedirects(args[3]);
        const auto root=fs::canonical(args[1]);
        const auto output=fs::weakly_canonical(fs::absolute(args[3]));
        if(!fs::is_directory(root)||output==output.root_path()||Within(output,root)||Within(root,output))
            throw std::runtime_error("output must be a dedicated private folder outside the input game tree");
        const unsigned map_id=map->id;
        const auto path=output/("map-"+std::to_string(map_id)+".hpvc");
        RejectRedirects(path);
        const bool verify=args.size()==7;
        if(!verify&&fs::exists(path))throw std::runtime_error("prepared file already exists; use a new output folder");
        const auto began=std::chrono::steady_clock::now();
        const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();};
        hpvr::wand::Hp1PackageReadScope package_reads;
        const auto fingerprint=cache::ComputeSourceFingerprint(root,map_id);
        const auto fingerprint_seconds=elapsed();
        if(!verify){
            const auto package=root/map->package_path;
            hpvr_hp1_player_start_report start{};
            if(hpvr_hp1_load_player_start_utf8(package.string().c_str(),kMetersPerUnrealUnit,0,&start)!=HPVR_HP1_PROFILE_OK||
                start.status!=HPVR_HP1_PROFILE_OK||start.abi_version!=HPVR_HP1_PLAYER_START_ABI_VERSION||
                !start.location_serialized||start.rotation_units[0]!=0||start.rotation_units[2]!=0)
                throw std::runtime_error("owned player start is invalid");
            const float yaw=static_cast<float>(start.rotation_units[1])*kTau/65536.0F;
            WorldMetadata world;
            if(!LoadWorldMetadata(root,package,start,yaw,&world))throw std::runtime_error("owned scene metadata could not be loaded");
            PreparedGeometry geometry;
            if(!PrepareGeometryFromOwnedData(root,map_id,start,yaw,world,geometry,[&](const char* stage){
                std::cout<<"PREPARE_STAGE="<<stage<<" map="<<map_id<<" seconds="<<elapsed()<<std::endl;
            }))throw std::runtime_error("owned scene preparation failed");
            if(!ValidatePreparedGeometry(geometry))throw std::runtime_error("prepared scene layout is invalid");
            if(cache::ComputeSourceFingerprint(root,map_id)!=fingerprint)
                throw std::runtime_error("input game files changed during preparation");
            const auto prepared_at=elapsed();
            cache::Writer writer(path,map_id,fingerprint);
            WritePreparedGeometry(writer,geometry);writer.Finish();
            std::cout<<"PREPARE_WRITE=PASS map="<<map_id<<" vertices="<<geometry.vertices.size()
                <<" layers="<<geometry.texture_layers<<" prepare_seconds="<<prepared_at
                <<" write_seconds="<<elapsed()-prepared_at<<std::endl;
        }
        // Free preparation arrays before the independent streamed read-back.
        const auto read_at=elapsed();
        cache::Reader reader(path,map_id,fingerprint);
        auto candidate=ReadPreparedGeometry(reader,verify?prepared_codec::kMaxFrontendVertexReserve:0);reader.Finish();
        if(!ValidatePreparedGeometry(candidate))throw std::runtime_error("prepared read-back layout is invalid");
        const auto read_seconds=elapsed()-read_at;
        if(verify){
            if(map_id==kCharmsTrainingMapId){
                const auto census=hpvr::wand::inspect_hp1_actor_visuals(root/map->package_path);
                hpvr_hp1_player_start_report start{};
                if(hpvr_hp1_load_player_start_utf8((root/map->package_path).string().c_str(),kMetersPerUnrealUnit,0,&start)!=0)
                    throw std::runtime_error("prepared charms player start is invalid");
                const float yaw=start.rotation_units[1]*kTau/65536;
                CharmsRuntime charms;ChallengeRuntime challenge;
                if(!LoadCharmsMetadata(root,census,start,yaw,candidate.challenge_props,charms,&candidate.vertices)||
                   !LoadChallengeMetadata(census,start,yaw,challenge,23)||
                   !ValidateCharmsSceneLayout(challenge,charms,candidate.doors.size(),candidate.fixture_actors,candidate.characters))
                    throw std::runtime_error("prepared charms runtime metadata mismatch");
                unsigned chests=0,rewards=0,cards=0;
                const auto mirrors=FindMirrorSurfaces(candidate.vertices,candidate.map_vertices);
                if(mirrors.size()!=3||std::ranges::count_if(mirrors,[](const auto& m){return std::abs(m.normal[1])>.99F;})!=1)
                    throw std::runtime_error("prepared charms must contain two wall mirrors and one reflecting pool");
                for(const auto& mirror:mirrors)std::cout<<"PREPARED_MIRROR normal="<<mirror.normal[0]<<','<<mirror.normal[1]<<','<<mirror.normal[2]
                    <<" center="<<mirror.center[0]<<','<<mirror.center[1]<<','<<mirror.center[2]<<'\n';
                for(const auto& prop:candidate.challenge_props)if(prop.name=="hprops.knight"){
                    auto center=ScaleVector(AddVector(prop.minimum,prop.maximum),.5F);center[1]=prop.minimum[1];
                    std::cout<<"PREPARED_KNIGHT ref="<<prop.reference<<" foot="<<center[0]<<','<<center[1]<<','<<center[2]<<'\n';
                    float visible_top=-1e9F;
                    for(unsigned i=0;i+2<candidate.map_vertices;i+=3){
                        if(candidate.vertices[i].polygon_flags&1U)continue;
                        std::vector<GpuVertex> tri(candidate.vertices.begin()+i,candidate.vertices.begin()+i+3);
                        const auto faces=BuildCollisionTriangles(tri,3);
                        for(const auto& face:faces){float h=0;if(CollisionTriangleHeightAtXZ(face,center[0],center[2],&h)&&
                            h<center[1]+.05F&&h>center[1]-.8F)visible_top=std::max(visible_top,h);}
                    }
                    if(std::abs(visible_top-center[1])>.005F)throw std::runtime_error("prepared knight is not grounded on its visible pedestal");
                }
                unsigned attached_stars=0;
                auto moving_doors=candidate.doors;auto moving_pickups=candidate.beans;
                for(auto& door:moving_doors)if(door.tag=="secretjumpledge"){
                    (void)movers::Start(door.motion,true);
                    for(unsigned i=0;i<100;++i)(void)movers::Advance(door.motion,.05F);
                }
                UpdateCharmsPickupAttachments(charms,moving_doors,moving_pickups);
                for(const auto& pickup:moving_pickups)if(pickup.kind==3&&charms.prop_attachments.contains(pickup.actor_reference)){
                    ++attached_stars;
                    if(std::abs(pickup.attachment_offset[1]-8.64F)>.005F||BeanWorldPosition(pickup)==pickup.position)
                        throw std::runtime_error("prepared final star does not follow the lifted platform");
                }
                if(attached_stars!=1)throw std::runtime_error("prepared final star attachment is missing");
                auto entry_doors=candidate.doors;
                if(OpenCharmsCutsceneDoors(1526,entry_doors)<=0)throw std::runtime_error("entry door does not open for Harry");
                for(auto& door:entry_doors)for(unsigned i=0;i<100;++i)(void)movers::Advance(door.motion,.05F);
                if(CloseCharmsEntryDoors(entry_doors)<=0)throw std::runtime_error("entry door does not close after Harry");
                for(auto& door:entry_doors)for(unsigned i=0;i<100;++i)(void)movers::Advance(door.motion,.05F);
                if(CloseCharmsEntryDoors(entry_doors)!=0)throw std::runtime_error("closed entry door would hold player control");
                std::cout<<"PREPARED_C70=PASS mirrors=3 grounded_knights=4 attached_stars=1 entry_door=OPEN_CLOSE\n";
                for(int ref:{1989,1990,2018,1979,1546,1547}){
                    auto scene=challenge.scenes.at(ref);scene.camera_target="locname1";
                    if(!CharmsFirstPersonFocus(scene,candidate.characters))throw std::runtime_error("charms reveal has no authored focus");
                }
                if(!CharmsFirstPersonFocus(challenge.scenes.at(1411),candidate.characters))
                    throw std::runtime_error("charms exit has no authored destination");
                std::cout<<"PREPARED_C71=PASS reveal_targets=6 exit_destination=VALID\n";
                auto collision=candidate.collision;const auto static_count=collision.size();
                for(const auto& door:candidate.doors){
                    std::vector<GpuVertex> vertices(candidate.vertices.begin()+door.first_vertex,candidate.vertices.begin()+door.first_vertex+door.vertex_count);
                    auto extra=BuildCollisionTriangles(vertices,vertices.size());collision.insert(collision.end(),extra.begin(),extra.end());
                }
                AppendCharmsCollision(charms,collision);
                for(auto& [ref,block]:charms.blocks){
                    const auto box=CharmsBlockBounds(block);
                    const auto center=ScaleVector(AddVector(box.minimum,box.maximum),.5F);
                    const auto lift=charms_block::Advance(block.motion,box,AddVector(center,{0,1,0}),true,.05F,15,
                        collision,static_count,block.collision_first,block.collision_count,block.collision_yaw);
                    std::cout<<"PREPARED_BLOCK_LIFT ref="<<ref<<" rise="<<lift.offset[1]<<'\n';
                    if(std::abs(lift.offset[1]-.2F)>.001F)
                        throw std::runtime_error("prepared levitation block is wedged in starting geometry");
                }
                const auto sky_count=std::count_if(candidate.vertices.begin(),candidate.vertices.begin()+candidate.map_vertices,
                    [](const auto& vertex){return (vertex.polygon_flags&kBroomSkyFlag)!=0;});
                if(sky_count!=36)throw std::runtime_error("prepared balcony sky must contain only its six authored faces");
                std::cout<<"PREPARED_BALCONY_SKY=PASS vertices="<<sky_count<<'\n';
                for(const auto& prop:candidate.challenge_props)if(chest::IsChest(prop.name)){
                    if(!prop.spell_target||prop.animation_frames!=74||prop.settled_frames!=6)
                        throw std::runtime_error("prepared chest animation is incomplete");
                    ++chests;
                    for(const auto& reward:candidate.beans)if(reward.source_actor==prop.reference){++rewards;cards+=reward.kind==4;}
                }
                if(chests!=11||rewards!=50||cards!=1||charms.valid_plates.size()!=9)
                    throw std::runtime_error("prepared charms rewards or plates are incomplete");
                std::cout<<"PREPARED_CHARMS=PASS scenes=23 blocks=7 plates=9 chests=11 rewards=50 chest_cards=1\n";
            }
            if(map_id==2){
                const auto player=std::ranges::find_if(candidate.characters,[](const auto& draw){return draw.player;});
                BroomAvatar avatar;
                if(player==candidate.characters.end()||!BuildBroomAvatar(root,candidate.vertices,*player,avatar))
                    throw std::runtime_error("prepared mounted avatar is incomplete");
                std::cout<<"PREPARED_BROOM_AVATAR=PASS vertices="<<avatar.vertex_count<<" frames="<<avatar.frame_count
                    <<" body="<<!avatar.broom_only<<" broom_triangles="<<avatar.broom_triangles<<'\n';
            }
            const auto* vertex_address=candidate.vertices.data();
            const auto* texture_address=candidate.textures.data();
            if(map_id==kCharmsTrainingMapId){
                auto mirrors=FindMirrorSurfaces(candidate.vertices,candidate.map_vertices);
                const auto before=candidate.vertices.size();AppendWaterSurfaceGeometry(mirrors,candidate.vertices);
                if(std::ranges::count_if(mirrors,[](const auto& m){return m.water_draw.second>0;})!=1)
                    throw std::runtime_error("exactly one water surface must receive wave geometry");
                std::cout<<"PREPARED_WATER_WAVES=PASS added_vertices="<<candidate.vertices.size()-before<<'\n';
            }
            QuestFrontEnd front;
            if(!LoadFrontAssets(root,&front.assets,map_id))throw std::runtime_error("prepared scene frontend assets are incomplete");
            // Reserve the same two extra layers used by fire and the target
            // marker. This check does not render or alter the cooked file.
            if(candidate.texture_layers+2+front.assets.textures.size()>kMaximumCombinedTextureLayers)
                throw std::runtime_error("prepared scene leaves insufficient frontend texture layers");
            candidate.texture_layers+=2;
            candidate.textures.resize(std::uint64_t(candidate.texture_layers)*256*256*4);
            if(map_id==3){
                unsigned feather_layer=0;
                if(!LoadWingFeatherTexture(root,256,256,candidate.textures,candidate.texture_layers,feather_layer))
                    throw std::runtime_error("original Wingardium feather texture could not be loaded");
            }
            std::map<std::string,FrontDrawRange> ranges;std::uint32_t front_vertices=0;
            if(!AppendFrontGeometry(front,candidate.vertices,candidate.textures,candidate.texture_layers,ranges,front_vertices)||
                candidate.vertices.data()!=vertex_address||candidate.textures.data()!=texture_address)
                throw std::runtime_error("frontend exceeded prepared-scene allocation headroom");
            std::cout<<"PREPARED_FRONTEND=PASS map="<<map_id<<" vertices="<<front_vertices
                <<" layers="<<candidate.texture_layers<<" scene_buffer_reallocated=NO\n";
        }
        std::cout<<std::fixed<<std::setprecision(3)<<"PREPARED_SCENE=PASS map="<<map_id
            <<" schema="<<cache::kSchema<<" cook="<<cache::CookRevision(map_id)<<" bytes="<<fs::file_size(path)
            <<" vertices="<<candidate.vertices.size()<<" fingerprint_seconds="<<fingerprint_seconds
            <<" read_seconds="<<read_seconds<<" total_seconds="<<elapsed()<<" mode="<<(verify?"VERIFY":"PREPARE")<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<"PREPARED_SCENE=FAILED reason="<<e.what()<<'\n';return 1;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return Run(args);}
#else
int main(int argc,char** argv){std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return Run(args);}
#endif
