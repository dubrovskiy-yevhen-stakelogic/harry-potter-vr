#include "hpvr/quest_dialogue.h"
#include <iostream>
#include <stdexcept>
using namespace hpvr::quest;
int main(){try{
    unsigned checks=0;
    const auto check=[&](bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);};
    const std::map<std::string,std::string> lines{{"line_1","Don't stop.  Keep going!  "},{"line_2","Hello there."}};
    check(dialogue::Resolve(" LINE_2 ",lines)=="line_2","named dialogue remains stable");
    check(dialogue::Resolve(" (point) <LINE_2>",lines)=="line_2","emote speech uses bracketed ID");
    check(dialogue::Resolve("Don\xe2\x80\x99t stop. Keep going!",lines)=="line_1","Unicode apostrophe and spacing resolve authored literal");
    check(dialogue::Resolve("Hello elsewhere.",lines).empty(),"unknown prose cannot become an audio name");
    check(dialogue::Resolve(" (point) <bad name>",lines).empty(),"malformed bracketed ID rejected");
    auto ambiguous=lines;ambiguous.emplace("other","Hello there.");
    check(dialogue::Resolve("Hello there.",ambiguous).empty(),"ambiguous subtitle cannot select arbitrary voice");
    check(dialogue::SpeechCommand("talk6")&&dialogue::SpeechCommand("emote")&&!dialogue::SpeechCommand("talking")&&
        !dialogue::SpeechCommand("talk7"),"only supported speech commands accepted");
    check(dialogue::Argument("\t Hello  there. \r\n")=="hello there.","normalization trims and folds whitespace");
    std::cout<<"DIALOGUE_TESTS=PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
