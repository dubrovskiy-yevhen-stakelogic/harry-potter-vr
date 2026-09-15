#pragma once

#include "hpvr/quest_charms_lesson.h"

namespace hpvr::quest::charms {

inline std::string LessonLine(const LessonMetadata& lesson,std::string_view name,unsigned variation=0){
    std::vector<std::string> choices;
    for(unsigned index=0;index<10;++index){
        const auto* property=detail::Property(lesson.properties,name,index);
        if(!property||!property->text_value_serialized||property->text_value.empty())break;
        choices.push_back(property->text_value);
    }
    return choices.empty()?std::string{}:choices[variation%choices.size()];
}

inline std::vector<std::string> LessonFeedback(const LessonMetadata& lesson,unsigned previous,
    unsigned round,bool finished,float score,unsigned variation){
    std::vector<std::string> result;
    const auto append=[&](std::string line){if(!line.empty())result.push_back(std::move(line));};
    const auto threshold=[&](const char* name,float fallback){
        return detail::Property(lesson.properties,name)?detail::Number(lesson.properties,name):fallback;
    };
    if(round>previous){
        append(LessonLine(lesson,score>=1?"LessonFullMarks":score>threshold("fVGoodThreshold",1)?
            "LessonJudgeVGood":"LessonJudgeSuccess",variation));
        if(const auto* points=detail::Property(lesson.properties,"LessonPoints",previous);
           points&&points->text_value_serialized)append(points->text_value);
        if(!finished)append(LessonLine(lesson,"LessonRepeatSuccess",variation));
    }else{
        append(LessonLine(lesson,score>threshold("fVBadThreshold",0)?"LessonJudgeFailure":"LessonJudgeVBad",variation));
        if(!finished)append(LessonLine(lesson,round?"LessonRepeatSuccess":"LessonRepeatFailure",variation));
    }
    return result;
}

struct LessonSpeechQueue {
    struct Action {std::string line;bool stop=false,timed_out=false;};
    std::vector<std::string> lines;
    std::size_t next=0;
    float delay=0,waiting=0;
    bool playing=false;
    void Reset(std::vector<std::string> value){*this={};lines=std::move(value);delay=.25F;}
    [[nodiscard]] bool Pending()const{return playing||delay>0||next<lines.size();}
    void Started(float seconds){playing=true;delay=std::clamp(seconds,.05F,30.0F)+.3F;}
    Action Advance(float seconds,bool blocked,bool busy){
        if(blocked)return {};
        const float step=std::isfinite(seconds)?std::clamp(seconds,0.0F,.1F):0;
        delay=std::max(0.0F,delay-step);
        if(delay>0)return {};
        if(playing){playing=false;if(busy)return {{},true,false};}
        if(next==lines.size())return {};
        if(busy){
            waiting+=step;
            if(waiting>=30){next=lines.size();return {{},false,true};}
            return {};
        }
        waiting=0;return {lines[next++],false,false};
    }
};

} // namespace hpvr::quest::charms
