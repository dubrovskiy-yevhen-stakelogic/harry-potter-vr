#include "hpvr/quest_frontend.h"
#include "hpvr/quest_campaign_progress.h"
#include "hpvr/quest_charms_lesson.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <cstring>
#include <cstdio>
#include <bit>
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
    const auto* map=FindQuestMap(s.map_id);
    if(!map || s.map_id>=kPlayableQuestMapCount || s.phase>2 || s.page>14 || s.quest_stage>map->maximum_quest_stage || s.peeves_phase>3 || s.filch_resume_stage>23 || s.generation>1000000000ULL || s.health>100 || s.lesson_passes>4 || (s.card_taken&&!s.card_awarded))return false;
    if(s.banked_beans>1000000 || s.spent_beans>1000000 || s.challenge_stars>1024)return false;
    if((s.earned_cards&~0x1ffffffU)!=0 || (s.completed_maps&~0x3fffffffU)!=0)return false;
    if(std::ranges::any_of(s.house_points,[](unsigned n){return n>1000000;}) ||
       std::ranges::any_of(s.lesson_best,[](unsigned n){return n>100;}) ||
       std::ranges::any_of(s.lesson_points,[](unsigned n){return n>100;}))return false;
    const auto valid_state=[](const auto& state,std::size_t limit){
        return state.size()<=limit&&std::ranges::none_of(state,
            [](unsigned char c){return (c<32&&c!='\n'&&c!='\r'&&c!='\t')||c>=127;});
    };
    if(!valid_state(s.graph_state,16384)||!valid_state(s.world_state,32768)||!valid_state(s.charms_state,4096))return false;
    const auto valid_refs=[](const auto& refs,unsigned reward_map){
        return refs.size()<=1024 && std::ranges::is_sorted(refs) &&
            std::adjacent_find(refs.begin(),refs.end())==refs.end() &&
            std::ranges::all_of(refs,[&](auto ref){return (ref>0&&ref<=100000)||
                (IsWalkingSpellMap(reward_map)&&ref>=0x20000000+16&&ref<0x20000000+1600016)||
                ((reward_map==3||reward_map==kHogwartsReturnMapId)&&ref>0x30000000&&ref<=0x30000000+100000);});
    };
    if(!valid_refs(s.collected_beans,s.map_id)||!valid_refs(s.activated_events,0))return false;
    const auto finite=[](float f){return std::isfinite(f)&&std::abs(f)<2000;};
    if(!std::ranges::all_of(s.player,finite)||!finite(s.yaw))return false;
    for(const auto& a:s.cast)if(!std::ranges::all_of(a,finite))return false;
    for(float d:s.doors)if(!std::isfinite(d)||d<0||d>1)return false;
    return true;
}
bool ReadBank(const std::filesystem::path& path,ProgressSave* out) {
    std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
    if(ec||size>131072||size<20)return false;
    std::ifstream f(path,std::ios::binary);std::string all(static_cast<std::size_t>(size),'\0');
    if(!f.read(all.data(),static_cast<std::streamsize>(all.size()))||f.peek()!=std::char_traits<char>::eof())return false;
    const auto end=all.rfind('#');if(end==std::string::npos)return false;
    const auto body=all.substr(0,end);
    std::uint32_t hash=0;std::istringstream check(all.substr(end+1));check>>std::hex>>hash;
    if(!check||hash!=Hash(body))return false;
    check>>std::ws;if(!check.eof())return false;
    std::istringstream in(body);std::string magic;unsigned version=0;ProgressSave s;
    in.imbue(std::locale::classic());
    in>>magic>>version>>s.generation>>s.phase>>s.page;
    for(auto& v:s.player)in>>v;in>>s.yaw;
    if(version<1 || version>10)return false;
    for(unsigned i=0;i<(version==1?2U:version==2?3U:version==3?5U:11U);++i)for(auto& v:s.cast[i])in>>v;
    for(auto& v:s.doors)in>>v;
    if(version>=2)in>>s.quest_stage;
    else s.quest_stage=s.phase==2?1U:0U;
    if(version>=3){
        unsigned count=0;in>>count;if(count>(version>=8?1024U:128U))return false;
        s.collected_beans.resize(count);for(auto& ref:s.collected_beans)in>>ref;
    }
    if(version>=5)in>>s.peeves_phase>>s.twins_departed>>s.filch_seen>>s.filch_resume_stage;
    else {s.peeves_phase=s.quest_stage>=11?3U:0U;s.twins_departed=s.quest_stage>=8;s.filch_seen=s.quest_stage==14;}
    if(version>=6)in>>s.health>>s.lesson_passes>>s.frog_taken>>s.card_awarded>>s.card_taken;
    else if(s.quest_stage>12){s.card_awarded=s.card_taken=true;} // Do not trap existing saves behind a new gate.
    if(version>=7)in>>s.peeves_first_hit;
    else s.peeves_first_hit=s.peeves_phase>=2||s.health<100||s.frog_taken;
    if(version>=8){
        unsigned count=0;in>>s.map_id>>s.banked_beans>>s.challenge_stars>>count;
        if(!in||count>1024)return false;
        s.activated_events.resize(count);for(auto& ref:s.activated_events)in>>ref;
        in>>std::ws;if(in.peek()!='"')return false;
        in>>std::quoted(s.graph_state);
        in>>std::ws;if(in.peek()!='"')return false;
        in>>std::quoted(s.world_state);
    }
    if(version>=9){
        in>>s.earned_cards>>s.completed_maps;
        for(auto& points:s.house_points)in>>points;
        for(auto& score:s.lesson_best)in>>score;
        for(auto& points:s.lesson_points)in>>points;
        in>>std::ws;if(in.peek()!='"')return false;
        in>>std::quoted(s.charms_state);
    }else{
        if(s.card_taken)s.earned_cards|=1U;
        if(s.map_id==kBroomstickTrainingMapId&&std::binary_search(s.collected_beans.begin(),s.collected_beans.end(),265))s.earned_cards|=2U;
    }
    if(version>=10)in>>s.spent_beans;
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
    return p.map_id==0&&p.quest_stage==12&&!p.card_awarded&&p.collected_beans.size()>=25;
}
void ApplyTutorialDamage(ProgressSave& p){
    // Tutorial contact is survivable; death/respawn is a separate gameplay system.
    // Retail MaxLifePotions=50: five raw damage is ten percent, not five.
    p.health=p.health>10?p.health-10:1;
}
bool ApplyFirstPeevesContact(ProgressSave& p){
    if(p.map_id!=0||p.peeves_first_hit)return false;
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
    body<<"HPVR_PROGRESS 10 "<<next.generation<<' '<<next.phase<<' '<<next.page<<' '<<std::setprecision(9);
    for(auto f:next.player)body<<f<<' ';body<<next.yaw<<' ';
    for(const auto& a:next.cast)for(auto f:a)body<<f<<' ';
    for(auto f:next.doors)body<<f<<' ';body<<next.quest_stage<<' '<<next.collected_beans.size()<<' ';
    for(auto ref:next.collected_beans)body<<ref<<' ';body<<next.peeves_phase<<' '<<next.twins_departed<<' '<<next.filch_seen<<' '<<next.filch_resume_stage<<' '
        <<next.health<<' '<<next.lesson_passes<<' '<<next.frog_taken<<' '<<next.card_awarded<<' '<<next.card_taken<<' '<<next.peeves_first_hit<<' '
        <<next.map_id<<' '<<next.banked_beans<<' '<<next.challenge_stars<<' '<<next.activated_events.size()<<' ';
    for(auto ref:next.activated_events)body<<ref<<' ';
    body<<std::quoted(next.graph_state)<<' '<<std::quoted(next.world_state)<<' '
        <<next.earned_cards<<' '<<next.completed_maps<<' ';
    for(auto points:next.house_points)body<<points<<' ';
    for(auto score:next.lesson_best)body<<score<<' ';
    for(auto points:next.lesson_points)body<<points<<' ';
    body<<std::quoted(next.charms_state)<<' '<<next.spent_beans<<'\n';
    std::ostringstream complete;complete<<body.str()<<'#'<<std::hex<<Hash(body.str())<<'\n';
    const auto payload=complete.str();
    if(payload.size()>131072)return false;
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
bool LoadFrontAssets(const std::filesystem::path& root,FrontAssets* out,unsigned map_id){
    if(!out)return false;
    try{
        const auto* map=FindQuestMap(map_id);
        if(!map)throw std::runtime_error("Unsupported map ID");
        FrontAssets a;a.map_id=map_id;
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
            a.folio_secret[i]=tile("system/HPMenu.u","FEFolioHarryTexture"+n,true);
            a.report[i]=tile("system/HPMenu.u","FEReportBackTexture"+n,true);
        }
        const std::array<std::string,3> tabs{"MainTabTexture","FolioTabTexture","ReportTabTexture"};
        for(unsigned i=0;i<3;++i)a.tabs[i]=tile("system/HPMenu.u",tabs[i],true);
        a.health_full=tile("system/HPBase.u","HarryBarFull",true);
        a.health_empty=tile("system/HPBase.u","HarryBarEmpty",true);
        if(map_id==kHogwartsReturnMapId){
            a.boss_empty=tile("system/HPMenu.u","EnemyBarEmpty",true);
            a.peeves_health=tile("system/HPMenu.u","PeevesHead",true);
            a.malfoy_health=tile("system/HPMenu.u","MalfoyHead",true);
            a.has_boss_art=true;
        }
        {
            const auto& pixels=a.textures.at(a.health_full).rgba;unsigned first=256,last=0;
            for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)if(pixels[(y*256+x)*4+3]>=128){first=std::min(first,y);last=std::max(last,y);}
            if(first<256){a.health_top=float(first)/256;a.health_bottom=float(last+1)/256;}
        }
        a.bean_counter=tile("system/HPMenu.u","beancounter",true);
        for(unsigned i=0;i<4;++i)a.bean_pile[i]=tile("system/HPMenu.u","beans"+std::to_string(i+1),true);
        a.point_badge=tile("system/HPMenu.u","PointBadgeTexture",true);
        a.star_icon=tile("system/HPMenu.u","StarIcon",true);
        {
            auto owned_art=[&](const std::string& name){
                auto art=wand::load_hp1_p8_texture(root/"system/HPMenu.u",reference("system/HPMenu.u",name,"Engine.Texture"),true);
                if(art.status!=wand::Hp1ProfileStatus::ok||art.mips.empty())throw std::runtime_error("Missing report art: "+name);
                return art;
            };
            auto paste=[&](const std::string& name,unsigned left,unsigned top){
                const auto art=owned_art(name);const auto& mip=art.mips.front();
                for(unsigned y=0;y<mip.height&&top+y<480;++y)for(unsigned x=0;x<mip.width&&left+x<640;++x){
                    const auto source=(std::size_t(y)*mip.width+x)*4;
                    if(art.rgba8[source+3]==0)continue;
                    const unsigned dx=left+x,dy=top+y,index=dy/256*3+dx/256;
                    const auto target=(std::size_t(dy%256)*256+dx%256)*4;
                    std::copy_n(art.rgba8.begin()+source,4,a.textures[a.report[index]].rgba.begin()+target);
                }
            };
            paste("BeanBadgeTexture",31,62);paste("CardBadgeTexture",246,62);paste("PointBadgeTexture",466,62);
            paste("BlueUpTexture",107,354);paste("GreenUpTexture",286,354);paste("PurpleUpTexture",450,354);
            const std::array<std::string,4> sands{"BookReportBlueSand","BookReportYellowSand","BookReportGreenSand","BookReportRedSand"};
            for(unsigned i=0;i<sands.size();++i){
                const auto art=owned_art(sands[i]);const auto& mip=art.mips.front();
                if(mip.width!=256||mip.height!=256)throw std::runtime_error("Unsupported report sand dimensions");
                unsigned x0=256,y0=256,x1=0,y1=0;
                for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)if(art.rgba8[(y*256+x)*4+3]!=0){
                    x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);
                }
                if(x0>x1||y0>y1||x1-x0+1>64)throw std::runtime_error("Report sand does not fit its reserved atlas strip");
                // These tile halves lie outside the 640-pixel book. Retain native
                // sand pixels there without allocating another GPU texture layer.
                ReportSandArt crop{a.report[i<2?2:5],128+(i%2)*64,0,x1-x0+1,y1-y0+1,x0,y0};
                for(unsigned y=0;y<crop.height;++y)for(unsigned x=0;x<crop.width;++x){
                    const auto source=((y+y0)*256+x+x0)*4,target=((y+crop.y)*256+x+crop.x)*4;
                    std::copy_n(art.rgba8.begin()+source,4,a.textures[crop.texture].rgba.begin()+target);
                }
                a.report_sand[i]=crop;
            }
        }
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
        if(map_id==kHogwartsReturnMapId){
            std::ifstream letters(root/"system/pickup.int");std::string line;
            while(std::getline(letters,line))if(Fold(line).starts_with("hermione_letter_1=")){
                a.owl_letter=line.substr(18);if(!a.owl_letter.empty()&&a.owl_letter.back()=='\r')a.owl_letter.pop_back();
            }
            if(a.owl_letter.empty())throw std::runtime_error("Missing owned Hermione letter in system/pickup.int");
        }
        std::map<std::string,std::string> subtitles;
        std::ifstream menu_strings(root/"system/hpmenu.int",std::ios::binary);std::string menu_line;
        unsigned menu_lines=0;std::string menu_section;
        while(std::getline(menu_strings,menu_line)){
            ++menu_lines;
            if(!menu_line.empty()&&menu_line.back()=='\r')menu_line.pop_back();
            if(!menu_line.empty()&&menu_line.front()=='[')menu_section=Fold(menu_line);
            const std::string key(map->objective_key);
            if(menu_section=="[text]"&&Fold(menu_line).starts_with(key))a.level_objective=menu_line.substr(key.size());
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
        {
            constexpr std::array<const char*,25> names{"Dumbledore","Merlin","Tilly","Burdock","Adalbert","Uric","Derwent","Cornelius",
                "Newt","Edgar","Bowman","Godric","Morgan","Bertie","Salizar","Cassandra","Ignatia","Gifford","Elladora","Hengist",
                "Helga","Rowena","Roderic","Herpo","Harry"};
            for(unsigned page=0;page<a.card_atlas.size();++page){
                a.card_atlas[page]=page<6?a.folio_secret[page]:a.card_face;
                auto& atlas=a.textures[a.card_atlas[page]];
                atlas.name="owned-folio-card-atlas-"+std::to_string(page);std::fill(atlas.rgba.begin(),atlas.rgba.end(),std::uint8_t{0});
            }
            for(unsigned index=0;index<names.size();++index){
                const auto name=std::string("WizCard")+names[index]+"SmallTexture";
                const auto art=wand::load_hp1_p8_texture(root/"system/HPMenu.u",reference("system/HPMenu.u",name,"Engine.Texture"),true);
                if(art.status!=wand::Hp1ProfileStatus::ok||art.mips.empty()||art.mips.front().width!=128||art.mips.front().height!=128)
                    throw std::runtime_error("Unsupported owned folio card: "+name);
                auto& atlas=a.textures[a.card_atlas[index/4]].rgba;
                const unsigned x0=(index%2)*128,y0=(index%4/2)*128;
                for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x)
                    std::copy_n(art.rgba8.begin()+(y*128+x)*4,4,atlas.begin()+((y+y0)*256+x+x0)*4);
            }
        }
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
        if(map_id!=kIntroductionMapId){
            const auto map_actors=wand::inspect_hp1_actor_visuals(root/map->package_path);
            if(map_actors.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(map_actors.error);
            // Actor references belong to one package. Preserve the old audio
            // array prefix, but never associate map-zero NPC IDs with this map.
            a.bump_speech.clear();
            auto append_voice=[&](const std::string& name){
                for(const auto& voice:a.gameplay_audio)if(Fold(voice.object_name)==Fold(name))return voice.object_name;
                auto voice=wand::load_hp1_mpeg_sound(root/"Sounds/AllDialog.uax",
                    reference("Sounds/AllDialog.uax",name,"Engine.Sound"));
                if(voice.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(voice.error);
                const auto canonical=voice.object_name;
                a.gameplay_audio.push_back(std::move(voice));return canonical;
            };
            if(map_id==kHogwartsReturnMapId){
                for(const auto* name:{"111Peeves2","111Peeves3","111Peeves4","111Peeves5","111Peeves7"})append_voice(name);
                for(unsigned number:{19U,20U,21U,22U,31U,32U,33U,34U})append_voice("EmotivePeeves"+std::to_string(number));
            }
            if(map_id==3){
                const auto lessons=charms::LoadLessons(root,map_actors);
                for(const auto& lesson:lessons){
                    if(!lesson.valid)throw std::runtime_error("Missing owned charms lesson audio: "+lesson.error);
                    for(const auto& property:lesson.properties)
                        if(property.text_value_serialized&&Fold(property.name).starts_with("lesson")&&!property.text_value.empty())
                            append_voice(property.text_value);
                }
            }
            for(const auto& actor:map_actors.actors){
                std::map<std::int64_t,std::string> bumps;
                for(const auto& prop:actor.serialized_properties){
                    if(!prop.text_value_serialized)continue;
                    const auto key=Fold(prop.name);
                    if(key=="bumplines"){
                        const auto first=prop.text_value.find('<'),last=prop.text_value.find('>');
                        if(first!=std::string::npos&&last!=std::string::npos&&last>first+1)
                            bumps[std::max<std::int64_t>(0,prop.array_index)]=append_voice(prop.text_value.substr(first+1,last-first-1));
                    }else if(key.starts_with("cast")&&key.ends_with("script")){
                        std::istringstream command(prop.text_value);std::string op,argument;
                        command>>op;std::getline(command,argument);op=Fold(op);
                        if(dialogue::SpeechCommand(op)&&(op!="emote"||map_id==kHogwartsReturnMapId)){
                            const auto name=dialogue::Resolve(argument,subtitles);
                            if(name.empty()){
                                if(op=="emote"&&argument.find('<')==std::string::npos)continue;
                                throw std::runtime_error("Unresolved owned dialogue: "+actor.object_name+": "+prop.text_value);
                            }
                            a.dialogue_aliases[dialogue::Argument(argument)]=append_voice(name);
                        }
                    }
                }
                if(!bumps.empty()){
                    FrontAssets::BumpSpeech speech;speech.actor_reference=actor.actor_reference;
                    for(const auto& [index,voice_line]:bumps)speech.lines.push_back(voice_line);
                    a.bump_speech.push_back(std::move(speech));
                }
                const bool entry_song=map_id==kBroomstickTrainingMapId&&actor.qualified_class_name=="Engine.LevelInfo";
                if(!entry_song&&actor.qualified_class_name!="Engine.MusicEvent")continue;
                FrontAssets::MusicCue cue;cue.actor_reference=actor.actor_reference;cue.tag=actor.tag;
                for(const auto& prop:actor.serialized_properties){
                    if(prop.name=="PercentMusicVolume"&&prop.value.size()==1)
                        cue.volume_percent=std::min(100U,static_cast<unsigned>(prop.value.front()));
                    if(prop.name!="Song"||prop.object_path.empty())continue;
                    const auto& name=prop.object_path.back();
                    for(std::size_t i=0;i<a.music.size();++i)if(Fold(a.music[i].object_name)==Fold(name)){
                        cue.music_index=static_cast<int>(i);break;
                    }
                    if(cue.music_index>=0)continue;
                    const auto package="Music/"+prop.object_path.front()+".umx";
                    auto music=wand::load_hp1_mpeg_sound(root/package,reference(package,name,"Engine.Music"));
                    if(music.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(music.error);
                    cue.music_index=static_cast<int>(a.music.size());a.music.push_back(std::move(music));
                }
                if(entry_song){
                    if(cue.music_index<0)throw std::runtime_error("Missing owned broomstick entry music");
                    a.level_music_index=static_cast<unsigned>(cue.music_index);
                }else a.music_cues.push_back(std::move(cue));
            }
            if(map_id==kBroomstickTrainingMapId){
                const auto hooch=wand::inspect_hp1_class_visual_defaults(root/"system/Tut2.u",
                    reference("system/Tut2.u","BroomHooch","Core.Class"));
                if(hooch.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(hooch.error);
                unsigned comments=0;
                for(const auto& property:hooch.serialized_properties){
                    if(Fold(property.name)!="comments")continue;
                    std::size_t offset=0;const auto name=Text(property.value,offset);
                    if(!name.empty()){append_voice(name);++comments;}
                }
                if(comments<10)throw std::runtime_error("Missing owned flying-lesson commentary");
                const auto append_effect=[&](const std::string& name){
                    if(std::ranges::any_of(a.gameplay_audio,[&](const auto& sound){return Fold(sound.object_name)==Fold(name);}))return;
                    auto sound=wand::load_hp1_mpeg_sound(root/"system/HPSounds.u",
                        reference("system/HPSounds.u",name,"Engine.Sound"));
                    if(sound.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(sound.error);
                    a.gameplay_audio.push_back(std::move(sound));
                };
                append_effect("Q_Whistle_Short");append_effect("Q_Through_Hoop");append_effect("broom_accel");
                for(unsigned number=1;number<=15;++number)
                    append_effect("Q_Through_Hoop"+std::string(number<10?"0":"")+std::to_string(number));
            }
            // The challenge's MU1 music is triggered inside its first room.
            // Keep the inherited Hogwarts track until that authored event.
        }
        // Append additional object effects without changing the existing dialogue prefix.
        for(const auto& [package,name]:std::array<std::pair<const char*,const char*>,4>{{
            {"Sounds/Magic_sfx.uax","pickup_star"},{"Sounds/Hub1_sfx.uax","vase_breaking"},
            {"Sounds/Hub1_sfx.uax","cauldron_flip"},{"Sounds/Menu_sfx.uax","save_game"}}}){
            auto sound=wand::load_hp1_mpeg_sound(root/package,reference(package,name,"Engine.Sound"));
            if(sound.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(sound.error);
            a.gameplay_audio.push_back(std::move(sound));
        }
        if(map_id==3||map_id==kHogwartsReturnMapId)for(const auto* name:{"METAL_CHEST_OPEN_2","METAL_CHEST_OPEN_4","WOOD_CHEST_OPEN_1","WOOD_CHEST_OPEN_2","chest_landing"}){
            auto sound=wand::load_hp1_mpeg_sound(root/"system/HPSounds.u",reference("system/HPSounds.u",name,"Engine.Sound"));
            if(sound.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(sound.error);
            a.gameplay_audio.push_back(std::move(sound));
        }
        if(map_id==kHogwartsReturnMapId){
            a.scroll_pickup=wand::load_hp1_pcm_sound(root/"system/HPSounds.u",reference("system/HPSounds.u","pickup_page","Engine.Sound"));
            if(a.scroll_pickup.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(a.scroll_pickup.error);
        }
        if(map_id==kHogwartsReturnMapId)for(const auto* name:{"Malfoy_throws","MAL_candy_pickup","MAL_candy_hits_floor","MAL_candy_explodes","Cracker_Stretch_01","wizardcard_rotate","owl_hoot2"}){
            auto sound=wand::load_hp1_mpeg_sound(root/"system/HPSounds.u",reference("system/HPSounds.u",name,"Engine.Sound"));
            if(sound.status!=wand::Hp1ProfileStatus::ok)throw std::runtime_error(sound.error);
            a.gameplay_audio.push_back(std::move(sound));
        }
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
    if(!ok){message="SAVE FAILED - PREVIOUS SAVE KEPT";screen=FrontScreen::Stub;selection=0;}
    else message.clear();
    RefreshSlots();return ok;
}
void QuestFrontEnd::BeginStory(unsigned first_page){screen=FrontScreen::Story;page=std::min(first_page,13U);page_time=0;page_voice_started=false;}
void QuestFrontEnd::BeginGame(){screen=FrontScreen::Game;paused=FrontScreen::Game;selection=0;error_notice.clear();}
void QuestFrontEnd::ShowError(std::string text){
    if(text.empty())text="UNKNOWN ERROR";
    message=text;error_notice=std::move(text);screen=FrontScreen::Main;selection=0;
}
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
    if(screen==FrontScreen::Letter){
        if(press||cancel){screen=FrontScreen::Game;return FrontAction::Resume;}
        return FrontAction::None;
    }
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
    if(screen==FrontScreen::Controls){
        if(cancel){screen=FrontScreen::Vr;selection=kVrControlsRow;}
        else {
            int direction=press?1:step;
            if(horizontal&&!horizontal_down_)direction=move_x>0?1:-1;
            if(direction)controls_page=(controls_page+kControlsPageCount+direction)%kControlsPageCount;
        }
        horizontal_down_=horizontal;return FrontAction::None;
    }
    if(screen==FrontScreen::Vr){
        if(step)selection=(selection+kVrMenuRowCount+step)%kVrMenuRowCount;
        if(cancel||(press&&selection==kVrMenuRowCount-1)){ToggleVrMenu();return FrontAction::Resume;}
        if(press&&selection==2){debug_pinned=false;screen=FrontScreen::Debug;selection=0;return FrontAction::None;}
        if(press&&selection==kVrControlsRow){screen=FrontScreen::Controls;controls_page=0;selection=0;
            horizontal_down_=horizontal;return FrontAction::None;}
        if(selection<2&&horizontal&&!horizontal_down_){
            auto& value=selection==0?vr.render_scale:vr.ssr;
            value=std::clamp(value+(move_x>0?5:-5),selection==0?50:0,selection==0?175:100);
            vr_save_failed=!WriteVrSettings(saves.parent_path(),vr);
        }
        if(selection==3&&(press||(horizontal&&!horizontal_down_))){
            vr.relaxed_lesson=!vr.relaxed_lesson;
            vr_save_failed=!WriteVrSettings(saves.parent_path(),vr);
        }
        if(selection==4&&(press||(horizontal&&!horizontal_down_))){
            vr.first_person_cutscenes=!vr.first_person_cutscenes;
            vr_save_failed=!WriteVrSettings(saves.parent_path(),vr);
        }
        if(selection==5&&(press||(horizontal&&!horizontal_down_))){
            const int direction=horizontal&&move_x<0?-1:1;
            vr.casting_mode=static_cast<CastingMode>((static_cast<int>(vr.casting_mode)+3+direction)%3);
            vr_save_failed=!WriteVrSettings(saves.parent_path(),vr);
        }
        if(selection==6&&!refresh_rates.empty()&&(press||(horizontal&&!horizontal_down_))){
            const auto found=std::ranges::find(refresh_rates,vr.refresh_rate);
            const auto index=found==refresh_rates.end()?0:static_cast<int>(found-refresh_rates.begin());
            const auto count=static_cast<int>(refresh_rates.size());
            vr.refresh_rate=refresh_rates[(index+count+(move_x<0?-1:1))%count];
            vr_save_failed=!WriteVrSettings(saves.parent_path(),vr);
        }
        if(selection==7&&(press||(horizontal&&!horizontal_down_))){
            vr.voice_cast=!vr.voice_cast;
            vr_save_failed=!WriteVrSettings(saves.parent_path(),vr);
        }
        if(selection==8&&(press||(horizontal&&!horizontal_down_))){
            vr.voice_hints=!vr.voice_hints;
            vr_save_failed=!WriteVrSettings(saves.parent_path(),vr);
        }
        if(selection==kVrGpuBoostRow&&(press||(horizontal&&!horizontal_down_))){
            vr.gpu_boost=!vr.gpu_boost;
            vr_save_failed=!WriteVrSettings(saves.parent_path(),vr);
        }
        if(selection==kVrTurningRow&&(press||(horizontal&&!horizontal_down_))){
            vr.turning_mode=vr.turning_mode==TurningMode::Snap?TurningMode::Smooth:TurningMode::Snap;
            vr_save_failed=!WriteVrSettings(saves.parent_path(),vr);
        }
        if(selection==kVrTurnSpeedRow&&(press||(horizontal&&!horizontal_down_))){
            vr.smooth_turn_speed=std::clamp(vr.smooth_turn_speed+(horizontal&&move_x<0?-30:30),30,180);
            vr_save_failed=!WriteVrSettings(saves.parent_path(),vr);
        }
        horizontal_down_=horizontal;return FrontAction::None;
    }
    if(screen==FrontScreen::Cards&&horizontal&&!horizontal_down_)
        card_page=(card_page+7+(move_x>0?1:-1))%7;
    horizontal_down_=horizontal;
    unsigned count=0;
    switch(screen){
    case FrontScreen::Main:count=5;break;
    case FrontScreen::Levels:count=kPlayableQuestMapCount+1;break;
    case FrontScreen::LevelSlots:count=4;break;
    case FrontScreen::LevelStart:count=2;break;
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
        else if(selection==4){screen=FrontScreen::Levels;}
        else if(selection==3){screen=FrontScreen::Stub;message="USE THE QUEST MENU TO CLOSE THE APP";}
        else{screen=FrontScreen::Stub;message="NOT AVAILABLE IN THIS BUILD";}
        selection=0;break;
    case FrontScreen::Levels:
        if(selection>=kPlayableQuestMapCount){screen=FrontScreen::Main;selection=4;break;}
        selected_map=kQuestMaps[selection].id;RefreshSlots();screen=FrontScreen::LevelSlots;selection=0;break;
    case FrontScreen::LevelSlots:
        if(selection==3){screen=FrontScreen::Levels;selection=selected_map;break;}
        slot=selection;screen=FrontScreen::LevelStart;selection=1;break;
    case FrontScreen::LevelStart:
        if(selection==0)return FrontAction::StartSelectedLevel;
        screen=FrontScreen::LevelSlots;selection=slot;break;
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
        if(selection==3){screen=FrontScreen::Cards;selection=0;}
        else ToggleVrMenu();
        break;
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
    if(screen==FrontScreen::Controls)return s+"_"+std::to_string(controls_page);
    s+="_"+std::to_string(selection);
    if(screen==FrontScreen::Cards)s+="_"+std::to_string(card_page);
    if(screen==FrontScreen::Slots)for(bool b:occupied)s+=b?"1":"0";
    if(screen==FrontScreen::Slot)s+="_"+std::to_string(slot)+(occupied[slot]?"1":"0");
    if(screen==FrontScreen::Pause)s+=(paused==FrontScreen::Story?std::string("S"):std::string("G"))+
        std::to_string(assets.map_id!=kIntroductionMapId?0U:progress.quest_stage);
    if(screen==FrontScreen::Stub)s+=message.starts_with("SAVE FAILED")?"F":message.starts_with("USE")?"E":"N";
    if(screen==FrontScreen::Vr){s+=vr_save_failed?"F":"S";s+=vr.relaxed_lesson?"R":"O";s+=vr.first_person_cutscenes?"H":"T";s+=std::to_string(static_cast<int>(vr.casting_mode));s+=vr.voice_cast?"V":"N";s+=vr.voice_hints?"H":"Q";}
    if(screen==FrontScreen::Debug&&debug_pinned)s+="_pinned";
    return s;
}
std::vector<FrontQuad> QuestFrontEnd::Quads(bool include_report_values)const{
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
    if(screen==FrontScreen::Letter){
        tile(0,0,640,480,assets.white,0xb9d5e8);
        text("A LETTER FROM HERMIONE",74,66,2.2F,0x203040);
        std::istringstream words(assets.owl_letter);std::string word,line;float y=128;
        while(words>>word){
            if(line.size()+word.size()+1>42){text(line,66,y,2,0x203040);y+=27;line.clear();}
            if(!line.empty())line+=' ';line+=word;
        }
        if(!line.empty())text(line,66,y,2,0x203040);
        text("TRIGGER OR B: CONTINUE",140,410,2,0x203040);return out;
    }
    if(FloatingPanel()){
        tile(0,0,640,480,assets.white,0x140a05);
        tile(14,14,612,452,assets.white,0x26160a);
        text("HARRY POTTER VR",194,32,3,0xcd78ff);
        if(DemoNotice()){
            const bool finished=screen==FrontScreen::DemoEnd;
            const bool challenge=assets.map_id==1;
            const bool flying=assets.map_id==kBroomstickTrainingMapId;
            const std::string heading=!finished?"0.1.3 ALPHA - WELCOME":
                (flying?"BROOMSTICK TRAINING COMPLETE":challenge?"FLIPENDO CHALLENGE COMPLETE":"THANK YOU FOR PLAYING!");
            text(heading,320-float(heading.size())*6,78,2,0xffe164);
            if(finished){
                text(challenge||flying?"THE NEXT MAP IS NOT AVAILABLE YET.":"THIS IS THE END OF THE DEMO FOR NOW.",62,134,2,0xffffff);
                text(challenge||flying?"MORE STORY CONTENT IS IN DEVELOPMENT.":"DEVELOPMENT IS IN PROGRESS.",62,173,2,0xffffff);
                text("WE WOULD LOVE TO HEAR YOUR FEEDBACK.",62,209,2,0xffffff);
                text("THANKS FOR TRYING HARRY POTTER VR!",62,245,2,0xffffff);
            }else{
                const std::array lines{"VERY EARLY ALPHA - EXPECT BUGS.","NOW TESTING ALOHOMORA AND WINGARDIUM.",
                    "VOICE CASTING IS EXPERIMENTAL.","IT HAS ONLY BEEN TESTED BY THE AUTHOR.",
                    "IT MAY NOT RECOGNIZE EVERY PLAYER", "OR ACCENT. PLEASE SEND YOUR FEEDBACK.",
                    "FOLLOW DEVELOPMENT NEWS ON DISCORD."};
                for(unsigned i=0;i<lines.size();++i)text(lines[i],62,122+24*float(i),1.8F,0xffffff);
            }
            text("https://discord.com/channels/",62,292,1.65F,0xc7eaff);
            text("747967102895390741/1547254536203407390",62,315,1.65F,0xc7eaff);
            for(unsigned i=0;i<2;++i){
                const float y=354+float(i)*42;
                if(selection==i)tile(42,y-7,556,32,assets.white,0x875f19);
                text(i?"OPEN DISCORD IN BROWSER":"CONTINUE",62,y,2,0xffffff);
            }
            text("STICK: SELECT   TRIGGER: CONFIRM   B: CONTINUE",64,446,1.8F,0xd2beaa);
            return out;
        }
        if(screen==FrontScreen::Controls){
            text("CONTROLS",272,78,2,0xffe164);
            const unsigned help_page=std::min(controls_page,kControlsPageCount-1);
            const std::array<std::string,kControlsPageCount> titles{"BASICS","CLASSIC CASTING","GESTURE CASTING","VOICE CASTING","BROOM FLIGHT","CHARMS"};
            text(titles[help_page],320-float(titles[help_page].size())*6,111,2,0xc7eaff);
            const std::array<std::array<const char*,8>,kControlsPageCount> pages{{
                {{"MOVE - LEFT STICK","TURN - RIGHT STICK; SNAP / SMOOTH IN VR MENU","JUMP - A",
                  "SPRINT - CLICK LEFT STICK WHILE MOVING","PAUSE / BACK - B",
                  "VR MENU - HOLD BOTH GRIPS + LEFT MENU","RECENTER - CLICK BOTH STICKS TOGETHER",
                  "RELEASE BOTH STICKS BEFORE RECENTERING AGAIN"}},
                {{"SELECT CLASSIC IN VR SETTINGS.","HOLD THE RIGHT TRIGGER TO AIM.",
                  "POINT AT AN OBJECT'S FLIPENDO SYMBOL.","THE FLIPENDO SYMBOL LIGHTS UP.",
                  "RELEASE THE TRIGGER TO CAST FLIPENDO.","NO GESTURE IS NEEDED IN CLASSIC GAMEPLAY.",
                  "FLIPENDO IS CHOSEN FOR ELIGIBLE OBJECTS.","LESSON GESTURES ARE STILL REQUIRED."}},
                {{"SELECT VISIBLE GESTURE OR GESTURE.","HOLD THE RIGHT TRIGGER.",
                  "AIM AT A FLIPENDO SYMBOL TO LOCK ON.","DRAW THE FLIPENDO GESTURE, THEN RELEASE.",
                  "VISIBLE GESTURE SHOWS YOUR DRAWN LINE.","GESTURE HIDES THE LINE - SAME CONTROLS.",
                  "IN GAMEPLAY, ROTATION IS FLEXIBLE.","IN LESSONS, FOLLOW THE SHOWN PATTERN."}},
                {{"TURN ON VOICE CAST IN VR SETTINGS.","HOLD THE RIGHT TRIGGER.",
                  "AIM AT A FLIPENDO SYMBOL TO LOCK ON.","SAY FLIPENDO TO CAST.",
                  "KEEP HOLDING TO CAST AGAIN BY VOICE.","PAUSE BRIEFLY BETWEEN SPOKEN CASTS.",
                  "EXPERIMENTAL - ONLY TESTED BY THE AUTHOR.","MAY NOT RECOGNIZE EVERY VOICE OR ACCENT."}},
                {{"LEFT STICK - FLY FORWARD / BACK / SIDEWAYS.","RIGHT STICK LEFT / RIGHT - TURN.",
                  "RIGHT STICK UP / DOWN - ASCEND / DESCEND.","RELEASE THE LEFT STICK TO BRAKE.",
                  "FLY THROUGH THE ACTIVE HOOPS IN ORDER.","LOOK AROUND FREELY WITH YOUR HEAD.",
                  "RECENTER - CLICK BOTH STICKS TOGETHER.","VR SETTINGS AND CONTROLS REMAIN AVAILABLE."}},
                {{"COMPLETE THE LESSON TO LEARN EACH CHARM.","HOLD RIGHT TRIGGER AND AIM AT THE SYMBOL.",
                  "LOCKS USE ALOHOMORA; BLOCKS USE WINGARDIUM.","CLASSIC: RELEASE TO CAST THE SELECTED CHARM.",
                  "GESTURE: DRAW ITS PATTERN, THEN RELEASE.","AFTER LIFTING A BLOCK, HOLD RIGHT TRIGGER.",
                  "MOVE THE WAND TO GUIDE IT ONTO A PLATE.","RELEASE TO DROP. VOICE CAST IS FLIPENDO ONLY."}}
            }};
            for(unsigned i=0;i<pages[help_page].size();++i)text(pages[help_page][i],62,151+28*float(i),1.8F,0xffffff);
            text("PAGE "+std::to_string(help_page+1)+" / "+std::to_string(kControlsPageCount),260,389,2,0xffe164);
            text("STICK: CHANGE PAGE   TRIGGER: NEXT",62,421,1.65F,0xd2beaa);
            text("B: VR SETTINGS   BOTH GRIPS + MENU: CLOSE",62,446,1.65F,0xd2beaa);
            return out;
        }
        text(screen==FrontScreen::Debug?"PERFORMANCE DEBUGGER":"VR SETTINGS",screen==FrontScreen::Debug?194.0F:254.0F,78,2,0xffe164);
        if(screen==FrontScreen::Debug){
            text("LIVE FRAME TIMINGS / DEVICE COUNTERS",110,108,2,0xd2beaa);
            text(debug_pinned?"BOTH GRIPS + MENU: HIDE / SETTINGS":"TRIGGER: PIN + PLAY    B: BACK TO SETTINGS",65,438,1.8F,0xd2beaa);
        }else{
            const std::array<std::string,kVrMenuRowCount> rows{"RENDER SCALE","SSR - WOOD FLOORS","PERFORMANCE DEBUGGER","LESSON DIFFICULTY","CUTSCENE CAMERA","SPELL CASTING","REFRESH RATE","VOICE CAST","VOICE HINTS","TURNING","TURN SPEED","CONTROLS","GPU BOOST","RETURN"};
            for(unsigned i=0;i<kVrMenuRowCount;++i){
                const float y=VrMenuRowY(i);
                if(selection==i)tile(42,y-3,556,20,assets.white,0x875f19);
                text(rows[i],62,y,2.1F,selection==i?0xfff5ff:0xffe164);
            }
            text(vr.relaxed_lesson?"RELAXED":"ORIGINAL",443,VrMenuRowY(3),2.1F,0xffffff);
            text(vr.first_person_cutscenes?"HARRY 1ST PERSON":"THEATRICAL",410,VrMenuRowY(4),1.65F,0xffffff);
            text(vr.casting_mode==CastingMode::VisibleGesture?"VISIBLE GESTURE":vr.casting_mode==CastingMode::Gesture?"GESTURE":"CLASSIC",410,VrMenuRowY(5),1.65F,0xffffff);
            text(vr.voice_cast?"ON":"OFF",443,VrMenuRowY(7),2.1F,0xffffff);
            text(vr.voice_hints?"ON":"OFF",443,VrMenuRowY(8),2.1F,0xffffff);
            text("STICK: SELECT / ADJUST    TRIGGER: OPEN",72,393,1.8F,0xd2beaa);
            text("BOTH GRIPS + MENU: CLOSE    B: BACK",82,426,1.8F,0xd2beaa);
            const char* hint=selection==3?(vr.relaxed_lesson?"RELAXED: WIDE TOLERANCE, NO TIMER":"ORIGINAL: 12S, 50 / 65 / 80 / 95%"):
                selection==4?"SWITCHES LIVE - HEAD MOVEMENT STAYS FREE":
                selection==5?(vr.casting_mode==CastingMode::VisibleGesture?"DRAW YOUR GESTURE - WAND TRACE VISIBLE":
                    vr.casting_mode==CastingMode::Gesture?"DRAW YOUR GESTURE - NO TRACE OR TEMPLATE":"AIM, HOLD AND RELEASE - AUTO SPELL SELECT"):
                selection==6?"AVAILABLE HEADSET RATES - APPLIES LIVE":
                selection==7?"OPTIONAL VOICE INPUT WITH ANY CASTING MODE":
                selection==8?"SHOW VOICE PROMPTS ABOVE THE AIMED TARGET":
                selection==kVrTurningRow?"SNAP: 30 DEGREE STEPS / SMOOTH: CONTINUOUS":
                selection==kVrTurnSpeedRow?"SMOOTH TURN SPEED - DEGREES PER SECOND":
                selection==kVrControlsRow?"MOVEMENT, RECENTERING AND ALL CASTING MODES":
                selection==kVrGpuBoostRow?"HIGHER GPU POWER REQUEST - USES MORE BATTERY":
                "LIVE SETTINGS - 175% USES 3.06X BASE PIXELS";
            text(vr_save_failed?"SAVE FAILED - SETTINGS ARE TEMPORARY":hint,65,446,1.65F,0xd2beaa);
        }
        return out;
    }
    const bool book=screen==FrontScreen::Pause||screen==FrontScreen::Cards||screen==FrontScreen::Report;
    const auto& bg=book?assets.book:(screen==FrontScreen::Main?assets.menu:assets.paper);
    for(unsigned i=0;i<6;++i)tile(float(i%3*256),float(i/3*256),256,256,bg[i]);
    if(book){
        if(screen!=FrontScreen::Cards)for(unsigned i=0;i<6;++i)tile(float(i%3*256),float(i/3*256),256,256,assets.report[i]);
        if(screen==FrontScreen::Cards)for(unsigned i=0;i<3;++i)tile(8,float(80+i*72),32,64,assets.tabs[i]);
    }
    if(screen==FrontScreen::Pause||screen==FrontScreen::Report){
        if(include_report_values){
            for(unsigned house=0;house<4;++house){
                const auto fill=std::min(256U,ReportValue(house+3)*256U/400U);
                const auto quads=ReportSandQuads(fill,house);out.insert(out.end(),quads.begin(),quads.end());
            }
            for(unsigned field=0;field<7;++field){
                auto value=ReportValue(field);unsigned place=0;
                do{const auto quads=ReportDigitQuads(value%10,field,place++);out.insert(out.end(),quads.begin(),quads.end());value/=10;}while(value&&place<7);
            }
        }
        text("/25",311,131,1.6F,0x17110a);
        const std::array<std::string,3> labels{"SAVE AND QUIT","OPTIONS","FOLIO MAGI"};
        constexpr std::array<float,3> centers{139,318,482};
        constexpr std::array<unsigned,3> rows{1,4,3};
        for(unsigned i=0;i<3;++i){
            const auto color=selection==rows[i]?0x7b187bU:0xc000c0U;
            const float x=centers[i]-float(labels[i].size())*4.5F;
            text(labels[i],x+1,417,1.5F,0x402040);text(labels[i],x,416,1.5F,color);
            if(selection==rows[i])text(">",centers[i]-5,345,1.8F,0xc000c0);
        }
        const std::string resume=selection==0?"> RESUME":"RESUME";
        const std::string skip=paused==FrontScreen::Story?"SKIP STORY":"SKIP SCENE";
        text(resume,29,27,1.6F,selection==0?0xc000c0:0x46342b);
        text((selection==2?"> ":"")+skip,479,27,1.6F,selection==2?0xc000c0:0x46342b);
        text("STICK: SELECT   TRIGGER: CONFIRM   B: RESUME",68,452,1.4F,0x46342b);
        if(message.starts_with("SAVE FAILED"))text(message,74,435,1.5F,0x2222cc);
        return out;
    }
    if(screen==FrontScreen::Cards){
        text("WIZARD CARDS",244,14,2,0x302820);
        const auto mask=progress.earned_cards|(progress.card_taken?1U:0U);
        for(unsigned local=0;local<4;++local){
            const unsigned index=card_page*4+local;if(index>=25)break;
            tile(148+float(local%2)*204,46+float(local/2)*155,128,128,assets.missing_small);
            if(include_report_values&&(mask&(1U<<index))!=0){
                const auto quads=FolioCardQuads(index);out.insert(out.end(),quads.begin(),quads.end());
            }
        }
        text("CARDS "+std::to_string(card_page*4+1)+" - "+std::to_string(std::min(25U,card_page*4+4)),239,358,1.5F,0xc7eaff);
        tile(80,400,64,40,assets.arrow_left);tile(485,400,64,40,assets.arrow_right);
        const std::array<std::string,3> actions{"NEXT PAGE","PREVIOUS PAGE","BACK TO BOOK"};
        text(actions[selection],210,415,1.8F,0xc7eaff);
        text(std::to_string(card_page+1)+" / 7",294,387,1.7F,0xc7eaff);
        text("STICK: PAGE / SELECT   TRIGGER: CONFIRM   B: BACK",41,459,1.5F,0xc7eaff);
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
        const std::array<std::string,5> labels{"START GAME","OPTIONS","QUIDDITCH","EXIT","LEVEL SELECT"};
        for(unsigned i=0;i<5;++i){
            const float x=320-static_cast<float>(labels[i].size())*6,y=342+static_cast<float>(i)*22;
            text(labels[i],x+1,y+1,2,0);text(labels[i],x,y,2,i==selection?0x7373ff:0xffffff);
            if(i==selection)text(">",x-18,y,2,0x7373ff);
        }
        text("LEFT STICK: SELECT   TRIGGER: CONFIRM   B: BACK",50,456,1.7F,0xffffff);
        return out;
    }
    std::vector<std::string> labels;std::string title;
    switch(screen){
    case FrontScreen::Main:title="MAIN MENU";labels={"START GAME","OPTIONS","QUIDDITCH","EXIT"};break;
    case FrontScreen::Levels:
        title="START LEVEL FROM THE BEGINNING";
        for(unsigned i=0;i<kPlayableQuestMapCount;++i)labels.emplace_back(kQuestMaps[i].menu_title);
        labels.emplace_back("BACK");break;
    case FrontScreen::LevelSlots:title="CHOOSE SAVE SLOT FOR THIS RUN";labels={"GAME 1","GAME 2","GAME 3","BACK"};break;
    case FrontScreen::LevelStart:title="REPLACE SELECTED SLOT WITH A NEW RUN?";labels={"YES - START LEVEL","NO - KEEP SAVE"};break;
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
        if(assets.map_id==0&&progress.quest_stage<objectives.size())
            text(objectives[progress.quest_stage],55,306,1.65F,0xffffff);
        else {
            std::istringstream words(assets.level_objective);std::string word,line;float y=306;
            while(words>>word){
                if(line.size()+word.size()+1>50){text(line,55,y,1.65F,0xffffff);line.clear();y+=19;}
                if(!line.empty())line+=' ';line+=word;
            }
            text(line,55,y,1.65F,0xffffff);
        }
    }
    if(message.starts_with("SAVE FAILED"))text(message,74,426,1.5F,0x9999ff);
    return out;
}
unsigned QuestFrontEnd::ReportValue(unsigned field)const{
    if(field==0){
        auto local=static_cast<unsigned>(progress.collected_beans.size());
        if(!assets.non_bean_pickups.empty()){
            for(const auto ref:assets.non_bean_pickups)if(std::binary_search(progress.collected_beans.begin(),progress.collected_beans.end(),ref))--local;
            const auto total=std::min(1000000U,progress.banked_beans+local);
            return total-std::min(total,progress.spent_beans);
        }
        if(progress.map_id==kBroomstickTrainingMapId&&std::binary_search(progress.collected_beans.begin(),progress.collected_beans.end(),265))--local;
        if(progress.map_id==3)for(const int reference:{1568,2918,0x20000000+2846*16})
            if(std::binary_search(progress.collected_beans.begin(),progress.collected_beans.end(),reference))--local;
        if(progress.map_id==1||progress.map_id==3)local-=std::min(local,progress.challenge_stars);
        const auto total=std::min(1000000U,progress.banked_beans+local);
        return total-std::min(total,progress.spent_beans);
    }
    if(field==1){
        auto cards=progress.earned_cards;
        if(progress.card_taken)cards|=1U;
        if(progress.map_id==kBroomstickTrainingMapId&&std::binary_search(progress.collected_beans.begin(),progress.collected_beans.end(),265))cards|=2U;
        return static_cast<unsigned>(std::popcount(cards&0x1ffffffU));
    }
    if(field==2)return progress.house_points[3];
    if(field<7)return progress.house_points[field-3];
    return 0;
}
std::vector<FrontQuad> QuestFrontEnd::ReportDigitQuads(unsigned digit,unsigned field,unsigned place)const{
    if(digit>9||field>=7||place>=7)return {};
    constexpr std::array<float,7> right{108,307,543,225,289,353,418};
    const float scale=field<3?1.6F:1.35F;
    const unsigned ch='0'+digit;
    return {{right[field]-float(place+1)*6*scale,field<3?131.0F:227.0F,5*scale,7*scale,
        float(ch%16*16+2)/256,float(ch/16*16+2)/256,5.0F/256,7.0F/256,assets.font,0x17110a}};
}
std::vector<FrontQuad> QuestFrontEnd::ReportSandQuads(unsigned fill_pixels,unsigned house)const{
    if(house>=4||fill_pixels==0)return {};
    const auto& art=assets.report_sand[house];
    if(art.width==0||art.height==0)return {};
    const unsigned cut=257-std::min(256U,fill_pixels);
    const unsigned top=std::max(art.source_y,cut),bottom=art.source_y+art.height;
    if(top>=bottom)return {};
    constexpr std::array<unsigned,4> offsets{87,151,215,280};
    const unsigned skip=top-art.source_y;
    return {{float(offsets[house]+art.source_x),float(125+top),float(art.width),float(bottom-top),
        float(art.x)/256,float(art.y+skip)/256,float(art.width)/256,float(bottom-top)/256,art.texture,0xffffff}};
}
std::vector<FrontQuad> QuestFrontEnd::FolioCardQuads(unsigned card_index)const{
    if(card_index>=campaign::kCardIds.size())return {};
    constexpr std::array<const char*,25> names{"DUMBLEDORE","MERLIN","TILLY TOKE","BURDOCK","ADALBERT","URIC","DERWENT","CORNELIUS",
        "NEWT","EDGAR","BOWMAN","GODRIC","MORGAN","BERTIE BOTT","SALAZAR","CASSANDRA","IGNATIA","GIFFORD","ELLADORA","HENGIST",
        "HELGA","ROWENA","RODERIC","HERPO","HARRY POTTER"};
    const unsigned local=card_index%4;
    const float x=148+float(local%2)*204,y=46+float(local/2)*155;
    std::vector<FrontQuad> quads{{x,y,128,128,float(local%2)*0.5F,float(local/2)*0.5F,0.5F,0.5F,assets.card_atlas[card_index/4],0xffffff}};
    const std::string label=names[card_index];float cursor=x+64-float(label.size())*3.6F;
    for(unsigned char ch:label){
        if(ch!=' ')quads.push_back({cursor,y+128,6,8.4F,float(ch%16*16+2)/256,float(ch/16*16+2)/256,5.0F/256,7.0F/256,assets.font,0xc7eaff});
        cursor+=7.2F;
    }
    return quads;
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
std::vector<FrontQuad> QuestFrontEnd::ChallengeStarQuads(unsigned count,bool report)const{
    const unsigned total=assets.map_id==3?6U:8U;
    count=std::min(count,total);
    std::vector<FrontQuad> out;float x=report?175.0F:55.0F;
    const float y=report?175.0F:375.0F,scale=report?1.8F:1.65F;
    for(unsigned char ch:std::string("CHALLENGE STARS: ")+std::to_string(count)+" / "+std::to_string(total)){
        if(ch!=' ')out.push_back({x,y,5*scale,7*scale,float(ch%16*16+2)/256,
            float(ch/16*16+2)/256,5.0F/256,7.0F/256,assets.font,report?0x302820U:0xffffffU});
        x+=6*scale;
    }
    return out;
}
std::vector<FrontQuad> QuestFrontEnd::PeevesHealthQuads(unsigned hits_left,bool active)const{
    if(!active||!assets.has_boss_art)return {};
    // The portrait occupies the first 97 pixels; only the bar is depleted.
    const float width=97.F+159.F*float(std::min(hits_left,4U))/4.F;
    return {{8,316,256,256,0,0,1,1,assets.boss_empty,0xffffff},
            {8,316,width,256,0,0,width/256.F,1,assets.peeves_health,0xffffff}};
}
std::vector<FrontQuad> QuestFrontEnd::MalfoyHealthQuads(unsigned hits_left,bool active)const{
    if(!active||!assets.has_boss_art)return {};
    const float width=97.F+159.F*float(std::min(hits_left,3U))/3.F;
    return {{8,316,256,256,0,0,1,1,assets.boss_empty,0xffffff},
            {8,316,width,256,0,0,width/256.F,1,assets.malfoy_health,0xffffff}};
}
namespace {
void BroomHudText(std::vector<FrontQuad>& out,std::uint32_t font,std::string_view text,float x,float y,float scale){
    for(unsigned char ch:text){
        if(ch!=' '){
            const float u=float(ch%16*16+2)/256,v=float(ch/16*16+2)/256;
            out.push_back({x+1,y+1,5*scale,7*scale,u,v,5.0F/256,7.0F/256,font,0});
            out.push_back({x,y,5*scale,7*scale,u,v,5.0F/256,7.0F/256,font,0xffffff});
        }
        x+=6*scale;
    }
}
}
std::vector<FrontQuad> QuestFrontEnd::BroomLabelQuads()const{
    std::vector<FrontQuad> out;
    constexpr float y=375,scale=1.6F;
    BroomHudText(out,assets.font,"HOOPS",55,y,scale);
    BroomHudText(out,assets.font,"/82",142,y,scale);
    BroomHudText(out,assets.font,"TIME",225,y,scale);
    BroomHudText(out,assets.font,"STAGE",370,y,scale);
    BroomHudText(out,assets.font,"/5",444,y,scale);
    BroomHudText(out,assets.font,"LEFT STICK: FLY   RIGHT STICK: TURN / HEIGHT",55,396,1.25F);
    return out;
}
std::vector<FrontQuad> QuestFrontEnd::BroomNumberQuads(unsigned value,unsigned field)const{
    if(field>2)return {};
    const unsigned bounded=field==0?std::min(value,82U):field==1?std::min(value,180U):std::clamp(value,1U,5U);
    constexpr std::array<float,3> right{139.2F,308.8F,441.6F};
    constexpr float scale=1.6F;
    const auto number=std::to_string(bounded);
    std::vector<FrontQuad> out;
    BroomHudText(out,assets.font,number,right[field]-float(number.size())*6*scale,375,scale);
    return out;
}
std::vector<FrontQuad> QuestFrontEnd::VrValueQuads(int value,bool scale)const{
    std::vector<FrontQuad> out;float x=443;
    const std::string label=value==0&&!scale?"OFF":std::to_string(value)+"%";
    for(unsigned char ch:label){
        if(ch!=' ')out.push_back({x,VrMenuRowY(scale?0:1),10.5F,14.7F,float(ch%16*16+2)/256,float(ch/16*16+2)/256,5.0F/256,7.0F/256,assets.font,0xffffff});
        x+=12.6F;
    }return out;
}
std::vector<FrontQuad> QuestFrontEnd::VrRefreshQuads(int hz)const{
    std::vector<FrontQuad> out;float x=443;
    for(unsigned char ch:hz?std::to_string(hz)+" HZ":std::string("RUNTIME")){
        if(ch!=' ')out.push_back({x,VrMenuRowY(6),10.5F,14.7F,float(ch%16*16+2)/256,float(ch/16*16+2)/256,5.0F/256,7.0F/256,assets.font,0xffffff});
        x+=12.6F;
    }return out;
}
std::vector<FrontQuad> QuestFrontEnd::VrGpuBoostQuads(bool enabled)const{
    auto result=VrTurningQuads(enabled);
    result.clear();float x=443;
    for(unsigned char ch:std::string(enabled?"ON":"OFF")){
        result.push_back({x,VrMenuRowY(kVrGpuBoostRow),8.25F,11.55F,float(ch%16*16+2)/256,float(ch/16*16+2)/256,11.0F/256,14.0F/256,assets.font,0xffffff});x+=8.25F;
    }
    return result;
}
std::vector<FrontQuad> QuestFrontEnd::VrTurningQuads(bool smooth)const{
    std::vector<FrontQuad> out;float x=443;
    for(unsigned char ch:smooth?std::string("SMOOTH"):std::string("SNAP")){
        out.push_back({x,VrMenuRowY(kVrTurningRow),10.5F,14.7F,float(ch%16*16+2)/256,float(ch/16*16+2)/256,
            5.0F/256,7.0F/256,assets.font,0xffffff});
        x+=12.6F;
    }return out;
}
std::vector<FrontQuad> QuestFrontEnd::VrTurnSpeedQuads(int degrees)const{
    std::vector<FrontQuad> out;float x=443;
    for(unsigned char ch:std::to_string(degrees)+" DEG/S"){
        if(ch!=' ')out.push_back({x,VrMenuRowY(kVrTurnSpeedRow),8.25F,11.55F,float(ch%16*16+2)/256,float(ch/16*16+2)/256,
            5.0F/256,7.0F/256,assets.font,0xffffff});
        x+=9.9F;
    }return out;
}
std::vector<FrontQuad> QuestFrontEnd::VrVoiceStatusQuads(unsigned status)const{
    const char* labels[]{"VOICE OFF","MIC PERMISSION REQUIRED","LOADING VOICE MODEL","VOICE READY - AIM AND HOLD",
        "LISTENING FOR FLIPENDO","WAITING TO LISTEN AGAIN","VOICE MODEL UNAVAILABLE","MIC RETRYING AUTOMATICALLY"};
    std::vector<FrontQuad> out;float x=145;
    for(unsigned char ch:std::string(labels[std::min(status,7U)])){
        if(ch!=' ')out.push_back({x,408,7.5F,10.5F,float(ch%16*16+2)/256,float(ch/16*16+2)/256,5.0F/256,7.0F/256,assets.font,0xffffff});
        x+=9;
    }return out;
}
std::vector<FrontQuad> QuestFrontEnd::VoiceAimQuads(unsigned status,bool alohomora)const{
    const char* labels[]{"","ALLOW MICROPHONE","PREPARING VOICE","PLEASE WAIT",
        "SAY FLIPENDO","WAIT...","VOICE UNAVAILABLE","MIC RETRYING"};
    const std::string label=status==4&&alohomora?"SAY ALOHOMORA":labels[std::min(status,7U)];
    std::vector<FrontQuad> out;float x=320-float(label.size())*4.5F;
    for(unsigned char ch:label){
        if(ch!=' ')out.push_back({x,235,7.5F,10.5F,float(ch%16*16+2)/256,float(ch/16*16+2)/256,
            5.0F/256,7.0F/256,assets.font,status>=6?0xffc080U:0xffffffU});
        x+=9;
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
std::vector<FrontQuad> QuestFrontEnd::HousePointQuads(int digit,unsigned place,unsigned digits)const{
    if(digit<0)return {{256,12,128,128,0,0,1,1,assets.point_badge,0xffffff}};
    if(digit>9||digits<1||digits>7||place>=digits)return {};
    const unsigned ch=static_cast<unsigned>('0'+digit);
    return {{315+float(digits-1)*5.5F-float(place)*11,88,10,14,float(ch%16*16+2)/256,float(ch/16*16+2)/256,
        5.0F/256,7.0F/256,assets.font,0x151515}};
}
std::vector<FrontQuad> QuestFrontEnd::StarPickupQuads(unsigned count)const{
    std::vector<FrontQuad> out{{256,12,128,128,0,0,1,1,assets.bean_counter,0xffffff},
        {288,32,64,64,0,0,1,1,assets.star_icon,0xffffff}};
    const auto number=std::to_string(count);float x=320-float(number.size())*6;
    for(unsigned char ch:number){
        out.push_back({x,124,10,14,float(ch%16*16+2)/256,float(ch/16*16+2)/256,
            5.0F/256,7.0F/256,assets.font,0xffffff});x+=12;
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
std::vector<std::string> ErrorNoticeLines(std::string_view text){
    std::vector<std::string> lines{"ERROR - TAKE A PHOTO OF THIS AND SEND IT TO THE AUTHOR"};
    std::string line;bool truncated=false;
    const auto push=[&](){
        if(line.empty())return;
        if(lines.size()<kErrorNoticeLines)lines.push_back(std::move(line));else truncated=true;
        line.clear();
    };
    std::size_t start=0;
    while(start<text.size()){
        while(start<text.size()&&std::isspace(static_cast<unsigned char>(text[start])))++start;
        auto end=start;
        while(end<text.size()&&!std::isspace(static_cast<unsigned char>(text[end])))++end;
        if(end==start)break;
        std::string word;
        for(auto i=start;i<end;++i){
            const auto ch=static_cast<unsigned char>(text[i]);
            // Only printable ASCII has glyphs in the owned menu font.
            word.push_back(ch>='a'&&ch<='z'?char(ch-32):ch>32&&ch<127?char(ch):'?');
        }
        start=end;
        if(!line.empty()&&line.size()+1+word.size()>kErrorNoticeColumns)push();
        while(word.size()>kErrorNoticeColumns){
            push();line=word.substr(0,kErrorNoticeColumns);word.erase(0,kErrorNoticeColumns);push();
        }
        if(!line.empty())line+=' ';
        line+=word;
    }
    push();
    if(truncated){
        auto& last=lines.back();
        if(last.size()+3>kErrorNoticeColumns)last.resize(kErrorNoticeColumns-3);
        last+="...";
    }
    return lines;
}
} // namespace hpvr::quest
