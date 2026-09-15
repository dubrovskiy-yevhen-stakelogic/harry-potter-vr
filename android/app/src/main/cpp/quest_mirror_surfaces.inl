struct MirrorSurface {
    std::array<float,3> normal{},center{},minimum{},maximum{};
    std::vector<std::pair<unsigned,unsigned>> ranges;
    std::int32_t mover_reference=0;
    std::pair<unsigned,unsigned> water_draw{};
};
std::vector<MirrorSurface> FindMirrorSurfaces(const std::vector<GpuVertex>& vertices,unsigned count){
    std::vector<MirrorSurface> result;
    for(unsigned i=0;i+2<std::min<std::size_t>(count,vertices.size());i+=3){
        if(!(vertices[i].polygon_flags&0x04000000U)||(vertices[i].polygon_flags&1U))continue;
        const auto point=[&](unsigned j){const auto& p=vertices[i+j].position;return std::array<float,3>{p[0],p[1],p[2]};};
        const auto a=point(0),b=point(1),c=point(2);
        std::array<float,3> n{};
        if(!NormalizeVector(CrossVector(SubtractVector(b,a),SubtractVector(c,a)),&n))continue;
        auto found=std::ranges::find_if(result,[&](const auto& m){return std::abs(DotVector(n,m.normal))>.999F&&std::abs(DotVector(n,SubtractVector(a,m.center)))<.005F;});
        if(found==result.end()){result.push_back({n,a,a,a,{}});found=std::prev(result.end());}
        for(const auto& p:{a,b,c})for(unsigned axis=0;axis<3;++axis){found->minimum[axis]=std::min(found->minimum[axis],p[axis]);found->maximum[axis]=std::max(found->maximum[axis],p[axis]);}
        if(!found->ranges.empty()&&found->ranges.back().first+found->ranges.back().second==i)found->ranges.back().second+=3;
        else found->ranges.emplace_back(i,3);
    }
    for(auto& m:result)m.center=ScaleVector(AddVector(m.minimum,m.maximum),.5F);
    return result;
}
void BindMirrorMovers(std::vector<MirrorSurface>& mirrors,const std::vector<DoorDraw>& doors){
    for(auto& mirror:mirrors){
        if(std::abs(mirror.normal[1])>.02F)continue;
        float nearest=9;
        for(const auto& door:doors)if(door.tag=="mirror1"||door.tag=="mirror3"){
            const auto delta=SubtractVector(door.pivot,mirror.center);const float distance=DotVector(delta,delta);
            if(distance<nearest){nearest=distance;mirror.mover_reference=door.actor_reference;}
        }
    }
}
bool MirrorClosed(const DoorDraw& door){return !door.opening&&door.motion.current==0&&!door.motion.moving;}
void AppendWaterSurfaceGeometry(std::vector<MirrorSurface>& mirrors,std::vector<GpuVertex>& vertices){
    constexpr unsigned subdivisions=16;
    for(auto& mirror:mirrors){
        if(std::abs(mirror.normal[1])<.9F)continue;
        const auto first=static_cast<unsigned>(vertices.size());
        for(const auto& range:mirror.ranges)for(unsigned i=range.first;i+2<range.first+range.second;i+=3){
            const GpuVertex a=vertices[i],b=vertices[i+1],c=vertices[i+2];
            const auto point=[&](unsigned x,unsigned y){
                GpuVertex v=a;const float u=float(x)/subdivisions,w=float(y)/subdivisions;
                for(unsigned axis=0;axis<3;++axis)v.position[axis]=a.position[axis]+u*(b.position[axis]-a.position[axis])+w*(c.position[axis]-a.position[axis]);
                return v;
            };
            for(unsigned y=0;y<subdivisions;++y)for(unsigned x=0;x+y<subdivisions;++x){
                vertices.push_back(point(x,y));vertices.push_back(point(x+1,y));vertices.push_back(point(x,y+1));
                if(x+y+1<subdivisions){vertices.push_back(point(x+1,y));vertices.push_back(point(x+1,y+1));vertices.push_back(point(x,y+1));}
            }
        }
        mirror.water_draw={first,static_cast<unsigned>(vertices.size())-first};
    }
}
bool IsMirrorBlocker(const DoorDraw& door,const std::vector<MirrorSurface>& mirrors){
    return std::ranges::any_of(mirrors,[&](const auto& mirror){return mirror.mover_reference==door.actor_reference;});
}
