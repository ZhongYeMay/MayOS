#ifndef MAYOS_NATIVE_GALGAME_H
#define MAYOS_NATIVE_GALGAME_H
#include "core.h"

/* Independently implemented MayKernel game application.
   No SDL, Linux ABI, webview or compatibility code. */
typedef void (*MayGameFill)(int,int,int,int,u32);
typedef int (*MayGameText)(int,int,const char*,u32);
typedef void (*MayGameBlit)(int,int,int,int,const u8*,int,int);
void maygame_open(void);
int maygame_active(void);
void maygame_key(int key);
void maygame_click(int x,int y);
void maygame_draw(MayGameFill fill,MayGameText text,MayGameBlit blit);
#endif
