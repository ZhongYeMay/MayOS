#ifndef SUMMER_RENDER_H
#define SUMMER_RENDER_H
#include <stdint.h>
typedef struct {uint32_t *pixels; int width,height,stride;} SummerCanvas;
typedef struct {int x,y,w,h;const uint32_t *pixels;int stride;} SummerImage;
typedef struct {float scene_fade,actor_fade,actor_offset;int reduced_motion;} SummerMotion;
void summer_fill(SummerCanvas *dst,uint32_t rgb);
void summer_rect(SummerCanvas *dst,int x,int y,int w,int h,uint32_t rgb);
void summer_blit(SummerCanvas *dst,const SummerImage *src,int x,int y,unsigned opacity);
void summer_composite_scene(SummerCanvas *dst,const SummerImage *background,const SummerImage *actor,const SummerMotion *motion);
void summer_motion_step(SummerMotion *motion,float delta_seconds);
#endif
