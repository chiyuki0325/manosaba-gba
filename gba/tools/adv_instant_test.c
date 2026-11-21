#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/arm/arm.h>
static unsigned profile;
static unsigned metric(int i){return core->busRead32(core,profile+i*4);}
static int assertion(void){unsigned red=0,black=0;for(int i=0;i<240*160;++i){unsigned c=pixels[i]&0xffffff;black+=c==0;red+=(i<240*45 && (c&255)>200 && ((c>>8)&255)<90 && ((c>>16)&255)<90);}return red>20 && black>31000;}
int main(int argc,char** argv){
 expect(argc==5,"ROM output profile A_period");profile=strtoul(argv[3],0,16);unsigned period=atoi(argv[4]);
 core=mCoreFind(argv[1]);expect(core&&core->init(core),"mGBA init");mCoreInitConfig(core,0);mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");core->reset(core);wait_for_title();
 press(KEY_A);for(int i=0;i<4;++i)press(KEY_DOWN);press(KEY_A);press(KEY_RIGHT);press(KEY_RIGHT);
 unsigned home=elf_symbol("home_runtime");expect(core->busRead32(core,home+6*4)==2,"instant text enabled through settings");
 debug_hotkey();press(KEY_START);adv04_selector();core->setKeys(core,KEY_A);frames(2);core->setKeys(core,0);frames(180);
 unsigned last=~0u,total=0;
 for(;total<40000 && metric(4)==3;++total){core->setKeys(core,total%period<2 ? KEY_A : 0);frames(1);unsigned event=metric(5);if(last!=event){fprintf(stderr,"frame=%u bank=%u event=%u loading=%u clock=%u\n",total,metric(4),event,metric(11),metric(12));last=event;}
  if(assertion()){
   frames(10);screenshot(argv[2],"assertion");struct GBA* gba=core->board;
   fprintf(stderr,"ASSERT frame=%u event=%u bank=%u\n",total,event,metric(4));for(int i=0;i<16;++i)fprintf(stderr,"r%d=%08x ",i,(unsigned)gba->cpu->gprs[i]);fputc('\n',stderr);
   for(unsigned at=0x03007500;at<0x03007f00;at+=4){unsigned v=core->busRead32(core,at);if(v>=0x08000000 && v<0x0a000000)fprintf(stderr,"stack %08x %08x\n",at,v);}
   return 1;
  }
 }
 core->setKeys(core,0);frames(180);screenshot(argv[2],"next-chapter");expect(metric(4)!=3,"instant rapid A completes Adv04");printf("PASS: Adv04 instant text with A period=%u; %u frames\n",period,total);mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
