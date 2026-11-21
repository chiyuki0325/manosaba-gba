/* Two targeted production scenes; verify upload compositing and clean restoration. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/trial_runtime.h"
#include "../src/witch_book_runtime.h"
#include <string.h>
static unsigned toast_address,canvas_address,trial_address,book_address;
typedef struct {unsigned script,pc,state,action,arg,kind;} checkpoint;
static checkpoint points[256];
static trial_runtime_profile trial(void)
{
    trial_runtime_profile p;uint32_t* v=(uint32_t*)&p;
    for(unsigned i=0;i<sizeof(p)/4;++i)v[i]=core->busRead32(core,trial_address+i*4);
    return p;
}
static witch_book_profile book(void)
{
    witch_book_profile p;uint32_t* v=(uint32_t*)&p;
    for(unsigned i=0;i<sizeof(p)/4;++i)v[i]=core->busRead32(core,book_address+i*4);
    return p;
}
static void tap(unsigned key){core->setKeys(core,key);frames(2);core->setKeys(core,0);frames(22);}
static unsigned tile_address(int tile)
{
    unsigned display=core->busRead16(core,0x04000000);
    for(int bg=0;bg<4;++bg)
    {
        unsigned ctrl=core->busRead16(core,0x04000008+bg*2);
        if(!(display&(0x100u<<bg)) || (ctrl&3) || (ctrl&128))continue;
        unsigned cell=core->busRead16(core,0x06000000+((ctrl>>8)&31)*2048+(tile/30*32+tile%30)*2);
        return 0x06000000+((ctrl>>2)&3)*16384+(cell&1023)*32;
    }
    return 0;
}
static int visible(int id)
{
    for(int t=0;t<36;++t)
    {
        int logical=(3+t/12)*30+t%12;unsigned at=tile_address(logical);if(!at)return 0;
        for(int row=0;row<8;++row)
        {
            unsigned value=core->busRead32(core,toast_address+(id-1)*1152+t*32+row*4);
            unsigned occupied=(((value&0x77777777u)+0x77777777u)|value)&0x88888888u;
            if((core->busRead32(core,at+row*4)&((occupied>>3)*15))!=value)return 0;
        }
    }
    return 1;
}
static void restored(void)
{
    for(int t=0;t<36;++t)
    {
        int logical=(3+t/12)*30+t%12;unsigned at=tile_address(logical);expect(at!=0,"UI layer exists");
        for(int row=0;row<8;++row)
            expect(core->busRead32(core,at+row*4)==core->busRead32(core,canvas_address+logical*32+row*4),"toast clears to exact underlying canvas");
    }
}
static int matches(checkpoint s,trial_runtime_profile p){return s.script==p.vm_script && s.pc==p.pc && s.state==p.vm_state;}
int main(int argc,char** argv)
{
    expect(argc==8,"ROM output toast_tiles canvas_tiles trial book plan");
    toast_address=strtoul(argv[3],NULL,16);canvas_address=strtoul(argv[4],NULL,16);
    trial_address=strtoul(argv[5],NULL,16);book_address=strtoul(argv[6],NULL,16);
    FILE* plan=fopen(argv[7],"r");expect(plan!=NULL,"open route");unsigned offset,count;
    expect(fscanf(plan,"%u %u",&offset,&count)==2 && count<256,"read route header");
    for(unsigned i=0;i<=count;++i)expect(fscanf(plan,"%u %u %u %u %u %u",&points[i].script,&points[i].pc,&points[i].state,&points[i].action,&points[i].arg,&points[i].kind)==6,"read route");
    fclose(plan);
    core=mCoreFind(argv[1]);expect(core && core->init(core),"init mGBA");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");debug_boot();
    tap(KEY_DOWN);tap(KEY_A);tap(KEY_RIGHT);tap(KEY_A);
    unsigned n;
    for(n=0;n<6000 && !visible(1);++n){core->setKeys(core,n%28<2 ? KEY_A : 0);frames(1);}
    if(n==6000)screenshot(argv[2],"failure-adv");
    expect(n<6000,"ADV07 executes its real book-update toast");core->setKeys(core,0);frames(2);screenshot(argv[2],"toast-book");
    frames(180);expect(!visible(1),"book toast expires");restored();screenshot(argv[2],"toast-book-cleared");
    // Retrigger using a fresh chapter, then open the book while the prompt is visible.
    debug_boot();tap(KEY_DOWN);tap(KEY_A);tap(KEY_RIGHT);tap(KEY_A);
    for(n=0;n<6000 && !visible(1);++n){core->setKeys(core,n%28<2 ? KEY_A : 0);frames(1);}
    expect(n<6000,"retrigger book toast");core->setKeys(core,0);frames(1);tap(KEY_SELECT);frames(180);
    expect(book().open && !visible(1),"book overlay hides the toast");tap(KEY_B);frames(280);restored();
    debug_boot();tap(KEY_DOWN);tap(KEY_DOWN);tap(KEY_A);
    for(unsigned i=0;i<offset/6;++i)tap(KEY_RIGHT);
    for(unsigned i=0;i<offset%6;++i)tap(KEY_DOWN);
    tap(KEY_A);
    unsigned completed=0,pulse=0;
    for(n=0;n<18000 && !visible(2);++n)
    {
        trial_runtime_profile p=trial();witch_book_profile b=book();
        if(completed<count && matches(points[completed+1],p)){++completed;pulse=0;}
        unsigned key=0;if(pulse)--pulse;
        if(completed<count && matches(points[completed],p) && !pulse)
        {
            checkpoint s=points[completed];
            if(b.open && b.ready)
            {
                if(b.source==s.arg)key=KEY_A;else if(b.mode)key=KEY_B;else key=KEY_RIGHT;
            }
            else if(!b.open && p.interaction_ready && !p.preview)
            {
                if(s.action<2)key=KEY_A;
                else if(s.action==2)key=s.arg==0 ? KEY_UP : s.arg==1 ? KEY_LEFT : KEY_RIGHT;
                else if(s.action==3)key=p.choice==s.arg ? KEY_A : KEY_DOWN;
            }
            if(key)pulse=24;
        }
        if(!b.open && p.preview && p.interaction_ready && !pulse){key=KEY_B;pulse=24;}
        core->setKeys(core,key);frames(1);
    }
    if(n==18000){trial_runtime_profile p=trial();fprintf(stderr,"stalled step=%u pc=%u state=%u\n",completed,p.pc,p.vm_state);screenshot(argv[2],"failure");}
    expect(n<18000,"Trial07 executes its real discussion-progress toast");core->setKeys(core,0);frames(2);screenshot(argv[2],"toast-discussion");
    frames(180);expect(!visible(2),"discussion toast expires");restored();
    printf("PASS: real ADV07 + Trial07 prompts; exact VRAM glyphs, timeout restoration, book overlay; trial_frames=%u\n",n);
    mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
