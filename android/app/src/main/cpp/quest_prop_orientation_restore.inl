// Runtime-only correction of the immutable C43-C45 preparation convention.
// Include after PreparedGeometry; call once before transferring its vectors.
// Cooking retains the legacy convention, so existing prepared caches stay valid.
struct PropOrientationRestoreStats {
    bool valid=false,already_applied=false;
    const char* error="";
    std::size_t props=0,vertices=0,collision_faces=0,aim_faces=0,beans=0;
};
struct CauldronTipPose {
    bool valid=false;
    std::array<float,3> mouth{},direction{};
};
CauldronTipPose PreparedCauldronTip(const PreparedGeometry&,const ChallengeProp&);

namespace prop_orientation_restore {
using Point=std::array<float,3>;
using TriangleKey=std::array<Point,3>;
constexpr std::size_t kMaximumProps=256;
constexpr std::size_t kMaximumVertices=1048576;
struct Range {
    std::size_t first=0,count=0;
    Point origin{};
    float sine=0,cosine=1;
};
Point Transform(const Point& p,const Range& range){
    const float x=p[0]-range.origin[0],z=p[2]-range.origin[2];
    return {range.origin[0]+range.cosine*x+range.sine*z,p[1],
        range.origin[2]-range.sine*x+range.cosine*z};
}
Point Position(const GpuVertex& vertex){
    return {vertex.position[0],vertex.position[1],vertex.position[2]};
}
TriangleKey Key(const std::vector<GpuVertex>& vertices,std::size_t first){
    return {Position(vertices[first]),Position(vertices[first+1]),Position(vertices[first+2])};
}
void Refresh(CollisionTriangle& triangle){
    triangle.minimum=triangle.maximum=triangle.vertices[0];
    for(const auto& p:triangle.vertices)for(unsigned axis=0;axis<3;++axis){
        triangle.minimum[axis]=std::min(triangle.minimum[axis],p[axis]);
        triangle.maximum[axis]=std::max(triangle.maximum[axis],p[axis]);
    }
    const auto cross=CrossVector(SubtractVector(triangle.vertices[1],triangle.vertices[0]),
        SubtractVector(triangle.vertices[2],triangle.vertices[0]));
    if(!NormalizeVector(cross,&triangle.normal))triangle.normal={0,0,0};
}
}

