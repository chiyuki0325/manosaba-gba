/* Host-only, real-input patrol of a generated production trial route. */
#define main unused_navigation_main
#include "smoke_test.c"
#undef main
#include "../src/trial_runtime.h"
#include "../src/witch_book_runtime.h"
#include "../src/trial_special_runtime.h"
#include "../src/save_runtime.h"
#include "../src/graphics_profile.h"
#include <stddef.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/renderers/video-software.h>
#include <string.h>

typedef struct {unsigned script,pc,state,action,arg,kind;} checkpoint;
static unsigned profile,book_profile,frame_count,peak_ticks,peak_cycles,peak_objects;
static unsigned risk_frames,missing_cases,checked_cases,missed,completed,halted,diagnostic_limit;
static const char* directory;
#ifndef MANOSABA_TRIAL_WALK_LIBRARY
static checkpoint points[4096];
#endif
static uint64_t checked[65536];
static unsigned checked_count;
static FILE* events;
typedef struct {unsigned pc,frames,delta,r,g,b,mode;} screen_probe;
static screen_probe screen_probes[64];static unsigned screen_probe_count,screen_suspended;
#ifndef MANOSABA_TRIAL_WALK_LIBRARY
static unsigned screen_pause,screen_paused;
#endif
static unsigned special_address,map_bits[11],map_suspended;
#ifndef MANOSABA_TRIAL_WALK_LIBRARY
static unsigned map_overlay_tested,picture_overlay_tested,save_address;
static save_runtime_profile save_status(void)
{save_runtime_profile p;uint32_t* v=(uint32_t*)&p;for(unsigned i=0;i<sizeof(p)/4;++i)v[i]=core->busRead32(core,save_address+4*i);return p;}
#endif
static trial_special_profile special(void)
{trial_special_profile p;uint32_t* v=(uint32_t*)&p;for(unsigned i=0;i<sizeof(p)/4;++i)v[i]=core->busRead32(core,special_address+4*i);return p;}
static trial_runtime_profile metric(void);
static unsigned map_canvas_address,map_checked_pc,upload_pending_address;
static void evidence(const char* name);
static void check_map_ui(trial_special_profile map_state)
{
    if(!map_state.map_ready)return;
    if(!upload_pending_address)upload_pending_address=elf_symbol("_ZN8manosaba20graphics_performanceE")+offsetof(struct graphics_profile,upload_pending);
    if(core->busRead32(core,upload_pending_address))return;
    trial_runtime_profile player=metric();
    if(!player.canvas_ready || !player.interaction_ready || player.vm_state!=1)return;
    unsigned key=(player.pc<<4)|map_state.map;
    if(key==map_checked_pc)return;
    map_checked_pc=key;
    unsigned bg=4,dispcnt=core->busRead16(core,0x04000000);
    for(unsigned i=0;i<4;++i)
        if((dispcnt&(0x100u<<i)) && !(core->busRead16(core,0x04000008+i*2)&3)){bg=i;break;}
    expect(bg<4,"map dialogue has a visible UI background");
    unsigned cnt=core->busRead16(core,0x04000008+bg*2);
    unsigned base=0x06000000+((cnt>>2)&3)*16384,map=0x06000000+((cnt>>8)&31)*2048;
    // Scroll registers are write-only on the GBA; inspect the renderer shadow.
    struct GBA* gba=core->board;
    struct GBAVideoSoftwareRenderer* renderer=(struct GBAVideoSoftwareRenderer*)gba->video.renderer;
    unsigned ox=renderer->bg[bg].x&255,oy=renderer->bg[bg].y&255,wrong=0;
    for(unsigned y=0;y<160;++y)for(unsigned x=0;x<240;++x)
    {
        unsigned tx=x/8,ty=y/8;
        if(!(ty>=10 || (tx<4 && ty<5) || (tx>=26 && ty<8)))continue;
        unsigned sx=(x+ox)&255,sy=(y+oy)&255;
        unsigned cell=core->busRead16(core,map+(sy/8*32+sx/8)*2),px=sx%8,py=sy%8;
        if(cell&1024)px=7-px;
        if(cell&2048)py=7-py;
        unsigned actual=core->busRead8(core,base+(cell&1023)*32+py*4+px/2)>>(px%2*4)&15;
        unsigned expected=core->busRead8(core,map_canvas_address+(ty*30+tx)*32+(y%8)*4+(x%8)/2)>>(x%2*4)&15;
        wrong+=actual!=expected;
    }
    if(wrong)
    {
        fprintf(stderr,"MAP_UI_MISMATCH map=%u pc=%u wrong=%u bg=%u cnt=%04x offset=%u,%u\n",map_state.map,player.pc,wrong,bg,cnt,ox,oy);
        evidence("map-ui-mismatch");
        expect(0,"map dialogue decoration is uploaded to its current BG allocation");
    }
}

