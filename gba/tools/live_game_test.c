#define main unused_smoke_main
#include "smoke_test.c"
#undef main
static int red_ring(void)
{
    int count=0;
    for(int y=31;y<=39;++y)for(int x=6;x<=14;++x)
    {unsigned c=pixels[y*240+x];count+=(c&255)>220 && ((c>>8)&255)<70 && ((c>>16)&255)<90;}
    return count>=16;
}
int main(int argc,char** argv)
{
    expect(argc==3,"ROM output");
    core=mCoreFind(argv[1]);expect(core&&core->init(core),"initialize mGBA");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");
    debug_boot();press(KEY_DOWN);press(KEY_A);press(KEY_DOWN);press(KEY_A);expect(!red_ring(),"ordinary dialogue has no LIVE marker");
    for(int i=0;i<120 && !red_ring();++i)press(KEY_A);
    expect(red_ring(),"ADV02 Stream displays the red circle");screenshot(argv[2],"adv02-live");
    press(KEY_START);expect(!red_ring(),"pause hides LIVE");press(KEY_B);expect(red_ring(),"return restores LIVE");
    press(KEY_SELECT);expect(!red_ring(),"book hides LIVE");press(KEY_B);expect(red_ring(),"book return restores LIVE");
    for(int i=0;i<30 && red_ring();++i)press(KEY_A);
    expect(!red_ring(),"Normal.Default removes LIVE");frames(240);expect(!red_ring(),"LIVE remains cleared");screenshot(argv[2],"adv02-live-cleared");
    puts("PASS: real LIVE marker, pause/book return and clean Stream exit");
    mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
