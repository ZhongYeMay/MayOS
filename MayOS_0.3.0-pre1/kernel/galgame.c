/*
 * MayKernel's own freestanding game engine.
 * No SDL, Linux ABI, WebView, compatibility patches or AI networking.
 * The story and artwork are read-only exports of the original game assets.
 */
#include "galgame.h"
#ifdef MAYGAME_WITH_ART
#include "summer_art.h"
extern const u8 _binary_build_summer_art_bin_start[];
#endif
#ifdef MAYGAME_WITH_STORY
#include "summer_story.h"
#endif

static int active=0,route=0,dirty_draft=0;
static char draft[512],status[128],node_id[32],last_reply[120];
static const char *names[3]={"白石 澪","橘 凪","月島 栞"};
static const char *places[3]={"潮音放送室 / Kamakura","七里滨 / 海边","月灯旧书店 / Hase"};
static const char *route_start[3]={"m0","n0","s0"};

typedef struct {
    char magic[8];
    u32 route;
    char node[32];
    char draft[512];
} MayGameSave;

#ifdef MAYGAME_WITH_STORY
static const MayGameStoryNode *current_node(void) {
    for(u32 i=0;i<MAYGAME_STORY_COUNT;i++)
        if(!strcmp(node_id,maygame_story[i].id))return &maygame_story[i];
    return 0;
}
static int valid_node(const char *id) {
    for(u32 i=0;i<MAYGAME_STORY_COUNT;i++)
        if(!strcmp(id,maygame_story[i].id))return 1;
    return 0;
}
static int contains_word(const char *haystack,const char *needle){
    if(!needle[0])return 1;
    while(*haystack) {
        const char *a=haystack,*b=needle;
        while(*a&&*b&&*a==*b){a++;b++;}
        if(!*b)return 1;
        haystack++;
    }
    return 0;
}
#endif

static void route_set(int index){
    if(index<0||index>=3)return;
    route=index;
    draft[0]=0;last_reply[0]=0;dirty_draft=0;
    copy(node_id,route_start[route],sizeof(node_id));
#ifdef MAYGAME_WITH_STORY
    copy(status,"原版离线剧情已加载；请自由填写回答。",sizeof(status));
#else
    copy(status,"完整原版剧情资源未打包；请使用 SUMMER_SITE 重建。",sizeof(status));
#endif
}
static void save_progress(void){
    int file=fs_find(1,"SummerSave.dat");
    if(file<0)file=fs_create(1,"SummerSave.dat",0);
    if(file<0){copy(status,"存储空间已满；无法保存。",sizeof(status));return;}
    MayGameSave record;
    memset(&record,0,sizeof(record));
    copy(record.magic,"MAYSUM1",sizeof(record.magic));
    record.route=(u32)route;
    copy(record.node,node_id,sizeof(record.node));
    copy(record.draft,draft,sizeof(record.draft));
    memcpy(files[file].data,&record,sizeof(record));
    files[file].size=sizeof(record);
    if(disk_ready){
        if(fs_sync()){
            dirty_draft=0;
            copy(status,"游戏进度已保存至 MayFS。",sizeof(status));
        }else copy(status,"磁盘写入失败，尚未完成持久化。",sizeof(status));
    }else{
        dirty_draft=0;
        copy(status,"游戏进度已保存在本次 RAM 会话。",sizeof(status));
    }
}
static void load_progress(void){
    int file=fs_find(1,"SummerSave.dat");
    if(file<0||files[file].size!=sizeof(MayGameSave))return;
    MayGameSave record;
    memcpy(&record,files[file].data,sizeof(record));
    record.magic[7]=0;
    record.node[sizeof(record.node)-1]=0;
    record.draft[sizeof(record.draft)-1]=0;
    if(strcmp(record.magic,"MAYSUM1")||record.route>2)return;
#ifdef MAYGAME_WITH_STORY
    if(!valid_node(record.node))return;
#endif
    route=(int)record.route;
    copy(node_id,record.node,sizeof(node_id));
    copy(draft,record.draft,sizeof(draft));
    copy(status,"已恢复 MayFS 游戏进度。",sizeof(status));
    dirty_draft=0;
}
void maygame_open(void){
    active=1;route_set(0);load_progress();
    serial("MayKernel: native Summer game launched (offline story engine)\n");
}
int maygame_active(void){return active;}

