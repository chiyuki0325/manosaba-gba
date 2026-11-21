/* Real-input reenactment patrol with sparse OBJ checks and pose screenshots. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/trial_runtime.h"
#include "../src/save_runtime.h"
#include <mgba/internal/gba/gba.h>
static unsigned profile,assets,palette,entry,first,last,total,peak,missed,save_address;
static unsigned seen[56],dialogues[56],poses,objects_peak,tiles_peak,overlay_done,cycles_peak,loop_checks[56];
static unsigned curtain_checks,curtain_pending;
static unsigned loop_samples[56];
static int ignore_captures;
static unsigned stable_key=~0u,stable_frames;
static FILE* captures,*loop_captures;
static const char* directory;
static trial_runtime_profile metric(void)
{
    trial_runtime_profile p;uint32_t* v=(uint32_t*)&p;
    for(unsigned i=0;i<sizeof(p)/4;++i)v[i]=core->busRead32(core,profile+i*4);
    return p;
}
static save_runtime_profile save_state(void)
{
    save_runtime_profile p;uint32_t* v=(uint32_t*)&p;
    for(unsigned i=0;i<sizeof(p)/4;++i)v[i]=core->busRead32(core,save_address+i*4);
    return p;
}
static int uncovered(void)
{
    return !(core->busRead16(core,0x04000000)&0x6000);
}
static int palette_ready(void)
{
    for(int bank=0;bank<16;++bank)
    {
        int same=1;
        for(int i=0;i<16;++i)if(core->busRead16(core,0x05000000+bank*32+i*2)!=core->busRead16(core,palette+i*2)){same=0;break;}
        if(same)return 1;
    }
    return 0;
}
static void sparse(void)
{
    static const int widths[3][4]={{8,16,32,64},{16,32,32,64},{8,8,16,32}};
    static const int heights[3][4]={{8,16,32,64},{8,8,16,32},{16,32,32,64}};
    unsigned objects=0,tiles=0,line_cycles[160]={0},last_index=0;
    for(int obj=0;obj<128;++obj)
    {
        unsigned at=0x07000000+obj*8,a0=core->busRead16(core,at),a1=core->busRead16(core,at+2),a2=core->busRead16(core,at+4);
        if(!(a0&256) && (a0&512))continue;
        expect(!(a0&256),"reenact OBJ uses ordinary sprites");expect(!(a0&8192),"reenact OBJ uses 4bpp");
        unsigned shape=a0>>14,size=a1>>14;expect(shape<3,"valid OBJ shape");
        int y=(int)(a0&255);if(y>=160 && y<256 && y+heights[shape][size]<=256)continue;
        for(int row=0;row<160;++row)if(((row-y)&255)<heights[shape][size])line_cycles[row]+=(unsigned)widths[shape][size];
        last_index=(unsigned)obj;
        int count=widths[shape][size]*heights[shape][size]/64;unsigned start=(a2&1023)*32;
        for(int t=0;t<count;++t)
        {
            unsigned occupied=0;for(int row=0;row<8;++row)occupied|=core->busRead32(core,0x06010000+start+t*32+row*4);
            expect(occupied!=0,"no completely empty 8x8 tile in any allocated OBJ rectangle");
        }
        ++objects;tiles+=(unsigned)count;
    }
    expect(objects<=128 && tiles<=1024,"native OBJ budgets");
    unsigned limit=(core->busRead16(core,0x04000000)&32) ? 954 : 1210;
    for(int row=0;row<160;++row)
    {
        unsigned cycles=line_cycles[row]+last_index*2;
        expect(cycles<=limit,"OBJ scanline cycle budget");if(cycles>cycles_peak)cycles_peak=cycles;
    }
    if(objects>objects_peak)objects_peak=objects;
    if(tiles>tiles_peak)tiles_peak=tiles;
}
static void step(int count)
{
    while(count--)
    {
        frames(1);++total;trial_runtime_profile p=metric();
        if(p.vm_script!=entry)continue;
        if(p.core_ticks>peak)peak=p.core_ticks;
        if(p.missed>missed)missed=p.missed;
        if(!ignore_captures && p.scene_ready && (core->busRead16(core,0x04000000)&0x6000)==0x6000)
        {
            // WINxH is write-only on the GBA; inspect mGBA's register shadow.
            struct GBA* gba=core->board;
            unsigned left=gba->memory.io[0x40/2],right=gba->memory.io[0x42/2];
            int aligned=(left>>8)==0 && (right&255)==240 && (left&255)+(right>>8)==240;
            if(!aligned && ++curtain_pending>3)
            {screenshot(directory,"curtain-failure");fprintf(stderr,"curtains frame=%u left=%04x right=%04x\n",total,left,right);expect(0,"two curtains split at the center and move symmetrically");}
            if(aligned)curtain_pending=0;
            if(aligned && (left&255)>0 && (left&255)<120)
            {
                ++curtain_checks;
                if(curtain_checks==12)screenshot(directory,"curtain-opening");
            }
        }
        else curtain_pending=0;
        if(ignore_captures || !p.scene_ready || !p.background || p.background-1<first || p.background-1>last || !uncovered() || !palette_ready())
        {stable_frames=0;stable_key=~0u;continue;}
        unsigned id=p.background-1,duration=core->busRead16(core,assets+id*16+8),count=core->busRead16(core,assets+id*16+10);
        unsigned loop=core->busRead8(core,assets+id*16+15),phase=loop && duration ? p.movie_frame%duration : p.movie_frame;
        unsigned pose=duration ? (phase*(count-1)+(loop ? duration/2 : 0))/duration : 0;if(pose>=count)pose=count-1;
        if(loop && duration && p.movie_frame>=duration*2)loop_checks[id]=1;
        unsigned key=id*16+pose;
        if(key!=stable_key){stable_key=key;stable_frames=0;}
        if(++stable_frames<3)continue; // Observe pixels after the queued VBlank/OAM update.
        if(loop && duration && p.movie_frame>=duration*2)
        {
            unsigned sample=phase*4/duration;
            if(!(loop_samples[id]&(1u<<sample)))
            {
                char name[64];snprintf(name,sizeof(name),"loop-%02u-sample-%u",id,sample);screenshot(directory,name);
                fprintf(loop_captures,"%u %u %u %u\n",id,pose,p.movie_frame,sample);fflush(loop_captures);loop_samples[id]|=1u<<sample;
            }
        }
        if(seen[id]&(1u<<pose))continue;
        sparse();seen[id]|=1u<<pose;++poses;
        char name[64];snprintf(name,sizeof(name),"scene-%02u-pose-%u",id,pose);screenshot(directory,name);
        fprintf(captures,"%u %u %u %u\n",id,pose,p.movie_frame,total);fflush(captures);
    }
}
static void tap(unsigned key){core->setKeys(core,key);step(2);core->setKeys(core,0);step(22);}
static void wait_ui(int view)
{
    for(int n=0;n<2400;++n)
    {step(1);save_runtime_profile p=save_state();if((int)p.view==view && !p.busy && p.paint==6){step(4);return;}}
    screenshot(directory,"save-timeout");expect(0,"save UI becomes ready");
}
int main(int argc,char** argv)
{
    expect(argc==12,"ROM output trial assets palette offset entry first last overlay save");
    directory=argv[2];profile=strtoul(argv[3],NULL,16);assets=strtoul(argv[4],NULL,16);palette=strtoul(argv[5],NULL,16);
    unsigned offset=(unsigned)atoi(argv[6]);entry=(unsigned)atoi(argv[7]);first=(unsigned)atoi(argv[8]);last=(unsigned)atoi(argv[9]);int overlay=atoi(argv[10]);
    save_address=strtoul(argv[11],NULL,16);
    char path[512];snprintf(path,sizeof(path),"%s/captures.txt",directory);captures=fopen(path,"w");expect(captures!=NULL,"open capture log");
    snprintf(path,sizeof(path),"%s/loop-captures.txt",directory);loop_captures=fopen(path,"w");expect(loop_captures!=NULL,"open loop capture log");
    core=mCoreFind(argv[1]);expect(core && core->init(core),"init mGBA");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");core->reset(core);debug_menu_navigate();
    tap(KEY_DOWN);tap(KEY_DOWN);tap(KEY_A);step(80);
    for(unsigned i=0;i<offset/6;++i)tap(KEY_RIGHT);
    for(unsigned i=0;i<offset%6;++i)tap(KEY_DOWN);
    tap(KEY_A);
    unsigned entered=0,pulse=0;
    while(total<18000)
    {
        trial_runtime_profile p=metric();if(p.vm_script==entry)entered=1;
        if(entered && p.vm_script!=entry)break;
        if(overlay && !overlay_done && poses && p.vm_state==1 && p.interaction_ready &&
           p.movie_frame>=core->busRead16(core,assets+(p.background-1)*16+8))
        {
            ignore_captures=1;
            core->setKeys(core,0);tap(KEY_SELECT);step(120);tap(KEY_B);step(160);
            tap(KEY_L);step(100);tap(KEY_B);step(160);
            unsigned scene=p.background,pc=metric().pc;
            tap(KEY_START);wait_ui(2);tap(KEY_DOWN);tap(KEY_A);wait_ui(0);
            unsigned slot=save_state().selected;tap(KEY_A);wait_ui(0);
            step(30);screenshot(directory,"reenact-save");
            tap(KEY_R);wait_ui(1);
            for(int n=0;n<13 && save_state().selected!=slot+1;++n){tap(KEY_DOWN);wait_ui(1);}
            expect(save_state().selected==slot+1,"select the saved reenactment slot");
            tap(KEY_A);wait_ui(1);tap(KEY_RIGHT);tap(KEY_A);
            int loaded=0;
            for(int n=0;n<2400;++n){step(1);if((int)save_state().view==-1 && !save_state().restoring){loaded=1;break;}}
            expect(loaded,"saved reenactment rebuilds its graphics");step(180);
            expect(metric().vm_script==entry && metric().background==scene && metric().pc==pc,"load preserves scene and dialogue position");
            overlay_done=1;
            ignore_captures=0;
        }
        unsigned key=0;if(pulse)--pulse;
        unsigned id=p.background ? p.background-1 : 99,duration=id<56 ? core->busRead16(core,assets+id*16+8) : 0;
        if(id>=first && id<=last && !dialogues[id] && p.vm_state==1 && p.interaction_ready && uncovered())
        {
            dialogues[id]=1;core->setKeys(core,0);step(160);
            char name[64];snprintf(name,sizeof(name),"scene-%02u-dialogue",id);screenshot(directory,name);
            p=metric();
        }
        unsigned loop=id<56 ? core->busRead8(core,assets+id*16+15) : 0;
        if(p.interaction_ready && (p.vm_state==1 || p.vm_state==3) && !pulse && p.movie_frame>=duration*(loop ? 3 : 1))
        {key=KEY_A;pulse=24;}
        core->setKeys(core,key);step(1);
    }
    unsigned scenes=0;for(unsigned i=first;i<=last;++i)scenes+=seen[i]!=0;
    if(total==18000)screenshot(directory,"failure");
    expect(total<18000,"reenact script completes");expect(scenes==last-first+1,"every scene captured");
    expect(curtain_checks>0,"horizontal curtain animation was observed");
    unsigned loops=0;for(unsigned i=first;i<=last;++i)if(core->busRead8(core,assets+i*16+15) && core->busRead16(core,assets+i*16+8))
    {expect(loop_checks[i] && loop_samples[i]==15,"looping timeline renders all four sampled phases after two complete cycles");++loops;}
    printf("{\"passed\":true,\"scenes\":%u,\"poses\":%u,\"frames\":%u,\"peak_ticks\":%u,\"missed\":%u,\"objects\":%u,\"obj_bytes\":%u,\"scanline_cycles\":%u,\"overlay\":%u,\"loop_scenes\":%u,\"curtain_frames\":%u}\n",scenes,poses,total,peak,missed,objects_peak,tiles_peak*32,cycles_peak,overlay_done,loops,curtain_checks);
    fclose(captures);fclose(loop_captures);mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
