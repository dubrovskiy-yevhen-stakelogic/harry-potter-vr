// Included after PreparedGeometry inside quest_scene.cpp's private namespace.
// This schema stores fresh CPU preparation, never a running scene or save state.
namespace prepared_codec {
constexpr std::uint32_t kSchema = 1;
constexpr std::uint64_t kMaxVertices = 22000000;
constexpr std::uint64_t kMaxTriangles = 4000000;
constexpr std::uint64_t kMaxBytes = 1024ULL*1024ULL*1024ULL;
// Includes all menu variants without reallocating the cooked world buffer.
constexpr std::uint64_t kMaxFrontendVertexReserve = 1048576;
// Transient original gnome clips are appended after cache validation on map 1.
constexpr std::uint64_t kMaxRuntimeVertexReserve = kMaxFrontendVertexReserve + 600000;
constexpr std::uint64_t kTextureCapacityBytes = 256ULL*256ULL*256ULL*4ULL;
constexpr std::uint32_t kMaxString = 4096;
using Reader = cache::Reader;
using Writer = cache::Writer;

static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
static_assert(std::is_trivially_copyable_v<GpuVertex> && std::is_standard_layout_v<GpuVertex>);
static_assert(sizeof(GpuVertex)==44 && offsetof(GpuVertex,position)==0 &&
    offsetof(GpuVertex,texture_uv)==12 && offsetof(GpuVertex,lightmap_uv)==20 &&
    offsetof(GpuVertex,texture_layer)==28 && offsetof(GpuVertex,polygon_flags)==32 &&
    offsetof(GpuVertex,has_lightmap)==36 && offsetof(GpuVertex,packed_light)==40);
static_assert(std::is_trivially_copyable_v<CollisionTriangle> &&
    std::is_standard_layout_v<CollisionTriangle>);
static_assert(sizeof(CollisionTriangle)==72 && offsetof(CollisionTriangle,vertices)==0 &&
    offsetof(CollisionTriangle,minimum)==36 && offsetof(CollisionTriangle,maximum)==48 &&
    offsetof(CollisionTriangle,normal)==60);

void Vec(Writer& w,const std::array<float,3>& p){for(float f:p)w.F32(f);}
std::array<float,3> Vec(Reader& r){return {r.F32(),r.F32(),r.F32()};}
void Key(Writer& w,const movers::Key& p){Vec(w,p.offset_unreal);Vec(w,p.rotation_units);}
movers::Key Key(Reader& r){movers::Key p;p.offset_unreal=Vec(r);p.rotation_units=Vec(r);return p;}
void String(Writer& w,const std::string& p){w.String(p,kMaxString);}
std::string String(Reader& r){return r.String(kMaxString);}

template<class T> void RawArray(Writer& w,const std::vector<T>& values){
    static_assert(std::is_same_v<T,GpuVertex>||std::is_same_v<T,CollisionTriangle>);
    w.U64(values.size());if(!values.empty())w.Raw(values.data(),values.size()*sizeof(T));
}
template<class T> std::vector<T> RawArray(Reader& r,std::uint64_t maximum,
    std::uint64_t extra_capacity=0){
    static_assert(std::is_same_v<T,GpuVertex>||std::is_same_v<T,CollisionTriangle>);
    if(extra_capacity>kMaxRuntimeVertexReserve||
        (extra_capacity&&!std::is_same_v<T,GpuVertex>))
        throw cache::CacheError("invalid prepared geometry reserve");
    const auto count=r.U64();r.CheckCount(count,maximum,sizeof(T));
    if(count>r.Remaining()/sizeof(T))throw cache::CacheError("truncated prepared geometry array");
    // Reserve before reading: appending frontend geometry must not duplicate
    // the entire cooked vertex array during vector growth.
    std::vector<T> values;
    values.reserve(static_cast<std::size_t>(count+extra_capacity));
    values.resize(static_cast<std::size_t>(count));
    if(count)r.Raw(values.data(),count*sizeof(T));return values;
}
std::vector<std::uint8_t> TextureArray(Reader& r,std::uint64_t expected,bool frontend_reserve){
    const auto count=r.U64();r.CheckCount(count,expected);
    if(count!=expected||count>kTextureCapacityBytes)
        throw cache::CacheError("prepared texture payload size mismatch");
    std::vector<std::uint8_t> values;
    values.reserve(static_cast<std::size_t>(frontend_reserve?kTextureCapacityBytes:count));
    values.resize(static_cast<std::size_t>(count));
    if(count)r.Raw(values.data(),count);return values;
}
template<class T,class Write> void Array(Writer& w,const std::vector<T>& values,Write write){
    w.U32(static_cast<std::uint32_t>(values.size()));
    for(const auto& value:values)write(w,value);
}
template<class T,class Read> std::vector<T> Array(Reader& r,std::uint32_t maximum,Read read){
    const auto count=r.U32();r.CheckCount(count,maximum,4);
    std::vector<T> values;values.reserve(count);
    for(std::uint32_t i=0;i<count;++i)values.push_back(read(r));return values;
}

void Door(Writer& w,const DoorDraw& d){
    Vec(w,d.placement.pivot_scene);Vec(w,d.placement.base_rotation_units);
    w.F32(d.placement.player_yaw);w.F32(d.placement.meters_per_unit);
    for(const auto& key:d.motion.keys)Key(w,key);
    w.U32(d.motion.count);w.U32(d.motion.current);w.U32(d.motion.target);
    Key(w,d.motion.source);Key(w,d.motion.pose);w.F32(d.motion.phase);w.F32(d.motion.seconds);
    w.Bool(d.motion.moving);w.Bool(d.motion.looping);w.Bool(d.motion.chain);w.I32(d.motion.direction);
    w.Bool(d.challenge);w.Bool(d.collision_only);w.F32(d.open_seconds);w.F32(d.close_seconds);
    w.I32(d.actor_reference);String(w,d.initial_state);
    w.Bool(d.looping);w.Bool(d.loop_started);w.Bool(d.grid);w.Bool(d.completion_sent);
    w.F32(d.hold);w.F32(d.stay_open);w.F32(d.grid_increment);Vec(w,d.grid_offset);Vec(w,d.grid_target);
    w.U32(d.first_vertex);w.U32(d.vertex_count);Vec(w,d.pivot);Vec(w,d.open_offset);
    w.F32(d.open_yaw);w.F32(d.phase);w.F32(d.duration);w.Bool(d.opening);String(w,d.tag);
}
DoorDraw Door(Reader& r){
    DoorDraw d;d.placement.pivot_scene=Vec(r);d.placement.base_rotation_units=Vec(r);
    d.placement.player_yaw=r.F32();d.placement.meters_per_unit=r.F32();
    for(auto& key:d.motion.keys)key=Key(r);
    d.motion.count=r.U32();d.motion.current=r.U32();d.motion.target=r.U32();
    d.motion.source=Key(r);d.motion.pose=Key(r);d.motion.phase=r.F32();d.motion.seconds=r.F32();
    d.motion.moving=r.Bool();d.motion.looping=r.Bool();d.motion.chain=r.Bool();d.motion.direction=r.I32();
    d.challenge=r.Bool();d.collision_only=r.Bool();d.open_seconds=r.F32();d.close_seconds=r.F32();
    d.actor_reference=r.I32();d.initial_state=String(r);
    d.looping=r.Bool();d.loop_started=r.Bool();d.grid=r.Bool();d.completion_sent=r.Bool();
    d.hold=r.F32();d.stay_open=r.F32();d.grid_increment=r.F32();d.grid_offset=Vec(r);d.grid_target=Vec(r);
    d.first_vertex=r.U32();d.vertex_count=r.U32();d.pivot=Vec(r);d.open_offset=Vec(r);
    d.open_yaw=r.F32();d.phase=r.F32();d.duration=r.F32();d.opening=r.Bool();d.tag=String(r);return d;
}
void Character(Writer& w,const CharacterDraw& a){
    w.Bool(a.player);w.Bool(a.flying);w.Bool(a.child_template);w.Bool(a.enabled);
    w.U32(static_cast<std::uint32_t>(a.clips.size()));
    for(const auto& [name,c]:a.clips){String(w,name);w.U32(c.first_vertex);w.F32(c.duration);w.U32(c.frame_count);}
    String(w,a.active_clip);w.F32(a.animation_time);w.F32(a.base_yaw);w.F32(a.yaw);w.F32(a.desired_yaw);
    w.U32(a.first_vertex);w.U32(a.vertex_count);w.I32(a.actor_reference);String(w,a.object_name);String(w,a.class_name);
    Vec(w,a.collision_center);Vec(w,a.base_origin);Vec(w,a.cutscene_offset);Vec(w,a.visual_minimum);Vec(w,a.visual_maximum);
    w.F32(a.collision_radius);w.F32(a.collision_min_y);w.F32(a.collision_max_y);w.Bool(a.staged);
}
CharacterDraw Character(Reader& r){
    CharacterDraw a;a.player=r.Bool();a.flying=r.Bool();a.child_template=r.Bool();a.enabled=r.Bool();
    const auto count=r.U32();r.CheckCount(count,256,16);
    for(std::uint32_t i=0;i<count;++i){auto name=String(r);CharacterClip c;
        c.first_vertex=r.U32();c.duration=r.F32();c.frame_count=r.U32();
        if(!a.clips.emplace(std::move(name),c).second)throw cache::CacheError("duplicate prepared animation clip");}
    a.active_clip=String(r);a.animation_time=r.F32();a.base_yaw=r.F32();a.yaw=r.F32();a.desired_yaw=r.F32();
    a.first_vertex=r.U32();a.vertex_count=r.U32();a.actor_reference=r.I32();a.object_name=String(r);a.class_name=String(r);
    a.collision_center=Vec(r);a.base_origin=Vec(r);a.cutscene_offset=Vec(r);a.visual_minimum=Vec(r);a.visual_maximum=Vec(r);
    a.collision_radius=r.F32();a.collision_min_y=r.F32();a.collision_max_y=r.F32();a.staged=r.Bool();return a;
}
void Bean(Writer& w,const BeanDraw& b){
    w.I32(b.actor_reference);w.U32(b.first);w.U32(b.count);Vec(w,b.position);w.U32(b.kind);w.U32(b.frames);
    w.F32(b.duration);w.F32(b.yaw);w.I32(b.source_actor);Vec(w,b.emission);w.F32(b.emission_time);
}
BeanDraw Bean(Reader& r){BeanDraw b;
    b.actor_reference=r.I32();b.first=r.U32();b.count=r.U32();b.position=Vec(r);b.kind=r.U32();b.frames=r.U32();
    b.duration=r.F32();b.yaw=r.F32();b.source_actor=r.I32();b.emission=Vec(r);b.emission_time=r.F32();return b;
}
void Knight(Writer& w,const KnightDraw& k){Vec(w,k.origin);w.F32(k.yaw);w.F32(k.scale);w.F32(k.time);
    w.U32(k.layer);w.U32(k.first);w.U32(k.count);w.U32(k.frames);w.I32(k.last_sound_phase);}
KnightDraw Knight(Reader& r){KnightDraw k;k.origin=Vec(r);k.yaw=r.F32();k.scale=r.F32();k.time=r.F32();
    k.layer=r.U32();k.first=r.U32();k.count=r.U32();k.frames=r.U32();k.last_sound_phase=r.I32();return k;}
void Prop(Writer& w,const ChallengeProp& p){w.I32(p.reference);String(w,p.name);w.U32(p.first);w.U32(p.count);
    Vec(w,p.minimum);Vec(w,p.maximum);w.Bool(p.breakable);w.Bool(p.spell_target);
    w.U32(p.animation_first);w.U32(p.animation_frames);w.U32(p.settled_first);w.U32(p.settled_frames);
    w.U32(p.broken_first);w.U32(p.broken_count);w.F32(p.animation_duration);w.F32(p.settled_duration);
    w.Bool(p.cauldron);w.Bool(p.savebook);}
ChallengeProp Prop(Reader& r){ChallengeProp p;p.reference=r.I32();p.name=String(r);p.first=r.U32();p.count=r.U32();
    p.minimum=Vec(r);p.maximum=Vec(r);p.breakable=r.Bool();p.spell_target=r.Bool();
    p.animation_first=r.U32();p.animation_frames=r.U32();p.settled_first=r.U32();p.settled_frames=r.U32();
    p.broken_first=r.U32();p.broken_count=r.U32();p.animation_duration=r.F32();p.settled_duration=r.F32();
    p.cauldron=r.Bool();p.savebook=r.Bool();return p;}
void Flame(Writer& w,const FlameEmitter& f){Vec(w,f.position);w.F32(f.phase);w.F32(f.scale);}
FlameEmitter Flame(Reader& r){FlameEmitter f;f.position=Vec(r);f.phase=r.F32();f.scale=r.F32();return f;}
void Glow(Writer& w,const GlowEmitter& g){Vec(w,g.position);w.F32(g.radius);w.F32(g.intensity);w.F32(g.phase);}
GlowEmitter Glow(Reader& r){GlowEmitter g;g.position=Vec(r);g.radius=r.F32();g.intensity=r.F32();g.phase=r.F32();return g;}
void Target(Writer& w,const SpellTargetDescriptor& t){w.I32(t.actor_reference);Vec(w,t.bounds_min);Vec(w,t.bounds_max);w.Bool(t.enabled);}
SpellTargetDescriptor Target(Reader& r){SpellTargetDescriptor t;t.actor_reference=r.I32();t.bounds_min=Vec(r);t.bounds_max=Vec(r);t.enabled=r.Bool();return t;}

bool Finite(float f){return std::isfinite(f);}
template<class Range> bool FiniteVector(const Range& p){
    return std::ranges::all_of(p,[](float f){return std::isfinite(f)&&std::abs(f)<=10000.0F;});
}
bool Bounds(const std::array<float,3>& low,const std::array<float,3>& high){
    if(!FiniteVector(low)||!FiniteVector(high))return false;
    for(unsigned axis=0;axis<3;++axis)if(low[axis]>high[axis])return false;return true;
}
bool Text(const std::string& s,bool required=false){
    return (!required||!s.empty())&&s.size()<=kMaxString&&s.find('\0')==std::string::npos;
}
bool Range(std::uint32_t first,std::uint64_t count,std::uint64_t total){
    return count>0&&first%3==0&&count%3==0&&first<=total&&count<=total-first;
}
bool Collision(const std::vector<CollisionTriangle>& triangles){
    if(triangles.size()>kMaxTriangles)return false;
    for(const auto& t:triangles){
        if(!Bounds(t.minimum,t.maximum)||!FiniteVector(t.normal))return false;
        const float norm=DotVector(t.normal,t.normal);
        if(norm<0.998F||norm>1.002F)return false;
        auto low=t.vertices[0],high=low;
        for(const auto& p:t.vertices){if(!FiniteVector(p))return false;
            for(unsigned axis=0;axis<3;++axis){low[axis]=std::min(low[axis],p[axis]);high[axis]=std::max(high[axis],p[axis]);}}
        for(unsigned axis=0;axis<3;++axis)if(std::abs(low[axis]-t.minimum[axis])>.0001F||
            std::abs(high[axis]-t.maximum[axis])>.0001F)return false;
        const auto ab=SubtractVector(t.vertices[1],t.vertices[0]);
        const auto ac=SubtractVector(t.vertices[2],t.vertices[0]);
        const std::array<float,3> cross{ab[1]*ac[2]-ab[2]*ac[1],
            ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]};
        std::array<float,3> expected{};
        if(!NormalizeVector(cross,&expected)||DotVector(expected,t.normal)<.999F)return false;
    }return true;
}
bool Dimensions(const PreparedGeometry& g){
    return g.texture_width==256&&g.texture_height==256&&g.texture_layers>0&&g.texture_layers<=256&&
        g.lightmap_width>0&&g.lightmap_height>0&&g.lightmap_width<=16384&&g.lightmap_height<=16384&&
        std::uint64_t(g.lightmap_width)*g.lightmap_height*4<=kMaxBytes/2;
}
} // namespace prepared_codec

