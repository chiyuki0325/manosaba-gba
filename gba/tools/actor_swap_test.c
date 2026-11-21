/* Swap a small and large colored actor: final sets fit, overlapping sets do not. */
#define main unused_navigation_main
#include "smoke_test.c"
#undef main
static unsigned profile;
static unsigned metric(int i){return core->busRead32(core,profile+i*4);}
static void tap(unsigned k){core->setKeys(core,k);frames(2);core->setKeys(core,0);frames(24);}
static int obj_bytes(void){
    static const int widths[3][4]={{8,16,32,64},{16,32,32,64},{8,8,16,32}};
    static const int heights[3][4]={{8,16,32,64},{8,8,16,32},{16,32,32,64}};
    unsigned char seen[1024]={0};
    for(int i=0;i<128;++i){
        unsigned a=core->busRead16(core,0x07000000+i*8),b=core->busRead16(core,0x07000002+i*8),c=core->busRead16(core,0x07000004+i*8);
        if((!(a&256) && (a&512)) || (a>>14)==3)continue;
        int count=widths[a>>14][b>>14]*heights[a>>14][b>>14]/64;
        for(int t=0;t<count;++t)seen[(c+t)&1023]=1;
    }
    int count=0;for(int t=0;t<1024;++t)count+=seen[t];return count*32;
}
static void pose(unsigned entity,int image){
    core->busWrite16(core,entity+48,image);core->busWrite32(core,entity,120*256);
    core->busWrite32(core,entity+4,(uint32_t)(-80*256));core->busWrite32(core,entity+8,256);
    core->busWrite32(core,entity+12,0);core->busWrite32(core,entity+16,256);core->busWrite32(core,entity+20,256);
}
int main(int argc,char** argv){
    expect(argc==5 || argc==6,"ROM output profile banks [skip-cancel]");profile=strtoul(argv[3],NULL,16);unsigned banks=strtoul(argv[4],NULL,16);
    core=mCoreFind(argv[1]);expect(core&&core->init(core),"initialize mGBA");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");debug_boot();
    tap(KEY_DOWN);tap(KEY_A);for(int i=0;i<21/6;++i)tap(KEY_RIGHT);for(int i=0;i<21%6;++i)tap(KEY_DOWN);tap(KEY_A);
    unsigned cooldown=140;
    for(unsigned f=0;f<30000 && !(metric(4)==21 && metric(5)>=39);++f){
        if(!cooldown){core->setKeys(core,KEY_A);cooldown=166;}else if(cooldown==164)core->setKeys(core,0);
        --cooldown;frames(1);
    }
    core->setKeys(core,0);frames(240);expect(metric(4)==21 && metric(5)==39,"reach stationary cast");
    unsigned commands=core->busRead32(core,banks+21*24+4),keys=core->busRead32(core,banks+21*24+8),player=0;
    for(unsigned a=0x02000000;a<0x02040000-960;a+=4)
        if(core->busRead32(core,a)==keys && core->busRead32(core,a+4)==commands){expect(!player,"unique stage player");player=a;}
    expect(player!=0,"locate production stage state");unsigned a=player+8,b=a+60;
    for(int i=0;i<3;++i)core->busWrite32(core,a+i*60+16,0);
    frames(240);pose(a,65);pose(b,216);frames(240);
    expect((core->busRead16(core,0x04000000)&7)==1,"initial resident set fits");
    color_t original[240*104];memcpy(original,pixels,sizeof(original));int initial=obj_bytes();screenshot(argv[2],"swap-before");
    // Reverse a request while outgoing blocks are only partially retired.
    if(argc==5){
        pose(a,216);pose(b,65);frames(2);pose(a,65);pose(b,216);frames(240);
        expect((core->busRead16(core,0x04000000)&7)==1,"canceling a partial retirement recovers");
        expect(!memcmp(original,pixels,sizeof(original)),"canceled swap restores original pixels");
    }
    for(int test=0;test<2;++test){
        pose(a,test%2 ? 65 : 216);pose(b,test%2 ? 216 : 65);
        for(int f=0;f<240;++f){frames(1);expect((core->busRead16(core,0x04000000)&7)==1,"no VRAM assertion during actor swap");}
        expect(!metric(11),"swap uploads finish");
    }
    expect(!memcmp(original,pixels,sizeof(original)),"repeated swaps restore original pixels");screenshot(argv[2],"swap-restored");
    printf("PASS: simultaneous small/large swaps and mid-retirement retarget; initial OBJ bytes=%d\n",initial);
    mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
