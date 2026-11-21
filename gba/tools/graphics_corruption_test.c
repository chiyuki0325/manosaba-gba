/* Walk the reported ADV35/36 scenes with production input and keep evidence. */
#define main unused_navigation_main
#include "smoke_test.c"
#undef main
static unsigned profile;
static unsigned metric(int i){return core->busRead32(core,profile+4*i);}
static unsigned peak_ticks,peak_graphics,samples;
static void measured_frames(int count){
    while(count--){frames(1);if((core->busRead16(core,0x04000000)&7)!=1)continue;
        unsigned ticks=metric(6),graphics=metric(0);if(ticks>peak_ticks)peak_ticks=ticks;if(graphics>peak_graphics)peak_graphics=graphics;++samples;}
}
#define frames measured_frames
static void tap(unsigned k){core->setKeys(core,k);frames(2);core->setKeys(core,0);frames(24);}
int main(int argc,char** argv){
    expect(argc==4,"ROM output graphics_profile");profile=strtoul(argv[3],NULL,16);
    core=mCoreFind(argv[1]);expect(core&&core->init(core),"initialize mGBA");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");debug_boot();
    tap(KEY_DOWN);tap(KEY_A);for(int i=0;i<34/6;++i)tap(KEY_RIGHT);for(int i=0;i<34%6;++i)tap(KEY_DOWN);tap(KEY_A);
    unsigned previous=~0u;
    for(int step=0;step<440;++step){
        frames(140);
        unsigned bank=metric(4),event=metric(5);
        if(event!=previous){
            printf("step=%d bank=%u event=%u loading=%u clock=%u\n",step,bank,event,metric(11),metric(12));fflush(stdout);
            char name[80];snprintf(name,sizeof(name),"bank-%u-event-%03u-step-%03d",bank,event,step);screenshot(argv[2],name);previous=event;
        }
        unsigned red=0,blue=0,black=0;
        for(int i=0;i<240*40;++i){
            unsigned r=pixels[i]&255,g=(pixels[i]>>8)&255,b=(pixels[i]>>16)&255;
            red+=r>180 && g<90 && b<90;blue+=b>180 && r<100 && g<100;black+=!(r|g|b);
        }
        if(red>100 && blue>80 && black>8500){screenshot(argv[2],"allocator-failure");expect(0,"ADV35/36 must not exhaust sprite VRAM");}
        if(bank>35 && bank<230)break;
        tap(KEY_A);
    }
    puts("PASS: reported ADV35/36 flow has no allocator failure");
    printf("Route: frames=%u peak_ticks=%u graphics_peak=%u missed=%u\n",samples,peak_ticks,peak_graphics,metric(2));
    mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