PropOrientationRestoreStats RestorePreparedPropOrientations(PreparedGeometry& geometry,
    const wand::Hp1ActorVisualCensus& census,const hpvr_hp1_player_start_report& start,float player_yaw){
    using namespace prop_orientation_restore;
    PropOrientationRestoreStats stats;
    const auto fail=[&](const char* error){stats.error=error;return stats;};
    if(geometry.prop_orientation_restored){stats.valid=true;stats.already_applied=true;return stats;}
    if(census.status!=wand::Hp1ProfileStatus::ok||!std::isfinite(player_yaw))return fail("invalid actor census or yaw");
    for(float p:start.position_m)if(!std::isfinite(p))return fail("invalid player origin");
    const auto begin=std::size_t(geometry.map_vertices),count=std::size_t(geometry.fixture_vertices);
    if(begin>geometry.vertices.size()||count>geometry.vertices.size()-begin||begin%3||count%3)
        return fail("invalid prepared fixture span");
    const auto end=begin+count;
    std::vector<Range> ranges;
    std::vector<std::pair<std::size_t,Range>> bean_ranges;
    std::vector<std::size_t> changed_props;
    ranges.reserve(std::min(geometry.challenge_props.size(),kMaximumProps)*3);
    for(std::size_t index=0;index<geometry.challenge_props.size();++index){
        const auto& prop=geometry.challenge_props[index];
        const bool cauldron=prop.name=="hprops.bronzecauldron";
        const bool vase=prop.name=="hprops.flipendovasebronze"||prop.name=="hprops.flipendovasegreen"||
            prop.name=="hprops.flipendovaseming";
        if(!cauldron&&!vase)continue;
        if(changed_props.size()>=kMaximumProps||cauldron!=prop.cauldron||vase!=prop.breakable)
            return fail("invalid affected prop type or count");
        const wand::Hp1ActorVisual* actor=nullptr;
        for(const auto& candidate:census.actors)if(candidate.actor_reference==prop.reference){
            if(actor)return fail("duplicate prop actor");
            actor=&candidate;
        }
        if(!actor||AsciiFold(actor->qualified_class_name)!=prop.name||!actor->location_serialized)
            return fail("missing authored prop transform");
        const Point origin=ActorLocalPosition(*actor,start,player_yaw);
        for(float p:origin)if(!std::isfinite(p)||std::abs(p)>10000)return fail("invalid prop origin");
        const float actor_yaw=float(actor->rotation_units[1])*kTau/65536.0F;
        const auto append=[&](std::size_t first,std::size_t size,float yaw){
            if(!size||first<begin||first>end||size>end-first||first%3||size%3||
                size>kMaximumVertices-stats.vertices)return false;
            ranges.push_back({first,size,origin,std::sin(yaw),std::cos(yaw)});
            stats.vertices+=size;return true;
        };
        // The sampled prop skeleton and actor vectors have distinct local
        // axes. The original reward outlet agrees with skeletal
        // (-Y,Z,-X), followed by scene yaw (playerYaw-actorYaw). Converting the
        // legacy baked (Y,Z,+X), (playerYaw+actorYaw) is this proper rotation,
        // not C48's local-Z reflection. Preserve the source's broken-vase
        // +32000 and cauldron-emitter +5000 before changing coordinate systems.
        const float turn=kTau*.5F-2*actor_yaw;
        if(!append(prop.first,prop.count,turn))return fail("invalid prop rest range");
        if(cauldron){
            if(!prop.animation_frames||prop.animation_frames>256||!prop.settled_frames||prop.settled_frames>256||
                !append(prop.animation_first,std::size_t(prop.count)*prop.animation_frames,turn)||
                !append(prop.settled_first,std::size_t(prop.count)*prop.settled_frames,turn))
                return fail("invalid cauldron animation range");
        }else{
            // HProps.FlipendoVaseBronze.generateobject explicitly adds 32000.
            if(!append(prop.broken_first,prop.broken_count,turn-2*32000*kTau/65536.0F))
                return fail("invalid broken vase range");
        }
        const float emission_turn=-2*(actor_yaw+(cauldron?5000*kTau/65536.0F:0));
        const Range emission{0,0,origin,std::sin(emission_turn),std::cos(emission_turn)};
        for(std::size_t bean=0;bean<geometry.beans.size();++bean)if(geometry.beans[bean].source_actor==prop.reference){
                if(bean_ranges.size()>=kMaximumProps*16||geometry.beans[bean].emission_points)
                    return fail("invalid prepared prop rewards");
                for(const auto& prior:bean_ranges)if(prior.first==bean)return fail("duplicate prop reward owner");
                for(const auto& p:{geometry.beans[bean].emission,geometry.beans[bean].position}){
                    for(float value:p)if(!std::isfinite(value)||std::abs(value)>10000)return fail("invalid prop reward position");
                    for(float value:Transform(p,emission))if(!std::isfinite(value)||std::abs(value)>10000)return fail("invalid corrected prop reward");
                }
                bean_ranges.emplace_back(bean,emission);
        }
        changed_props.push_back(index);
    }
    if(ranges.empty()){stats.valid=true;geometry.prop_orientation_restored=true;return stats;}
    std::sort(ranges.begin(),ranges.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    for(std::size_t i=1;i<ranges.size();++i)if(ranges[i].first<ranges[i-1].first+ranges[i-1].count)
        return fail("overlapping affected prop ranges");

    // Preserve the entire BSP prefix, even if a prop face exactly coincides
    // with a world face. Verify its layout using the original collision filter
    // without allocating or rebuilding the map collision vector.
    std::size_t world_collision_count=0;
    for(std::size_t first=0;first<begin;first+=3){
        if(geometry.vertices[first].polygon_flags&kPolyNotSolid)continue;
        const auto key=Key(geometry.vertices,first);
        Point normal{};
        if(!NormalizeVector(CrossVector(SubtractVector(key[1],key[0]),SubtractVector(key[2],key[0])),&normal))continue;
        if(world_collision_count>=geometry.collision.size()||geometry.collision[world_collision_count].vertices!=key)
            return fail("prepared BSP collision prefix mismatch");
        ++world_collision_count;
    }
    // Exact vertex keys, not spatial proximity, identify the independently
    // cached collision/aim copies. No nearby floor, wall or unrelated prop is
    // transformed. The bounded lookup is built before any geometry is changed.
    std::map<TriangleKey,std::size_t> owners;
    for(std::size_t r=0;r<ranges.size();++r){
        const auto& range=ranges[r];
        for(std::size_t first=range.first;first<range.first+range.count;first+=3){
            const auto key=Key(geometry.vertices,first);
            for(const auto& p:key){
                for(float value:p)if(!std::isfinite(value)||std::abs(value)>10000)return fail("invalid prop vertex");
                for(float value:Transform(p,range))if(!std::isfinite(value)||std::abs(value)>10000)return fail("invalid corrected vertex");
            }
            const auto [entry,inserted]=owners.emplace(key,r);
            if(!inserted)for(const auto& p:key){
                const auto a=Transform(p,range),b=Transform(p,ranges[entry->second]);
                for(unsigned axis=0;axis<3;++axis)if(std::abs(a[axis]-b[axis])>.00001F)
                    return fail("ambiguous shared prop face");
            }
        }
    }
    const auto repair=[&](std::vector<CollisionTriangle>& triangles,std::size_t first){
        std::size_t repaired=0;
        for(std::size_t i=first;i<triangles.size();++i){
            auto& triangle=triangles[i];
            const auto found=owners.find(triangle.vertices);
            if(found==owners.end())continue;
            for(auto& p:triangle.vertices)p=Transform(p,ranges[found->second]);
            Refresh(triangle);++repaired;
        }
        return repaired;
    };
    stats.collision_faces=repair(geometry.collision,world_collision_count);
    stats.aim_faces=repair(geometry.prop_aim,0);
    for(const auto& range:ranges)for(std::size_t i=range.first;i<range.first+range.count;++i){
        auto& vertex=geometry.vertices[i];const auto p=Transform(Position(vertex),range);
        for(unsigned axis=0;axis<3;++axis)vertex.position[axis]=p[axis];
    }
    for(const auto index:changed_props){
        auto& prop=geometry.challenge_props[index];
        prop.minimum=prop.maximum=Position(geometry.vertices[prop.first]);
        for(std::size_t i=prop.first;i<std::size_t(prop.first)+prop.count;++i)for(unsigned axis=0;axis<3;++axis){
            prop.minimum[axis]=std::min(prop.minimum[axis],geometry.vertices[i].position[axis]);
            prop.maximum[axis]=std::max(prop.maximum[axis],geometry.vertices[i].position[axis]);
        }
    }
    for(const auto& [index,range]:bean_ranges){
        auto& bean=geometry.beans[index];
        bean.emission=Transform(bean.emission,range);bean.position=Transform(bean.position,range);
    }
    stats.beans=bean_ranges.size();
    stats.props=changed_props.size();stats.valid=true;geometry.prop_orientation_restored=true;
    for(auto& prop:geometry.challenge_props)if(prop.cauldron){
        const auto tip=PreparedCauldronTip(geometry,prop);prop.cauldron_tip_valid=tip.valid;
        prop.cauldron_mouth=tip.mouth;prop.cauldron_direction=tip.direction;
    }
    return stats;
}

// Pair the original rim/base rest vertices with their corresponding vertices
// in the restored settled frame. This is a one-time topology query, not a
// hardcoded wick/mesh offset or work performed while rendering a frame.
CauldronTipPose PreparedCauldronTip(const PreparedGeometry& geometry,const ChallengeProp& prop){
    using namespace prop_orientation_restore;
    CauldronTipPose result;
    const std::size_t first=prop.first,settled=prop.settled_first,count=prop.count;
    if(!geometry.prop_orientation_restored||!prop.cauldron||!prop.settled_frames||!count||count>65536||
        first>geometry.vertices.size()||count>geometry.vertices.size()-first||
        settled>geometry.vertices.size()||count>geometry.vertices.size()-settled)return result;
    float low=10000,high=-10000;
    for(std::size_t i=0;i<count;++i){const float y=geometry.vertices[first+i].position[1];
        if(!std::isfinite(y))return result;low=std::min(low,y);high=std::max(high,y);}
    if(high-low<.01F)return result;
    std::map<Point,Point> rim,base;
    const float tolerance=std::max(.0001F,(high-low)*.001F);
    for(std::size_t i=0;i<count;++i){const auto rest=Position(geometry.vertices[first+i]);
        const auto posed=Position(geometry.vertices[settled+i]);
        for(float value:rest)if(!std::isfinite(value))return result;
        for(float value:posed)if(!std::isfinite(value))return result;
        if(rest[1]>=high-tolerance)rim.emplace(rest,posed);
        if(rest[1]<=low+tolerance)base.emplace(rest,posed);
    }
    if(rim.size()<3||base.empty())return result;
    const auto center=[](const auto& points){Point total{};for(const auto& [rest,posed]:points){(void)rest;
        for(unsigned axis=0;axis<3;++axis)total[axis]+=posed[axis]/float(points.size());}return total;};
    result.mouth=center(rim);auto direction=SubtractVector(result.mouth,center(base));direction[1]=0;
    if(!NormalizeVector(direction,&result.direction))return {};
    result.valid=true;return result;
}
