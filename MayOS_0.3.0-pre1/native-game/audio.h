#ifndef SUMMER_AUDIO_H
#define SUMMER_AUDIO_H
#include <stdint.h>
typedef struct {unsigned sample_rate;double time;double next_note;unsigned note;float volume;int enabled;} SummerAudio;
void summer_audio_init(SummerAudio *a,unsigned rate,float volume);
void summer_audio_toggle(SummerAudio *a,int enabled);
void summer_audio_render(SummerAudio *a,int16_t *pcm,unsigned frames);
#endif
