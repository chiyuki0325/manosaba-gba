/* Optional original line breaks: real settings, Flash persistence and the opening sentence. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/home_runtime.h"
#include "../src/save_runtime.h"
static unsigned home_address,save_address;
static home_runtime_profile home(void){home_runtime_profile p;for(unsigned i=0;i<sizeof(p)/4;++i)((uint32_t*)&p)[i]=core->busRead32(core,home_address+4*i);return p;}
static void tap(unsigned keys){core->setKeys(core,keys);frames(3);core->setKeys(core,0);frames(120);}
static unsigned pc(void){return core->busRead32(core,save_address+12*4);}
static void options(void){for(int i=0;i<4;++i)tap(KEY_DOWN);tap(KEY_A);expect(home().page==2,"settings page opens");}
int main(int argc,char** argv)
{
 expect(argc==3,"ROM and output directory");home_address=elf_symbol("home_runtime");save_address=elf_symbol("save_runtime");
 core=mCoreFind(argv[1]);expect(core && core->init(core),"mGBA init");mCoreInitConfig(core,0);
 mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
 struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");core->reset(core);wait_for_title();
 tap(KEY_A);options();expect(home().breaks,"source wrapping defaults on");tap(KEY_RIGHT);tap(KEY_RIGHT);expect(home().speed==2,"instant speed for page observations");
 for(int i=0;i<3;++i)tap(KEY_DOWN);expect(home().selected==3,"line breaks follow music and SFX");screenshot(argv[2],"options-breaks-on");
 tap(KEY_RIGHT);expect(home().confirm==2 && home().breaks,"disabling waits for explicit confirmation");screenshot(argv[2],"line-break-warning");
 tap(KEY_A);expect(!home().confirm && home().breaks,"warning defaults to cancel");tap(KEY_RIGHT);tap(KEY_RIGHT);tap(KEY_A);expect(!home().breaks && !home().confirm,"confirm disables only source soft breaks");
 tap(KEY_B);frames(180);core->reset(core);wait_for_title();tap(KEY_A);options();expect(!home().breaks && home().speed==2,"global Flash retains wrapping and speed");
 tap(KEY_B);expect(home().page==1 && home().selected==0,"settings returns to the new-game row");tap(KEY_A);frames(240);expect(core->busRead32(core,save_address)==~0u && pc()>0,"new game really enters dialogue, not the load menu");
 tap(KEY_A);tap(KEY_A);unsigned sentence=pc();fprintf(stderr,"sentence pc=%u\n",sentence);screenshot(argv[2],"sentence-folded");
 tap(KEY_START);for(int i=0;i<3;++i)tap(KEY_DOWN);tap(KEY_A);for(int i=0;i<3;++i)tap(KEY_DOWN);tap(KEY_LEFT);expect(home().breaks && !home().confirm,"restoring original breaks needs no warning");tap(KEY_B);tap(KEY_B);frames(180);
 expect(pc()==sentence,"changing layout keeps the current VM sentence");screenshot(argv[2],"sentence-original");tap(KEY_A);expect(pc()==sentence,"original layout still has the final short page");screenshot(argv[2],"sentence-original-last-page");tap(KEY_A);expect(pc()!=sentence,"one more page action advances exactly once");
 puts("PASS: warning/cancel/confirm, global Flash, pause return, folded two-line sentence and original final page");
 mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(0);mStandardLoggerDeinit(&logger);return 0;
}
