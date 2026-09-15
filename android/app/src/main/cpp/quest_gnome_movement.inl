// Tut1Gnome has equal radius/half-height, so its capsule is a sphere. Keep
// ground and wall contacts at those authored dimensions, not Harry's height.
std::array<float,3> MoveChallengeGnome(const std::vector<CollisionTriangle>& triangles,
                                    const std::array<float,3>& feet,
                                    const std::array<float,3>& requested){
    auto position=feet;
    const float length=std::hypot(requested[0],requested[2]);
    if(!std::isfinite(length)||length<=.00001F||length>.5F)return position;
    const unsigned count=std::max(1U,static_cast<unsigned>(std::ceil(length/.05F)));
    const auto substep=ScaleVector(requested,1.0F/static_cast<float>(count));
    const auto try_position=[&](const std::array<float,3>& delta){
        auto candidate=AddVector(position,delta);
        constexpr float radius=gnome::kCollisionRadiusMeters;
        constexpr float step_height=.35F;
        float floor=-std::numeric_limits<float>::infinity();
        for(const auto& triangle:triangles){
            if(std::abs(triangle.normal[1])<kWalkableNormalY)continue;
            float height=0;
            if(CollisionTriangleHeightAtXZ(triangle,candidate[0],candidate[2],&height)&&
               height<=position[1]+step_height&&height>=position[1]-step_height)floor=std::max(floor,height);
        }
        if(!std::isfinite(floor))return false;
        // The sphere meets a tread before its centre crosses the riser.
        // Follow that support envelope over shallow tracks; retain real pits.
        for(const auto& triangle:triangles){
            if(std::abs(triangle.normal[1])<kWalkableNormalY||
               triangle.maximum[1]>position[1]+step_height||triangle.maximum[1]<floor)continue;
            const auto nearest=ClosestPointOnTriangle({candidate[0],triangle.maximum[1],candidate[2]},triangle);
            const float dx=nearest[0]-candidate[0],dz=nearest[2]-candidate[2],squared=dx*dx+dz*dz;
            if(squared<radius*radius)
                floor=std::max(floor,nearest[1]+std::sqrt(radius*radius-squared)-radius);
        }
        candidate[1]=floor;auto center=candidate;center[1]+=gnome::kCollisionHalfHeightMeters;
        constexpr float contact=radius-kGroundContactEpsilonMeters;
        for(const auto& triangle:triangles){
            const bool walkable=std::abs(triangle.normal[1])>=kWalkableNormalY;
            if((walkable&&triangle.maximum[1]<=center[1])||
               center[0]+radius<triangle.minimum[0]||center[0]-radius>triangle.maximum[0]||
               center[1]+radius<triangle.minimum[1]||center[1]-radius>triangle.maximum[1]||
               center[2]+radius<triangle.minimum[2]||center[2]-radius>triangle.maximum[2])continue;
            const auto closest=ClosestPointOnTriangle(center,triangle);
            if(walkable&&closest[1]<=center[1])continue;
            const auto separation=SubtractVector(center,closest);
            if(DotVector(separation,separation)<contact*contact)return false;
        }
        position=candidate;return true;
    };
    for(unsigned i=0;i<count;++i)if(!try_position(substep)){
        if(std::abs(substep[0])>.00001F)(void)try_position({substep[0],0,0});
        if(std::abs(substep[2])>.00001F)(void)try_position({0,0,substep[2]});
    }
    return position;
}
