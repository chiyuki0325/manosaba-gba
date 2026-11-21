/* Real intermediate native frames and frozen stage clock while the menu is open. */
#define main navigation_smoke_main
#include "smoke_test.c"
#undef main
static unsigned profile_address;
static unsigned stage_clock(void){return core->busRead32(core,profile_address+12*4);}
static unsigned stage_event(void){return core->busRead32(core,profile_address+5*4);}
static int blur_entries;
static unsigned previous_event;
static void parallel_frames(int count){
 while(count--){
  frames(1);unsigned event=stage_event();
  // Adv04 source lines 82/83/84/88: background, zero-duration blur, actor, printer.
  expect(event<36 || event>38,"submit background, blur and character together without displaying the sharp background first");
  expect(event>=previous_event,"scene events advance once without replay");
  if(event==39 && previous_event!=39)++blur_entries;
  previous_event=event;
 }
}
static uint32_t scene_hash(void){uint32_t hash=2166136261u;for(int i=0;i<240*80;++i)hash=(hash^pixels[i])*16777619u;return hash;}
static void begin_graphics(void){debug_boot();adv04_selector();core->setKeys(core,KEY_A);frames(4);core->setKeys(core,0);}
int main(int argc,char** argv){
 expect(argc==4,"usage: stage_smoke_test ROM screenshot_directory profile_address_hex");
 profile_address=(unsigned)strtoul(argv[3],NULL,16);core=mCoreFind(argv[1]);expect(core&&core->init(core),"initialize mGBA");mCoreInitConfig(core,NULL);mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");
 begin_graphics();
 expect(stage_event()==2,"queue Adv04 background, dissolve and explicit wait together before image loading completes");
 uint32_t previous=0;int changes=0;
 for(int f=0;f<180;++f){frames(1);if(f%8==0 && (core->busRead16(core,0x04000000)&7)==1){uint32_t current=scene_hash();if(previous && current!=previous)++changes;previous=current;}if(f==12)screenshot(argv[2],"adv04-dissolve-start");if(f==36)screenshot(argv[2],"adv04-dissolve-middle");}
 expect(changes>=3,"mask dissolve has real intermediate native frames");frames(240);uint32_t steady=frame_hash();screenshot(argv[2],"adv04-dissolve-complete");
 begin_graphics();frames(16);core->setKeys(core,KEY_START);frames(4);core->setKeys(core,0);frames(30);unsigned frozen=stage_clock();frames(180);
 expect((core->busRead16(core,0x04000000)&7)==0,"Start opens the menu during a transition");expect(stage_clock()==frozen,"menu freezes the stage clock and waits");
 core->setKeys(core,KEY_B);frames(4);core->setKeys(core,0);frames(30);expect(stage_clock()>frozen,"stage clock resumes after B");screenshot(argv[2],"adv04-dissolve-resumed");frames(420);screenshot(argv[2],"adv04-dissolve-resumed-complete");expect(frame_hash()==steady,"resumed transition reaches the same scene and dialogue");
 begin_graphics();frames(420);previous_event=stage_event();
 for(int step=0;step<50 && stage_event()<47;++step){core->setKeys(core,KEY_A);parallel_frames(4);core->setKeys(core,0);parallel_frames(260);}
 expect(stage_event()>=47 && blur_entries==1,"play the blurred introduction once and reach Alisa's dialogue");
 printf("PASS: %d intermediate native scene changes, pause/resume, concurrent loading and one blurred introduction\n",changes);mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
