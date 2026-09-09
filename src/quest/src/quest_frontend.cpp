#include "hpvr/quest_frontend.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <cstring>
#include <cstdio>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif
namespace hpvr::quest {
namespace {
std::string Fold(std::string s){for(auto& c:s) if(c>='A'&&c<='Z')c+=32;return s;}
std::uint32_t Hash(const std::string& s){std::uint32_t h=2166136261U;for(unsigned char c:s)h=(h^c)*16777619U;return h;}
std::string Text(const std::vector<std::uint8_t>& bytes,std::size_t& at) {
    if(at>=bytes.size())throw std::runtime_error("Truncated book string");
    // This adapter accepts bounded positive ANSI compact strings only.
    unsigned n=bytes[at++];
    if(n&0xc0U || !n || n>bytes.size()-at)throw std::runtime_error("Invalid book string length");
    if(bytes[at+n-1]!=0)throw std::runtime_error("Unterminated book string");
    std::string s(reinterpret_cast<const char*>(bytes.data()+at),n-1);at+=n;return s;
}
bool Valid(const ProgressSave& s) {
    if(s.phase>2 || s.page>14 || s.quest_stage>23 || s.peeves_phase>3 || s.filch_resume_stage>23 || s.generation>1000000000ULL || s.health>100 || s.lesson_passes>4 || (s.card_taken&&!s.card_awarded))return false;
    if(s.collected_beans.size()>128 || !std::ranges::is_sorted(s.collected_beans) ||
       std::adjacent_find(s.collected_beans.begin(),s.collected_beans.end())!=s.collected_beans.end())return false;
    for(auto ref:s.collected_beans)if(ref<=0||ref>100000)return false;
    const auto finite=[](float f){return std::isfinite(f)&&std::abs(f)<2000;};
    if(!std::ranges::all_of(s.player,finite)||!finite(s.yaw))return false;
    for(const auto& a:s.cast)if(!std::ranges::all_of(a,finite))return false;
    for(float d:s.doors)if(!std::isfinite(d)||d<0||d>1)return false;
    return true;
}
bool ReadBank(const std::filesystem::path& path,ProgressSave* out) {
    std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
    if(ec||size>4096||size<20)return false;
    std::ifstream f(path,std::ios::binary);std::string all((std::istreambuf_iterator<char>(f)),{});
    const auto end=all.rfind('#');if(end==std::string::npos)return false;
    const auto body=all.substr(0,end);
    std::uint32_t hash=0;std::istringstream check(all.substr(end+1));check>>std::hex>>hash;
    if(!check||hash!=Hash(body))return false;
    std::istringstream in(body);std::string magic;unsigned version=0;ProgressSave s;
    in>>magic>>version>>s.generation>>s.phase>>s.page;
    for(auto& v:s.player)in>>v;in>>s.yaw;
    if(version<1 || version>7)return false;
    for(unsigned i=0;i<(version==1?2U:version==2?3U:version==3?5U:11U);++i)for(auto& v:s.cast[i])in>>v;
    for(auto& v:s.doors)in>>v;
    if(version>=2)in>>s.quest_stage;
    else s.quest_stage=s.phase==2?1U:0U;
    if(version>=3){
        unsigned count=0;in>>count;if(count>128)return false;
        s.collected_beans.resize(count);for(auto& ref:s.collected_beans)in>>ref;
    }
    if(version>=5)in>>s.peeves_phase>>s.twins_departed>>s.filch_seen>>s.filch_resume_stage;
    else {s.peeves_phase=s.quest_stage>=11?3U:0U;s.twins_departed=s.quest_stage>=8;s.filch_seen=s.quest_stage==14;}
    if(version>=6)in>>s.health>>s.lesson_passes>>s.frog_taken>>s.card_awarded>>s.card_taken;
    else if(s.quest_stage>12){s.card_awarded=s.card_taken=true;} // Do not trap existing saves behind a new gate.
    if(version>=7)in>>s.peeves_first_hit;
    else s.peeves_first_hit=s.peeves_phase>=2||s.health<100||s.frog_taken;
    if(!in || magic!="HPVR_PROGRESS" || !Valid(s))return false;
    in>>std::ws;if(!in.eof())return false;*out=s;return true;
}
std::filesystem::path Bank(const std::filesystem::path& dir,unsigned slot,unsigned bank) {
    return dir/("slot"+std::to_string(slot+1)+"."+std::to_string(bank)+".hpvr");
}
}
std::string AudioCacheName(const wand::Hp1MpegSound& source,bool stereo){
    std::uint32_t h=2166136261U;for(auto b:source.encoded_bytes)h=(h^b)*16777619U;
    std::ostringstream s;s<<source.object_name<<'.'<<std::hex<<std::setw(8)<<std::setfill('0')<<h
        <<(stereo?".stereo.s16":".s16");return s.str();
}
bool TutorialRewardReady(const ProgressSave& p){
    return p.quest_stage==12&&!p.card_awarded&&p.collected_beans.size()>=25;
}
void ApplyTutorialDamage(ProgressSave& p){
    // Tutorial contact is survivable; death/respawn is a separate gameplay system.
    // Retail MaxLifePotions=50: five raw damage is ten percent, not five.
    p.health=p.health>10?p.health-10:1;
}
bool ApplyFirstPeevesContact(ProgressSave& p){
    if(p.peeves_first_hit)return false;
    p.peeves_first_hit=true;ApplyTutorialDamage(p);return true;
}
bool RewardApproach::Update(const ProgressSave& p,float distance,bool visible){
    const bool was_ready=ready;ready=TutorialRewardReady(p);
    if(!ready){armed=false;return false;}
    if(std::isfinite(distance)&&distance>2.3F)armed=true;
    // Collecting the 25th bean is not an interaction with Fred.
    if(!was_ready||!armed||!visible||!std::isfinite(distance)||distance>1.9F)return false;
    armed=false;return true;
}
bool ReadProgress(const std::filesystem::path& directory,unsigned slot,ProgressSave* out){
    if(slot>=3||!out)return false;
    ProgressSave a,b;bool aa=ReadBank(Bank(directory,slot,0),&a),bb=ReadBank(Bank(directory,slot,1),&b);
    if(!aa&&!bb)return false;*out=aa&&(!bb||a.generation>=b.generation)?a:b;return true;
}
bool WriteProgress(const std::filesystem::path& directory,unsigned slot,ProgressSave* inout){
    if(slot>=3||!inout||!Valid(*inout))return false;
    std::error_code ec;std::filesystem::create_directories(directory,ec);if(ec)return false;
    ProgressSave latest;ReadProgress(directory,slot,&latest);
    auto next=*inout;next.generation=std::max(next.generation,latest.generation)+1;
    if(!Valid(next))return false;
    std::ostringstream body;body.imbue(std::locale::classic());
    body<<"HPVR_PROGRESS 7 "<<next.generation<<' '<<next.phase<<' '<<next.page<<' '<<std::setprecision(9);
    for(auto f:next.player)body<<f<<' ';body<<next.yaw<<' ';
    for(const auto& a:next.cast)for(auto f:a)body<<f<<' ';
    for(auto f:next.doors)body<<f<<' ';body<<next.quest_stage<<' '<<next.collected_beans.size()<<' ';
    for(auto ref:next.collected_beans)body<<ref<<' ';body<<next.peeves_phase<<' '<<next.twins_departed<<' '<<next.filch_seen<<' '<<next.filch_resume_stage<<' '
        <<next.health<<' '<<next.lesson_passes<<' '<<next.frog_taken<<' '<<next.card_awarded<<' '<<next.card_taken<<' '<<next.peeves_first_hit<<'\n';
    std::ostringstream complete;complete<<body.str()<<'#'<<std::hex<<Hash(body.str())<<'\n';
    const auto payload=complete.str();
    const auto path=Bank(directory,slot,static_cast<unsigned>(next.generation%2));
    auto temp=path;temp+=".tmp";
#ifdef _WIN32
    FILE* file=nullptr;
    if (_wfopen_s(&file,temp.c_str(),L"wb")!=0) return false;
#else
    FILE* file=std::fopen(temp.c_str(),"wb");
#endif
    if(!file)return false;
    bool ok=std::fwrite(payload.data(),1,payload.size(),file)==payload.size() && std::fflush(file)==0;
#ifdef _WIN32
    ok=ok&&_commit(_fileno(file))==0;
#else
    ok=ok&&fsync(fileno(file))==0;
#endif
    ok=std::fclose(file)==0&&ok;
    if(!ok)return false;
#ifdef _WIN32
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return false;
#else
    std::filesystem::rename(temp,path,ec);if(ec)return false;
    const int dirfd=open(directory.c_str(),O_RDONLY|O_DIRECTORY);
    if(dirfd<0)return false;
    const bool synced=fsync(dirfd)==0;close(dirfd);if(!synced)return false;
#endif
    ProgressSave verified;if(!ReadBank(path,&verified)||verified.generation!=next.generation)return false;
    *inout=next;return true;
}
bool LoadFrontAssets(const std::filesystem::path& root,FrontAssets* out){
    if(!out)return false;
    try{
        FrontAssets a;
        std::map<std::string,wand::Hp1PackageLinkTable> tables;
        auto reference=[&](const std::string& package,const std::string& name,const std::string& cls){
            if(!tables.contains(package))tables[package]=wand::inspect_hp1_package_link_table(root/package);
            for(const auto& e:tables.at(package).exports)
                if(e.qualified_class_name==cls&&!e.object_path.empty()&&Fold(e.object_path.back())==Fold(name))return e.reference;
            throw std::runtime_error("Missing owned object: "+package+":"+name);
        };
        auto tile=[&](const std::string& package,const std::string& name,bool force_masked=false){
            const std::string key=package+":"+Fold(name);
            for(std::size_t i=0;i<a.textures.size();++i)if(a.textures[i].name==key)return static_cast<std::uint32_t>(i);
            const bool masked=force_masked || package=="Textures/StoryBookTest.utx" || name.starts_with("Logo");
            auto t=wand::load_hp1_p8_texture(root/package,reference(package,name,"Engine.Texture"),masked);
            if(t.status!=wand::Hp1ProfileStatus::ok||t.mips.empty())throw std::runtime_error(t.error);
            FrontTexture image{key,std::vector<std::uint8_t>(256*256*4)};
            const auto& m=t.mips.front();
            for(std::size_t y=0;y<256;++y)for(std::size_t x=0;x<256;++x){
                auto src=((y*m.height/256)*m.width+x*m.width/256)*4;
                std::copy_n(t.rgba8.begin()+src,4,image.rgba.begin()+(y*256+x)*4);
            }
            a.textures.push_back(std::move(image));return static_cast<std::uint32_t>(a.textures.size()-1);
        };
        for(unsigned i=0;i<6;++i){
            a.menu[i]=tile("Textures/MenuArt.utx","moontitle"+std::to_string(i+1));
            a.paper[i]=tile("system/HPMenu.u","HPStoryTextureBackground"+std::to_string(i+1));
        }
        for(unsigned i=0;i<2;++i)a.logo[i]=tile("Textures/MenuArt.utx","Logo"+std::to_string(i+1));
        for(unsigned i=0;i<6;++i){
            const auto n=std::to_string(i+1);
            a.book[i]=tile("system/HPMenu.u","FEBookTexture"+n);
            a.folio[i]=tile("system/HPMenu.u","FEFolioBackTexture"+n,true);
            a.folio_secret[i]=tile("system/HPMenu.u","FEFolioHarryTexture"+n,true);
            a.report[i]=tile("system/HPMenu.u","FEReportBackTexture"+n,true);
        }
        const std::array<std::string,3> tabs{"MainTabTexture","FolioTabTexture","ReportTabTexture"};
        for(unsigned i=0;i<3;++i)a.tabs[i]=tile("system/HPMenu.u",tabs[i],true);
        a.health_full=tile("system/HPBase.u","HarryBarFull",true);
        a.health_empty=tile("system/HPBase.u","HarryBarEmpty",true);
        {
            const auto& pixels=a.textures.at(a.health_full).rgba;unsigned first=256,last=0;
            for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)if(pixels[(y*256+x)*4+3]>=128){first=std::min(first,y);last=std::max(last,y);}
            if(first<256){a.health_top=float(first)/256;a.health_bottom=float(last+1)/256;}
        }
        a.bean_counter=tile("system/HPMenu.u","beancounter",true);
        for(unsigned i=0;i<4;++i)a.bean_pile[i]=tile("system/HPMenu.u","beans"+std::to_string(i+1),true);
        a.bean_badge=tile("system/HPMenu.u","BeanBadgeTexture",true);
        a.card_badge=tile("system/HPMenu.u","CardBadgeTexture",true);
        a.missing_big=tile("system/HPMenu.u","WizCardMissingBigTexture",true);
        a.missing_small=tile("system/HPMenu.u","WizCardMissingSmallTexture",true);
        a.arrow_left=tile("system/HPMenu.u","FELeftArrowUpIcon",true);
        a.arrow_right=tile("system/HPMenu.u","FERightArrowUpIcon",true);
        auto defaults=wand::inspect_hp1_class_visual_defaults(root/"system/HPMenu.u",
            reference("system/HPMenu.u","FEStoryBookPage","Core.Class"));
        std::map<int,std::pair<std::string,std::string>> pages;
        for(const auto& p:defaults.serialized_properties)if(Fold(p.name)=="bookpages3"){
            std::size_t at=0;auto graphic=Text(p.value,at);auto dialog=Text(p.value,at);
            if(at!=p.value.size())throw std::runtime_error("Story page has trailing bytes");
            pages.emplace(static_cast<int>(std::max<std::int64_t>(0,p.array_index)),std::make_pair(graphic,dialog));
        }
        if(pages.size()!=14)throw std::runtime_error("Opening story must contain 14 authored pages");
        std::map<std::string,std::string> subtitles;
        std::ifstream menu_strings(root/"system/hpmenu.int",std::ios::binary);std::string menu_line;
        unsigned menu_lines=0;std::string menu_section;
        while(std::getline(menu_strings,menu_line)){
            ++menu_lines;
            if(!menu_line.empty()&&menu_line.back()=='\r')menu_line.pop_back();
            if(!menu_line.empty()&&menu_line.front()=='[')menu_section=Fold(menu_line);
            if(menu_section=="[text]"&&Fold(menu_line).starts_with("objective_01="))a.level_objective=menu_line.substr(13);
        }
        if(a.level_objective.empty())throw std::runtime_error("Missing owned level objective: "+
            (root/"system/hpmenu.int").string()+" open="+std::to_string(menu_strings.is_open())+" lines="+std::to_string(menu_lines));
        std::ifstream strings(root/"system/hpdialog.int");std::string line;
        while(std::getline(strings,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();
            auto eq=line.find('=');if(eq!=std::string::npos)subtitles[Fold(line.substr(0,eq))]=line.substr(eq+1);}
        for(const auto& [index,names]:pages){
            if(index!=static_cast<int>(a.story.size()))throw std::runtime_error("Story index gap");
            StoryPage page;page.dialogue_name=names.second;page.subtitle=subtitles.at(Fold(names.second));
            for(unsigned i=0;i<4;++i)page.tiles[i]=tile("Textures/StoryBookTest.utx",names.first+"00"+std::to_string(i+1));
            page.voice=wand::load_hp1_mpeg_sound(root/"Sounds/AllDialog.uax",
                reference("Sounds/AllDialog.uax",names.second,"Engine.Sound"));
            if(page.voice.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(page.voice.error);
            a.story.push_back(std::move(page));
        }
        for(const auto& name:{"JS_HP_Title_Screen_v2","JS_StoryBook_v2_mx",
                              "JS_Opening_Castle_Fly_Through_mx","happy_hogwarts_mxlp1"}){
            const auto package=std::string("Music/")+name+".umx";
            auto music=wand::load_hp1_mpeg_sound(root/package,reference(package,name,"Engine.Music"));
            if(music.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(music.error);
            a.music.push_back(std::move(music));
        }
        a.smoke=tile("system/HPParticle.u","Smoke5");
        a.card_face=tile("system/HProps.u","WizardCardDumbledoreTex0");
        for(const auto& item:std::array<std::pair<std::string,std::string>,2>{{
            {"Sounds/AllDialog.uax","RON_001"},{"Sounds/Magic_sfx.uax","spell_dud"}}}){
            auto audio=wand::load_hp1_mpeg_sound(root/item.first,reference(item.first,item.second,"Engine.Sound"));
            if(audio.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(audio.error);
            a.gameplay_audio.push_back(std::move(audio));
        }
        for(const auto& name:{"FRED_GEORGE_001","FRED_GEORGE_002","FRED_GEORGE_003",
                             "FRED_GEORGE_004","FRED_GEORGE_005","FRED_GEORGE_006","ron_new_2"}){
            auto audio=wand::load_hp1_mpeg_sound(root/"Sounds/AllDialog.uax",
                reference("Sounds/AllDialog.uax",name,"Engine.Sound"));
            if(audio.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(audio.error);
            a.gameplay_audio.push_back(std::move(audio));
        }
        auto pickup=wand::load_hp1_mpeg_sound(root/"Sounds/Magic_sfx.uax",
            reference("Sounds/Magic_sfx.uax","pickup11","Engine.Sound"));
        if(pickup.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(pickup.error);
        a.gameplay_audio.push_back(std::move(pickup));
        for(const auto& name:{"FRED_GEORGE_007","FRED_GEORGE_009","FRED_GEORGE_010",
                             "fred_george_new_139","fred_george_new_11","FRED_GEORGE_014",
                             "FRED_GEORGE_015","FRED_GEORGE_016","fred_george_new_15",
                             "fred_george_new_112","fred_george_new_17"}){
            auto voice=wand::load_hp1_mpeg_sound(root/"Sounds/AllDialog.uax",
                reference("Sounds/AllDialog.uax",name,"Engine.Sound"));
            if(voice.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(voice.error);
            a.gameplay_audio.push_back(std::move(voice));
        }
        for(const auto& name:{"armor_head_move1","armor_head_move2"}){
            auto sound=wand::load_hp1_mpeg_sound(root/"system/HPSounds.u",reference("system/HPSounds.u",name,"Engine.Sound"));
            if(sound.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(sound.error);
            a.gameplay_audio.push_back(std::move(sound));
        }
        for(const auto& name:{"FILCH_01","142FilchD4","112MalfoyInfo1","112MalfoyInfo2","malfoy_int_1",
            "malfoy_int_2","malfoy_int_3","malfoy_int_4","111HermioneInfo1","HERMIONE_002","QUIRRELL_001"}){
            auto voice=wand::load_hp1_mpeg_sound(root/"Sounds/AllDialog.uax",reference("Sounds/AllDialog.uax",name,"Engine.Sound"));
            if(voice.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(voice.error);
            a.gameplay_audio.push_back(std::move(voice));
        }
        // Keep existing audio indices stable. New sounds and instance-authored
        // bump lines are appended; never substitute a cinematic or invented line.
        auto stone=wand::load_hp1_mpeg_sound(root/"system/HPSounds.u",
            reference("system/HPSounds.u","stone_door_long","Engine.Sound"));
        if(stone.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(stone.error);
        a.gameplay_audio.push_back(std::move(stone));
        const auto census=wand::inspect_hp1_actor_visuals(root/"Maps/Lev_Tut1.unr");
        if(census.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(census.error);
        for(const auto& actor:census.actors){
            std::map<std::int64_t,std::string> ordered;
            for(const auto& p:actor.serialized_properties){
                if(p.name!="BumpLines"||!p.text_value_serialized)continue;
                const auto first=p.text_value.find('<'),last=p.text_value.find('>');
                if(first==std::string::npos||last<=first+1||last==std::string::npos)continue;
                ordered[std::max<std::int64_t>(0,p.array_index)]=p.text_value.substr(first+1,last-first-1);
            }
            if(ordered.empty())continue;
            FrontAssets::BumpSpeech profile;profile.actor_reference=actor.actor_reference;
            for(const auto& [index,name]:ordered){
                auto voice=wand::load_hp1_mpeg_sound(root/"Sounds/AllDialog.uax",
                    reference("Sounds/AllDialog.uax",name,"Engine.Sound"));
                if(voice.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(voice.error);
                profile.lines.push_back(voice.object_name);
                if(std::ranges::none_of(a.gameplay_audio,[&](const auto& v){return v.object_name==voice.object_name;}))
                    a.gameplay_audio.push_back(std::move(voice));
            }
            a.bump_speech.push_back(std::move(profile));
        }
        for(const auto& name:{"111Peeves1","fred_george_new_135","fred_george_new_133"}){
            auto voice=wand::load_hp1_mpeg_sound(root/"Sounds/AllDialog.uax",reference("Sounds/AllDialog.uax",name,"Engine.Sound"));
            if(voice.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(voice.error);
            a.gameplay_audio.push_back(std::move(voice));
        }
        // Original-work 5x7 glyphs: portable, no system font or proprietary bitmap in APK.
        for(const auto& actor:census.actors)if(actor.actor_reference==1610||actor.actor_reference==1702||actor.actor_reference==2140){
            for(const auto& prop:actor.serialized_properties){
                if(!prop.text_value_serialized)continue;
                std::string name;
                if(actor.actor_reference==1610&&prop.name.starts_with("Lesson"))name=prop.text_value;
                else {const auto space=prop.text_value.find(' ');auto op=prop.text_value.substr(0,space);
                    for(auto& c:op)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if(op.starts_with("talk")||op=="say")name=prop.text_value.substr(space+1);}
                if(name.empty())continue;
                auto voice=wand::load_hp1_mpeg_sound(root/"Sounds/AllDialog.uax",reference("Sounds/AllDialog.uax",name,"Engine.Sound"));
                if(voice.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(voice.error);
                if(std::ranges::none_of(a.gameplay_audio,[&](const auto& v){return v.object_name==voice.object_name;}))
                    a.gameplay_audio.push_back(std::move(voice));
            }
        }
        a.frog_pickup=wand::load_hp1_pcm_sound(root/"system/HPSounds.u",reference("system/HPSounds.u","pickup_frog","Engine.Sound"));
        if(a.frog_pickup.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(a.frog_pickup.error);
        // WizzardCardIcon.Rising.BeginState, not its similarly named spawn sound.
        a.card_pickup=wand::load_hp1_mpeg_sound(root/"system/HPSounds.u",reference("system/HPSounds.u","pickup_wizardcard2","Engine.Sound"));
        if(a.card_pickup.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(a.card_pickup.error);
        a.gameplay_audio.push_back(a.card_pickup);
        const std::map<char,std::array<unsigned char,7>> glyphs{
            {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},
            {'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},
            {'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},
            {'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},
            {'I',{14,4,4,4,4,4,14}},{'J',{7,2,2,2,18,18,12}},
            {'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
            {'M',{17,27,21,21,17,17,17}},{'N',{17,25,21,19,17,17,17}},
            {'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},
            {'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},
            {'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},
            {'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},
            {'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
            {'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
            {'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},
            {'2',{14,17,1,2,4,8,31}},{'3',{30,1,1,14,1,1,30}},
            {'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},
            {'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},
            {'8',{14,17,17,14,17,17,14}},{'9',{14,17,17,15,1,1,14}},
            {'.',{0,0,0,0,0,12,12}},{',',{0,0,0,0,0,4,8}},
            {'!',{4,4,4,4,4,0,4}},{'?',{14,17,1,2,4,0,4}},
            {'-',{0,0,0,31,0,0,0}},{':',{0,4,4,0,4,4,0}},
            {'/',{1,2,2,4,8,8,16}},{'\'',{4,4,8,0,0,0,0}},
            {'+',{0,4,4,31,4,4,0}},{'%',{25,25,2,4,8,19,19}},
            {'(',{2,4,8,8,8,4,2}},{')',{8,4,2,2,2,4,8}},
            {'>',{16,8,4,2,4,8,16}}
        };
        FrontTexture font{"generated-glyphs",std::vector<std::uint8_t>(256*256*4,0)};
        for(const auto& [ch,rows]:glyphs)for(unsigned y=0;y<7;++y)for(unsigned x=0;x<5;++x)
            if(rows[y]&(1U<<(4-x))){
                auto p=((static_cast<unsigned>(ch)/16*16+2+y)*256+(static_cast<unsigned>(ch)%16*16+2+x))*4;
                for(unsigned c=0;c<4;++c)font.rgba[p+c]=255;
            }
        a.font=static_cast<std::uint32_t>(a.textures.size());a.textures.push_back(std::move(font));
        a.white=static_cast<std::uint32_t>(a.textures.size());a.textures.push_back({"white",std::vector<std::uint8_t>(256*256*4,255)});
        *out=std::move(a);return true;
    }catch(const std::exception& e){out->error=e.what();return false;}
}
void QuestFrontEnd::RefreshSlots(){for(unsigned i=0;i<3;++i){ProgressSave s;occupied[i]=ReadProgress(saves,i,&s);}}
bool QuestFrontEnd::Save(){
    const bool ok=WriteProgress(saves,slot,&progress);
    if(!ok){message="SAVE FAILED - CHECK FREE STORAGE";screen=FrontScreen::Stub;selection=0;}
    else message.clear();
    RefreshSlots();return ok;
}
void QuestFrontEnd::BeginStory(unsigned first_page){screen=FrontScreen::Story;page=std::min(first_page,13U);page_time=0;page_voice_started=false;}
void QuestFrontEnd::BeginGame(){screen=FrontScreen::Game;paused=FrontScreen::Game;selection=0;}
void QuestFrontEnd::ShowDemoNotice(bool finished){
    screen=finished?FrontScreen::DemoEnd:FrontScreen::Welcome;selection=0;debug_pinned=false;
    confirm_down_=back_down_=stick_down_=horizontal_down_=true;
}
void QuestFrontEnd::ToggleVrMenu(){
    if(VrPanel()){screen=vr_return;selection=vr_return_selection;}
    else{debug_pinned=false;vr_return=screen;vr_return_selection=selection;screen=FrontScreen::Vr;selection=0;}
    confirm_down_=true;back_down_=true;stick_down_=true;horizontal_down_=true;
}
FrontAction QuestFrontEnd::Input(float move_y,bool confirm,bool back,float move_x){
    bool press=confirm&&!confirm_down_,cancel=back&&!back_down_;
    confirm_down_=confirm;back_down_=back;
    bool stick=std::abs(move_y)>0.65F;int step=0;
    if(stick&&!stick_down_)step=move_y>0?-1:1;stick_down_=stick;
    const bool horizontal=std::abs(move_x)>0.65F;
    if(DemoNotice()){
        if(step)selection=(selection+2+step)%2;
        if(press&&selection==1)return FrontAction::OpenCommunity;
        if(cancel||(press&&selection==0)){
            if(screen==FrontScreen::Welcome){vr.welcome_seen=true;vr_save_failed=!WriteVrSettings(saves.parent_path(),vr);}
            BeginGame();return FrontAction::Resume;
        }
        horizontal_down_=horizontal;return FrontAction::None;
    }
    if(screen==FrontScreen::Debug){
        if(press&&vr_return==FrontScreen::Game){debug_pinned=true;screen=FrontScreen::Game;selection=vr_return_selection;return FrontAction::Resume;}
        if(cancel){screen=FrontScreen::Vr;selection=2;}
        horizontal_down_=horizontal;return FrontAction::None;
    }
    if(screen==FrontScreen::Vr){
        if(step)selection=(selection+5+step)%5;
        if(cancel||(press&&selection==4)){ToggleVrMenu();return FrontAction::Resume;}
        if(press&&selection==2){debug_pinned=false;screen=FrontScreen::Debug;selection=0;return FrontAction::None;}
        if(selection<2&&horizontal&&!horizontal_down_){
            auto& value=selection==0?vr.render_scale:vr.ssr;
            value=std::clamp(value+(move_x>0?5:-5),selection==0?50:0,selection==0?175:100);
            vr_save_failed=!WriteVrSettings(saves.parent_path(),vr);
        }
        if(selection==3&&(press||(horizontal&&!horizontal_down_))){
            vr.relaxed_lesson=!vr.relaxed_lesson;
            vr_save_failed=!WriteVrSettings(saves.parent_path(),vr);
        }
        horizontal_down_=horizontal;return FrontAction::None;
    }
    if(screen==FrontScreen::Cards&&horizontal&&!horizontal_down_)
        card_page=(card_page+7+(move_x>0?1:-1))%7;
    horizontal_down_=horizontal;
    unsigned count=0;
    switch(screen){
    case FrontScreen::Main:count=4;break;
    case FrontScreen::Slots:count=4;break;
    case FrontScreen::Slot:count=3;break;
    case FrontScreen::Replace:count=2;break;
    case FrontScreen::Pause:count=5;break;
    case FrontScreen::Cards:count=3;break;
    case FrontScreen::Report:count=1;break;
    default:break;
    }
    if(count&&step)selection=(selection+count+step)%count;
    if(cancel){
        if(screen==FrontScreen::Game){paused=screen;screen=FrontScreen::Pause;selection=0;}
        else if(screen==FrontScreen::Story){paused=screen;screen=FrontScreen::Pause;selection=0;}
        else if(screen==FrontScreen::Pause){screen=paused;return FrontAction::Resume;}
        else if(screen==FrontScreen::Cards||screen==FrontScreen::Report){screen=FrontScreen::Pause;selection=0;}
        else{screen=FrontScreen::Main;selection=0;}
        return FrontAction::None;
    }
    if(!press)return FrontAction::None;
    switch(screen){
    case FrontScreen::Main:
        if(selection==0){RefreshSlots();screen=FrontScreen::Slots;}
        else if(selection==3){screen=FrontScreen::Stub;message="USE THE QUEST MENU TO CLOSE THE APP";}
        else{screen=FrontScreen::Stub;message="NOT AVAILABLE IN THIS BUILD";}
        selection=0;break;
    case FrontScreen::Slots:
        if(selection==3){screen=FrontScreen::Main;selection=0;break;}
        slot=selection;screen=FrontScreen::Slot;selection=occupied[slot]?0:1;break;
    case FrontScreen::Slot:
        if(selection==0&&ReadProgress(saves,slot,&progress))return FrontAction::Continue;
        if(selection==1){
            if(occupied[slot]){screen=FrontScreen::Replace;selection=1;}
            else return FrontAction::NewGame;
        }
        if(selection==2){screen=FrontScreen::Slots;selection=slot;}break;
    case FrontScreen::Replace:
        if(selection==0)return FrontAction::NewGame;
        screen=FrontScreen::Slot;selection=0;break;
    case FrontScreen::Story:
        page_time=10000;break; // explicit page skip, never skips by a held trigger
    case FrontScreen::Pause:
        if(selection==0){screen=paused;return FrontAction::Resume;}
        if(selection==1){screen=FrontScreen::Main;selection=0;return FrontAction::SaveMenu;}
        if(selection==2){screen=paused;return FrontAction::SkipScene;}
        screen=selection==3?FrontScreen::Cards:FrontScreen::Report;selection=0;break;
    case FrontScreen::Cards:
        if(selection==2){screen=FrontScreen::Pause;selection=3;}
        else card_page=(card_page+(selection==0?1:6))%7;
        break;
    case FrontScreen::Report:screen=FrontScreen::Pause;selection=4;break;
    case FrontScreen::Objective:BeginGame();return FrontAction::BeginLevel;
    case FrontScreen::Stub:screen=FrontScreen::Main;selection=0;break;
    default:break;
    }
    return FrontAction::None;
}
FrontAction QuestFrontEnd::TickStory(float seconds,float duration){
    if(screen!=FrontScreen::Story||!std::isfinite(seconds)||seconds<0)return FrontAction::None;
    page_time+=seconds;
    if(page_time<duration+0.6F)return FrontAction::None;
    const auto previous=progress;const auto previous_page=page;
    ++page;progress.page=page;progress.phase=page>=assets.story.size()?1U:0U;
    if(!Save()){progress=previous;page=previous_page;return FrontAction::None;}
    if(page>=assets.story.size()){BeginGame();return FrontAction::StoryDone;}
    page_time=0;page_voice_started=false;return FrontAction::None;
}
std::string QuestFrontEnd::DrawKey()const{
    std::string s=std::to_string(static_cast<int>(screen));
    if(screen==FrontScreen::Story)return s+"_"+std::to_string(page);
    s+="_"+std::to_string(selection);
    if(screen==FrontScreen::Cards)s+="_"+std::to_string(card_page)+(progress.card_awarded?"_earned":"_empty");
    if(screen==FrontScreen::Slots)for(bool b:occupied)s+=b?"1":"0";
    if(screen==FrontScreen::Slot)s+="_"+std::to_string(slot)+(occupied[slot]?"1":"0");
    if(screen==FrontScreen::Pause)s+=(paused==FrontScreen::Story?std::string("S"):std::string("G"))+std::to_string(progress.quest_stage);
    if(screen==FrontScreen::Stub)s+=message.starts_with("SAVE FAILED")?"F":message.starts_with("USE")?"E":"N";
    if(screen==FrontScreen::Vr){s+=vr_save_failed?"F":"S";s+=vr.relaxed_lesson?"R":"O";}
    if(screen==FrontScreen::Debug&&debug_pinned)s+="_pinned";
    return s;
}
std::vector<FrontQuad> QuestFrontEnd::Quads()const{
    std::vector<FrontQuad> out;
    auto tile=[&](float x,float y,float w,float h,std::uint32_t t,std::uint32_t tint=0xffffff){
        float cw=std::min(w,640-x),ch=std::min(h,480-y);
        if(cw>0&&ch>0)out.push_back({x,y,cw,ch,0,0,cw/w,ch/h,t,tint});
    };
    auto text=[&](std::string s,float x,float y,float scale,std::uint32_t color){
        for(unsigned char raw:s){unsigned char ch=raw;if(ch>='a'&&ch<='z')ch-=32;
            if(ch!=' '&&ch<128)out.push_back({x,y,5*scale,7*scale,
                float(ch%16*16+2)/256,float(ch/16*16+2)/256,5.0F/256,7.0F/256,assets.font,color});
            x+=6*scale;}
    };
    if(FloatingPanel()){
        tile(0,0,640,480,assets.white,0x140a05);
        tile(14,14,612,452,assets.white,0x26160a);
        text("HARRY POTTER VR",194,32,3,0xcd78ff);
        if(DemoNotice()){
            const bool finished=screen==FrontScreen::DemoEnd;
            text(finished?"THANK YOU FOR PLAYING!":"WELCOME TO THE DEMO",finished?194.0F:212.0F,78,2,0xffe164);
            text(finished?"THIS IS THE END OF THE DEMO FOR NOW.":"A SMALL DEMO - THROUGH THE FIRST LESSON.",62,134,2,0xffffff);
            text("DEVELOPMENT IS IN PROGRESS.",62,173,2,0xffffff);
            text(finished?"WE WOULD LOVE TO HEAR YOUR FEEDBACK.":"FOLLOW DEVELOPMENT NEWS ON DISCORD.",62,209,2,0xffffff);
            text("THANKS FOR TRYING HARRY POTTER VR!",62,245,2,0xffffff);
            text("https://discord.com/channels/",62,282,1.65F,0xc7eaff);
            text("747967102895390741/1543691482861408276",62,305,1.65F,0xc7eaff);
            for(unsigned i=0;i<2;++i){
                const float y=354+float(i)*42;
                if(selection==i)tile(42,y-7,556,32,assets.white,0x875f19);
                text(i?"OPEN DISCORD IN BROWSER":"CONTINUE",62,y,2,0xffffff);
            }
            text("STICK: SELECT   TRIGGER: CONFIRM   B: CONTINUE",64,446,1.8F,0xd2beaa);
            return out;
        }
        text(screen==FrontScreen::Debug?"PERFORMANCE DEBUGGER":"VR SETTINGS",screen==FrontScreen::Debug?194.0F:254.0F,78,2,0xffe164);
        if(screen==FrontScreen::Debug){
            text("LIVE FRAME TIMINGS / DEVICE COUNTERS",110,108,2,0xd2beaa);
            text(debug_pinned?"BOTH GRIPS + MENU: HIDE / SETTINGS":"TRIGGER: PIN + PLAY    B: BACK TO SETTINGS",65,438,1.8F,0xd2beaa);
        }else{
            const std::array<std::string,5> rows{"RENDER SCALE","SSR - WOOD FLOORS","PERFORMANCE DEBUGGER","LESSON DIFFICULTY","RETURN"};
            for(unsigned i=0;i<5;++i){
                const float y=142+float(i)*52;
                if(selection==i)tile(42,y-7,556,34,assets.white,0x875f19);
                text(rows[i],62,y,2.1F,selection==i?0xfff5ff:0xffe164);
            }
            text(vr.relaxed_lesson?"RELAXED":"ORIGINAL",443,298,2.1F,0xffffff);
            text("STICK: SELECT / ADJUST    TRIGGER: OPEN",72,393,1.8F,0xd2beaa);
            text("BOTH GRIPS + MENU: CLOSE    B: BACK",82,418,1.8F,0xd2beaa);
            const char* hint=selection==3?(vr.relaxed_lesson?"RELAXED: WIDE TOLERANCE, NO TIMER":"ORIGINAL: 12S, 50 / 65 / 80 / 95%"):
                "LIVE SETTINGS - 175% USES 3.06X BASE PIXELS";
            text(vr_save_failed?"SAVE FAILED - SETTINGS ARE TEMPORARY":hint,65,446,1.65F,0xd2beaa);
        }
        return out;
    }
    const bool book=screen==FrontScreen::Pause||screen==FrontScreen::Cards||screen==FrontScreen::Report;
    const auto& bg=book?assets.book:(screen==FrontScreen::Main?assets.menu:assets.paper);
    for(unsigned i=0;i<6;++i)tile(float(i%3*256),float(i/3*256),256,256,bg[i]);
    if(book){
        if(screen!=FrontScreen::Pause){
            const auto& layer=screen==FrontScreen::Report?assets.report:(card_page==6?assets.folio_secret:assets.folio);
            for(unsigned i=0;i<6;++i)tile(float(i%3*256),float(i/3*256),256,256,layer[i]);
        }
        for(unsigned i=0;i<3;++i)tile(8,float(80+i*72),32,64,assets.tabs[i]);
    }
    if(screen==FrontScreen::Cards){
        text("WIZARD CARDS",244,14,2,0x302820);
        if(card_page<6){
            tile(182,31,256,256,card_page==0&&progress.card_awarded?assets.card_face:assets.missing_big);
            tile(49,130,128,128,assets.missing_small);tile(49,268,128,128,assets.missing_small);
            tile(449,131,128,128,assets.missing_small);tile(451,268,128,128,assets.missing_small);
            text(card_page==0&&progress.card_awarded?"ALBUS DUMBLEDORE":"NOT COLLECTED YET",219,327,1.6F,0xc7eaff);
            text("CARDS "+std::to_string(card_page*4+1)+" - "+std::to_string(card_page*4+4),239,352,1.5F,0xc7eaff);
        }else text("SECRET CARD - LOCKED",206,324,1.8F,0xc7eaff);
        tile(80,400,64,40,assets.arrow_left);tile(485,400,64,40,assets.arrow_right);
        const std::array<std::string,3> actions{"NEXT PAGE","PREVIOUS PAGE","BACK TO BOOK"};
        text(actions[selection],210,415,1.8F,0xc7eaff);
        text(std::to_string(card_page+1)+" / 7",294,387,1.7F,0xc7eaff);
        text("STICK: PAGE / SELECT   TRIGGER: CONFIRM   B: BACK",41,459,1.5F,0xc7eaff);
        return out;
    }
    if(screen==FrontScreen::Report){
        text("REPORT",276,32,2.2F,0x302820);
        tile(94,85,64,64,assets.bean_badge);text("BEANS",175,100,1.8F,0x302820);
        tile(330,85,64,64,assets.card_badge);text("CARDS: 0 / 25",402,107,1.6F,0x302820);
        text("SPELLS: NOT LEARNED YET",193,364,1.8F,0xc7eaff);
        text("TRIGGER / B: BACK TO BOOK",180,418,1.8F,0xc7eaff);
        return out;
    }
    if(screen==FrontScreen::Story){
        const auto& p=assets.story.at(page);
        for(unsigned i=0;i<4;++i)tile(92+float(i%2*256),42+float(i/2*256),256,256,p.tiles[i]);
        tile(89,373,470,76,assets.white,0xcfe2e8);
        std::istringstream words(p.subtitle);std::string word,line;float y=379;
        while(words>>word){if(line.size()+word.size()+1>58){text(line,100,y,1.25F,0x302820);line.clear();y+=11;}
            if(!line.empty())line+=' ';line+=word;}
        text(line,100,y,1.25F,0x302820);
        text("TRIGGER: NEXT PAGE   B: PAUSE / SKIP STORY",86,457,1.4F,0xffffff);
        text(std::to_string(page+1)+" / 14",538,12,1.5F,0xffffff);
        return out;
    }
    if(screen==FrontScreen::Objective){
        tile(38,87,564,301,assets.white,0x26160a);
        text("LEVEL OBJECTIVE",230,108,2,0xffffff);
        std::istringstream words(assets.level_objective);std::string word,line;float y=181;
        while(words>>word){
            if(line.size()+word.size()+1>43){text(line,67,y,2,0xffffff);line.clear();y+=25;}
            if(!line.empty())line+=' ';line+=word;
        }
        text(line,67,y,2,0xffffff);
        text("TRIGGER: CONTINUE",224,360,2,0xffffff);
        return out;
    }
    if(screen==FrontScreen::Main){
        tile(74,243,256,256,assets.logo[0]);tile(330,243,256,256,assets.logo[1]);
        const std::array<std::string,4> labels{"START GAME","OPTIONS","QUIDDITCH","EXIT"};
        for(unsigned i=0;i<4;++i){
            const float x=320-static_cast<float>(labels[i].size())*6,y=360+static_cast<float>(i)*22;
            text(labels[i],x+1,y+1,2,0);text(labels[i],x,y,2,i==selection?0x7373ff:0xffffff);
            if(i==selection)text(">",x-18,y,2,0x7373ff);
        }
        text("LEFT STICK: SELECT   TRIGGER: CONFIRM   B: BACK",50,456,1.7F,0xffffff);
        return out;
    }
    std::vector<std::string> labels;std::string title;
    switch(screen){
    case FrontScreen::Main:title="MAIN MENU";labels={"START GAME","OPTIONS","QUIDDITCH","EXIT"};break;
    case FrontScreen::Slots:title="SELECT A GAME";
        for(unsigned i=0;i<3;++i)labels.push_back("GAME "+std::to_string(i+1)+(occupied[i]?" - SAVED":" - NEW"));
        labels.push_back("BACK");break;
    case FrontScreen::Slot:title="GAME "+std::to_string(slot+1);labels={occupied[slot]?"LOAD GAME":"NO SAVED GAME","NEW GAME","BACK"};break;
    case FrontScreen::Replace:title="REPLACE THIS SAVED GAME?";labels={"YES - NEW GAME","NO - KEEP SAVE"};break;
    case FrontScreen::Pause:title="PAUSED";labels={"RESUME","SAVE AND MAIN MENU",
        paused==FrontScreen::Story?"SKIP STORY":"SKIP CURRENT SCENE","WIZARD CARDS","REPORT"};break;
    case FrontScreen::Stub:title=message;labels={"BACK"};break;
    default:break;
    }
    text(title,std::max(15.0F,320-float(title.size())*6),28,2,0xc7eaff);
    for(std::size_t i=0;i<labels.size();++i){
        float y=67+float(i)*39;
        tile(142,y-8,356,31,assets.white,i==selection?0x6f5124:0x20180e);
        text(labels[i],165,y,2,i==selection?0xe6f4ff:0xaaaaaa);
        if(i==selection)text(">",145,y,2,0xffffff);
    }
    text("LEFT STICK: SELECT   TRIGGER: CONFIRM   B: BACK",50,456,1.7F,0xffffff);
    if(screen==FrontScreen::Pause && paused==FrontScreen::Game){
        const std::array<std::string,24> objectives{
            "LISTEN TO DUMBLEDORE","GO UPSTAIRS - MEET RON","LISTEN TO RON",
            "FOLLOW RON INTO THE CORRIDOR","MEET FRED AND GEORGE AHEAD",
            "FOLLOW THE TWINS - LISTEN TO THEIR LESSON",
            "PUSH FORWARD AT THE BOOKCASE TO CLIMB",
            "COLLECT BEANS - MEET TWINS IN THE NEXT ROOM",
            "MEET THE TWINS IN THE NEXT ROOM",
            "LISTEN TO THE TWINS - JUMP LESSON",
            "A: JUMP - CROSS THE ROOM TO THE TWINS",
            "LISTEN TO THE TWINS",
            "25 BEANS - RETURN TO TWINS FOR YOUR CARD",
            "LISTEN TO FILCH","FOLLOW THE CORRIDOR TO CLASS",
            "LISTEN TO DRACO","MEET HERMIONE OUTSIDE CLASS",
            "LISTEN TO HERMIONE","FOLLOW HERMIONE INTO CLASS",
            "LISTEN TO PROFESSOR QUIRRELL","FIRST LESSON - PRACTISE FLIPENDO",
            "YOUR FIRST WIZARD CARD","FOLLOW PROFESSOR QUIRRELL","FLIPENDO LEARNED - CHALLENGE NEXT"};
        text(objectives.at(progress.quest_stage),55,306,1.65F,0x302820);
    }
    if(message.starts_with("SAVE FAILED"))text(message,74,426,1.5F,0x9999ff);
    return out;
}
std::vector<FrontQuad> QuestFrontEnd::BeanCounterQuads(unsigned count)const{
    std::vector<FrontQuad> out;float x=55;
    for(unsigned char ch:std::string("BEANS: ")+std::to_string(count)){
        if(ch!=' ')out.push_back({x,343,10,14,float(ch%16*16+2)/256,
            float(ch/16*16+2)/256,5.0F/256,7.0F/256,assets.font,0x302820});
        x+=12;
    }
    return out;
}
std::vector<FrontQuad> QuestFrontEnd::VrValueQuads(int value,bool scale)const{
    std::vector<FrontQuad> out;float x=443;
    const std::string label=value==0&&!scale?"OFF":std::to_string(value)+"%";
    for(unsigned char ch:label){
        if(ch!=' ')out.push_back({x,scale?142.0F:194.0F,10.5F,14.7F,float(ch%16*16+2)/256,float(ch/16*16+2)/256,5.0F/256,7.0F/256,assets.font,0xffffff});
        x+=12.6F;
    }return out;
}
std::vector<FrontQuad> QuestFrontEnd::HudQuads(unsigned count,bool show_beans)const{
    std::vector<FrontQuad> out{{0,0,128,128,0,0,1,1,assets.health_empty,0xffffff}};
    const float fraction=std::clamp(float(progress.health)/100,0.0F,1.0F);
    const float cut=assets.health_top+(1-fraction)*(assets.health_bottom-assets.health_top);
    out.push_back({0,128*cut,128,128*(1-cut),0,cut,1,1-cut,assets.health_full,0xffffff});
    if(!show_beans)return out;
    out.push_back({482,20,128,128,0,0,1,1,assets.bean_counter,0xffffff});
    for(unsigned i=0;i<4;++i)if(count>(i+1)*3)
        out.push_back({482,20,128,128,0,0,1,1,assets.bean_pile[i],0xffffff});
    const auto number=std::to_string(count);float x=546-static_cast<float>(number.size())*6;
    for(unsigned char ch:number){
        const float u=float(ch%16*16+2)/256,v=float(ch/16*16+2)/256;
        out.push_back({x+1,133,10,14,u,v,5.0F/256,7.0F/256,assets.font,0});
        out.push_back({x,132,10,14,u,v,5.0F/256,7.0F/256,assets.font,0xffffff});x+=12;
    }
    return out;
}
std::vector<FrontQuad> QuestFrontEnd::LessonQuads(unsigned passes,bool ready)const{
    std::vector<FrontQuad> out;
    const std::string label="FLIPENDO: "+std::to_string(std::min(4U,passes))+" / 4";
    const std::string instruction=ready?"HOLD TRIGGER - TRACE - RELEASE":"LISTEN TO PROFESSOR QUIRRELL";
    auto text=[&](const std::string& line,float y){
        float x=320-float(line.size())*5;
        for(unsigned char ch:line){
            if(ch!=' '){
                const float u=float(ch%16*16+2)/256,v=float(ch/16*16+2)/256;
                out.push_back({x+1,y+1,8,12,u,v,5.0F/256,7.0F/256,assets.font,0});
                out.push_back({x,y,8,12,u,v,5.0F/256,7.0F/256,assets.font,0xffffff});
            }
            x+=10;
        }
    };
    text(label,396);text(instruction,415);return out;
}
} // namespace hpvr::quest
