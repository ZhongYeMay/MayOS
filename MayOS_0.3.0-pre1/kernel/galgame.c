/*
 * MayKernel original, freestanding x86 game interface.
 * No POSIX, Linux ABI, SDL, network shim, webview or imported engine.
 * Do not fake AI replies: online DeepSeek generation will be enabled only
 * when native MayKernel TCP+TLS+HTTPS+JSON support is implemented.
 */
#include "galgame.h"
#ifdef MAYGAME_WITH_ART
#include "summer_art.h"
extern const u8 _binary_build_summer_art_bin_start[];
#endif
static int active=0,route=0,dirty_draft=0;
static char draft[512],status[128];
static const char *names[3]={"白石 澪","橘 凪","月島 栞"};
static const char *places[3]={"潮音放送室 / Kamakura",
                            "七里滨 / 海边",
                            "月灯旧书店 / Hase"};
static void route_set(int n){
    if(n<0||n>=3)return;
    route=n;draft[0]=0;dirty_draft=0;
    copy(status,"当前为离线输入草稿；不生成虚假 AI 台词。",sizeof(status));
}
void maygame_open(void){
    active=1;route_set(0);
    serial("MayKernel: native Summer game launched (AI network backend unavailable)\n");
}
int maygame_active(void){return active;}
static void save_draft(void){
    /* Saves only the player's explicit draft, never credentials. */
    int file=fs_find(1,"SummerDraft.txt");
    if(file<0)file=fs_create(1,"SummerDraft.txt",0);
    if(file<0){copy(status,"存储空间已满；无法保存草稿。",sizeof(status));return;}
    copy((char*)files[file].data,draft,FILE_CAP);
    files[file].size=strlen(draft);
    dirty_draft=0;
    if(disk_ready){
        if(fs_sync())copy(status,"草稿已保存到 MayFS。",sizeof(status));
        else copy(status,"磁盘写入失败；草稿仅在内存。",sizeof(status));
    }else copy(status,"草稿保存在当前 RAM 会话。",sizeof(status));
}
void maygame_key(int key){
    if(!active)return;
    if(key==27){active=0;return;}
    if(key==0x101){route_set((route+2)%3);return;}
    if(key==0x102){route_set((route+1)%3);return;}
    if(key==0x106){save_draft();return;}
    int length=(int)strlen(draft);
    if(key=='\b'){
        if(length){draft[length-1]=0;dirty_draft=1;}
        return;
    }
    if(key=='\n'){
        /* This is not a canned NPC response. No fake 'AI complete' state. */
        copy(status,"未发送：MayKernel 尚无安全 HTTPS/DeepSeek 通道。",sizeof(status));
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
void maygame_draw(MayGameFill fill,MayGameText text,MayGameBlit blit){
#ifndef MAYGAME_WITH_ART
    (void)blit;
#endif
    if(!active)return;
    fill(0,0,W,H,0x15141b);
#ifdef MAYGAME_WITH_ART
    /* Original pixels are losslessly packed at build time. The independent
       MayKernel blitter applies RGBA alpha in its framebuffer, no SDL decoder. */
    const MayGameArtMeta *background=&maygame_original_art[route];
    int natural_h=(int)(W*background->height/background->width);
    if(natural_h>H)natural_h=H;
    blit(0,(H-natural_h)/2,W,natural_h,
         _binary_build_summer_art_bin_start+background->offset,
         (int)background->width,(int)background->height);
    const MayGameArtMeta *sprite=&maygame_original_art[3+route*3];
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
        u32 color=i==route?0x53665f:0x36343e;
        fill(80+i*275,118,220,40,color);
        text(94+i*275,128,names[i],0xf6f4f4);
    }
    /* The complete original artwork is NOT substituted with fake images.
       Framebuffer asset preparation and native decoding remain required. */
#ifndef MAYGAME_WITH_ART
    fill(80,182,864,186,0x35323d);
    text(105,202,"原版 WebP 场景与人物立绘：等待内核资源加载器。",0xcac5d3);
#endif
    fill(80,185,460,34,0x211e28);
    text(105,191,places[route],0xd2f2e9);
    fill(80,327,740,56,0x211e28);
    text(105,332,"女主主动台词：必须由 DeepSeek 实时生成。",0xf1e9e9);
    text(105,355,"当前无 TLS 网络，不会展示预设回复。",0xe0d8d4);
    text(82,405,"自由输入 / 每轮填写自己的回答（无固定选项）",0xb0e8de);
    fill(80,438,864,60,0x413d4a);
    text(98,458,draft[0]?draft:"Type your reply here...",0xf2edf5);
    text(80,520,status,0xffd5af);
    text(80,573,"Enter: 发送（联网前拒绝）   Ctrl+S: 保存草稿",0xcdc8d3);
    text(80,611,"↑ ↓: 切换角色  ESC: 返回桌面",0xcdc8d3);
    text(80,650,dirty_draft?"草稿未保存":"独立自研实现 / 无 SDL、Linux、WebView",0x99afa9);
}
