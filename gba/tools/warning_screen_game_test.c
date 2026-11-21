#define main unused_smoke_main
#include "smoke_test.c"
#undef main
int main(int argc,char** argv)
{
    expect(argc==3,"ROM screenshot_directory");
    unsigned home=elf_symbol("home_runtime"),music=elf_symbol("mmLayerMain");
    expect(home && music,"home and audio runtime profiles");
    core=mCoreFind(argv[1]);expect(core && core->init(core),"mGBA init");mCoreInitConfig(core,0);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");
    core->reset(core);frames(60);
    expect(core->busRead32(core,home)==1,"cold boot shows notice before title");
    expect(!core->busRead8(core,music+8),"notice stays silent");
    screenshot(argv[2],"warning");unsigned notice=frame_hash();
    core->setKeys(core,KEY_A|KEY_B|KEY_START|KEY_SELECT|KEY_L|KEY_R|KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT);frames(90);
    expect(core->busRead32(core,home)==1 && frame_hash()==notice,"notice ignores input without extra menus or prompts");
    core->setKeys(core,KEY_A);wait_for_title();
    expect(core->busRead32(core,home+4)==0,"held A cannot enter start menu after automatic title transition");
    expect(core->busRead8(core,music+8),"title starts gDie after silent notice");screenshot(argv[2],"title-after-warning");
    core->setKeys(core,0);frames(12);press(KEY_A);expect(core->busRead32(core,home+4)==1,"fresh A opens start menu");
    press(KEY_B);frames(12);expect(!core->busRead32(core,home) && core->busRead32(core,home+4)==0,"returning to title does not repeat notice");
    puts("PASS: silent cold-boot notice, exact shifted artwork, ignored keys, automatic title transition and no repeat on return");
    mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(0);mStandardLoggerDeinit(&logger);return 0;
}
