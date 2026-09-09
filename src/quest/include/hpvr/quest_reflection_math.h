#pragma once
#include "hpvr/quest_view.h"
#include <algorithm>
#include <cmath>
#include <string_view>
namespace hpvr::quest {
inline unsigned ReflectionExtent(unsigned extent,unsigned divisor){
    return std::max(1U,(extent+divisor-1)/divisor);
}
inline bool DelayReflectionOverlay(bool history_enabled,unsigned eye){return history_enabled&&eye==0;}
inline float ReflectionCameraDepth(const Matrix4& inverse,float x,float y,float depth){
    const float reciprocal=inverse[3]*x+inverse[7]*y+inverse[11]*depth+inverse[15];
    return reciprocal>0?1/reciprocal:-1;
}
inline bool InvertReflectionMatrix(const Matrix4& m,Matrix4& inverse){
    double a[4][8]{};
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){a[r][c]=m[c*4+r];a[r][c+4]=r==c?1:0;}
    for(int c=0;c<4;++c){
        int pivot=c;for(int r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;
        if(!std::isfinite(a[pivot][c])||std::abs(a[pivot][c])<1e-12)return false;
        for(int k=0;k<8;++k)std::swap(a[c][k],a[pivot][k]);
        const double d=a[c][c];for(double& v:a[c])v/=d;
        for(int r=0;r<4;++r)if(r!=c){const double f=a[r][c];for(int k=0;k<8;++k)a[r][k]-=a[c][k]*f;}
    }
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){inverse[c*4+r]=static_cast<float>(a[r][c+4]);if(!std::isfinite(inverse[c*4+r]))return false;}
    return true;
}
inline bool IsReflectiveWoodFloor(std::string_view name,float vertical_normal){
    // Textured BSP normals have ALREADY been converted from UE Z-up to Y-up.
    return std::abs(vertical_normal)>.98F&&(name=="woodredfloor_3"||name=="3rdwood1_b"||name=="woodflr_01_b");
}
}
