bool ReadCheckpointCharacters(std::istream& input,std::vector<CharacterDraw>& actors,bool allow_restored_students){
    unsigned count=0;
    if(!(input>>count)||count>actors.size())return false;
    const auto restored_student=[](const auto& actor){
        const auto name=AsciiFold(actor.class_name);
        return name.starts_with("harrypotter.gen_fem_")||name.starts_with("harrypotter.gen_male_");
    };
    std::set<std::int32_t> seen;
    auto next=actors;
    for(unsigned i=0;i<count;++i){
        std::int32_t ref=0;bool enabled=false;float yaw=0;std::array<float,3> offset{};
        if(!(input>>ref>>enabled>>yaw>>offset[0]>>offset[1]>>offset[2])||!seen.insert(ref).second||
            !std::isfinite(yaw)||!std::ranges::all_of(offset,[](float v){return std::isfinite(v);}))return false;
        const auto actor=std::ranges::find_if(next,[&](const auto& a){return a.actor_reference==ref;});
        if(actor==next.end())return false;
        actor->enabled=enabled;actor->yaw=actor->desired_yaw=yaw;actor->cutscene_offset=offset;
    }
    for(const auto& actor:next)if(!seen.contains(actor.actor_reference)&&
        (!allow_restored_students||!restored_student(actor)))return false;
    actors=std::move(next);return true;
}
