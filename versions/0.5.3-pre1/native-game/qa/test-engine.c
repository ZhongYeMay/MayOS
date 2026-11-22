#include "engine.h"
#include "story_data.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void branch(const char *id,unsigned option,const char *expected){
 SummerGame *g=calloc(1,sizeof(*g));SummerSave *save=calloc(1,sizeof(*save));
 assert(g&&save);
 assert(summer_init(g,summer_original_story,summer_original_story_count)==0);
 assert(summer_go(g,id,1)==0);
 assert(summer_choose(g,option)==0);
 assert(strcmp(summer_current(g)->id,expected)==0);
 assert(summer_save(g,save)==0);
 assert(summer_go(g,"p0",1)==0);
 assert(summer_load(g,save)==0);
 assert(strcmp(summer_current(g)->id,expected)==0);
 save->checksum^=1;assert(summer_load(g,save)==-2);
 free(save);free(g);
}
int main(void){
 SummerGame *g=calloc(1,sizeof(*g));assert(g);
 assert(summer_init(g,summer_original_story,summer_original_story_count)==0);
 assert(strcmp(summer_current(g)->id,"p0")==0);
 assert(summer_next(g)==0);
 assert(strcmp(summer_current(g)->id,"p1")==0);
 branch("routeChoice",0,"m0");
 branch("routeChoice",1,"n0");
 branch("routeChoice",2,"s0");
 branch("mChoice",0,"mGood0");
 branch("mChoice",1,"mSoft0");
 branch("nChoice",0,"nGood0");
 branch("nChoice",1,"nSoft0");
 branch("sChoice",0,"sGood0");
 branch("sChoice",1,"sSoft0");
 assert(summer_go(g,"mIntimate",1)==0);
 g->mature=0;assert(summer_next(g)==0);
 assert(strcmp(summer_current(g)->id,"endM1")==0);
 assert(summer_go(g,"mIntimate",1)==0);
 g->mature=1;assert(summer_next(g)==0);
 assert(strcmp(summer_current(g)->id,"mAfter0")==0);
 puts("PASS native story branching, save/load, corruption detection, optional routes");
 free(g);return 0;
}
