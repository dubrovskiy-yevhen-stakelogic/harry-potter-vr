#pragma once
#include <cmath>
#include "hpvr/quest_view.h"

namespace hpvr::quest {
// A world-space window may ride a SCRIPTED rig, never the tracked head.
// Looking/leaning therefore stays 6DOF while a moving shot cannot abandon UI.
struct CinematicPanelAnchor {
    bool valid=false,cinematic=false,first_person=false;
    Matrix4 relative{};
    bool Update(const ViewPose& rendered_head,const ViewPose* rig,bool harry,
                bool recapture,float scale,Matrix4* output){
        if(!output||!std::isfinite(scale)||scale<=0)return false;
        Matrix4 rig_matrix{};
        if(rig&&!BuildRigidTransform(*rig,&rig_matrix))return false;
        if(recapture||!valid||cinematic!=(rig!=nullptr)||first_person!=harry){
            ViewPose theater;Matrix4 panel{};
            if(!BuildTheaterPose(rendered_head,&theater)||!BuildRigidTransform(theater,&panel))return false;
            for(unsigned i=0;i<12;++i)panel[i]*=scale;
            relative=panel;
            if(rig){
                Matrix4 inverse{};inverse[15]=1;
                for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)inverse[c*4+r]=rig_matrix[r*4+c];
                for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)inverse[12+r]-=inverse[c*4+r]*rig_matrix[12+c];
                relative=MultiplyMatrices(inverse,panel);
            }
            valid=true;cinematic=rig!=nullptr;first_person=harry;
        }
        *output=rig?MultiplyMatrices(rig_matrix,relative):relative;
        return true;
    }
};
}
