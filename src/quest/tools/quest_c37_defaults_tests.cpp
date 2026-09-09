#include "hpvr/quest_vr_settings.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace hpvr::quest;
namespace {
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void CheckDefaults(const VrSettings& v){
    Check(v.render_scale==100&&v.ssr==30&&v.relaxed_lesson&&v.first_person_cutscenes&&
          !v.welcome_seen&&v.generation==0,"fresh settings match release preferences without suppressing welcome");
}
}
int main(){try{
    const auto root=std::filesystem::temp_directory_path()/
        ("hpvr-c37-defaults-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(std::filesystem::create_directory(root),"create isolated test root");
    CheckDefaults(VrSettings{});
    CheckDefaults(ReadVrSettings(root/"missing"));
    CheckDefaults(ReadVrSettings(root));
    {std::ofstream f(root/"vr-settings.0");f<<"HPVR_VR3 100 30 2 0 1 0 0\n";}
    {std::ofstream f(root/"vr-settings.1");f<<"HPVR_VR3 broken\n";}
    CheckDefaults(ReadVrSettings(root));

    VrSettings explicit_off;
    explicit_off.render_scale=175;explicit_off.ssr=0;
    explicit_off.relaxed_lesson=false;explicit_off.first_person_cutscenes=false;
    explicit_off.welcome_seen=true;
    Check(WriteVrSettings(root,explicit_off),"persist explicit original and theatrical choices");
    auto read=ReadVrSettings(root);
    Check(read.generation==1&&read.render_scale==175&&read.ssr==0&&!read.relaxed_lesson&&
          !read.first_person_cutscenes&&read.welcome_seen,"saved OFF choices override fresh defaults on reload");
    {std::ofstream f(root/"vr-settings.0");f<<"HPVR_VR3 100 30 2 1 1 1 0\n";}
    read=ReadVrSettings(root);
    Check(read.generation==1&&!read.relaxed_lesson&&!read.first_person_cutscenes,
          "corrupt newer bank cannot override explicit OFF choices");

    const auto legacy1=root/"legacy1";std::filesystem::create_directory(legacy1);
    {std::ofstream f(legacy1/"vr-settings.1");f<<"HPVR_VR1 125 55 7 "<<VrSettingsChecksum(125,55,7)<<'\n';}
    read=ReadVrSettings(legacy1);
    Check(read.generation==7&&read.render_scale==125&&read.ssr==55&&read.relaxed_lesson&&
          read.first_person_cutscenes&&!read.welcome_seen,"VR1 preserves rendering and inherits only absent flags");

    const auto legacy2=root/"legacy2";std::filesystem::create_directory(legacy2);
    VrSettings old;old.render_scale=150;old.ssr=20;old.relaxed_lesson=false;old.welcome_seen=true;
    {std::ofstream f(legacy2/"vr-settings.0");f<<"HPVR_VR2 150 20 8 0 1 "<<VrSettingsChecksumV2(old,8)<<'\n';}
    read=ReadVrSettings(legacy2);
    Check(read.generation==8&&read.render_scale==150&&read.ssr==20&&!read.relaxed_lesson&&
          read.first_person_cutscenes&&read.welcome_seen,"VR2 keeps explicit original difficulty and inherits only absent camera flag");
    Check(WriteVrSettings(legacy2,read),"upgrade legacy settings without forcing fresh preferences");
    const auto upgraded=ReadVrSettings(legacy2);
    Check(upgraded.generation==9&&upgraded.render_scale==150&&upgraded.ssr==20&&!upgraded.relaxed_lesson&&
          upgraded.first_person_cutscenes&&upgraded.welcome_seen,"VR3 upgrade preserves legacy choices");

    std::filesystem::remove_all(root);
    std::cout<<"C37_DEFAULTS=PASS fresh=100_30_RELAXED_FIRST_PERSON welcome=VISIBLE saved_choices=PRESERVED legacy=COMPATIBLE\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"C37_DEFAULTS_FAIL="<<e.what()<<'\n';return 1;}}
