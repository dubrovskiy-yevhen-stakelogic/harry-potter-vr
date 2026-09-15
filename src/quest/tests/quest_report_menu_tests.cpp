#include "hpvr/quest_frontend.h"
#include "hpvr/quest_charms_lesson.h"
#include "hpvr/quest_lesson_speech.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace hpvr::quest;
namespace {
void Check(bool passed,const char* message){if(!passed)throw std::runtime_error(message);}
void Bounds(const FrontQuad& q){
    Check(q.x>=0&&q.y>=0&&q.x+q.w<=640&&q.y+q.h<=480,"menu quad remains in panel");
    Check(q.u>=0&&q.v>=0&&q.u+q.uw<=1&&q.v+q.vh<=1,"menu UV remains inside texture");
}
}
int main(int argc,char** argv){try{
    Check(argc<=2,"usage: hpvr_quest_report_menu_tests [owned-root]");
    charms::LessonMetadata speech_lesson;
    const auto speech_property=[&](const char* name,int index,const char* text){
        hpvr::wand::Hp1ClassDefaultProperty property;property.name=name;property.array_index=index;
        property.text_value=text;property.text_value_serialized=true;speech_lesson.properties.push_back(std::move(property));
    };
    speech_property("LessonJudgeSuccess",0,"success");speech_property("LessonPoints",0,"five_points");
    speech_property("LessonRepeatSuccess",0,"next_round");speech_property("LessonJudgeVBad",0,"bad");
    speech_property("LessonRepeatFailure",0,"retry");speech_property("LessonFullMarks",0,"perfect");
    Check(charms::LessonFeedback(speech_lesson,0,1,false,.75F,0)==std::vector<std::string>{"success","five_points","next_round"},"judge, award and repeat play in authored order");
    Check(charms::LessonFeedback(speech_lesson,0,0,false,0,0)==std::vector<std::string>{"bad","retry"},"failed first attempt gets retry instruction without points");
    Check(charms::LessonFeedback(speech_lesson,0,1,true,1,0)==std::vector<std::string>{"perfect","five_points"},"completed lesson waits for award without a repeat instruction");
    charms::LessonSpeechQueue speech;speech.Reset({"intro","points"});
    for(unsigned i=0;i<20;++i)Check(speech.Advance(.1F,true,false).line.empty(),"cutscene keeps the intro queued");
    for(unsigned i=0;i<20;++i)Check(speech.Advance(.1F,false,true).line.empty(),"existing dialogue cannot be overwritten by the lesson");
    Check(speech.Advance(.1F,false,false).line=="intro","intro begins after cutscene dialogue");speech.Started(1);
    for(unsigned i=0;i<10;++i)Check(speech.Advance(.1F,false,true).line.empty(),"points cannot replace unfinished intro");
    bool stopped=false;for(unsigned i=0;i<10;++i)stopped|=speech.Advance(.1F,false,true).stop;
    Check(stopped,"stalled owned lesson playback has a bounded timeout");
    Check(speech.Advance(.1F,false,false).line=="points","award follows completed intro");speech.Started(.2F);
    for(unsigned i=0;i<10;++i)(void)speech.Advance(.1F,false,false);
    Check(!speech.Pending(),"finished speech releases lesson input");
    QuestFrontEnd front;front.screen=FrontScreen::Pause;
    for(unsigned map:{1U,3U}){
        front.assets.map_id=map;
        const auto total=map==3?6U:8U;
        for(unsigned count=0;count<=total;++count){
            const auto quads=front.ChallengeStarQuads(count,false);
            Check(!quads.empty(),"challenge star counter exists for both walking challenges");
            for(const auto& q:quads){Bounds(q);Check(q.y>=350,"star count stays near the bottom of the menu");}
        }
        const auto full=front.ChallengeStarQuads(total,false),clamped=front.ChallengeStarQuads(999,false);
        Check(full.size()==clamped.size()&&full.back().u==clamped.back().u&&full.back().v==clamped.back().v,
            "star counter clamps to this map's actual total");
    }
    front.assets.report_sand={{{1,128,0,57,174,99,42},{1,192,0,57,172,99,42},
                              {2,128,0,57,172,99,42},{2,192,0,57,172,99,42}}};
    front.progress.banked_beans=45;front.progress.collected_beans={2,7};
    front.progress.earned_cards=7;front.progress.house_points={78,46,113,90};
    Check(front.ReportValue(0)==47&&front.ReportValue(1)==3&&front.ReportValue(2)==90,"live cumulative badge values");
    for(unsigned i=0;i<4;++i)Check(front.ReportValue(i+3)==front.progress.house_points[i],"house display order");
    const auto key=front.DrawKey();front.progress.banked_beans=999999;front.progress.earned_cards=0x1ffffff;
    Check(front.DrawKey()==key,"totals do not multiply baked menu variants");
    Check(front.ReportValue(0)==1000000&&front.ReportValue(1)==25,"badge bounds");
    front.progress.map_id=kBroomstickTrainingMapId;front.progress.banked_beans=0;front.progress.collected_beans={265};
    Check(front.ReportValue(0)==0,"Merlin pickup is not a bean");
    front.progress.map_id=3;front.progress.collected_beans={7,2918,0x20000000+2846*16};front.progress.challenge_stars=1;
    Check(front.ReportValue(0)==0,"charms stars and wizard cards are not beans");
    for(unsigned field=0;field<7;++field)for(unsigned place=0;place<7;++place)for(unsigned digit=0;digit<10;++digit)
        for(const auto& q:front.ReportDigitQuads(digit,field,place))Bounds(q);
    Check(front.ReportDigitQuads(10,0,0).empty()&&front.ReportDigitQuads(1,7,0).empty()&&front.ReportDigitQuads(1,0,7).empty(),"invalid digits rejected");
    for(unsigned house=0;house<4;++house){
        float previous=0;
        for(unsigned fill=0;fill<=256;++fill){
            const auto quads=front.ReportSandQuads(fill,house);float height=0;
            for(const auto& q:quads){Bounds(q);height+=q.h;}
            Check(height>=previous,"sand rises monotonically");previous=height;
        }
        Check(previous==float(front.assets.report_sand[house].height),"full vial shows all authored sand");
    }
    for(unsigned card=0;card<25;++card){const auto quads=front.FolioCardQuads(card);Check(!quads.empty(),"all 25 original folio slots render");for(const auto& q:quads)Bounds(q);}
    Check(front.FolioCardQuads(25).empty(),"unknown folio slot rejected");
    front.selection=4;front.Input(0,false,false);front.Input(0,true,false);
    Check(front.screen==FrontScreen::Vr&&front.vr_return==FrontScreen::Pause,"original Options orb opens VR settings");
    front.Input(0,false,false);front.Input(0,false,true);
    Check(front.screen==FrontScreen::Pause&&front.selection==4,"options return preserves pause selection");
    const auto directory=std::filesystem::temp_directory_path()/("hpvr-report-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ProgressSave save,read;save.earned_cards=7;save.completed_maps=7;save.house_points={10,20,30,90};
    save.lesson_best={100,0,95};save.lesson_points={50,0,30};save.charms_state="CHARMS_WORLD 1 blocks";
    Check(WriteProgress(directory,0,&save)&&ReadProgress(directory,0,&read),"V9 save roundtrip");
    Check(read.earned_cards==save.earned_cards&&read.completed_maps==save.completed_maps&&read.house_points==save.house_points&&
          read.lesson_best==save.lesson_best&&read.lesson_points==save.lesson_points&&read.charms_state==save.charms_state,"V9 campaign and charms fields durable");
    save.earned_cards=0x2000000;Check(!WriteProgress(directory,0,&save),"unknown card bit rejected");save.earned_cards=7;
    save.lesson_best[0]=101;Check(!WriteProgress(directory,0,&save),"invalid lesson score rejected");save.lesson_best[0]=100;
    save.charms_state.assign(4097,'x');Check(!WriteProgress(directory,0,&save),"oversized charms state rejected");
    std::filesystem::remove_all(directory);
    if(argc==2){
        FrontAssets owned;
        Check(LoadFrontAssets(argv[1],&owned,3),"owned map3 frontend loads");
        const auto actors=hpvr::wand::inspect_hp1_actor_visuals(std::filesystem::path(argv[1])/"Maps/Lev_Tut3.unr");
        const auto lessons=charms::LoadLessons(argv[1],actors);unsigned count=0;
        for(const auto& lesson:lessons){
            Check(lesson.valid,"owned charms lesson metadata loads");
            Check(!charms::LessonLine(lesson,"LessonIntro").empty(),"owned lesson starts with an authored intro");
            for(unsigned round=0;round<4;++round)for(float score:{0.0F,.5F,.95F,1.0F}){
                const bool passed=score>=lesson.pass_marks[round];
                const auto feedback=charms::LessonFeedback(lesson,round,round+unsigned(passed),
                    passed?round==3:round>0,score,round);
                Check(!feedback.empty(),"every owned score band has spoken feedback");
                for(const auto& line:feedback)Check(std::ranges::any_of(owned.gameplay_audio,[&](const auto& voice){
                    return charms::detail::Fold(voice.object_name)==charms::detail::Fold(line);
                }),"every queued original lesson response has prepared audio");
            }
            for(const auto& property:lesson.properties){
                if(!property.text_value_serialized||!property.name.starts_with("Lesson")||property.text_value.empty())continue;
                const auto found=std::ranges::find_if(owned.gameplay_audio,[&](const auto& voice){
                    return charms::detail::Fold(voice.object_name)==charms::detail::Fold(property.text_value);
                });
                Check(found!=owned.gameplay_audio.end()&&!found->encoded_bytes.empty(),"every original lesson line reaches the frontend audio plan");++count;
            }
        }
        Check(count>=40,"both complete original lesson dialogue sets present");
        for(const auto* name:{"METAL_CHEST_OPEN_2","METAL_CHEST_OPEN_4","WOOD_CHEST_OPEN_1","WOOD_CHEST_OPEN_2","chest_landing"}){
            const auto found=std::ranges::find_if(owned.gameplay_audio,[&](const auto& sound){
                return charms::detail::Fold(sound.object_name)==charms::detail::Fold(name);
            });
            Check(found!=owned.gameplay_audio.end()&&!found->encoded_bytes.empty(),"owned chest effect reaches the frontend audio plan");
        }
        std::cout<<"OWNED_CHARMS_MENU_AUDIO=PASS lesson_lines="<<count<<" voices="<<owned.gameplay_audio.size()<<" textures="<<owned.textures.size()<<'\n';
    }
    std::cout<<"REPORT_MENU_TESTS=PASS original_vial_layout=4 card_slots=25 journal=9\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