bool ValidatePreparedGeometry(const PreparedGeometry& g,std::string* reason=nullptr){
    using namespace prepared_codec;
    if(reason)reason->clear();
    const auto reject=[&](const char* detail){if(reason)*reason=detail;return false;};
    if(!Dimensions(g)||g.vertices.empty()||g.vertices.size()>kMaxVertices||g.vertices.size()%3||
        !g.map_vertices||g.map_vertices%3||!g.fixture_vertices||g.fixture_vertices%3||
        std::uint64_t(g.map_vertices)+g.fixture_vertices>g.vertices.size()||!g.fixture_actors||g.fixture_actors>32768||
        g.textures.size()!=std::uint64_t(g.texture_width)*g.texture_height*g.texture_layers*4||
        g.lightmaps.size()!=std::uint64_t(g.lightmap_width)*g.lightmap_height*4||
        !g.decoded_lightmaps||g.decoded_lightmaps>1000000||g.decoded_textures>1000000||g.fallback_materials>1000000||
        g.doors.size()>8192||g.characters.empty()||g.characters.size()>128||g.beans.size()>8192||
        g.knights.size()>256||g.challenge_props.size()>32768||g.flames.size()>65536||g.glows.size()>65536||
        g.targets.size()!=g.characters.size()||g.collision.empty()||!Collision(g.collision)||!Collision(g.prop_aim)||
        g.animation_frames!=kCharacterAnimationFrameCount||!g.character_frame_vertices)return reject("scene dimensions or counts");
    const std::uint64_t major_bytes=g.vertices.size()*sizeof(GpuVertex)+g.textures.size()+g.lightmaps.size()+
        (g.collision.size()+g.prop_aim.size())*sizeof(CollisionTriangle);
    if(major_bytes>kMaxBytes)return reject("scene byte budget");
    for(const auto& v:g.vertices){
        if(!FiniteVector(v.position)||v.texture_layer>=g.texture_layers||v.has_lightmap>1)return reject("vertex attributes");
        for(float uv:v.texture_uv)if(!Finite(uv))return false;
        for(float uv:v.lightmap_uv)if(!Finite(uv))return false;
    }
    const auto fixture_end=std::uint64_t(g.map_vertices)+g.fixture_vertices;
    std::uint64_t expected=fixture_end;
    std::set<std::int32_t> ids;
    for(const auto& d:g.doors){
        if(d.actor_reference<=0||!ids.insert(d.actor_reference).second||d.first_vertex!=expected||
            !Range(d.first_vertex,d.vertex_count,g.vertices.size())||!movers::ValidCount(d.motion)||
            (d.motion.direction!=-1&&d.motion.direction!=1)||!FiniteVector(d.placement.pivot_scene)||
            !std::ranges::all_of(d.placement.base_rotation_units,Finite)||!Finite(d.placement.player_yaw)||
            !Finite(d.placement.meters_per_unit)||d.placement.meters_per_unit<=0||d.placement.meters_per_unit>1||
            !Finite(d.motion.phase)||d.motion.phase<0||d.motion.phase>1||!Finite(d.motion.seconds)||d.motion.seconds<0||
            !FiniteVector(d.grid_offset)||!FiniteVector(d.grid_target)||!FiniteVector(d.pivot)||!FiniteVector(d.open_offset)||
            !Finite(d.open_seconds)||d.open_seconds<0||!Finite(d.close_seconds)||d.close_seconds<0||
            !Finite(d.hold)||!Finite(d.stay_open)||!Finite(d.grid_increment)||d.grid_increment<=0||
            !Finite(d.open_yaw)||!Finite(d.phase)||d.phase<0||d.phase>1||!Finite(d.duration)||d.duration<0||
            !Text(d.tag)||!Text(d.initial_state))return reject("mover layout or state");
        const auto valid_key=[](const movers::Key& k){return std::ranges::all_of(k.offset_unreal,Finite)&&
            std::ranges::all_of(k.rotation_units,Finite);};
        if(!std::ranges::all_of(d.motion.keys,valid_key)||!valid_key(d.motion.source)||!valid_key(d.motion.pose))return false;
        expected+=d.vertex_count;
    }
    std::map<std::uint32_t,std::uint64_t> clips;
    ids.clear();
    for(const auto& a:g.characters){
        if(a.actor_reference<=0||!ids.insert(a.actor_reference).second||a.clips.empty()||a.clips.size()>256||
            !a.clips.contains(a.active_clip)||!Text(a.active_clip,true)||!Text(a.object_name,true)||!Text(a.class_name,true)||
            !Range(a.first_vertex,a.vertex_count,g.vertices.size())||!Finite(a.animation_time)||a.animation_time<0||
            !Finite(a.base_yaw)||!Finite(a.yaw)||!Finite(a.desired_yaw)||!FiniteVector(a.collision_center)||
            !FiniteVector(a.base_origin)||!FiniteVector(a.cutscene_offset)||!Bounds(a.visual_minimum,a.visual_maximum)||
            !Finite(a.collision_radius)||a.collision_radius<=0||!Finite(a.collision_min_y)||!Finite(a.collision_max_y)||
            a.collision_max_y<a.collision_min_y)return reject("character layout or bounds");
        for(const auto& [name,c]:a.clips){const auto count=std::uint64_t(a.vertex_count)*c.frame_count;
            if(!Text(name,true)||!Finite(c.duration)||c.duration<=0||!c.frame_count||c.frame_count>4096||
                !Range(c.first_vertex,count,g.vertices.size())||!clips.emplace(c.first_vertex,count).second)return reject("character clip range");}
    }
    if(g.character_frame_vertices!=g.characters.front().vertex_count)return reject("first character vertex count");
    for(const auto& [first,count]:clips){if(first!=expected)return reject("character clip continuity");expected+=count;}
    ids.clear();
    for(const auto& b:g.beans){
        const auto count=std::uint64_t(b.count)*b.frames;
        if(b.actor_reference<=0||!ids.insert(b.actor_reference).second||b.source_actor<0||b.kind>4||
            b.first!=expected||!b.frames||b.frames>4096||!Range(b.first,count,g.vertices.size())||
            !FiniteVector(b.position)||!FiniteVector(b.emission)||!Finite(b.duration)||b.duration<=0||
            !Finite(b.yaw)||!Finite(b.emission_time)||b.emission_time<0||b.emission_time>1)return reject("pickup layout or state");
        expected+=count;
    }
    if(expected!=g.vertices.size())return reject("unreferenced vertex tail");
    for(const auto& k:g.knights){const auto count=std::uint64_t(k.count)*k.frames;
        if(!FiniteVector(k.origin)||!Finite(k.yaw)||!Finite(k.scale)||k.scale<=0||!Finite(k.time)||k.time<0||
            k.layer>=g.texture_layers||!k.frames||k.frames>4096||k.first<g.map_vertices||
            !Range(k.first,count,fixture_end)||k.last_sound_phase<-1||k.last_sound_phase>64)return false;}
    ids.clear();
    for(const auto& p:g.challenge_props){
        if(p.reference<=0||!ids.insert(p.reference).second||!Text(p.name,true)||
            p.first<g.map_vertices||!Range(p.first,p.count,fixture_end)||!Bounds(p.minimum,p.maximum)||
            !Finite(p.animation_duration)||p.animation_duration<=0||!Finite(p.settled_duration)||p.settled_duration<=0||
            p.animation_frames>4096||p.settled_frames>4096){
            if(reason)*reason="prop layout or bounds: "+std::to_string(p.reference)+" "+p.name+
                " first="+std::to_string(p.first)+" count="+std::to_string(p.count);
            return false;
        }
        if(p.animation_frames&&!Range(p.animation_first,std::uint64_t(p.count)*p.animation_frames,fixture_end))return false;
        if(p.settled_frames&&!Range(p.settled_first,std::uint64_t(p.count)*p.settled_frames,fixture_end))return false;
        if(p.broken_count&&!Range(p.broken_first,p.broken_count,fixture_end))return false;
    }
    for(const auto& f:g.flames)if(!FiniteVector(f.position)||!Finite(f.phase)||!Finite(f.scale)||f.scale<=0)return false;
    for(const auto& glow:g.glows)if(!FiniteVector(glow.position)||!Finite(glow.phase)||!Finite(glow.radius)||
        glow.radius<=0||!Finite(glow.intensity)||glow.intensity<0)return false;
    for(std::size_t i=0;i<g.targets.size();++i){const auto& t=g.targets[i];
        if(t.actor_reference!=g.characters[i].actor_reference||!Bounds(t.bounds_min,t.bounds_max))return false;}
    return true;
}

