/* Production credits ring, per-frame scroll continuity, input and live music. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/credits_runtime.h"
#include "../src/save_runtime.h"
#include <stddef.h>
#include <mgba/core/blip_buf.h>
static unsigned address,layer,continuity;
static const char* output;
static FILE* recorded_audio;
static credits_runtime_profile metric(void){credits_runtime_profile p;uint32_t* v=(uint32_t*)&p;for(unsigned i=0;i<sizeof(p)/4;++i)v[i]=core->busRead32(core,address+i*4);return p;}
static void run(int count)
{
    while(count--)
    {
        frames(1);
        if(recorded_audio)
        {
            short pcm[2048];int count=blip_read_samples(core->getAudioChannel(core,0),pcm,1024,1);
            blip_read_samples(core->getAudioChannel(core,1),pcm+1,1024,1);
            fwrite(pcm,sizeof(short)*2,count,recorded_audio);
        }
    }
}
static void key(unsigned k){core->setKeys(core,k);run(2);core->setKeys(core,0);run(12);}
static void ready(void){for(int i=0;i<180;++i){run(1);if(metric().ready)return;}expect(0,"credits fill and reveal their ring");}
static unsigned scroll(void){return metric().pixel;}
static unsigned snow_pixels(void)
{
    unsigned count=0;
    for(int y=0;y<160;++y)for(int x=0;x<12;++x)
        count+=!!(pixels[y*240+x]&0xffffffu)+!!(pixels[y*240+239-x]&0xffffffu);
    return count;
}
static void check_frame(void){
    static color_t before[240*160];unsigned y=scroll();
    for(int i=0;i<240*160;++i)before[i]=pixels[i];
    run(1);int dy=(int)scroll()-(int)y;
    if(dy==1 || dy==2){
        for(int row=0;row<160-(int)dy;++row)for(int x=12;x<228;++x)
            if(before[(row+dy)*240+x]!=pixels[row*240+x]){fprintf(stderr,"scroll mismatch act=%u row=%u pixel=%u dy=%d at %d,%d\n",metric().act,metric().row,metric().pixel,dy,x,row);screenshot(output,"continuity-fail");expect(0,"visible rows only translate; no seams or overwritten lines");}
        ++continuity;
    }
    if(dy<0 && dy>=-8){
        for(int row=0;row<160+dy;++row)for(int x=12;x<228;++x)
            expect(before[row*240+x]==pixels[(row-dy)*240+x],"reverse scrolling restores each entering line without seams");
        ++continuity;
    }
    unsigned background=metric().act==1 ? 0 : 0xffffff;
    expect((pixels[0]&0xffffff)==background && (pixels[239]&0xffffff)==background,"Act1 has black margins and Act2 has white margins");
}
static void navigation_guard(void)
{
    const unsigned keys[]={KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,KEY_SELECT,KEY_L,KEY_R,KEY_A};
    unsigned act=metric().act;
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i)
    {unsigned before=metric().pixel;key(keys[i]);expect(metric().active && metric().act==act,"credits ignore directional, shoulder, Select and A input");expect(metric().pixel>before && metric().pixel<=before+8,"ignored input cannot restart, accelerate or reverse credits");}
}
int main(int argc,char** argv){
    expect(argc==5 || argc==8,"ROM output credits_profile music_layer [script_index act save_profile]");output=argv[2];address=strtoul(argv[3],NULL,16);layer=strtoul(argv[4],NULL,16);
    core=mCoreFind(argv[1]);expect(core&&core->init(core),"initialize mGBA");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");core->reset(core);debug_menu_navigate();
    if(argc==8){
        int index=atoi(argv[5]);unsigned act=atoi(argv[6]),save=strtoul(argv[7],NULL,16);
        key(KEY_DOWN);key(KEY_A);
        for(int i=0;i<index/6;++i)key(KEY_RIGHT);
        for(int i=0;i<index%6;++i)key(KEY_DOWN);
        key(KEY_A);unsigned previous_song=0,previous_position=0,entered=0;
        for(int i=0;i<30000;++i){
            previous_song=core->busRead32(core,layer+24);previous_position=core->busRead32(core,layer);
            core->setKeys(core,i%8<2 ? KEY_A : 0);run(1);
            if(metric().active && metric().source){entered=1;break;}
        }
        core->setKeys(core,0);expect(entered && metric().act==act,"formal source reaches its credits opcode");
        expect(core->busRead32(core,layer+24)==previous_song,"formal credits entry keeps the active module");
        expect(core->busRead32(core,layer)>=previous_position,"formal credits entry does not rewind the playing music");
        ready();navigation_guard();check_frame();expect(metric().source,"formal credits keep their source");
        screenshot(output,"formal-entry");
        key(KEY_START);unsigned paused=metric().pixel;run(60);expect(metric().pixel==paused,"formal pause freezes credits");
        key(KEY_START);ready();expect(metric().pixel<=paused+2,"formal resume preserves scroll position");check_frame();
        unsigned reverse=0,snow=0,quiet=0,snow_audio=0;
        unsigned effects=elf_symbol("mm_sfx_bitmask");
        int capture=act==1 && getenv("ENDING_CAPTURE");
        for(int i=0;i<24000 && !metric().done;++i)
        {
            if(metric().phase==2)
            {
                if(capture && !recorded_audio)
                {
                    char path[512];snprintf(path,sizeof path,"%s/reverse-audio.s16",output);recorded_audio=fopen(path,"wb");expect(recorded_audio!=NULL,"open actual ROM audio capture");
                    for(int channel=0;channel<2;++channel){blip_clear(core->getAudioChannel(core,channel));blip_set_rates(core->getAudioChannel(core,channel),16777216,32768);}
                }
                if(capture && !(reverse%2)){char name[48];snprintf(name,sizeof name,"reverse-%03u",reverse/2);screenshot(output,name);}
                if(snow_pixels()){++snow;if(core->busRead32(core,effects))++snow_audio;}else ++quiet;
                ++reverse;
            }
            else if(recorded_audio){fclose(recorded_audio);recorded_audio=NULL;}
            run(1);
        }
        if(act==1)
        {
            expect(reverse>=418 && reverse<=422,"Act1 reverses for seven seconds");
            expect(snow>=110 && snow<=130 && quiet>280,"Act1 reverse has five short snow bursts separated by clear text");
            expect(snow_audio>=110,"snow bursts play through the real Maxmod SFX bus");
            expect(!snow_pixels(),"snow objects disappear when reversing ends");
            printf("Act1 reverse=%u snow=%u clear=%u snow_audio=%u\n",reverse,snow,quiet,snow_audio);
        }
        expect(metric().done,"formal roll completes automatically");
        unsigned peak=metric().max_ticks,missed=metric().missed;
        for(int i=0;i<1200;++i){run(1);if(core->busRead32(core,save+offsetof(save_runtime_profile,script))!=(unsigned)index)break;}
        expect(core->busRead32(core,save+offsetof(save_runtime_profile,script))==(unsigned)index+1,"roll returns through original EndCredits and jumps to following ADV");
        screenshot(output,"formal-continuation");
        printf("Formal Act%u peak=%u missed=%u next_script=%u\n",act,peak,missed,index+1);
        expect(!missed && peak<4389,"formal credits stay in frame budget with their source music");
        puts("PASS: formal credits music inheritance, input guard, pause and original-script continuation");
        mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
    }
    key(KEY_DOWN);key(KEY_DOWN);key(KEY_DOWN);key(KEY_A);ready();
    expect(metric().act==2 && metric().source==0,"standalone ending starts Act2");expect(core->busRead8(core,layer+8),"empty debug music bus starts Song004");
    unsigned music=core->busRead32(core,layer);navigation_guard();
    for(int i=0;i<760;++i)check_frame();
    key(KEY_START);unsigned frozen=metric().pixel;run(90);expect(metric().pixel==frozen,"debug menu freezes scroll");expect(core->busRead32(core,layer)!=music,"menu keeps music advancing");
    key(KEY_START);ready();expect(metric().pixel<=frozen+2,"menu close rebuilds at retained scroll position");
    unsigned slow=0,fast=0,last=0,captured=0;
    for(int i=0;i<24000 && !metric().done;++i){
        credits_runtime_profile p=metric();
        if(p.ready && p.phase==0){if(p.flags&4)++fast;else ++slow;}
        if(p.row>=45 && !captured){screenshot(output,"act2-cast");captured=1;}
        if((p.flags&4) && !last){screenshot(output,"act2-thanks");last=1;}
        if(p.phase==0 || p.phase==2)check_frame();else run(1);
    }
    expect(metric().done && slow>1000 && fast>5000,"Act2 completes staff and accelerated continuous thanks");
    printf("Act2 peak=%u draw_peak=%u missed=%u slow_frames=%u fast_frames=%u continuity=%u\n",metric().max_ticks,metric().max_draw,metric().missed,slow,fast,continuity);
    expect(metric().missed==0 && metric().max_ticks<4389,"credits with real Song004 stay in frame budget");
    expect(continuity>10000,"scroll continuity covers many 128/256px ring seams");
    run(30);expect((pixels[0]&0xffffffu)==0 && (pixels[239]&0xffffffu)==0,"automatic ending exit restores black menu background");screenshot(output,"ending-to-menu");
    key(KEY_A);ready();key(KEY_B);run(30);expect((pixels[0]&0xffffffu)==0,"manual ending exit restores black background");
    key(KEY_UP);key(KEY_A);run(120);expect((pixels[0]&0xffffffu)==0,"trial chapter selector remains black after ending");screenshot(output,"ending-to-trial-selector");
    puts("PASS: automatic credits, ignored direction/shoulder/Select/A input, pause restoration, accelerated thanks and black exit backgrounds");
    mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
