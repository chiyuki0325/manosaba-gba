/* Formal-build navigation through real keys; no debug hotkey or demo entry. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/home_runtime.h"
#include "../src/music_room_runtime.h"
static unsigned home_address,save_address,room_address;
static home_runtime_profile home(void){home_runtime_profile p;for(unsigned i=0;i<sizeof(p)/4;++i)((uint32_t*)&p)[i]=core->busRead32(core,home_address+4*i);return p;}
static void tap(unsigned keys){core->setKeys(core,keys);frames(3);core->setKeys(core,0);frames(100);}
static void options(void){for(int i=0;i<4;++i){tap(KEY_DOWN);}tap(KEY_A);expect(home().page==2 && home().selected==0,"formal options open at dialogue speed");}
static void check_options(void)
{
    for(unsigned i=1;i<=4;++i){tap(KEY_DOWN);expect(home().selected==i%4,"four settings wrap without a debug row");}
    tap(KEY_UP);expect(home().selected==3,"up wraps to branch hints");tap(KEY_A);tap(KEY_START);
    expect(home().page==2 && home().selected==3,"A and Start do not open debug from options");
    tap(KEY_RIGHT);expect(!home().hints,"branch hints setting remains usable");tap(KEY_LEFT);expect(home().hints,"branch hints can be restored");
}
int main(int argc,char** argv)
{
    expect(argc==3,"release ROM screenshot_directory");
    expect(!elf_symbol("debug_hotkey"),"release has no debug hotkey symbol");
    home_address=elf_symbol("home_runtime");save_address=elf_symbol("save_runtime");room_address=elf_symbol("music_room_runtime");expect(home_address && save_address && room_address,"formal runtime profiles");
    core=mCoreFind(argv[1]);expect(core && core->init(core),"mGBA init");mCoreInitConfig(core,0);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");core->reset(core);wait_for_title();
    tap(KEY_START);tap(KEY_B);tap(KEY_L|KEY_R);expect(home().page==0,"title ignores debug/menu shortcuts");
    tap(KEY_A);options();check_options();screenshot(argv[2],"options-release");tap(KEY_B);
    for(int i=0;i<3;++i){tap(KEY_DOWN);}
    tap(KEY_A);frames(120);expect(core->busRead32(core,room_address),"formal music room is retained");tap(KEY_START);expect(core->busRead32(core,room_address),"music room Start cannot open demo");tap(KEY_B);expect(home().page==1 && home().selected==3,"music room returns to main menu");
    tap(KEY_UP);tap(KEY_A);frames(120);tap(KEY_START);tap(KEY_B);expect(home().page==1 && home().selected==2,"gallery returns to main menu without demo");
    tap(KEY_UP);tap(KEY_A);tap(KEY_B);expect(home().page==1 && home().selected==1,"load returns to main menu");
    tap(KEY_UP);tap(KEY_A);frames(240);tap(KEY_START);frames(120);expect(core->busRead32(core,save_address)==2,"new game has normal pause menu");
    for(int i=0;i<3;++i){tap(KEY_DOWN);}tap(KEY_A);expect(home().page==2,"pause settings open formal options");screenshot(argv[2],"pause-options-release");tap(KEY_B);tap(KEY_B);frames(60);expect(core->busRead32(core,save_address)!=2,"settings and pause return to story");
    screenshot(argv[2],"story-release");puts("PASS: no debug symbol; four settings; title, music, gallery, load, new game and pause return paths");
    mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(0);mStandardLoggerDeinit(&logger);return 0;
}
