/* Input-to-ready latency and deterministic book screenshots on the real ROM. */
#define main unused_navigation_main
#include "smoke_test.c"
#undef main
#include "../src/witch_book_runtime.h"
#include "../src/graphics_profile.h"
static unsigned book_addr,graphics_addr;
static const char* directory;
static witch_book_profile state(void){witch_book_profile p;uint32_t* v=(uint32_t*)&p;for(unsigned i=0;i<sizeof(p)/4;++i)v[i]=core->busRead32(core,book_addr+4*i);return p;}
static struct graphics_profile perf(void){struct graphics_profile p;uint32_t* v=(uint32_t*)&p;for(unsigned i=0;i<sizeof(p)/4;++i)v[i]=core->busRead32(core,graphics_addr+4*i);return p;}
static void settle(void){for(int i=0;i<350;++i){frames(1);if(state().ready){frames(6);return;}}expect(0,"settle");}
static void tap_quiet(unsigned key){core->setKeys(core,key);frames(4);core->setKeys(core,0);frames(20);}
static void operation(const char* label,unsigned key){
    frames(12);witch_book_profile before=state();unsigned total_ui=0,total_cpu=0,max_cpu=0,max_ui=0;
    unsigned phase_n[7]={},phase_ui[7]={},phase_max[7]={};int changed=0,n;
    core->setKeys(core,key);
    for(n=1;n<=350;++n){
        if(n==5)core->setKeys(core,0);
        frames(1);witch_book_profile p=state();struct graphics_profile g=perf();
        changed|=p.open!=before.open||p.tab!=before.tab||p.mode!=before.mode||p.selected!=before.selected||p.map_floor!=before.map_floor||!p.ready;
        total_ui+=g.ui_ticks;total_cpu+=g.core_ticks;if(g.core_ticks>max_cpu)max_cpu=g.core_ticks;if(g.ui_ticks>max_ui)max_ui=g.ui_ticks;
        int phase=p.loading ? 0 : p.paint<600 ? 1 : p.paint<602 ? 2 : p.paint==602 ? 3 : p.paint<605 ? 4 : p.paint<610 ? 5 : 6;
        ++phase_n[phase];phase_ui[phase]+=g.ui_ticks;if(g.ui_ticks>phase_max[phase])phase_max[phase]=g.ui_ticks;
        if(n>=5 && changed && p.open && p.ready)break;
    }
    core->setKeys(core,0);witch_book_profile p=state();
    printf("OP\t%s\tframes=%d\tms=%.1f\tmean_core=%.1f\tmax_core=%u\tmean_ui=%.1f\tmax_ui=%u\tview=%d,%d->%d,%d\ttab=%u\tmode=%u\n",label,n,n*16.7427063,(double)total_cpu/n,max_cpu,(double)total_ui/n,max_ui,before.map_left,before.map_top,p.map_left,p.map_top,p.tab,p.mode);
    for(int i=0;i<7;++i)if(phase_n[i])printf("PHASE\t%d\tframes=%u\tmean_ui=%.1f\tmax_ui=%u\n",i,phase_n[i],(double)phase_ui[i]/phase_n[i],phase_max[i]);
    expect(n<=350,"operation finishes");
    screenshot(directory,label);
}
int main(int argc,char** argv){
    expect(argc==5,"ROM book_addr graphics_addr OUTPUT");directory=argv[4];book_addr=strtoul(argv[2],NULL,16);graphics_addr=strtoul(argv[3],NULL,16);
    core=mCoreFind(argv[1]);expect(core&&core->init(core),"init");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load");debug_boot();
    adv04_selector();tap_quiet(KEY_A);frames(180);
    operation("cold_open_clue",KEY_SELECT);
    tap_quiet(KEY_SELECT);frames(140);operation("warm_open_same_clue",KEY_SELECT);
    operation("clue_to_profile",KEY_R);operation("next_profile",KEY_RIGHT);
    operation("cached_previous_profile",KEY_LEFT);operation("cached_next_profile",KEY_RIGHT);
    operation("cached_profile_detail",KEY_A);operation("detail_to_cached_profile",KEY_B);
    operation("profile_detail",KEY_A);operation("profile_full_image",KEY_A);
    operation("full_to_detail",KEY_B);operation("detail_to_same_profile",KEY_B);
    operation("profile_to_map",KEY_R);operation("next_map_place",KEY_RIGHT);
    operation("map_detail",KEY_A);operation("map_detail_back",KEY_B);
    operation("next_floor",KEY_DOWN);operation("previous_floor",KEY_UP);
    while(state().selected!=15){tap_quiet(KEY_RIGHT);settle();}
    operation("same_view_fire_to_water",KEY_RIGHT);
    operation("map_to_rules",KEY_R);
    operation("rule_scroll_one_line",KEY_DOWN);operation("rule_scroll_second_line",KEY_DOWN);
    operation("rule_page_forward",KEY_RIGHT);operation("rule_page_back",KEY_LEFT);
    core->deinit(core);return 0;
}
