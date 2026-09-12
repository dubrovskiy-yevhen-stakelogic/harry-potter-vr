// Runtime-only restoration of immutable prepared fixture geometry. Include
// after PreparedGeometry and call once before transferring its vectors.
struct CandleRestoreStats {
    std::size_t fixtures=0,removed_faces=0,wick_anchors=0,collision_faces=0;
};

CandleRestoreStats RestorePreparedCandleFixtures(PreparedGeometry& geometry){
    CandleRestoreStats stats;
    const auto begin=std::size_t(geometry.map_vertices);
    const auto count=std::size_t(geometry.fixture_vertices);
    if(begin>geometry.vertices.size()||count>geometry.vertices.size()-begin||begin%3||count%3)return stats;
    const auto end=begin+count;
    const auto position=[](const GpuVertex& v)->const float(&)[3]{return v.position;};
    const auto writable_position=[](GpuVertex& v)->float(&)[3]{return v.position;};
    // The same fixture span exists in both maps. Strict complete-mesh matching
    // also covers cached map0 fixtures which have no ChallengeProp records.
    for(std::size_t first=begin;first+fixtures::kPlainCandleVertexCount<=end;first+=3){
        auto* vertices=geometry.vertices.data()+first;
        fixtures::PlainCandleMeshPatch patch;
        if(!fixtures::IdentifyPlainCandle(vertices,fixtures::kPlainCandleVertexCount,position,&patch))continue;
        if(!std::all_of(vertices,vertices+fixtures::kPlainCandleVertexCount,[&](const auto& v){
            return v.texture_layer==vertices[0].texture_layer&&v.has_lightmap==0;
        }))continue;
        auto minimum=patch.original_triangles[0][0],maximum=minimum;
        for(const auto& triangle:patch.original_triangles)for(const auto& p:triangle)for(unsigned axis=0;axis<3;++axis){
            minimum[axis]=std::min(minimum[axis],p[axis]);maximum[axis]=std::max(maximum[axis],p[axis]);
        }
        // Existing caches place the particle at the baked diamond's upper tip.
        // Match only that exact candle emitter; torches and nearby wicks remain
        // independent and no additional effect or light is created here.
        for(auto& flame:geometry.flames){
            if(flame.scale>=.5F)continue;
            float squared=0;
            for(unsigned axis=0;axis<3;++axis){const float d=flame.position[axis]-patch.old_tip[axis];squared+=d*d;}
            if(squared>.000025F)continue;
            flame.position=patch.wick;flame.position[1]+=fixtures::kCandleSupportBias;
            ++stats.wick_anchors;
        }
        const auto repair_collision=[&](std::vector<CollisionTriangle>& triangles){
            std::erase_if(triangles,[&](CollisionTriangle& triangle){
                bool outside=false;
                for(unsigned axis=0;axis<3;++axis)
                    outside=outside||triangle.vertices[0][axis]<minimum[axis]||triangle.vertices[0][axis]>maximum[axis];
                if(outside)return false;
                // Exact vertex identity avoids modifying surrounding BSP or
                // overlapping props; the cache stores these same float values.
                for(std::size_t face=0;face<patch.original_triangles.size();++face){
                    if(triangle.vertices!=patch.original_triangles[face])continue;
                    if(fixtures::IsPlainCandleFlameFace(face)){
                        // Collision requires nondegenerate triangles. No external
                        // collision indices exist until after this loader hook.
                        ++stats.collision_faces;return true;
                    }else{
                        for(auto& p:triangle.vertices)p[1]+=fixtures::kCandleSupportBias;
                        triangle.minimum[1]+=fixtures::kCandleSupportBias;
                        triangle.maximum[1]+=fixtures::kCandleSupportBias;
                    }
                    ++stats.collision_faces;return false;
                }
                return false;
            });
        };
        repair_collision(geometry.collision);repair_collision(geometry.prop_aim);
        fixtures::ApplyPlainCandlePatch(vertices,patch,writable_position);
        for(auto& prop:geometry.challenge_props){
            if(prop.first!=first||prop.count!=fixtures::kPlainCandleVertexCount)continue;
            prop.minimum=prop.maximum={vertices[0].position[0],vertices[0].position[1],vertices[0].position[2]};
            for(std::size_t i=1;i<fixtures::kPlainCandleVertexCount;++i)for(unsigned axis=0;axis<3;++axis){
                prop.minimum[axis]=std::min(prop.minimum[axis],vertices[i].position[axis]);
                prop.maximum[axis]=std::max(prop.maximum[axis],vertices[i].position[axis]);
            }
        }
        ++stats.fixtures;stats.removed_faces+=fixtures::kPlainCandleFlameFaces.size();
        first+=fixtures::kPlainCandleVertexCount-3;
    }
    return stats;
}
