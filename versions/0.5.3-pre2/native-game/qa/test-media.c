#include "render.h"
#include "audio.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
int main(void){
 uint32_t pixels[16];SummerCanvas canvas={pixels,4,4,4};
 summer_fill(&canvas,0x101010);assert(pixels[15]==0x101010);
 summer_rect(&canvas,1,1,2,2,0xff0000);assert(pixels[5]==0xff0000);
 uint32_t art[1]={0x800000ff};SummerImage image={0,0,1,1,art,1};
 summer_blit(&canvas,&image,1,1,255);
 assert((pixels[5]&0xff)>0&&(pixels[5]>>16)>0);
 SummerMotion m={.scene_fade=1,.actor_fade=0,.actor_offset=20,.reduced_motion=0};
 summer_motion_step(&m,0.2f);assert(m.scene_fade<1&&m.actor_fade>0);
 m.reduced_motion=1;summer_motion_step(&m,0.01f);
 assert(m.scene_fade==0&&m.actor_fade==1&&m.actor_offset==0);
 SummerAudio a;summer_audio_init(&a,48000,.24f);
 int16_t *pcm=calloc(48000,sizeof(int16_t));assert(pcm);
 summer_audio_render(&a,pcm,48000);
 for(int i=0;i<48000;i++)assert(pcm[i]==0);
 summer_audio_init(&a,48000,.24f);summer_audio_toggle(&a,1);
 summer_audio_render(&a,pcm,48000);
 int nonzero=0;for(int i=0;i<48000;i++)if(pcm[i])nonzero++;
 assert(nonzero>1000);
 free(pcm);puts("PASS: native framebuffer, transparency, animation, synthesized audio");
 return 0;
}
