#include "audio.h"
#include <math.h>
#include <stddef.h>
/* Original game's oscillator notes and 950ms timing, with native PCM output. */
static const double melody[]={293.66,369.99,440,587.33,554.37,440,369.99,329.63,
 293.66,349.23,440,523.25,493.88,440,349.23,329.63};
#define PI 3.14159265358979323846
void summer_audio_init(SummerAudio *a,unsigned rate,float volume){
 if(!a)return;
 a->sample_rate=rate?rate:48000;a->time=0;a->next_note=0;a->note=0;
 a->volume=volume<0?0:volume>1?1:volume;a->enabled=0;
}
void summer_audio_toggle(SummerAudio *a,int enabled){if(a)a->enabled=!!enabled;}
void summer_audio_render(SummerAudio *a,int16_t *pcm,unsigned frames){
 if(!a||!pcm||!a->sample_rate)return;
 for(unsigned i=0;i<frames;i++){
  double t=a->time, sample=0;
  if(a->enabled){
   /* Web Audio version overlaps each 4s note and a low accompaniment. */
   unsigned count=sizeof(melody)/sizeof(melody[0]);
   for(unsigned k=0;k<5;k++){
    if(a->note<=k)break;
    unsigned idx=a->note-1-k;
    double start=idx*0.95,age=t-start;
    if(age<0||age>=4)continue;
    double env=age<0.025?age/0.025:exp(-age*2.2);
    double main_note=sin(2*PI*melody[idx%count]*age);
    double bass=sin(2*PI*146.83*age);
    sample+=(main_note*0.13+bass*0.025)*env*a->volume;
   }
  }
  if(sample>1)sample=1;
  if(sample< -1)sample=-1;
  pcm[i]=(int16_t)(sample*32767.0);
  a->time+=1.0/a->sample_rate;
  if(a->time>=a->next_note){a->note++;a->next_note+=0.95;}
 }
}
