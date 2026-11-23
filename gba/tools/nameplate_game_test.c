/* Native witness: nameplate text must survive CPU composition and VRAM commit. */
#define main navigation_smoke_main
#include "smoke_test.c"
#undef main
static unsigned name_ink(int x,int y){
    for(int id=0;id<4;++id){unsigned cnt=core->busRead16(core,0x04000008+id*2);
        if(!(core->busRead16(core,0x04000000)&(1u<<(8+id))) || (cnt&128)||(cnt&3))continue;
        unsigned cell=core->busRead16(core,0x06000000+((cnt>>8)&31)*2048+(y/8*32+x/8)*2);
        unsigned packed=core->busRead8(core,0x06000000+((cnt>>2)&3)*16384+(cell&1023)*32+(y%8)*4+(x%8)/2);
        return (packed>>((x&1)*4))&15;
    }return 0;
}
static void check(const char* output,const char* name){
    unsigned white=0,initial=0;
    for(int y=88;y<120;++y)for(int x=8;x<104;++x){unsigned ink=name_ink(x,y);white+=ink==1;initial+=ink==3;}
    fprintf(stderr,"%s white=%u initial=%u\n",name,white,initial);screenshot(output,name);
    expect(white>20 && initial>5,"nameplate retains the white name and colored initial over its neutral diamond");
}
int main(int argc,char** argv){
    expect(argc==3,"nameplate_game_test ROM output");
    core=mCoreFind(argv[1]);expect(core&&core->init(core),"initialize mGBA");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");
    debug_boot();press(KEY_DOWN);press(KEY_DOWN);press(KEY_A);press(KEY_A);frames(200);
    check(argv[2],"nameplate-leia");press(KEY_A);check(argv[2],"nameplate-sherry");
    press(KEY_START);press(KEY_B);check(argv[2],"nameplate-resumed");
    mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);
    puts("PASS: two speakers and pause return retain nameplate text");return 0;
}
