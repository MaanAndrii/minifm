#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    float x,y,z,bx,by,bz;bool initialized,armed;
    int rotation,candidate,moving_samples;
    int64_t candidate_at,quiet_at,cooldown;
} motion_filter_t;
static inline float motion_abs(float v){return v<0?-v:v;}
static inline bool motion_feed(motion_filter_t *f,float x,float y,float z,int64_t now)
{
    if(!f->initialized){f->x=f->bx=x;f->y=f->by=y;f->z=f->bz=z;f->initialized=true;f->candidate=-1;f->quiet_at=now;}
    float delta=motion_abs(x-f->x)+motion_abs(y-f->y)+motion_abs(z-f->z);
    f->x+=.25f*(x-f->x);f->y+=.25f*(y-f->y);f->z+=.25f*(z-f->z);
    int candidate=-1;float ax=motion_abs(f->x),ay=motion_abs(f->y);
    float norm=f->x*f->x+f->y*f->y+f->z*f->z;
    // Ignore a flat face, diagonal poses and acceleration unrelated to gravity.
    if(norm>.65f && norm<1.4f && motion_abs(f->z)<.82f) {
        if(ay>.65f && ay>ax*1.35f)candidate=f->y<0?0:2;
        else if(ax>.65f && ax>ay*1.35f)candidate=f->x>0?1:3;
    }
    if(candidate!=f->candidate){f->candidate=candidate;f->candidate_at=now;}
    if(candidate>=0 && now-f->candidate_at>=500000)f->rotation=candidate;
    if(delta<.10f) {
        if(!f->armed && now-f->quiet_at>=1500000 && now>=f->cooldown) {
            f->armed=true;f->bx=f->x;f->by=f->y;f->bz=f->z;
        }
    } else f->quiet_at=now;
    float displacement=motion_abs(f->x-f->bx)+motion_abs(f->y-f->by)+motion_abs(f->z-f->bz);
    bool moved=displacement>.65f || motion_abs(norm-1.f)>.35f;
    if(f->armed && moved && norm>.5f && norm<1.8f)f->moving_samples++;
    else f->moving_samples=0;
    if(f->moving_samples>=3) {
        f->armed=false;f->moving_samples=0;f->cooldown=now+3000000;f->quiet_at=now;return true;
    }
    return false;
}
