#include "render.h"
#include <stddef.h>
static int clamp(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
void summer_fill(SummerCanvas *d,uint32_t rgb){
 if(!d||!d->pixels||d->width<=0||d->height<=0||d->stride<d->width)return;
 for(int y=0;y<d->height;y++)for(int x=0;x<d->width;x++)d->pixels[(size_t)y*d->stride+x]=rgb&0xffffffu;
}
void summer_rect(SummerCanvas *d,int x,int y,int w,int h,uint32_t rgb){
 if(!d||!d->pixels||w<=0||h<=0||d->stride<d->width)return;
 int x0=clamp(x,0,d->width),y0=clamp(y,0,d->height);
 long long xx=(long long)x+w;int x1=clamp(xx>2147483647LL?2147483647:xx< -2147483647LL?-2147483647:(int)xx,0,d->width);
 long long yy=(long long)y+h;int y1=clamp(yy>2147483647LL?2147483647:yy< -2147483647LL?-2147483647:(int)yy,0,d->height);
 for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++)d->pixels[(size_t)yy*d->stride+xx]=rgb&0xffffffu;
}
void summer_blit(SummerCanvas *d,const SummerImage *s,int x,int y,unsigned opacity){
 if(!d||!s||!d->pixels||!s->pixels||d->stride<d->width||s->stride<s->w||opacity==0)return;
 if(d->width<=0||d->height<=0||s->w<=0||s->h<=0)return;
 if(opacity>255)opacity=255;
 for(int sy=0;sy<s->h;sy++){
  long long dy=(long long)y+sy;if(dy<0||dy>=d->height)continue;
  for(int sx=0;sx<s->w;sx++){
   long long dx=(long long)x+sx;if(dx<0||dx>=d->width)continue;
   uint32_t v=s->pixels[(size_t)sy*s->stride+sx];
   unsigned a=((v>>24)&255u)*opacity/255u;
   if(!a)continue;
   uint32_t *out=&d->pixels[(size_t)dy*d->stride+(size_t)dx];
   uint32_t old=*out, rgb=0;
   for(int shift=0;shift<=16;shift+=8){
    unsigned sc=(v>>shift)&255u,dc=(old>>shift)&255u;
    rgb|=((sc*a+dc*(255-a)+127)/255)<<shift;
   }
   *out=rgb;
  }
 }
}
void summer_composite_scene(SummerCanvas *d,const SummerImage *bg,const SummerImage *actor,const SummerMotion *m){
 if(!d||!m)return;
 summer_fill(d,0x161729);
 if(bg)summer_blit(d,bg,0,0,(unsigned)(255*(1.f-m->scene_fade)));
 if(actor)summer_blit(d,actor,(d->width-actor->w)/2,(d->height-actor->h)/2+(int)m->actor_offset,
                      (unsigned)(255*m->actor_fade));
}
void summer_motion_step(SummerMotion *m,float dt){
 if(!m||dt<=0)return;
 if(m->reduced_motion){m->scene_fade=0;m->actor_fade=1;m->actor_offset=0;return;}
 m->scene_fade-=dt/0.45f;if(m->scene_fade<0)m->scene_fade=0;
 m->actor_fade+=dt/0.32f;if(m->actor_fade>1)m->actor_fade=1;
 if(m->actor_offset>0){m->actor_offset-=dt*60;if(m->actor_offset<0)m->actor_offset=0;}
 if(m->actor_offset<0){m->actor_offset+=dt*60;if(m->actor_offset>0)m->actor_offset=0;}
}
