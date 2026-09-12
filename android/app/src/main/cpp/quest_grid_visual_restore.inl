#pragma once

// Existing prepared scenes retain the mover's obsolete BSP materials. Restore
// only authored Polys UVs/materials; cached positions, lighting and physics stay
// byte-for-byte unchanged. This runs once before the GPU upload, never per frame.
namespace grid_visual_restore {
constexpr std::size_t kLayerBytes=256U*256U*4U;
constexpr std::size_t kMaximumMovers=3;
constexpr std::size_t kMaximumNewLayers=3;
constexpr float kPositionTolerance=.0002F;
using Point=std::array<float,3>;
struct SourceVertex {Point position{};std::array<float,2> uv{};std::uint32_t layer=0;};
struct Patch {std::size_t index=0;std::array<float,2> uv{};std::uint32_t layer=0;};

bool FinitePoint(const Point& point){
    return std::ranges::all_of(point,[](float v){return std::isfinite(v)&&std::abs(v)<=10000.F;});
}
Point Position(const GpuVertex& vertex){return {vertex.position[0],vertex.position[1],vertex.position[2]};}
bool Near(const Point& a,const Point& b){
    for(unsigned axis=0;axis<3;++axis)if(std::abs(a[axis]-b[axis])>kPositionTolerance)return false;
    return true;
}
float Dot(const Point& a,const Point& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
bool Normal(const Point& a,const Point& b,const Point& c,Point& result){
    const auto u=SubtractVector(b,a),v=SubtractVector(c,a);
    result=CrossVector(u,v);const auto length=std::sqrt(Dot(result,result));
    if(!std::isfinite(length)||length<.000001F)return false;
    for(auto& axis:result)axis/=length;
    return true;
}

// Match corners on the same plane, not triangle indices: Polys and old BSP may
// choose different quad diagonals or triangle order. Shared cube-edge corners
// must not borrow UVs from the adjacent side. Ambiguous overlapping faces reject.
bool StageVertices(const std::vector<GpuVertex>& vertices,std::size_t first,std::size_t count,
    const std::vector<SourceVertex>& source,std::vector<Patch>& patches){
    if(count!=36||source.size()!=count||first>vertices.size()||count>vertices.size()-first||
        patches.size()>kMaximumMovers*36-count)return false;
    for(const auto& vertex:source)if(!FinitePoint(vertex.position)||
        !std::isfinite(vertex.uv[0])||!std::isfinite(vertex.uv[1]))return false;
    for(const auto& vertex:source){
        bool present=false;
        for(std::size_t i=0;i<count;++i)if(Near(vertex.position,Position(vertices[first+i]))){present=true;break;}
        if(!present)return false;
    }
    std::vector<Patch> staged;staged.reserve(count);
    for(std::size_t triangle=0;triangle<count;triangle+=3){
        const auto a=Position(vertices[first+triangle]),b=Position(vertices[first+triangle+1]),
            c=Position(vertices[first+triangle+2]);Point normal{};
        if(!FinitePoint(a)||!FinitePoint(b)||!FinitePoint(c)||!Normal(a,b,c,normal))return false;
        for(unsigned corner=0;corner<3;++corner){
            const auto point=Position(vertices[first+triangle+corner]);
            Patch patch;patch.index=first+triangle+corner;bool found=false;
            for(std::size_t other=0;other<source.size();other+=3){
                Point other_normal{};
                if(!Normal(source[other].position,source[other+1].position,source[other+2].position,other_normal))return false;
                if(std::abs(Dot(normal,other_normal))<.9999F||
                    std::abs(Dot(normal,SubtractVector(source[other].position,a)))>kPositionTolerance)continue;
                for(unsigned candidate=0;candidate<3;++candidate){
                    const auto& vertex=source[other+candidate];if(!Near(point,vertex.position))continue;
                    if(found&&(patch.layer!=vertex.layer||std::abs(patch.uv[0]-vertex.uv[0])>.00001F||
                        std::abs(patch.uv[1]-vertex.uv[1])>.00001F))return false;
                    patch.layer=vertex.layer;patch.uv=vertex.uv;found=true;
                }
            }
            if(!found)return false;
            staged.push_back(patch);
        }
    }
    patches.insert(patches.end(),staged.begin(),staged.end());return true;
}

bool StageLayer(const PreparedGeometry& geometry,const std::vector<std::uint8_t>& pixels,
    std::vector<std::vector<std::uint8_t>>& added,std::uint32_t& layer){
    if(pixels.size()!=kLayerBytes||geometry.texture_layers>256||
        geometry.textures.size()!=std::size_t(geometry.texture_layers)*kLayerBytes)return false;
    for(std::uint32_t existing=0;existing<geometry.texture_layers;++existing){
        if(std::equal(pixels.begin(),pixels.end(),geometry.textures.begin()+std::size_t(existing)*kLayerBytes)){
            layer=existing;return true;
        }
    }
    for(std::size_t existing=0;existing<added.size();++existing)if(added[existing]==pixels){
        layer=geometry.texture_layers+static_cast<std::uint32_t>(existing);return true;
    }
    if(added.size()>=kMaximumNewLayers||geometry.texture_layers+added.size()>=256)return false;
    layer=geometry.texture_layers+static_cast<std::uint32_t>(added.size());added.push_back(pixels);return true;
}

// The BSP adapter tiles smaller images to its largest material dimensions.
// Canonicalize that exact repeating period so a 64x64 cap is not appended once
// as a 128x128 tile and again as 128x256. Scaling its UVs preserves every sample.
std::uint32_t PixelPeriod(const wand::Hp1TexturedBspScene& model,std::uint32_t layer,bool vertical){
    const auto width=model.texture_layer_width,height=model.texture_layer_height;
    const auto limit=vertical?height:width;
    const auto start=std::size_t(layer)*width*height*4;
    for(std::uint32_t period=1;period<limit;++period){
        if(limit%period)continue;
        bool repeats=true;
        for(std::uint32_t y=0;y<height&&repeats;++y)for(std::uint32_t x=0;x<width;++x){
            const auto sx=vertical?x:x%period,sy=vertical?y%period:y;
            const auto a=start+(std::size_t(y)*width+x)*4,b=start+(std::size_t(sy)*width+sx)*4;
            if(!std::equal(model.texture_rgba8.begin()+a,model.texture_rgba8.begin()+a+4,model.texture_rgba8.begin()+b)){
                repeats=false;break;
            }
        }
        if(repeats)return period;
    }
    return limit;
}

bool StageModel(const PreparedGeometry& geometry,const DoorDraw& door,const wand::Hp1ActorVisual& actor,
    const wand::Hp1TexturedBspScene& model,std::vector<std::vector<std::uint8_t>>& added,
    std::vector<Patch>& patches){
    if(model.status!=wand::Hp1ProfileStatus::ok||model.vertices.size()!=36||door.vertex_count!=36||
        model.fallback_triangle_count||model.omitted_triangle_count||!model.decoded_texture_count||
        model.texture_layer_count<2||model.texture_layer_count>4||model.texture_layer_width==0||
        model.texture_layer_width>256||model.texture_layer_height==0||model.texture_layer_height>256||
        model.texture_rgba8.size()!=std::uint64_t(model.texture_layer_count)*model.texture_layer_width*model.texture_layer_height*4||
        door.first_vertex<geometry.map_vertices||!FinitePoint(door.pivot)||
        !std::isfinite(door.placement.player_yaw))return false;
    Point pre{};bool pre_seen=false;
    for(const auto& property:actor.serialized_properties)if(AsciiFold(property.name)=="prepivot"){
        if(pre_seen||property.value.size()!=12)return false;
        std::memcpy(pre.data(),property.value.data(),12);pre_seen=true;
    }
    if(!FinitePoint(pre))return false;
    for(const auto value:door.placement.base_rotation_units)if(!std::isfinite(value))return false;
    const Point prepivot{pre[1]*kMetersPerUnrealUnit,pre[2]*kMetersPerUnrealUnit,-pre[0]*kMetersPerUnrealUnit};
    std::array<std::uint32_t,4> layers{};std::array<bool,4> used{};
    std::array<std::array<float,2>,4> uv_scale{};
    for(const auto& vertex:model.vertices){
        if(vertex.texture_layer==0||vertex.texture_layer>=model.texture_layer_count)return false;
        used[vertex.texture_layer]=true;
    }
    for(std::uint32_t source=1;source<model.texture_layer_count;++source)if(used[source]){
        const auto period_x=PixelPeriod(model,source,false),period_y=PixelPeriod(model,source,true);
        uv_scale[source]={float(model.texture_layer_width)/float(period_x),float(model.texture_layer_height)/float(period_y)};
        std::vector<std::uint8_t> tile(kLayerBytes);
        for(std::size_t y=0;y<256;++y)for(std::size_t x=0;x<256;++x){
            const auto sx=x*period_x/256,sy=y*period_y/256;
            const auto offset=((std::size_t(source)*model.texture_layer_height+sy)*model.texture_layer_width+sx)*4;
            std::copy_n(model.texture_rgba8.begin()+offset,4,tile.begin()+(y*256+x)*4);
        }
        if(!StageLayer(geometry,tile,added,layers[source]))return false;
    }
    std::vector<SourceVertex> vertices;vertices.reserve(36);
    for(const auto& vertex:model.vertices){
        const auto local=SubtractVector({vertex.position_m.x,vertex.position_m.y,vertex.position_m.z},prepivot);
        const auto point=AddVector(door.pivot,movers::RotateBrushLocal(local,door.placement.base_rotation_units,door.placement.player_yaw));
        const auto& scale=uv_scale[vertex.texture_layer];
        vertices.push_back({point,{vertex.texture_uv[0]*scale[0],vertex.texture_uv[1]*scale[1]},layers[vertex.texture_layer]});
    }
    return StageVertices(geometry.vertices,door.first_vertex,door.vertex_count,vertices,patches);
}

bool PillarTextureName(const std::string& name){
    const auto folded=AsciiFold(name);
    return folded=="bflipendotall"||folded=="bflipendoshort"||folded=="topflipendoswitch"||folded=="flipendoswitch";
}

// These three brushes reference only HP_Basic.Detail textures. Resolve that
// exact package locally instead of rebuilding the whole world dependency/light
// context when adopting a prepared cache. No exported game pixels are needed.
bool DirectModel(const std::filesystem::path& map_package,const std::filesystem::path& texture_package,
    std::int32_t brush,const wand::Hp1PackageLinkTable& map_links,const wand::Hp1PackageLinkTable& texture_links,
    std::map<std::int32_t,wand::Hp1P8Texture>& decoded,wand::Hp1TexturedBspScene& model){
    const auto topology=wand::load_hp1_brush_polygon_topology(map_package,brush);
    if(topology.status!=wand::Hp1ProfileStatus::ok||topology.surfaces.size()!=6||
        topology.nodes.size()!=6||topology.vertices.size()!=24)return false;
    const auto mesh=wand::build_hp1_bsp_triangle_mesh(topology,kMetersPerUnrealUnit);
    if(mesh.status!=wand::Hp1ProfileStatus::ok||mesh.triangles.size()!=12||mesh.degenerate_triangle_count)return false;
    model={};model.texture_layer_width=model.texture_layer_height=256;
    model.texture_layer_count=1;model.texture_rgba8.resize(kLayerBytes);
    std::map<std::int32_t,std::uint32_t> layers;
    for(const auto& surface:topology.surfaces){
        const auto reference=surface.texture_reference;
        if(layers.contains(reference))continue;
        if(layers.size()>=3)return false;
        if(!decoded.contains(reference)){
            const wand::Hp1PackageImport* imported=nullptr;
            for(const auto& entry:map_links.imports)if(entry.reference==reference){if(imported)return false;imported=&entry;}
            if(!imported||AsciiFold(imported->qualified_class_name)!="engine.texture"||imported->object_path.size()!=3||
                AsciiFold(imported->object_path[0])!="hp_basic"||AsciiFold(imported->object_path[1])!="detail"||
                !PillarTextureName(imported->object_path[2])||decoded.size()>=4)return false;
            const wand::Hp1PackageExport* exported=nullptr;
            for(const auto& entry:texture_links.exports)if(entry.object_path.size()==2&&
                AsciiFold(entry.object_path[0])=="detail"&&AsciiFold(entry.object_path[1])==AsciiFold(imported->object_path[2])){
                if(exported||AsciiFold(entry.qualified_class_name)!="engine.texture")return false;exported=&entry;
            }
            if(!exported)return false;
            auto texture=wand::load_hp1_p8_texture(texture_package,exported->reference);
            if(texture.status!=wand::Hp1ProfileStatus::ok||texture.mips.empty()||texture.polygon_flags||
                texture.mips[0].width==0||texture.mips[0].width>256||texture.mips[0].height==0||texture.mips[0].height>256||
                texture.rgba8.size()!=std::uint64_t(texture.mips[0].width)*texture.mips[0].height*4)return false;
            decoded.emplace(reference,std::move(texture));
        }
        const auto& texture=decoded.at(reference);const auto width=texture.mips[0].width,height=texture.mips[0].height;
        const auto begin=model.texture_rgba8.size();model.texture_rgba8.resize(begin+kLayerBytes);
        for(std::size_t y=0;y<256;++y)for(std::size_t x=0;x<256;++x){
            const auto offset=((y*height/256)*width+x*width/256)*4;
            std::copy_n(texture.rgba8.begin()+offset,4,model.texture_rgba8.begin()+begin+(y*256+x)*4);
        }
        layers.emplace(reference,model.texture_layer_count++);++model.decoded_texture_count;
    }
    for(const auto& triangle:mesh.triangles){
        if(triangle.surface_index>=topology.surfaces.size())return false;
        const auto& surface=topology.surfaces[triangle.surface_index];const auto& texture=decoded.at(surface.texture_reference);
        for(unsigned corner=0;corner<3;++corner){wand::Hp1TexturedBspVertex vertex;
            vertex.position_m=triangle.positions_m[corner];vertex.normal=triangle.normal;
            vertex.texture_uv={triangle.texel_uv[corner][0]/float(texture.mips[0].width),
                triangle.texel_uv[corner][1]/float(texture.mips[0].height)};
            vertex.texture_layer=layers.at(surface.texture_reference);vertex.polygon_flags=surface.polygon_flags;
            model.vertices.push_back(vertex);
        }
    }
    model.status=wand::Hp1ProfileStatus::ok;return true;
}
} // namespace grid_visual_restore

struct GridVisualRestoreStats {std::uint32_t movers=0,vertices=0,texture_layers_added=0;};

[[maybe_unused]] bool RestorePreparedGridVisuals(PreparedGeometry& geometry,
    const std::filesystem::path& data_root,const std::filesystem::path& map_package,
    const wand::Hp1ActorVisualCensus& census,GridVisualRestoreStats* statistics=nullptr){
    using namespace grid_visual_restore;
    if(statistics)*statistics={};
    if(AsciiFold(map_package.stem().string())!="lev_tut1b")return true;
    if(census.status!=wand::Hp1ProfileStatus::ok||census.actors.size()>65536||geometry.doors.size()>4096||
        geometry.texture_width!=256||geometry.texture_height!=256||geometry.texture_layers==0||
        geometry.texture_layers>256||geometry.textures.size()!=std::size_t(geometry.texture_layers)*kLayerBytes)return false;
    std::vector<const DoorDraw*> doors;
    for(const auto& door:geometry.doors)if(door.grid){
        if(doors.size()>=kMaximumMovers||door.collision_only||door.vertex_count!=36||
            door.first_vertex>geometry.vertices.size()||door.vertex_count>geometry.vertices.size()-door.first_vertex)return false;
        for(const auto* previous:doors)if(previous->actor_reference==door.actor_reference||
            (std::size_t(previous->first_vertex)<std::size_t(door.first_vertex)+door.vertex_count&&
             std::size_t(door.first_vertex)<std::size_t(previous->first_vertex)+previous->vertex_count))return false;
        doors.push_back(&door);
    }
    if(doors.empty())return true;
    const auto texture_package=data_root/"Textures/HP_Basic.utx";
    const auto map_links=wand::inspect_hp1_package_link_table(map_package);
    const auto texture_links=wand::inspect_hp1_package_link_table(texture_package);
    if(map_links.status!=wand::Hp1ProfileStatus::ok||texture_links.status!=wand::Hp1ProfileStatus::ok||
        map_links.imports.size()>65536||texture_links.exports.size()>65536)return false;
    std::map<std::int32_t,wand::Hp1P8Texture> decoded;
    std::vector<std::vector<std::uint8_t>> added;
    std::vector<Patch> patches;patches.reserve(doors.size()*36);
    for(const auto* door:doors){
        const wand::Hp1ActorVisual* actor=nullptr;
        for(const auto& candidate:census.actors)if(candidate.actor_reference==door->actor_reference){
            if(actor)return false;actor=&candidate;
        }
        if(!actor||AsciiFold(actor->qualified_class_name)!="engine.gridmover")return false;
        std::int32_t brush=0;bool brush_seen=false;
        for(const auto& property:actor->serialized_properties)if(AsciiFold(property.name)=="brush"){
            if(brush_seen||!property.object_reference_serialized||property.object_reference<=0)return false;
            brush=property.object_reference;brush_seen=true;
        }
        if(!brush_seen)return false;
        wand::Hp1TexturedBspScene model;
        if(!DirectModel(map_package,texture_package,brush,map_links,texture_links,decoded,model))return false;
        if(!StageModel(geometry,*door,*actor,model,added,patches))return false;
    }
    // No visible state is changed until every mover and its material budget has
    // passed validation. Allocate before publication; no large vertex copy.
    geometry.textures.reserve((std::size_t(geometry.texture_layers)+added.size())*kLayerBytes);
    for(const auto& tile:added)geometry.textures.insert(geometry.textures.end(),tile.begin(),tile.end());
    geometry.texture_layers+=static_cast<std::uint32_t>(added.size());
    for(const auto& patch:patches){auto& vertex=geometry.vertices[patch.index];
        vertex.texture_uv[0]=patch.uv[0];vertex.texture_uv[1]=patch.uv[1];vertex.texture_layer=patch.layer;
    }
    if(statistics)*statistics={static_cast<std::uint32_t>(doors.size()),static_cast<std::uint32_t>(patches.size()),
        static_cast<std::uint32_t>(added.size())};
    return true;
}
