/* Production L overlay, shared reader, cache restoration and turbo-A pacing. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/dialogue_history_runtime.h"
#include "../src/trial_runtime.h"
#include "../src/graphics_profile.h"
static unsigned history_address,trial_address,graphics_address,history_peak,history_frames;
static const char* directory;
static dialogue_history_profile history(void){
    dialogue_history_profile p;uint32_t* words=(uint32_t*)&p;
    for(unsigned i=0;i<sizeof(p)/4;++i)words[i]=core->busRead32(core,history_address+i*4);
    return p;
}
static trial_runtime_profile trial(void){
    trial_runtime_profile p;uint32_t* words=(uint32_t*)&p;
    for(unsigned i=0;i<sizeof(p)/4;++i)words[i]=core->busRead32(core,trial_address+i*4);
    return p;
}
static void measured_frames(int count){
    while(count--){
        unsigned open=history().open;frames(1);
        if(open || history().open){
            struct graphics_profile p;uint32_t* words=(uint32_t*)&p;
            for(unsigned i=0;i<sizeof(p)/4;++i)words[i]=core->busRead32(core,graphics_address+i*4);
            if(p.core_ticks>history_peak){history_peak=p.core_ticks;if(history_peak>3500)fprintf(stderr,"history peak=%u ui=%u update=%u open=%u->%u ready=%u count=%u first=%u\n",history_peak,p.ui_ticks,p.update_ticks,open,history().open,history().ready,history().count,history().first);}
            ++history_frames;
        }
    }
}
#define frames measured_frames
static void tap(unsigned key){core->setKeys(core,key);frames(4);core->setKeys(core,0);frames(44);}
static void ready(void){for(int n=0;n<240;++n){frames(1);if(history().ready){frames(2);return;}}screenshot(directory,"history-timeout");expect(0,"history becomes ready");}
static void layers(void){
    unsigned display=core->busRead16(core,0x04000000),eight=0,four=0;
    for(int i=0;i<4;++i)if(display&(256u<<i)){
        if(core->busRead16(core,0x04000008+i*2)&128)++eight;else ++four;
    }
    expect((display&7)==0 && eight==1 && four==1,"history uses choice BG and a separate hardware text layer");
}
static void ema_color(void){
    unsigned display=core->busRead16(core,0x04000000);
    for(int i=0;i<4;++i)if(display&(256u<<i)){
        unsigned ctrl=core->busRead16(core,0x04000008+i*2);if(ctrl&128)continue;
        unsigned cell=core->busRead16(core,0x06000000+((ctrl>>8)&31)*2048);
        expect(core->busRead16(core,0x05000000+(cell>>12)*32+4)==(31u|(18u<<5)|(21u<<10)),"history Ema heading uses her existing nameplate color");
    }
}
int main(int argc,char** argv){
    expect(argc==6,"ROM directory history_address trial_address graphics_address");directory=argv[2];
    history_address=strtoul(argv[3],NULL,16);trial_address=strtoul(argv[4],NULL,16);
    graphics_address=strtoul(argv[5],NULL,16);
    core=mCoreFind(argv[1]);expect(core&&core->init(core),"initialize mGBA");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");debug_boot();
    adv04_selector();tap(KEY_A);frames(220);expect(history().count>0,"story records spoken dialogue");
    uint32_t before=frame_hash();unsigned first_count=history().count;
    tap(KEY_L);ready();layers();ema_color();expect(history().open && history().count==first_count,"L opens history without advancing playback");screenshot(directory,"history-story-first");
    uint32_t page=frame_hash();tap(KEY_A);expect(frame_hash()==page,"A does not replay or advance the underlying story");
    tap(KEY_B);frames(220);expect(!history().open && frame_hash()==before,"B restores story pixels after sharing its script cache");
    core->setKeys(core,KEY_L);frames(4);core->setKeys(core,0);frames(2);
    expect(history().open && !history().ready,"early-close check interrupts an active history upload");
    core->setKeys(core,KEY_B);frames(4);core->setKeys(core,0);frames(220);
    expect(!history().open && history().count==first_count,"history can close during its initial upload without losing playback");
    unsigned old=history().count,last=0,advanced=0;
    for(unsigned n=0;n<700;++n){
        core->setKeys(core,n%4<2 ? KEY_A : 0);frames(1);
        unsigned count=history().count;
        if(count>old){if(advanced)expect(n-last>=18,"turbo A cannot advance two sentences inside the 18-frame guard");last=n;old=count;++advanced;}
    }
    core->setKeys(core,0);frames(220);expect(advanced>=3,"turbo pacing test really advances dialogue");
    tap(KEY_L);ready();expect(history().rows>8 && history().first>0,"history opens at its newest lines");screenshot(directory,"history-story-bottom");
    uint32_t bottom_pixels=frame_hash();tap(KEY_LEFT);ready();tap(KEY_RIGHT);ready();expect(frame_hash()==bottom_pixels,"page turns restore text and per-character colors exactly");
    unsigned bottom=history().first;core->setKeys(core,KEY_UP);frames(60);core->setKeys(core,0);ready();
    expect(history().first<bottom,"held Up scrolls through earlier dialogue");screenshot(directory,"history-story-scroll");
    tap(KEY_LEFT);ready();tap(KEY_RIGHT);ready();
    unsigned count=history().count;tap(KEY_START);tap(KEY_START);ready();expect(history().count==count,"pause restores history without duplicating lines");
    tap(KEY_L);frames(180);expect(!history().open,"L also closes history");
    for(int n=0;n<6 && history().count==count;++n){tap(KEY_A);}
    expect(history().count>count,"playback continues after history reads old compressed blocks");
    unsigned previous_script=history().script;
    chapters_hotkey();tap(KEY_START);frames(80);tap(KEY_DOWN);tap(KEY_A);frames(200);
    expect(history().script!=previous_script && history().count<=1,"selecting another script clears history");
    debug_boot();tap(KEY_DOWN);tap(KEY_DOWN);tap(KEY_A);tap(KEY_DOWN);tap(KEY_A);frames(140);
    for(int n=0;n<100 && !(trial().state==6 && trial().interaction_ready);++n)tap(KEY_A);
    expect(trial().state==6,"formal trial reaches timed discussion");
    tap(KEY_L);ready();layers();unsigned line=history().line_left,total=history().total_left;count=history().count;
    frames(300);expect(history().line_left==line && history().total_left==total && history().count==count,"history freezes discussion clocks and its dialogue index");screenshot(directory,"history-trial");
    tap(KEY_B);frames(80);expect(!history().open && history().total_left<total,"closing history resumes the discussion clock");
    printf("History frames=%u peak_ticks=%u\n",history_frames,history_peak);expect(history_peak<4389,"history overlay frame budget");
    puts("PASS: shared history/reader, 18-frame turbo guard, old-block cache reads, pause, cross-script reset and frozen trial clocks");
    mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