static void advance_story(void){
#ifdef MAYGAME_WITH_STORY
    const MayGameStoryNode *node=current_node();
    if(!node){copy(status,"剧情节点不存在。",sizeof(status));return;}
    if(node->ending){
        route_set(route);
        copy(status,"已完成结局；从这条角色线路重新开始。",sizeof(status));
        return;
    }
    const char *target=node->next;
    if(node->branch[0][0]){
        int alternative=contains_word(draft,"later")||contains_word(draft,"maybe")||
                        contains_word(draft,"slow")||contains_word(draft,"wait")||
                        contains_word(draft,"下次")||contains_word(draft,"之后")||
                        contains_word(draft,"慢慢");
        target=node->branch[(node->branch[1][0]&&alternative)?1:0];
    }
    if(!target[0]||!valid_node(target)){
        copy(status,"原版剧情没有有效的下一节点。",sizeof(status));return;
    }
    copy(last_reply,draft,sizeof(last_reply));
    copy(node_id,target,sizeof(node_id));
    draft[0]=0;dirty_draft=1;
    copy(status,"已记录玩家文字并推进原版离线剧情。",sizeof(status));
#else
    copy(status,"未导入原版剧情数据，不能继续。",sizeof(status));
#endif
}
void maygame_key(int key){
    if(!active)return;
    if(key==27){active=0;return;}
    if(key==0x101){route_set((route+2)%3);return;}
    if(key==0x102){route_set((route+1)%3);return;}
    if(key==0x106){save_progress();return;}
    int length=(int)strlen(draft);
    if(key=='\b'){
        if(length){draft[length-1]=0;dirty_draft=1;}
        return;
    }
    if(key=='\n'){
        if(length)advance_story();
        else copy(status,"请先填写自己的回复，再按 Enter。",sizeof(status));
        return;
    }
    if(key>=32&&key<127&&length<(int)sizeof(draft)-2){
        draft[length]=(char)key;draft[length+1]=0;dirty_draft=1;
    }
}
void maygame_click(int x,int y){
    if(!active)return;
    if(x>=944&&y<=60){active=0;return;}
    if(y>=118&&y<158){
        for(int i=0;i<3;i++)
            if(x>=80+i*275 && x<300+i*275){route_set(i);return;}
    }
}
static void wrapped(MayGameText text,int x,int y,const char *s,u32 color) {
    /* Freestanding UTF-8 line wrap, respects character boundaries. */
    char line[192];
    int len=0,characters=0,rows=0;
    while(*s&&rows<4){
        unsigned char ch=(u8)*s;
        int width=ch<128?1:(ch<224?2:(ch<240?3:4));
        if(ch=='\n'||characters>=39||len+width>=191){
            line[len]=0;text(x,y+rows*23,line,color);
            len=0;characters=0;rows++;
            if(ch=='\n'){s++;continue;}
            if(rows>=4)break;
        }
        for(int i=0;i<width&&*s;i++)line[len++]=*s++;
        characters++;
    }
    if(len&&rows<4){line[len]=0;text(x,y+rows*23,line,color);}
}
void maygame_draw(MayGameFill fill,MayGameText text,MayGameBlit blit){
#ifndef MAYGAME_WITH_ART
    (void)blit;
#endif
    if(!active)return;
    fill(0,0,W,H,0x15141b);
#ifdef MAYGAME_WITH_ART
    const MayGameArtMeta *background=&maygame_original_art[route];
    int natural_h=(int)(W*background->height/background->width);
    if(natural_h>H)natural_h=H;
    blit(0,(H-natural_h)/2,W,natural_h,
         _binary_build_summer_art_bin_start+background->offset,
         (int)background->width,(int)background->height);
    int pose=0;
#ifdef MAYGAME_WITH_STORY
    const MayGameStoryNode *story=current_node();
    if(story&&story->pose>=0&&story->pose<3)pose=story->pose;
#endif
    const MayGameArtMeta *sprite=&maygame_original_art[3+route*3+pose];
    int portrait_h=400;
    int portrait_w=(int)(portrait_h*sprite->width/sprite->height);
    if(portrait_w>370){portrait_w=370;portrait_h=(int)(portrait_w*sprite->height/sprite->width);}
    blit(570,180,portrait_w,portrait_h,
         _binary_build_summer_art_bin_start+sprite->offset,
         (int)sprite->width,(int)sprite->height);
#endif
    fill(0,0,W,66,0x25232d);
    text(36,22,"夏日未寄 / MAYKERNEL 原生游戏",0xf3eef6);
    text(941,18,"×",0xffffff);
    fill(48,88,928,86,0x211e28);
    fill(48,391,928,291,0x211e28);
    text(80,95,"角色 / HEROINES",0xb0e8de);
    for(int i=0;i<3;i++){
        fill(80+i*275,118,220,40,i==route?0x53665f:0x36343e);
        text(94+i*275,128,names[i],0xf6f4f4);
    }
#ifndef MAYGAME_WITH_ART
    fill(80,182,864,186,0x35323d);
    text(105,202,"原版画面未装入。请使用 SUMMER_SITE 重新构建。",0xcac5d3);
#endif
    fill(80,185,460,34,0x211e28);
    text(105,191,places[route],0xd2f2e9);
    fill(80,266,750,118,0x211e28);
#ifdef MAYGAME_WITH_STORY
    const MayGameStoryNode *node=current_node();
    if(node){
        text(102,277,node->ending?"结局 / ENDING":node->who,0xb8e9dd);
        wrapped(text,105,302,node->ending?node->title:node->text,0xf1e9e9);
        if(node->ending)wrapped(text,105,340,node->text,0xe0d8d4);
    }
#else
    text(105,300,"等待完整原版剧情导入。",0xf1e9e9);
#endif
    text(82,405,"离线原版剧情 / 每轮自由填写回复（无固定选项）",0xb0e8de);
    fill(80,438,864,60,0x413d4a);
    text(98,458,draft[0]?draft:"Type your reply here...",0xf2edf5);
    text(80,520,status,0xffd5af);
    if(last_reply[0])text(80,548,last_reply,0xcdc8d3);
    text(80,573,"Enter: 提交并推进剧情   Ctrl+S: 保存进度",0xcdc8d3);
    text(80,611,"↑ ↓: 切换角色  ESC: 返回桌面",0xcdc8d3);
    text(80,650,dirty_draft?"有未保存的进度":"独立自研引擎 / 无 SDL、Linux、WebView",0x99afa9);
}
