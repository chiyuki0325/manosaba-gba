/* Real Flash1M, pause UI and production scene reconstruction. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/save_runtime.h"
#include "../src/trial_runtime.h"
#include "../src/graphics_profile.h"
#include <mgba/core/blip_buf.h>
#include <string.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/overrides.h>
#include <time.h>
#ifndef MANOSABA_TEST_SRAM
#define MANOSABA_TEST_SRAM 0
#endif
#define MANUAL_SLOTS (MANOSABA_TEST_SRAM ? 8 : 27)
static unsigned record_address(int page){return MANOSABA_TEST_SRAM ? (page<9 ? page*3072 : page==9 ? 27648 : page==10 ? 29184 : 32256+(page-11)*256) : (page<30 ? page*4096 : 122880+(page-30)*4096);}
static unsigned save_address,trial_address,graphics_address,menu_peak,menu_idle_peak,nonzero;
static const char* output;
static unsigned timeup_frames,timeup_changes,timeup_peak,last_timeup_hash;
static void timeup_sample(void)
{
    unsigned active=core->busRead32(core,trial_address+offsetof(trial_runtime_profile,state));
    unsigned vm_state=core->busRead32(core,trial_address+offsetof(trial_runtime_profile,vm_state));
    unsigned kind=core->busRead32(core,trial_address+offsetof(trial_runtime_profile,title_kind));
    unsigned stage=core->busRead32(core,trial_address+offsetof(trial_runtime_profile,title_stage));
    if(active!=12 || vm_state!=12 || kind!=2 || stage!=3)return;
    ++timeup_frames;unsigned hash=frame_hash();timeup_changes+=hash!=last_timeup_hash;last_timeup_hash=hash;
    unsigned ticks=core->busRead32(core,graphics_address+offsetof(struct graphics_profile,core_ticks));if(ticks>timeup_peak)timeup_peak=ticks;
    unsigned frame=core->busRead32(core,trial_address+offsetof(trial_runtime_profile,title_frame));
    if(frame==30 || frame==90 || frame==130){char name[40];snprintf(name,sizeof(name),"timeup-%03u",frame);screenshot(output,name);}
    if(frame==90)
    {
        int black=0;for(int i=0;i<240*24;++i)black+=!(pixels[i]&0xffffffu);
        expect(black==240*24,"TimeIsUp fades the court to black without old dialogue decorations");
    }
}
static save_runtime_profile state(void){save_runtime_profile p;uint32_t* v=(uint32_t*)&p;for(unsigned i=0;i<sizeof(p)/4;++i)v[i]=core->busRead32(core,save_address+i*4);return p;}
static trial_runtime_profile trial(void){trial_runtime_profile p;uint32_t* v=(uint32_t*)&p;for(unsigned i=0;i<sizeof(p)/4;++i)v[i]=core->busRead32(core,trial_address+i*4);return p;}
static void measured_frames(int count){static save_runtime_profile recent[8];static unsigned frame;short samples[4096];while(count--){save_runtime_profile before=state();frames(1);timeup_sample();for(int ch=0;ch<2;++ch){int n=blip_read_samples(core->getAudioChannel(core,ch),samples,4096,0);for(int i=0;i<n;++i)nonzero+=samples[i]!=0;}recent[frame++%8]=state();if((int)state().view>=0){unsigned ticks=core->busRead32(core,graphics_address+offsetof(struct graphics_profile,core_ticks));save_runtime_profile after=state();
if(before.paint==6 && after.paint==6 && before.view==after.view && before.selected==after.selected && before.confirm==after.confirm && !before.busy && !after.busy && ticks>menu_idle_peak)menu_idle_peak=ticks;
if(ticks>menu_peak){menu_peak=ticks;if(ticks>4200){for(unsigned i=frame>5 ? frame-5 : 0;i<frame;++i){save_runtime_profile p=recent[i%8];fprintf(stderr,"recent %u view=%u paint=%u draw=%u thumb=%u snapshot=%u op=%u row_step=%u\n",i,p.view,p.paint,p.draw_work,p.thumb_work,p.snapshot_work,p.operation,p.row_step);}fprintf(stderr,"peak=%u view=%u paint=%u draw=%u thumb=%u snapshot=%u\n",ticks,state().view,state().paint,state().draw_work,state().thumb_work,state().snapshot_work);}}}}}
#define frames measured_frames
static void tap(unsigned keys){core->setKeys(core,keys);frames(4);core->setKeys(core,0);frames(32);}
static void wait_ui_at(int view,int line){for(int i=0;i<1800;++i){frames(1);save_runtime_profile p=state();if((int)p.view==view && !p.busy && p.paint==6){frames(4);return;}}save_runtime_profile p=state();fprintf(stderr,"wait line=%d expected=%d view=%d paint=%u busy=%u op=%u progress=%u script=%u pc=%u trial=%u\n",line,view,(int)p.view,p.paint,p.busy,p.operation,p.progress,p.script,p.pc,trial().vm_state);screenshot(output,"save-timeout");expect(0,"save page becomes ready");}
static void wait_game(void){for(int i=0;i<1800;++i){frames(1);if((int)state().view==-1 && !state().restoring){frames(220);return;}}screenshot(output,"load-timeout");expect(0,"loaded resources become interactive");}
#define wait_ui(view) wait_ui_at(view,__LINE__)
static TEST_UNUSED void save_slot(void){unsigned before=nonzero;tap(KEY_A);wait_ui(0);expect(!state().confirm,"empty slot saves directly");expect(nonzero-before>100,"music continues while writing Flash");}
static void open_list(int load){tap(KEY_START);wait_ui(2);tap(KEY_DOWN);if(load)tap(KEY_DOWN);tap(KEY_A);wait_ui(load);}
static TEST_UNUSED unsigned records(const void* data,int logical){unsigned count=0;const uint8_t* p=data;for(int page=0;page<(MANOSABA_TEST_SRAM ? 13 : 32);++page){unsigned at=record_address(page);if(p[at]=='M' && p[at+1]=='N' && p[at+2]=='S' && p[at+3]=='V' && p[at+22]==logical && p[at+63]==0xa5)++count;}return count;}
#ifndef MANOSABA_SAVE_LIBRARY
int main(int argc,char** argv){
    expect(argc==6,"ROM output save_address trial_address graphics_address");output=argv[2];save_address=strtoul(argv[3],NULL,16);trial_address=strtoul(argv[4],NULL,16);graphics_address=strtoul(argv[5],NULL,16);
    core=mCoreFind(argv[1]);expect(core&&core->init(core),"initialize mGBA");mCoreInitConfig(core,NULL);mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    mCoreConfigSetDefaultIntValue(&core->config,"mute",0);mCoreConfigSetDefaultIntValue(&core->config,"volume",256);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");debug_boot();
    adv04_selector();press(KEY_A);frames(300);expect(state().available,"save backend is available");unsigned pc=state().pc,script=state().script;uint32_t first=frame_hash();screenshot(output,"save-source");
    open_list(0);expect(state().selected==0,"first empty manual slot selected");screenshot(output,"save-empty");
    save_slot();screenshot(output,"save-filled");
    tap(KEY_A);wait_ui(0);expect(state().confirm && !state().confirm_choice,"overwrite defaults to cancel");screenshot(output,"save-overwrite");tap(KEY_A);wait_ui(0);expect(!state().confirm,"new A cancels overwrite by default");
    int initial_rtc=!!(((struct GBA*)core->board)->memory.hw.devices&HW_RTC);
    struct Configuration* overrides=mCoreConfigGetOverrides(&core->config);
    ConfigurationSetIntValue(overrides,"override.MSBA","hardware",HW_RTC);
    GBAOverrideApplyDefaults((struct GBA*)core->board,overrides);
    core->rtc.override=RTC_FIXED;core->rtc.value=1700000000000LL;
    for(int slot=1;slot<MANUAL_SLOTS;++slot){((struct GBA*)core->board)->memory.hw.rtc.control=slot==2 ? 0x80 : 0x40;tap(KEY_DOWN);wait_ui(0);expect(state().selected==(unsigned)slot,"manual slot selection");expect(state().first==(unsigned)(slot<MANUAL_SLOTS-1 ? slot : MANUAL_SLOTS-2),"two cards scroll proactively");save_slot();if(slot==1 || slot==2)screenshot(output,slot==1 ? "save-rtc-time" : "save-time-unavailable");}
    uint8_t* flash=malloc(MANOSABA_TEST_SRAM ? 32768 : 131072);expect(flash!=NULL,"allocate host Flash copy");
    if(MANOSABA_TEST_SRAM){for(unsigned i=0;i<32768;++i)flash[i]=core->busRead8(core,0x0e000000+i);}
    else {
    uint8_t original[8];for(int i=0;i<8;++i)original[i]=core->busRead8(core,0x0e000000+i);
    for(unsigned bank=0;bank<2;++bank){core->busWrite8(core,0x0e005555,0xaa);core->busWrite8(core,0x0e002aaa,0x55);core->busWrite8(core,0x0e005555,0xb0);core->busWrite8(core,0x0e000000,bank);for(unsigned i=0;i<65536;++i)flash[bank*65536+i]=core->busRead8(core,0x0e000000+i);}
    core->busWrite8(core,0x0e005555,0xaa);core->busWrite8(core,0x0e002aaa,0x55);core->busWrite8(core,0x0e005555,0xb0);core->busWrite8(core,0x0e000000,memcmp(original,flash,8)!=0);
    }
    for(int slot=0;slot<MANUAL_SLOTS;++slot)expect(records(flash,slot)>0,"all manual slots committed");
    expect(records(flash,MANUAL_SLOTS)>0,"normal entry auto save committed");expect(records(flash,MANUAL_SLOTS+2)>0,"global flags committed in upper bank");for(int p=0;p<(MANOSABA_TEST_SRAM ? 11 : 30);++p){unsigned at=record_address(p);if(flash[at+63]!=0xa5)continue;unsigned slot=flash[at+22];if(slot==0 && !initial_rtc)expect(!flash[at+85],"default mGBA without RTC stores no wall-clock timestamp");if(slot==1){time_t fixed=1700000000;struct tm date;localtime_r(&fixed,&date);expect(flash[at+78]==date.tm_year-100 && flash[at+79]==date.tm_mon+1 && flash[at+80]==date.tm_mday && flash[at+81]==date.tm_hour && flash[at+82]==date.tm_min,"configured RTC stores the fixed wall clock rather than play time");fprintf(stderr,"RTC slot1 valid=%u date=%02x/%02x/%02x\n",flash[at+85],flash[at+78],flash[at+79],flash[at+80]);expect(flash[at+85],"valid optional RTC stored");}if(slot==2)expect(!flash[at+85],"power-lost RTC omitted");}
    {char path[1024];snprintf(path,sizeof path,"%s/roundtrip.sav",output);FILE* file=fopen(path,"wb");expect(file!=NULL,"export native save");fwrite(flash,1,MANOSABA_TEST_SRAM ? 32768 : 131072,file);fclose(file);}
    free(flash);
    fprintf(stderr,"stage: all slots written\n");tap(KEY_B);wait_ui(2);tap(KEY_B);wait_game();expect(frame_hash()==first,"pause/save restores unchanged story pixels");
    fprintf(stderr,"stage: returned to story\n");press(KEY_A);expect(state().pc!=pc,"story advanced before load");
    open_list(1);tap(KEY_DOWN);wait_ui(1);expect(state().selected==1,"load manual 01 below auto");screenshot(output,"save-load");tap(KEY_A);wait_ui(1);expect(state().confirm && !state().confirm_choice,"load defaults to cancel");tap(KEY_RIGHT);tap(KEY_A);wait_game();expect(state().script==script && state().pc==pc,"load restores original VM script and PC without replay");expect(frame_hash()==first,"loaded story pixels exactly match saved page");
    fprintf(stderr,"stage: initial reload done\n");debug_boot();adv04_selector();press(KEY_A);open_list(1);tap(KEY_DOWN);wait_ui(1);tap(KEY_A);wait_ui(1);tap(KEY_RIGHT);tap(KEY_A);wait_game();expect(state().script==script && state().pc==pc,"reset reloads committed directory and snapshot");
    fprintf(stderr,"stage: reset reload done\n");
    // Saving from a Note preserves its story progress, not the reader/window cursor.
    tap(KEY_SELECT);frames(300);tap(KEY_L);frames(300);tap(KEY_A);frames(300);tap(KEY_DOWN);frames(80);
    open_list(0);unsigned book_slot=state().selected;tap(KEY_A);wait_ui(0);tap(KEY_RIGHT);tap(KEY_A);wait_ui(0);
    tap(KEY_R);wait_ui(1);for(int i=0;i<MANUAL_SLOTS+1 && state().selected!=book_slot+1;++i){tap(KEY_DOWN);wait_ui(1);}
    tap(KEY_A);wait_ui(1);tap(KEY_RIGHT);tap(KEY_A);wait_game();expect(frame_hash()==first,"load from Notes returns to the story anchor with the book closed");
    tap(KEY_SELECT);frames(300);tap(KEY_L);frames(300);uint32_t note_list_pixels=frame_hash();
    open_list(0);tap(KEY_A);wait_ui(0);tap(KEY_RIGHT);tap(KEY_A);wait_ui(0);
    debug_boot();adv04_selector();press(KEY_A);open_list(1);
    while(state().selected!=book_slot+1){tap(KEY_DOWN);wait_ui(1);}
    tap(KEY_A);wait_ui(1);tap(KEY_RIGHT);tap(KEY_A);wait_game();
    expect(frame_hash()==first,"cold load from a saved book list returns to the story");
    tap(KEY_SELECT);frames(300);tap(KEY_L);frames(300);expect(frame_hash()==note_list_pixels,"reopened Notes rebuild from preserved versions at their default list position");tap(KEY_B);frames(220);
    if(getenv("MANOSABA_SAVE_UI_ONLY"))goto finished;
    fprintf(stderr,"stage: Notes progress roundtrip done\n");
    // Direct formal Trial01, then preserve the total discussion clock through save/load.
    debug_boot();tap(KEY_DOWN);tap(KEY_DOWN);tap(KEY_A);tap(KEY_DOWN);tap(KEY_A);
    for(int i=0;i<1800 && !(trial().state==6 && trial().interaction_ready);++i){if(i%40==0)tap(KEY_A);else frames(1);}
    fprintf(stderr,"stage: trial entered script=%u pc=%u vm=%u\n",state().script,state().pc,trial().vm_state);
    expect(trial().state==6,"formal trial reaches timed statement");expect(state().seed_size>0,"first discussion title establishes retry seed");
    tap(KEY_START);wait_ui(2);unsigned total=trial().total_left,hp=trial().hp;pc=state().pc;
    tap(KEY_DOWN);tap(KEY_A);wait_ui(0);unsigned saved_slot=state().selected;tap(KEY_A);wait_ui(0);expect(state().confirm,"overwrite test trial snapshot");tap(KEY_RIGHT);tap(KEY_A);wait_ui(0);
    tap(KEY_R);wait_ui(1);while(state().selected!=saved_slot+1){tap(KEY_DOWN);wait_ui(1);}tap(KEY_A);wait_ui(1);tap(KEY_RIGHT);tap(KEY_A);
    // Freeze immediately after reconstruction by reopening pause.
    for(int i=0;i<1800;++i){frames(1);if((int)state().view==-1 && !state().restoring)break;}
    tap(KEY_START);wait_ui(2);expect(state().pc==pc,"trial reload keeps current statement");expect(trial().hp==hp,"trial reload preserves the archived HP field");expect(trial().line_left<=trial().duration && trial().duration-trial().line_left<70,"trial reload restarts the current statement with a full line timer");expect(trial().total_left<=total && total-trial().total_left<70,"trial reload preserves total clock");screenshot(output,"save-trial-pause");
    tap(KEY_B);frames(200);
    for(int i=0;i<500 && !(trial().state==6 && trial().links && trial().interaction_ready);++i)frames(60);
    expect(trial().state==6 && trial().links,"discussion reaches objection point");if(trial().preview){tap(KEY_A);frames(40);}for(int i=0;i<30 && trial().state==6;++i)tap(KEY_UP);frames(180);screenshot(output,"save-objection-point");fprintf(stderr,"objection: state=%u vm=%u pc=%u links=%u left=%u total=%u\n",trial().state,trial().vm_state,state().pc,trial().links,trial().line_left,trial().total_left);expect(trial().state==2,"Up opens objection choices");
    uint32_t bubble=frame_hash();pc=state().pc;open_list(0);unsigned bubble_slot=state().selected;tap(KEY_A);wait_ui(0);tap(KEY_RIGHT);tap(KEY_A);wait_ui(0);
    tap(KEY_R);wait_ui(1);for(int i=0;i<MANUAL_SLOTS+1 && state().selected!=bubble_slot+1;++i){tap(KEY_DOWN);wait_ui(1);}tap(KEY_A);wait_ui(1);tap(KEY_RIGHT);tap(KEY_A);wait_game();
    expect(trial().state==2 && state().pc==pc && frame_hash()==bubble,"load restores objection bubble and selection");tap(KEY_B);frames(60);expect(trial().state==6,"loaded bubble retains its cancellation context");
    // Time out naturally; complete the original failure dialogue, then use retry.
    unsigned round_script=state().script;
    for(int i=0;i<500 && !(state().script!=round_script && trial().vm_state==4);++i)
    {if(i%20==0)fprintf(stderr,"timeout progress i=%d script=%u vm=%u preview=%u line=%u total=%u restoring=%u view=%d\n",i,state().script,trial().vm_state,trial().preview,trial().line_left,trial().total_left,state().restoring,(int)state().view);unsigned s=trial().vm_state;if(trial().preview)tap(KEY_A);else if(s==6 || s==10)frames(600);else if(s==1 || s==3)tap(KEY_A);else frames(60);}
    expect(trial().vm_state==4,"timeout plays failure dialogue to BAD END");
    expect(timeup_frames>=170 && timeup_changes>8,"timeout plays the original animated TimeIsUp before failure dialogue");
    expect(timeup_peak<4389,"TimeIsUp stays within the frame budget");
    printf("TimeIsUp: frames=%u changes=%u peak=%u ticks\n",timeup_frames,timeup_changes,timeup_peak);
    void* failed_state=malloc(core->stateSize(core));expect(failed_state && core->saveState(core,failed_state),"retain failure fixture for both actions");
    tap(KEY_START);wait_ui(2);expect(state().selected==0,"Start opens the same failure actions as A");
    tap(KEY_DOWN);tap(KEY_A);wait_ui(1);tap(KEY_L);expect(state().view==1,"failure load page cannot switch to saving a failed state");
    while(state().selected!=saved_slot+1){tap(KEY_DOWN);wait_ui(1);}
    tap(KEY_A);wait_ui(1);tap(KEY_RIGHT);tap(KEY_A);wait_game();
    expect(state().script==round_script && trial().hp==hp && trial().total_left>0,"loading from failure restores saved trial clocks and HP");
    expect(core->loadState(core,failed_state),"restore failure fixture");free(failed_state);
    tap(KEY_A);wait_ui(2);expect(state().selected==0,"failure defaults to available retry");screenshot(output,"save-failure-retry");tap(KEY_A);wait_game();expect(state().script==round_script && trial().hp==hp && trial().total_left>0,"retry restores original round, HP and timer from attached seed");
finished:
    printf("budget load_peak=%u steady_peak=%u ticks\n",menu_peak,menu_idle_peak);
    expect(menu_idle_peak<4389,"settled save/load screens meet the frame budget");
    if(getenv("MANOSABA_SAVE_UI_ONLY"))printf("PASS: save/load UI, all manual slots, RTC, live music, cold story reload and book progress; menu_peak=%u ticks PCM=%u\n",menu_peak,nonzero);
    else
    printf("PASS: save backend, all manual+auto+globals, COW commits, confirmation guards, proactive scrolling, exact story pixels, RTC/reset, trial clocks/HP and retry seed; menu_peak=%u ticks PCM=%u\n",menu_peak,nonzero);
    mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
#endif
