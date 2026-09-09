#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
namespace hpvr::quest {
struct PerformanceSnapshot {
    float fps=-1,frame_ms=-1,cpu_ms=-1,gpu_ms=-1,copy_ms=-1;
    std::array<float,2> cpu_eye{-1,-1},gpu_eye{-1,-1};
    float cpu_util=-1,gpu_util=-1,app_cpu_ms=-1,app_gpu_ms=-1;
    unsigned width=0,height=0;int scale=100;
};
inline double TimestampMilliseconds(std::uint64_t a,std::uint64_t b,unsigned bits,double period){
    if(!bits||bits>64||period<=0)return -1;
    const auto mask=bits==64?~std::uint64_t(0):(std::uint64_t(1)<<bits)-1;
    return double((b-a)&mask)*period*1e-6;
}
inline std::vector<std::string> PerformanceLines(const PerformanceSnapshot& p){
    const auto number=[](float n,const char* unit){char b[40];if(!std::isfinite(n)||n<0)return std::string("N/A");
        std::snprintf(b,sizeof(b),"%.1f%s",double(n),unit);return std::string(b);};
    return {"FPS "+number(p.fps,"")+"   FRAME "+number(p.frame_ms," MS"),
        "DEVICE CPU "+number(p.cpu_util," %")+"   GPU "+number(p.gpu_util," %"),
        "APP (META) CPU "+number(p.app_cpu_ms," MS")+" GPU "+number(p.app_gpu_ms," MS"),
        "FRAME CPU WORK "+number(p.cpu_ms," MS")+" GPU "+number(p.gpu_ms," MS"),
        "LEFT  CPU "+number(p.cpu_eye[0]," MS")+"  GPU "+number(p.gpu_eye[0]," MS"),
        "RIGHT CPU "+number(p.cpu_eye[1]," MS")+"  GPU "+number(p.gpu_eye[1]," MS"),
        "SSR HISTORY COPY GPU "+number(p.copy_ms," MS"),
        "SCALE "+std::to_string(p.scale)+"%   "+std::to_string(p.width)+" X "+std::to_string(p.height)+" / EYE",
        "CPU EYES = COMMAND RECORDING; N/A = UNAVAILABLE",
        "GPU EYES = TIMESTAMPS, NOT DEVICE LOAD"};
}
}
