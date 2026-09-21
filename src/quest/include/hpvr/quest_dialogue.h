#pragma once
#include <algorithm>
#include <map>
#include <string>
#include <string_view>

namespace hpvr::quest::dialogue {
inline std::string Normalize(std::string_view text){
    std::string result;
    bool space=false;
    for(std::size_t i=0;i<text.size();++i){
        unsigned char c=static_cast<unsigned char>(text[i]);
        if(c==0xe2&&i+2<text.size()&&static_cast<unsigned char>(text[i+1])==0x80&&
           (static_cast<unsigned char>(text[i+2])==0x98||static_cast<unsigned char>(text[i+2])==0x99)){
            c='\'';i+=2;
        }
        if(c==' '||c=='\t'||c=='\r'||c=='\n'){space=!result.empty();continue;}
        if(space){result+=' ';space=false;}
        result+=static_cast<char>(c>='A'&&c<='Z'?c+32:c);
    }
    return result;
}
inline bool Identifier(std::string_view text){
    return !text.empty()&&std::ranges::all_of(text,[](unsigned char c){
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';
    });
}
inline std::string Argument(std::string_view text){
    const auto first=text.find('<'),last=text.find('>');
    if(first!=std::string_view::npos&&last!=std::string_view::npos&&last>first+1){
        const auto name=Normalize(text.substr(first+1,last-first-1));
        return Identifier(name)?name:std::string{};
    }
    return Normalize(text);
}
inline std::string Resolve(std::string_view argument,const std::map<std::string,std::string>& subtitles){
    auto normalized=Argument(argument);
    if(Identifier(normalized))return normalized;
    if(normalized.empty())return {};
    std::string result;
    for(const auto& [key,text]:subtitles)if(Normalize(text)==normalized){
        if(!result.empty()&&result!=key)return {};
        result=key;
    }
    return result;
}
inline bool SpeechCommand(std::string_view op){
    return op=="talk"||op=="say"||op=="emote"||
        (op.size()==5&&op.starts_with("talk")&&op[4]>='0'&&op[4]<='6');
}
} // namespace hpvr::quest::dialogue
