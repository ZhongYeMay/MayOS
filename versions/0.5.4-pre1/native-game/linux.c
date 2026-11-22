/* Native Linux integration harness, not a certified complete game.
 * Draws original artwork using SDL2 and libwebp, with no browser runtime.
 */
#include "engine.h"
#include "render.h"
#include "webp.h"
#include "audio.h"
#include "storage.h"
#include "story_data.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static SummerAudio music;
static void callback(void *userdata,Uint8 *stream,int bytes){
 (void)userdata;
 summer_audio_render(&music,(int16_t *)stream,(unsigned)(bytes/2));
}
static int load(const char *root,const char *name,SummerImage *out){
 char filename[2048];
 int n=snprintf(filename,sizeof(filename),"%s/assets/retro/%s.webp",root,name);
 if(n<0||(size_t)n>=sizeof(filename))return -1;
 return summer_image_load_webp(filename,out);
}
int main(int argc,char **argv){
 if(argc<2){fprintf(stderr,"Usage: %s <original-site-directory>\n",argv[0]);return 2;}
 if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS)){fprintf(stderr,"%s\n",SDL_GetError());return 3;}
 SDL_Window *window=SDL_CreateWindow("夏日未寄 / Native Linux integration test",
 SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_SHOWN);
 SDL_Renderer *renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC):NULL;
 if(!renderer){fprintf(stderr,"SDL renderer unavailable: %s\n",SDL_GetError());SDL_Quit();return 4;}
 SummerGame *game=calloc(1,sizeof(*game));if(!game){SDL_Quit();return 5;}
 if(summer_init(game,summer_original_story,summer_original_story_count)){free(game);SDL_Quit();return 5;}
 SummerImage bg={0},actor={0};char previous_bg[80]="",previous_actor[80]="";
 summer_audio_init(&music,48000,.24f);
 SDL_AudioSpec wanted={0};wanted.freq=48000;wanted.format=AUDIO_S16SYS;
 wanted.channels=1;wanted.samples=1024;wanted.callback=callback;
 SDL_AudioDeviceID audio=SDL_OpenAudioDevice(NULL,0,&wanted,NULL,0);
 if(audio)SDL_PauseAudioDevice(audio,0);
 int running=1;unsigned choice=0;
 while(running){
  SDL_Event event;
  while(SDL_PollEvent(&event)){
   if(event.type==SDL_QUIT)running=0;
   if(event.type==SDL_KEYDOWN){
    switch(event.key.keysym.sym){
     case SDLK_ESCAPE:running=0;break;
     case SDLK_RETURN:case SDLK_SPACE:summer_next(game);break;
     case SDLK_1:case SDLK_2:case SDLK_3:
      choice=(unsigned)(event.key.keysym.sym-SDLK_1);summer_choose(game,choice);break;
     case SDLK_m:
      if(audio){SDL_LockAudioDevice(audio);summer_audio_toggle(&music,!music.enabled);SDL_UnlockAudioDevice(audio);}
      break;
     case SDLK_s:summer_save_file(game,"summer-linux-test.sav");break;
     case SDLK_l:summer_load_file(game,"summer-linux-test.sav");break;
    }
   }
  }
  const SummerNode *node=summer_current(game);if(!node)break;
  const char *background=node->bg&&*node->bg?node->bg:"studio";
  if(strcmp(previous_bg,background)){
   summer_image_free(&bg);
   if(load(argv[1],background,&bg))fprintf(stderr,"Missing original background %s\n",background);
   snprintf(previous_bg,sizeof(previous_bg),"%s",background);
  }
  char actor_name[80];snprintf(actor_name,sizeof(actor_name),"%s-%u",
   node->route&&*node->route?node->route:"mio",node->pose);
  if(strcmp(previous_actor,actor_name)){
   summer_image_free(&actor);
   if(load(argv[1],actor_name,&actor))fprintf(stderr,"Missing original sprite %s\n",actor_name);
   snprintf(previous_actor,sizeof(previous_actor),"%s",actor_name);
  }
  SDL_SetRenderDrawColor(renderer,16,18,28,255);SDL_RenderClear(renderer);
  if(bg.pixels){
   SDL_Texture *t=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,bg.w,bg.h);
   if(t){SDL_UpdateTexture(t,NULL,bg.pixels,bg.w*4);SDL_Rect dst={0,0,1280,720};SDL_RenderCopy(renderer,t,NULL,&dst);SDL_DestroyTexture(t);}
  }
  if(actor.pixels){
   SDL_Texture *t=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,actor.w,actor.h);
   if(t){SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);SDL_UpdateTexture(t,NULL,actor.pixels,actor.w*4);
    SDL_Rect dst={400,30,actor.w*600/actor.h,600};SDL_RenderCopy(renderer,t,NULL,&dst);SDL_DestroyTexture(t);}
  }
  /* Dialogue text and original controls require full native UI implementation.
   * Keep this integration harness out of official game releases. */
  SDL_RenderPresent(renderer);
 }
 if(audio)SDL_CloseAudioDevice(audio);
 summer_image_free(&bg);summer_image_free(&actor);free(game);
 SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 0;
}