[[maybe_unused]] void WritePreparedGeometry(cache::Writer& w,const PreparedGeometry& g){
    using namespace prepared_codec;
    if(!ValidatePreparedGeometry(g))throw cache::CacheError("invalid prepared geometry");
    w.U32(kSchema);
    w.U32(g.texture_width);w.U32(g.texture_height);w.U32(g.texture_layers);
    w.U32(g.lightmap_width);w.U32(g.lightmap_height);
    w.U32(g.map_vertices);w.U32(g.fixture_vertices);w.U32(g.fixture_actors);
    w.U32(g.character_frame_vertices);w.U32(g.animation_frames);
    w.U32(g.decoded_lightmaps);w.U32(g.decoded_textures);w.U32(g.fallback_materials);
    RawArray(w,g.vertices);w.Bytes(g.textures);w.Bytes(g.lightmaps);RawArray(w,g.collision);RawArray(w,g.prop_aim);
    Array(w,g.doors,[](Writer& a,const auto& b){Door(a,b);});
    Array(w,g.characters,[](Writer& a,const auto& b){Character(a,b);});
    Array(w,g.beans,[](Writer& a,const auto& b){Bean(a,b);});
    Array(w,g.knights,[](Writer& a,const auto& b){Knight(a,b);});
    Array(w,g.challenge_props,[](Writer& a,const auto& b){Prop(a,b);});
    Array(w,g.flames,[](Writer& a,const auto& b){Flame(a,b);});
    Array(w,g.glows,[](Writer& a,const auto& b){Glow(a,b);});
    Array(w,g.targets,[](Writer& a,const auto& b){Target(a,b);});
}
PreparedGeometry ReadPreparedGeometry(cache::Reader& r,std::uint64_t frontendVertexReserve=0){
    using namespace prepared_codec;
    if(frontendVertexReserve>kMaxRuntimeVertexReserve)
        throw cache::CacheError("frontend vertex reserve exceeds limit");
    if(r.U32()!=kSchema)throw cache::CacheError("unsupported prepared geometry schema");
    PreparedGeometry g;
    g.texture_width=r.U32();g.texture_height=r.U32();g.texture_layers=r.U32();
    g.lightmap_width=r.U32();g.lightmap_height=r.U32();
    g.map_vertices=r.U32();g.fixture_vertices=r.U32();g.fixture_actors=r.U32();
    g.character_frame_vertices=r.U32();g.animation_frames=r.U32();
    g.decoded_lightmaps=r.U32();g.decoded_textures=r.U32();g.fallback_materials=r.U32();
    if(!Dimensions(g))throw cache::CacheError("invalid prepared image dimensions");
    g.vertices=RawArray<GpuVertex>(r,kMaxVertices,frontendVertexReserve);
    const auto texture_bytes=std::uint64_t(g.texture_width)*g.texture_height*g.texture_layers*4;
    const auto lightmap_bytes=std::uint64_t(g.lightmap_width)*g.lightmap_height*4;
    g.textures=TextureArray(r,texture_bytes,frontendVertexReserve!=0);g.lightmaps=r.Bytes(lightmap_bytes);
    if(g.textures.size()!=texture_bytes||g.lightmaps.size()!=lightmap_bytes)
        throw cache::CacheError("prepared image payload size mismatch");
    g.collision=RawArray<CollisionTriangle>(r,kMaxTriangles);
    g.prop_aim=RawArray<CollisionTriangle>(r,kMaxTriangles);
    g.doors=Array<DoorDraw>(r,8192,[](Reader& a){return Door(a);});
    g.characters=Array<CharacterDraw>(r,128,[](Reader& a){return Character(a);});
    g.beans=Array<BeanDraw>(r,8192,[](Reader& a){return Bean(a);});
    g.knights=Array<KnightDraw>(r,256,[](Reader& a){return Knight(a);});
    g.challenge_props=Array<ChallengeProp>(r,32768,[](Reader& a){return Prop(a);});
    g.flames=Array<FlameEmitter>(r,65536,[](Reader& a){return Flame(a);});
    g.glows=Array<GlowEmitter>(r,65536,[](Reader& a){return Glow(a);});
    g.targets=Array<SpellTargetDescriptor>(r,128,[](Reader& a){return Target(a);});
    return g; // Caller verifies Reader::Finish and Validate before publishing.
}