static void map_observe(void)
{
    if(!special_address || map_suspended)return;
    trial_special_profile p=special();
    check_map_ui(p);
    static unsigned picture_pc;
    if(p.picture && metric().interaction_ready && (metric().vm_state==1 || metric().vm_state==6) && metric().pc!=picture_pc)
    {picture_pc=metric().pc;char name[64];snprintf(name,sizeof(name),"picture-%u",picture_pc);screenshot(directory,name);fprintf(events,"{\"kind\":\"picture\",\"pc\":%u}\n",picture_pc);fflush(events);}
    if(p.shake_actor){static unsigned seen;if(!seen){seen=1;screenshot(directory,"actor-shake");fprintf(events,"{\"kind\":\"actor_shake\",\"actor\":%u}\n",p.shake_actor);fflush(events);}}
    if(!p.map || !p.map_ready || p.map>11 || p.map_alpha<32)return;
    unsigned phase=p.map_clock*4/(p.map_duration ? p.map_duration : 1);if(phase>3)phase=3;
    if(map_bits[p.map-1]&(1u<<phase))return;
    map_bits[p.map-1]|=1u<<phase;
    char name[64];snprintf(name,sizeof(name),"map-%02u-phase-%u",p.map-1,phase);screenshot(directory,name);
    fprintf(events,"{\"kind\":\"map\",\"segment\":%u,\"clock\":%u,\"floor\":%u,\"left\":%u,\"top\":%u,\"pins\":%u,\"pin_x\":%u,\"pin_y\":%u}\n",p.map-1,p.map_clock,p.map_floor,p.map_left,p.map_top,p.map_pins,p.pin_x,p.pin_y);fflush(events);
}
static void screen_observe(trial_runtime_profile p)
{
    if(screen_suspended || p.vm_state!=14 || !p.scene_ready || !screen_probe_count)return;
    for(unsigned n=0;n<screen_probe_count;++n)if(screen_probes[n].pc==p.pc)
    {
        screen_probe* probe=&screen_probes[n];++probe->frames;
        unsigned r=0,g=0,b=0;
        for(int y=24;y<120;++y)for(int x=0;x<240;++x)
        {unsigned value=pixels[y*240+x];r+=(value>>3)&31;g+=(value>>11)&31;b+=(value>>19)&31;}
        r=r*100/(96*240);g=g*100/(96*240);b=b*100/(96*240);
        unsigned delta=probe->mode==1 ? 10000+2*r-g-b : probe->mode==2 ? 10000+2*g-r-b :
            probe->mode==3 ? r+g+b : probe->mode==4 ? 10000-r-g-b : probe->mode==5 ? 10000+2*b-r-g : probe->frames;
        if(!probe->delta || delta>probe->delta)
        {
            probe->delta=delta;probe->r=r;probe->g=g;probe->b=b;
            char name[64];snprintf(name,sizeof(name),"screen-%u",p.pc);screenshot(directory,name);
        }
    }
}
static void* diagnostic_state;
static void (*native_scanline)(struct GBAVideoRenderer*,int);
static trial_runtime_profile metric(void)
{
    trial_runtime_profile p;uint32_t* out=(uint32_t*)&p;
    for(unsigned i=0;i<sizeof(p)/4;++i)out[i]=core->busRead32(core,profile+i*4);
    return p;
}
static witch_book_profile book(void)
{
    witch_book_profile p;uint32_t* out=(uint32_t*)&p;
    for(unsigned i=0;i<sizeof(p)/4;++i)out[i]=core->busRead32(core,book_profile+i*4);
    return p;
}
static void unlimited(struct GBAVideoRenderer* base,int y)
{
    struct GBAVideoSoftwareRenderer* r=(struct GBAVideoSoftwareRenderer*)base;
    if(r->oamDirty){r->oamMax=GBAVideoRendererCleanOAM(r->d.oam->obj,r->sprites,r->objOffsetY);r->oamDirty=false;}
    for(int i=0;i<r->oamMax;++i)r->sprites[i].cycles=0;
    for(int i=0;i<5;++i)r->scanlineDirty[i]=0xffffffffu;
    native_scanline(base,y);
}
static void dirty_renderer(void)
{
    struct GBA* gba=core->board;
    struct GBAVideoSoftwareRenderer* r=(struct GBAVideoSoftwareRenderer*)gba->video.renderer;
    r->oamDirty=true;for(int i=0;i<5;++i)r->scanlineDirty[i]=0xffffffffu;
}
static void evidence(const char* name)
{
    screenshot(directory,name);
    char path[768];snprintf(path,sizeof(path),"%s/%s.state",directory,name);
    void* state=malloc(core->stateSize(core));expect(state && core->saveState(core,state),"save failing native state");
    FILE* f=fopen(path,"wb");expect(f!=NULL,"open failing state");fwrite(state,1,core->stateSize(core),f);fclose(f);free(state);
    snprintf(path,sizeof(path),"%s/%s-obj.bin",directory,name);f=fopen(path,"wb");expect(f!=NULL,"open OAM/VRAM evidence");
    const unsigned starts[]={0x07000000,0x06010000,0x05000200},sizes[]={1024,32768,512};
    for(int r=0;r<3;++r)for(unsigned i=0;i<sizes[r];++i)fputc(core->busRead8(core,starts[r]+i),f);
    fclose(f);
}
static void verify_scanline(unsigned cycles,unsigned limit,unsigned objects,unsigned row)
{
    trial_runtime_profile p=metric();
    uint64_t hash=1469598103934665603ull;
    for(unsigned i=0;i<1024;i+=4)hash=(hash^core->busRead32(core,0x07000000+i))*1099511628211ull;
    hash=(hash^p.vm_script)*1099511628211ull;hash=(hash^p.pc)*1099511628211ull;
    /* Palette fades, BG occlusion, windows and alpha can expose previously
     * hidden gaps even when OAM is identical. Match the entire display input. */
    struct GBA* gba=core->board;
    for(unsigned i=0;i<49152;++i)hash=(hash^gba->video.vram[i])*1099511628211ull;
    for(unsigned i=0;i<512;++i)hash=(hash^gba->video.palette[i])*1099511628211ull;
    for(unsigned i=0;i<0x56;i+=2)if(i!=4 && i!=6)hash=(hash^core->busRead16(core,0x04000000+i))*1099511628211ull;
    if(!hash)hash=1;
    unsigned slot=(unsigned)hash&65535;
    while(checked[slot] && checked[slot]!=hash)slot=(slot+1)&65535;
    if(checked[slot]==hash)return;
    if(checked_count==32768)
    {
        if(!diagnostic_limit)fputs("{\"kind\":\"diagnostic_limit\",\"limit\":32768}\n",events);
        diagnostic_limit=1;
        return;
    }
    checked[slot]=hash;++checked_count;++checked_cases;
    color_t current[240*160],reference[240*160];memcpy(current,pixels,sizeof(current));
    expect(core->saveState(core,diagnostic_state),"save paired-frame state");
    dirty_renderer();core->runFrame(core);memcpy(reference,pixels,sizeof(reference));
    trial_runtime_profile next=metric();
    expect(core->loadState(core,diagnostic_state),"restore paired-frame state");dirty_renderer();
    struct GBAVideoRenderer* renderer=gba->video.renderer;
    native_scanline=renderer->drawScanline;renderer->drawScanline=unlimited;core->runFrame(core);renderer->drawScanline=native_scanline;
    unsigned changed=0,above=0;
    for(int i=0;i<240*160;++i)if((reference[i]&0xffffffu)!=(pixels[i]&0xffffffu)){++changed;above+=i<240*104;}
    if(changed)
    {
        ++missing_cases;char name[100];snprintf(name,sizeof(name),"missing-%04u-reference",missing_cases);screenshot(directory,name);
        memcpy(pixels,reference,sizeof(reference));snprintf(name,sizeof(name),"missing-%04u-native",missing_cases);screenshot(directory,name);
    }
    expect(core->loadState(core,diagnostic_state),"restore production state after diagnostic");dirty_renderer();memcpy(pixels,current,sizeof(current));
    fprintf(events,"{\"kind\":\"scanline\",\"frame\":%u,\"step\":%u,\"script\":%u,\"pc\":%u,\"state\":%u,\"cycles\":%u,\"limit\":%u,\"objects\":%u,\"row\":%u,\"missing_pixels\":%u,\"above_dialogue\":%u,\"probe_script\":%u,\"probe_pc\":%u,\"probe_state\":%u}\n",frame_count,completed,p.vm_script,p.pc,p.vm_state,cycles,limit,objects,row,changed,above,next.vm_script,next.pc,next.vm_state);fflush(events);
    if(changed){char name[100];snprintf(name,sizeof(name),"missing-%04u-before",missing_cases);evidence(name);}
}
static void tick(void)
{
    static unsigned previous_pc,previous_state,previous_script,previous_missed;
    core->runFrame(core);++frame_count;
    trial_runtime_profile p=metric();witch_book_profile b=book();
    if(p.missed>previous_missed)
        fprintf(events,"{\"kind\":\"missed_frame\",\"frame\":%u,\"script\":%u,\"pc\":%u,\"state\":%u,\"ticks\":%u,\"count\":%u,\"previous_script\":%u,\"previous_pc\":%u,\"previous_state\":%u,\"ui\":%u,\"clear\":%u,\"text\":%u,\"hud\":%u}\n",frame_count,p.vm_script,p.pc,p.vm_state,p.core_ticks,p.missed-previous_missed,previous_script,previous_pc,previous_state,p.ui_work,p.clear_work,p.text_work,p.hud_work);
    previous_pc=p.pc;previous_state=p.vm_state;previous_script=p.vm_script;previous_missed=p.missed;
    screen_observe(p);map_observe();
    static unsigned last_book_frame,book_stalled;
    if(b.frames==last_book_frame)++book_stalled;else book_stalled=0;
    last_book_frame=b.frames;
    if(book_stalled>300 && completed>0)halted=1;
    if(p.core_ticks>peak_ticks)
    {
        peak_ticks=p.core_ticks;
        if(screen_probe_count)fprintf(events,"{\"kind\":\"cpu_peak\",\"frame\":%u,\"step\":%u,\"pc\":%u,\"state\":%u,\"ticks\":%u,\"ui\":%u,\"clear\":%u,\"text\":%u,\"hud\":%u}\n",frame_count,completed,p.pc,p.vm_state,p.core_ticks,p.ui_work,p.clear_work,p.text_work,p.hud_work);
    }
    if(p.missed>missed)missed=p.missed;
    unsigned display=core->busRead16(core,0x04000000);
    if(!(display&0x1000) || (display&0x80))return;
    struct GBA* gba=core->board;struct GBAVideoRendererSprite sprites[128];
    int count=GBAVideoRendererCleanOAM(gba->video.oam.obj,sprites,0);
    unsigned peak=0,row=0,limit=(display&32) ? 954 : 1210;
    for(int y=0;y<160;++y)
    {
        unsigned cost=0;int last=0;
        for(int i=0;i<count;++i)
        {
            cost+=2*(sprites[i].index-last);last=sprites[i].index;
            if((y>=sprites[i].y && y<sprites[i].endY) || (sprites[i].endY>256 && y<sprites[i].endY-256))cost+=sprites[i].cycles;
        }
        if(cost>peak){peak=cost;row=(unsigned)y;}
    }
    if(peak>peak_cycles)peak_cycles=peak;
    if((unsigned)count>peak_objects)peak_objects=(unsigned)count;
    if(peak>limit){++risk_frames;verify_scanline(peak,limit,(unsigned)count,row);}
}
static void wait_frames(int n){while(n-- && !halted)tick();}
static void tap(unsigned key){core->setKeys(core,key);wait_frames(2);core->setKeys(core,0);wait_frames(24);}
static int matches(checkpoint a,trial_runtime_profile p){return a.script==p.vm_script && a.pc==p.pc && a.state==p.vm_state;}
static void result(const char* status,unsigned total)
{
    trial_runtime_profile p=metric();
    printf("{\"status\":\"%s\",\"frames\":%u,\"completed\":%u,\"steps\":%u,\"script\":%u,\"pc\":%u,\"state\":%u,\"hp\":%u,\"peak_ticks\":%u,\"missed\":%u,\"peak_cycles\":%u,\"peak_objects\":%u,\"risk_frames\":%u,\"checked_cases\":%u,\"missing_cases\":%u}\n",status,frame_count,completed,total,p.vm_script,p.pc,p.vm_state,p.vm_hp,peak_ticks,missed,peak_cycles,peak_objects,risk_frames,checked_cases,missing_cases);fflush(stdout);
    fprintf(events,"{\"kind\":\"final_state\",\"script\":%u,\"pc\":%u,\"state\":%u,\"preview\":%u,\"camera_frame\":%u,\"camera_duration\":%u,\"scene_ready\":%u,\"canvas_ready\":%u,\"book_open\":%u}\n",p.vm_script,p.pc,p.vm_state,p.preview,p.camera_frame,p.camera_duration,p.scene_ready,p.canvas_ready,book().open);
}
static void crashed(void* context){(void)context;halted=1;}
static void reveal_capture(void)
{
    core->setKeys(core,0);
    expect(core->saveState(core,diagnostic_state),"save dialogue before reveal");
    wait_frames(160);screenshot(directory,"reveal-natural");
    color_t reference[240*160];memcpy(reference,pixels,sizeof(reference));
    const int delays[]={2,8,16,24,32};unsigned errors=0;
    for(unsigned n=0;n<sizeof(delays)/sizeof(*delays);++n)
    {
        expect(core->loadState(core,diagnostic_state),"restore dialogue before reveal");dirty_renderer();
        core->setKeys(core,0);wait_frames(delays[n]);
        core->setKeys(core,KEY_A);wait_frames(2);core->setKeys(core,0);wait_frames(160);
        unsigned mismatch=0;
        for(int y=118;y<156;++y)for(int x=20;x<234;++x)
        {
            unsigned a=reference[y*240+x],b=pixels[y*240+x];
            int white_a=(a&255)>=240 && ((a>>8)&255)>=240 && ((a>>16)&255)>=240;
            int white_b=(b&255)>=240 && ((b>>8)&255)>=240 && ((b>>16)&255)>=240;
            mismatch+=white_a!=white_b;
        }
        char name[64];snprintf(name,sizeof(name),"reveal-fast-%d",delays[n]);screenshot(directory,name);
        printf("{\"reveal_delay\":%d,\"mismatched_text_pixels\":%u}\n",delays[n],mismatch);errors+=mismatch;
    }
    expect(!errors,"A reveal preserves every dialogue glyph at every tested press time");
}
static unsigned text_mask(unsigned char* out)
{
    unsigned bg=0;for(;bg<4;++bg){unsigned c=core->busRead16(core,0x04000008+bg*2);if((core->busRead16(core,0x04000000)&(0x100u<<bg)) && !(c&131))break;}
    expect(bg<4,"find the 4bpp dialogue UI");unsigned c=core->busRead16(core,0x04000008+bg*2),count=0;
    struct GBAVideoSoftwareRenderer* renderer=(struct GBAVideoSoftwareRenderer*)((struct GBA*)core->board)->video.renderer;
    unsigned sx=renderer->bg[bg].x,sy=renderer->bg[bg].y;
    for(int y=118;y<156;++y)for(int x=20;x<234;++x)
    {
        unsigned px=(x+sx)&255,py=(y+sy)&255,cell=core->busRead16(core,0x06000000+((c>>8)&31)*2048+(py/8*32+px/8)*2);
        unsigned tx=(cell&1024) ? 7-(px&7) : px&7,ty=(cell&2048) ? 7-(py&7) : py&7;
        unsigned ink=(core->busRead8(core,0x06000000+((c>>2)&3)*16384+(cell&1023)*32+ty*4+tx/2)>>((tx&1)*4))&15;
        unsigned color=core->busRead16(core,0x05000000+(((cell>>12)*16+ink)*2));
        out[count++]=ink && (color&31)>=29 && ((color>>5)&31)>=29 && ((color>>10)&31)>=29;
    }
    return count;
}
static void burst_capture(void)
{
    core->setKeys(core,0);expect(core->saveState(core,diagnostic_state),"save partially typed sentence");
    unsigned first=metric().pc;wait_frames(200);tap(KEY_A);
    for(unsigned n=0;n<1800 && (metric().pc==first || metric().vm_state!=1 || !metric().interaction_ready);++n)wait_frames(1);
    unsigned second=metric().pc;expect(second!=first && metric().vm_state==1,"natural input reaches second sentence");
    wait_frames(200);unsigned char reference[214*38],third_reference[214*38],actual[214*38];unsigned size=text_mask(reference);screenshot(directory,"burst-natural");
    tap(KEY_A);for(unsigned n=0;n<1800 && (metric().pc==second || metric().vm_state!=1 || !metric().interaction_ready);++n)wait_frames(1);
    unsigned third=metric().pc;expect(third!=second,"natural input reaches third sentence");wait_frames(200);text_mask(third_reference);
    const unsigned delays[]={2,4,8,12,16,24};unsigned errors=0;
    for(unsigned k=0;k<sizeof(delays)/sizeof(*delays);++k)
    {
        expect(core->loadState(core,diagnostic_state),"restore first sentence for burst input");dirty_renderer();core->setKeys(core,0);wait_frames(delays[k]);
        unsigned n=0;
        for(;n<1800;++n)
        {
            core->setKeys(core,n%4<2 ? KEY_A : 0);wait_frames(1);
            if(metric().pc!=first && metric().vm_state==1)break;
        }
        core->setKeys(core,0);expect(n<1800 && metric().pc==second,"burst input advances exactly one sentence");wait_frames(200);text_mask(actual);
        unsigned mismatch=0;for(unsigned i=0;i<size;++i)mismatch+=reference[i]!=actual[i];errors+=mismatch;
        char name[48];snprintf(name,sizeof(name),"burst-fast-%u",delays[k]);screenshot(directory,name);
        printf("{\"burst_delay\":%u,\"mismatched_text_pixels\":%u}\n",delays[k],mismatch);
        expect(core->loadState(core,diagnostic_state),"restore continuous turbo trial");dirty_renderer();core->setKeys(core,0);wait_frames(delays[k]);unsigned saw_second=0;
        for(n=0;n<2400;++n)
        {
            core->setKeys(core,n%4<2 ? KEY_A : 0);wait_frames(1);
            if(metric().pc==second && metric().vm_state==1){text_mask(actual);saw_second|=!memcmp(reference,actual,size);}
            if(metric().pc==third && metric().vm_state==1)break;
        }
        core->setKeys(core,0);expect(n<2400 && saw_second,"second sentence reaches VRAM before continuous turbo advances to third");
        wait_frames(200);text_mask(actual);expect(!memcmp(third_reference,actual,size),"third sentence has no stale glyphs from previous sentences");
    }
    expect(!errors,"second sentence paints completely after rapid A input");
}
static void shake_capture(void)
{
    core->setKeys(core,0);wait_frames(85);
    struct GBA* gba=core->board;unsigned moved=0,samples=0;
    int previous_x=0,previous_y=0,previous_bgx=0,previous_bgy=0;
    for(int f=0;f<30;++f)
    {
        struct GBAVideoRendererSprite sprites[128];int count=GBAVideoRendererCleanOAM(gba->video.oam.obj,sprites,0);
        expect(count>0,"shaking court has a live actor");
        int bg=-1;
        for(int i=0;i<4;++i)if((gba->memory.io[0]&(0x100<<i)) && (gba->memory.io[4+i]&3)==2)bg=i;
        expect(bg>=0,"court background is enabled at priority two");
        int bgx=gba->memory.io[8+bg*2]&511,bgy=gba->memory.io[9+bg*2]&511;
        const struct GBAObj* obj=&gba->video.oam.obj[sprites[0].index];
        int x=obj->b&511,y=obj->a&255;
        if(f)
        {
            int dx=((x-previous_x+256)&511)-256,dy=((y-previous_y+128)&255)-128;
            int bx=((bgx-previous_bgx+256)&511)-256,by=((bgy-previous_bgy+256)&511)-256;
            expect(dx==-bx && dy==-by,"actor and background shake by the same screen-space offset");
            moved+=dx!=0 || dy!=0;++samples;
        }
        previous_x=x;previous_y=y;previous_bgx=bgx;previous_bgy=bgy;
        if(f==15)screenshot(directory,"shake");
        wait_frames(1);
    }
    expect(moved>=4,"continuous shake remains active during dialogue");
    printf("{\"shake_samples\":%u,\"shake_moving_frames\":%u}\n",samples,moved);
}
static unsigned heat_control(void)
{
    struct GBA* gba=core->board;return gba->memory.io[0xde/2];
}
static void heat_capture(int enabled)
{
    core->setKeys(core,0);wait_frames(180);
    if(!enabled)
    {
        expect(!(heat_control()&0x8000),"source fire-off command disables background HDMA");
        screenshot(directory,"heat-off");printf("{\"heat_disabled\":true}\n");return;
    }
    for(int i=0;i<1200 && metric().camera_frame<metric().camera_duration;++i)wait_frames(1);
    struct GBA* gba=core->board;unsigned last_hash=0,changes=0;
    uint16_t positions[128][2];int previous_count=0;
    for(int frame=0;frame<64;++frame)
    {
        expect((heat_control()&0xb200)==0xa200,"heat uses repeating HBlank DMA3");
        unsigned source=gba->memory.io[0xd4/2]|(unsigned)gba->memory.io[0xd6/2]<<16;
        unsigned dest=gba->memory.io[0xd8/2]|(unsigned)gba->memory.io[0xda/2]<<16;
        expect(dest>=0x04000010 && dest<=0x0400001c && !(dest&3),"heat writes only one background horizontal scroll register");
        unsigned hash=2166136261u,low=512,high=0;
        for(unsigned y=0;y<160;++y)
        {
            unsigned v=core->busRead16(core,source+y*2);hash=(hash^v)*16777619u;
            if(v<low)low=v;
            if(v>high)high=v;
        }
        expect(high>low,"heat table has spatial curvature");
        if(frame && hash!=last_hash)++changes;
        last_hash=hash;
        struct GBAVideoRendererSprite sprites[128];int count=GBAVideoRendererCleanOAM(gba->video.oam.obj,sprites,0);
        if(frame)expect(count==previous_count,"heat preserves actor object count");
        for(int i=0;i<count;++i)
        {
            const struct GBAObj* obj=&gba->video.oam.obj[sprites[i].index];
            if(frame)expect(positions[i][0]==obj->a && positions[i][1]==obj->b,"heat never warps or moves actor objects");
            positions[i][0]=obj->a;positions[i][1]=obj->b;
        }
        previous_count=count;
        if(!(frame%4)){char name[48];snprintf(name,sizeof(name),"heat-%02d",frame);screenshot(directory,name);}
        wait_frames(1);
    }
    expect(changes>=48,"heat wave advances continuously rather than using a static distortion");
    // Real overlays release the court. A lingering DMA must never touch their UI.
    const unsigned keys[]={KEY_START,KEY_SELECT,KEY_L};
    for(unsigned n=0;n<3;++n)
    {
        tap(keys[n]);wait_frames(160);expect(!(heat_control()&0x8000),"pause/book/history stop court HDMA");
        tap(keys[n]);
        for(int i=0;i<1200 && !(heat_control()&0x8000);++i)wait_frames(1);
        expect(heat_control()&0x8000,"court HDMA resumes when returning from overlay");wait_frames(20);
    }
    printf("{\"heat_frames\":64,\"heat_phase_changes\":%u,\"actors_unchanged\":true,\"overlay_returns\":3}\n",changes);
}
#ifndef MANOSABA_TRIAL_WALK_LIBRARY
static void save_ready(int view)
{
    for(int n=0;n<1800;++n){wait_frames(1);save_runtime_profile p=save_status();if((int)p.view==view && !p.busy && p.paint==6)return;}
    expect(0,"map save/load UI ready");
}
static void special_save_load(int map)
{
    tap(KEY_START);save_ready(2);unsigned script=metric().vm_script,pc=metric().pc;
    tap(KEY_DOWN);tap(KEY_A);save_ready(0);unsigned slot=save_status().selected;
    tap(KEY_A);save_ready(0);
    if(save_status().confirm){tap(KEY_RIGHT);tap(KEY_A);save_ready(0);}
    tap(KEY_R);save_ready(1);
    for(unsigned n=0;n<13 && save_status().selected!=slot+1;++n){tap(KEY_DOWN);save_ready(1);}
    expect(save_status().selected==slot+1,"map snapshot selected");
    tap(KEY_A);save_ready(1);tap(KEY_RIGHT);tap(KEY_A);
    for(unsigned n=0;n<2400;++n)
    {wait_frames(1);if((int)save_status().view==-1 && !save_status().restoring && (map ? special().map_ready : metric().interaction_ready))break;}
    trial_special_profile p=special();
    expect(metric().vm_script==script,"Flash restores current script");
    if(map){expect(p.map==4 && p.map_ready,"Flash load restores map segment");expect(p.map_clock==p.map_duration,"map animation settles on load");}
    else expect(p.picture && metric().pc==pc && metric().interaction_ready,"Flash restores picture and dialogue PC");
    screenshot(directory,map ? "map-loaded" : "picture-loaded");
    fprintf(events,"{\"kind\":\"%s_save_load\",\"passed\":true}\n",map ? "map" : "picture");fflush(events);
}
int main(int argc,char** argv)
{
    expect(argc==7,"trial_walk_test ROM output profile book_profile plan max_frames");
    directory=argv[2];profile=strtoul(argv[3],NULL,16);book_profile=strtoul(argv[4],NULL,16);unsigned max_frames=strtoul(argv[6],NULL,10);
    FILE* f=fopen(argv[5],"r");expect(f!=NULL,"open route fixture");unsigned offset,count;
    expect(fscanf(f,"%u %u",&offset,&count)==2 && count<4096,"route fixture header");
    for(unsigned i=0;i<=count;++i)expect(fscanf(f,"%u %u %u %u %u %u",&points[i].script,&points[i].pc,&points[i].state,&points[i].action,&points[i].arg,&points[i].kind)==6,"read route checkpoint");
    fclose(f);
    char path[768];snprintf(path,sizeof(path),"%s/events.jsonl",directory);events=fopen(path,"w");expect(events!=NULL,"open patrol events");
    core=mCoreFind(argv[1]);expect(core && core->init(core),"initialize mGBA");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load patrol ROM");core->reset(core);
    struct mCoreCallbacks callbacks={0};callbacks.coreCrashed=crashed;core->addCoreCallbacks(core,&callbacks);
    diagnostic_state=malloc(core->stateSize(core));expect(diagnostic_state!=NULL,"allocate host diagnostic state");
    if(getenv("MANOSABA_TRIAL_SPECIAL_PROFILE"))special_address=strtoul(getenv("MANOSABA_TRIAL_SPECIAL_PROFILE"),NULL,16);
    const char* screen_list=getenv("MANOSABA_TRIAL_SCREEN_PCS");
    if(screen_list)
    {
        while(*screen_list)
        {
            expect(screen_probe_count<64,"screen probe capacity");char* end;
            screen_probes[screen_probe_count++].pc=(unsigned)strtoul(screen_list,&end,10);screen_list=*end ? end+1 : end;
        }
        const char* modes=getenv("MANOSABA_TRIAL_SCREEN_MODES");
        for(unsigned i=0;i<screen_probe_count;++i){char* end;screen_probes[i].mode=(unsigned)strtoul(modes,&end,10);modes=*end ? end+1 : end;}
    }
    if(getenv("MANOSABA_TRIAL_SCREEN_PAUSE_PC"))screen_pause=(unsigned)strtoul(getenv("MANOSABA_TRIAL_SCREEN_PAUSE_PC"),NULL,10);
    if(special_address)
    {map_canvas_address=elf_symbol("_ZN12_GLOBAL__N_1L12canvas_tilesE");expect(map_canvas_address,"map canvas symbol is exported");}
    wait_frames(30);debug_hotkey();tap(KEY_START);tap(KEY_DOWN);tap(KEY_DOWN);tap(KEY_A);wait_frames(80);
    for(unsigned i=0;i<offset/6;++i)tap(KEY_RIGHT);
    for(unsigned i=0;i<offset%6;++i)tap(KEY_DOWN);
    tap(KEY_A);
    unsigned entered=0,step_frames=0,pulse=0,map_moves=0;
    const char* capture=getenv("MANOSABA_TRIAL_CAPTURE_PC");
    const char* capture_state=getenv("MANOSABA_TRIAL_CAPTURE_STATE");
    const char* status="timeout";
    while(frame_count<max_frames)
    {
        trial_runtime_profile p=metric();witch_book_profile b=book();
        if(halted){status="engine_halted";break;}
        if(diagnostic_limit){status="diagnostic_limit";break;}
        if((entered || p.vm_hp) && p.vm_hp<10){status="unexpected_penalty";break;}
        if(matches(points[completed],p))entered=1;
        else if(entered)
        {
            if(completed<count && matches(points[completed+1],p))
            {
                ++completed;entered=1;step_frames=pulse=map_moves=0;
                fprintf(events,"{\"kind\":\"checkpoint\",\"step\":%u,\"frame\":%u,\"script\":%u,\"pc\":%u,\"state\":%u}\n",completed,frame_count,p.vm_script,p.pc,p.vm_state);fflush(events);
            }
            /* Auto titles/cut-ins/camera/audio can sit between checkpoints. */
        }
        if(completed==count && matches(points[count],p)){status="passed";break;}
        if(capture && p.pc==strtoul(capture,NULL,10) && p.vm_state==(capture_state ? strtoul(capture_state,NULL,10) : 1) && p.interaction_ready && !p.preview)
        {
            if(getenv("MANOSABA_TRIAL_HOLD_TEST"))
            {
                unsigned pc=p.pc,total=p.total_left;
                core->setKeys(core,0);wait_frames(p.line_left+120);
                expect(metric().vm_state==6 && metric().pc==pc && !metric().line_left && metric().links,"Stop retains the expired statement and objection links");
                expect(metric().total_left<total,"Stop keeps the trial countdown running");
                tap(KEY_A);wait_frames(30);
                expect(metric().vm_state==6 && metric().pc==pc,"A cannot bypass the stopped discussion");
                screenshot(directory,"debate-held");
                tap(KEY_UP);wait_frames(180);
                expect(metric().vm_state==2,"the stopped discussion still opens its objection choice");
            }
            if(getenv("MANOSABA_TRIAL_BURST_TEST"))burst_capture();
            if(getenv("MANOSABA_TRIAL_REVEAL_TEST"))reveal_capture();
            if(getenv("MANOSABA_TRIAL_SHAKE_TEST"))shake_capture();
            if(getenv("MANOSABA_TRIAL_HEAT_TEST"))heat_capture(atoi(getenv("MANOSABA_TRIAL_HEAT_TEST")));
            core->setKeys(core,0);wait_frames(120);screenshot(directory,"capture");
            const char* color=getenv("MANOSABA_TRIAL_TIMER_COLOR");
            if(color)
            {
                unsigned rgb=pixels[159*240+2],actual=((rgb>>3)&31)|(((rgb>>11)&31)<<5)|(((rgb>>19)&31)<<10);
                expect(actual==strtoul(color,NULL,16),"trial countdown bar uses the current protagonist's color");
            }
            status="captured";break;
        }
        if(special_address && !map_overlay_tested && special().map==4 && special().map_ready && special().map_clock>=36 && special().map_clock<90)
        {
            map_overlay_tested=map_suspended=1;
            const unsigned overlays[]={KEY_START,KEY_SELECT,KEY_L};
            for(unsigned i=0;i<3;++i)
            {
                tap(overlays[i]);wait_frames(100);unsigned frozen=special().map_clock;
                wait_frames(60);expect(special().map_clock==frozen,"overlay freezes map timeline");
                tap(i==2 ? KEY_B : overlays[i]);map_checked_pc=0;
                for(unsigned n=0;n<1200 && !special().map_ready;++n)wait_frames(1);
                expect(special().map_ready && special().map_clock>=frozen && special().map_clock<frozen+40,"overlay restores current map frame and motion");
            }
            if(getenv("MANOSABA_TRIAL_MAP_SAVE")){save_address=strtoul(getenv("MANOSABA_TRIAL_MAP_SAVE"),NULL,16);special_save_load(1);}
            map_suspended=0;fprintf(events,"{\"kind\":\"map_overlays\",\"passed\":true}\n");
        }
        if(special_address && !picture_overlay_tested && getenv("MANOSABA_TRIAL_PICTURE_OVERLAYS") && special().picture && (p.vm_state==1 || (p.vm_state==6 && p.line_left>600)) && p.interaction_ready)
        {
            unsigned pc=p.pc;wait_frames(40);
            if(!special().picture || metric().pc!=pc)continue;  /* A non-waiting hide may be fading out. */
            picture_overlay_tested=map_suspended=1;
            const unsigned overlays[]={KEY_START,KEY_SELECT,KEY_L};
            for(unsigned i=0;i<3;++i)
            {
                tap(overlays[i]);wait_frames(120);expect(metric().pc==pc,"picture overlay freezes dialogue");
                tap(i==2 ? KEY_B : overlays[i]);map_checked_pc=0;
                for(unsigned n=0;n<1800 && !metric().interaction_ready;++n)wait_frames(1);
                wait_frames(60);expect(special().picture && metric().interaction_ready && metric().pc==pc,"picture restores after overlay");
            }
            save_address=strtoul(getenv("MANOSABA_TRIAL_PICTURE_OVERLAYS"),NULL,16);special_save_load(0);
            map_suspended=0;
        }
        if(!screen_paused && screen_pause && p.pc==screen_pause && p.vm_state==14)
        {
            unsigned observed=0;for(unsigned i=0;i<screen_probe_count;++i)if(screen_probes[i].pc==p.pc)observed=screen_probes[i].frames;
            if(observed>=8)
            {
                screen_paused=screen_suspended=1;tap(KEY_START);wait_frames(120);
                expect(metric().pc==p.pc && metric().vm_state==14,"pause freezes the active screen-effect wait");
                core->setKeys(core,KEY_START);wait_frames(2);core->setKeys(core,0);wait_frames(2);screen_suspended=0;continue;
            }
        }
        if(++step_frames>5400){status=entered ? "checkpoint_stalled" : "checkpoint_unreached";break;}
        unsigned key=0;
        if(pulse)--pulse;
        if(entered && matches(points[completed],p) && !pulse)
        {
            checkpoint s=points[completed];
            if(b.open && b.ready)
            {
                if(s.action==4 || (s.action==3 && b.tab==2))
                {
                    if(b.source==s.arg)key=KEY_A;
                    else if(b.mode)key=KEY_B;
                    else if(b.tab==2){key=(++map_moves%34) ? KEY_RIGHT : KEY_DOWN;}
                    else key=KEY_RIGHT;
                }
            }
            else if(!b.open && p.interaction_ready && !p.preview)
            {
                if(s.action==0 || s.action==1)key=KEY_A;
                else if(s.action==2)
                {
                    key=s.arg==0 ? KEY_UP : s.arg==1 ? KEY_LEFT : KEY_RIGHT;
                }
                else if(s.action==3)key=p.choice==s.arg ? KEY_A : KEY_DOWN;
            }
            if(key)pulse=24;
        }
        /* A proof overlay belongs to the same VM checkpoint and must be closed. */
        if(!b.open && p.preview && p.interaction_ready && !pulse){key=KEY_B;pulse=24;}
        core->setKeys(core,key);tick();
    }
    core->setKeys(core,0);
    if(strcmp(status,"passed") && strcmp(status,"captured")){evidence("failure");}
    else screenshot(directory,"end");
    for(unsigned i=0;i<screen_probe_count;++i)
    {screen_probe* p=&screen_probes[i];printf("{\"screen_pc\":%u,\"frames\":%u,\"palette_delta\":%u,\"bright_rgb\":[%u,%u,%u]}\n",p->pc,p->frames,p->delta,p->r,p->g,p->b);}
    if(screen_pause)expect(screen_paused,"active screen-effect pause path was exercised");
    result(status,count);fclose(events);free(diagnostic_state);
    mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);
    return strcmp(status,"passed") && strcmp(status,"captured") ? 1 : 0;
}
#endif
