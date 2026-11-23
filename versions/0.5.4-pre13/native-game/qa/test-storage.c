#include "storage.h"
#include "story_data.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void){
 SummerGame *g=calloc(1,sizeof(*g));assert(g);
 assert(summer_init(g,summer_original_story,summer_original_story_count)==0);
 assert(summer_go(g,"nChoice",1)==0);
 assert(summer_choose(g,1)==0);
 char file[256];snprintf(file,sizeof(file),"/tmp/summer-save-%ld.dat",(long)getpid());
 assert(summer_save_file(g,file)==0);
 assert(summer_go(g,"p0",1)==0);
 assert(summer_load_file(g,file)==0);
 assert(strcmp(summer_current(g)->id,"nSoft0")==0);
 FILE *f=fopen(file,"r+b");assert(f);
 assert(fseek(f,100,SEEK_SET)==0);assert(fputc(0xff,f)!=EOF);fclose(f);
 assert(summer_load_file(g,file)!=0);
 unlink(file);free(g);
 puts("PASS atomic native save file, restore and tamper detection");
 return 0;
}
