#include "engine.h"
#include <string.h>
static uint32_t checksum(const unsigned char *p, size_t n) {
 uint32_t h=2166136261u;
 for(size_t i=0;i<n;i++) h=(h^p[i])*16777619u;
 return h;
}
static int find(const SummerGame *g,const char *id){
 if(!id||!*id)return -1;
 for(size_t i=0;i<g->count;i++)if(!strcmp(g->nodes[i].id,id))return (int)i;
 return -1;
}
const SummerNode *summer_current(const SummerGame *g){
 return g&&g->index<g->count?&g->nodes[g->index]:0;
}
int summer_go(SummerGame *g,const char *id,int record){
 if(!g)return -1;
 int n=find(g,id);if(n<0)return -1;
 g->index=(size_t)n;
 if(record){
  if(g->history_count>=SUMMER_HISTORY_MAX)return -2;
  g->history[g->history_count++]=(size_t)n;
  if((size_t)n<sizeof(g->seen))g->seen[n]=1;
 }
 return 0;
}
int summer_init(SummerGame *g,const SummerNode *nodes,size_t count){
 if(!g||!nodes||!count||count>4096)return -1;
 memset(g,0,sizeof(*g));g->nodes=nodes;g->count=count;
 return summer_go(g,"p0",1);
}
int summer_next(SummerGame *g){
 const SummerNode *n=summer_current(g);
 if(!n||n->choice_count||n->ending)return -1;
 const char *next=(g->mature&&n->mature_next&&*n->mature_next)?n->mature_next:n->next;
 return summer_go(g,next,1);
}
int summer_choose(SummerGame *g,unsigned choice){
 const SummerNode *n=summer_current(g);
 if(!n||choice>=n->choice_count||choice>=3)return -1;
 return summer_go(g,n->targets[choice],1);
}
int summer_save(const SummerGame *g,SummerSave *out){
 if(!g||!out||g->index>=g->count||g->history_count>SUMMER_HISTORY_MAX)return -1;
 memset(out,0,sizeof(*out));out->version=SUMMER_SAVE_VERSION;
 out->node_index=(uint32_t)g->index;out->history_count=(uint32_t)g->history_count;
 out->flags=(g->mature?1:0)|(g->auto_play?2:0)|(g->reduced_motion?4:0);
 for(size_t i=0;i<g->history_count;i++)out->history[i]=(uint32_t)g->history[i];
 memcpy(out->seen,g->seen,sizeof(out->seen));
 memcpy(out->unlocked,g->unlocked,sizeof(out->unlocked));
 out->checksum=checksum((const unsigned char *)out,offsetof(SummerSave,checksum));
 return 0;
}
int summer_load(SummerGame *g,const SummerSave *in){
 if(!g||!in||in->version!=SUMMER_SAVE_VERSION||
 in->node_index>=g->count||in->history_count>SUMMER_HISTORY_MAX)return -1;
 if(checksum((const unsigned char *)in,offsetof(SummerSave,checksum))!=in->checksum)return -2;
 for(size_t i=0;i<in->history_count;i++)if(in->history[i]>=g->count)return -3;
 g->index=in->node_index;g->history_count=in->history_count;
 g->mature=(in->flags&1)!=0;g->auto_play=(in->flags&2)!=0;
 g->reduced_motion=(in->flags&4)!=0;
 for(size_t i=0;i<g->history_count;i++)g->history[i]=in->history[i];
 memcpy(g->seen,in->seen,sizeof(g->seen));
 memcpy(g->unlocked,in->unlocked,sizeof(g->unlocked));
 return 0;
}
