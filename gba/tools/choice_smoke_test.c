/* Exercise actual choice input and both jump destinations in the native player. */
#define main navigation_smoke_main
#include "smoke_test.c"
#undef main

static unsigned profile_address;
static unsigned metric(int index){return core->busRead32(core,profile_address+index*4);}
static void reach_choice(int bank){
    debug_boot();press(KEY_DOWN);press(KEY_A);
    for(int i=0;i<bank;++i)press(KEY_DOWN);
    press(KEY_A);
    int native=0;
    for(int step=0;step<70;++step){
        unsigned mode=core->busRead16(core,0x04000000)&7;
        if(mode==1)native=1;
        else if(native)return;
        press(KEY_A);
    }
    expect(0,"reach Adv05's first story choice");
}
static int light_pixels(int left,int top,int right,int bottom,int white){
    int count=0;
    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){
        unsigned c=pixels[y*240+x];int r=c&255,g=(c>>8)&255,b=(c>>16)&255;
        count+=white ? r>240 && g>240 && b>230 : r>160 && g>160 && b>150;
    }
    return count;
}
static uint32_t chrome_hash(void){
    uint32_t hash=2166136261u;
    for(int y=0;y<160;++y)if(y<40 || y>=120)
        for(int x=0;x<240;++x)hash=(hash^pixels[y*240+x])*16777619u;
    return hash;
}
static void check_choices(void){
    unsigned enabled=core->busRead16(core,0x04000000);
    unsigned bg=__builtin_ctz((enabled>>8)&15),cnt=core->busRead16(core,0x04000008+bg*2);
    expect((enabled&7)==0 && (cnt&128),"choice screen uses an 8bpp regular BG");
    expect(__builtin_popcount((enabled>>8)&15)==1,"choice UI replaces all scene backgrounds");
    unsigned colors[256];int unique=0;
    for(int i=0;i<240*160;++i){
        unsigned c=pixels[i];int found=0;for(int j=0;j<unique;++j)if(colors[j]==c){found=1;break;}
        if(!found && unique<256)colors[unique++]=c;
    }
    expect(unique>100,"background and frames retain more than 100 displayed colors");
    for(int y=40;y<=80;y+=40){
        expect(light_pixels(64,y+12,196,y+24,1)>50,"both selected and unselected labels use white text");
        expect(light_pixels(64,y+24,196,y+28,1)==0,"label foreground moved two pixels up within the frame");
    }
    expect(light_pixels(36,45,58,75,0)>12,"the enlarged skull remains legible against its dark flag");
}
int main(int argc,char** argv){
    expect(argc==4,"usage: choice_smoke_test ROM screenshot_directory profile_address_hex");
    profile_address=(unsigned)strtoul(argv[3],NULL,16);
    core=mCoreFind(argv[1]);expect(core&&core->init(core),"initialize mGBA");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");
    reach_choice(4);check_choices();screenshot(argv[2],"adv05-choices");
    uint32_t menu=frame_hash();press(KEY_START);press(KEY_B);expect(frame_hash()==menu,"pause and resume restore the entire 256-color choice screen");
    press(KEY_SELECT);expect(frame_hash()!=menu,"Select opens the book over an ordinary choice");
    press(KEY_R);press(KEY_R); // Exercise the 4bpp map before restoring 8bpp choices.
    press(KEY_SELECT);expect(frame_hash()==menu,"closing book restores every choice pixel and its palette");
    unsigned source_bank=metric(4);expect(source_bank==4,"choice belongs to Adv05");
    press(KEY_A);
    expect((core->busRead16(core,0x04000000)&7)==1 && metric(4)==36,"A confirms the first choice and jumps to Bad01");
    screenshot(argv[2],"adv05-choice-bad-ending");
    reach_choice(4);unsigned event=metric(5);uint32_t first=frame_hash(),chrome=chrome_hash();
    press(KEY_DOWN);expect(frame_hash()!=first,"Down highlights the second option");
    expect(chrome_hash()==chrome,"selection changes preserve the full-screen artwork and hide old dialogue");
    screenshot(argv[2],"adv05-choices-second-selected");
    press(KEY_A);
    expect((core->busRead16(core,0x04000000)&7)==1 && metric(4)==source_bank && metric(5)>event,"second choice jumps to a label in the same script and resumes graphics");
    screenshot(argv[2],"adv05-choice-continue");
    reach_choice(6);check_choices();screenshot(argv[2],"adv07-long-choices");
    puts("PASS: 256-color choice UI, white labels/baselines, skull, longest option, pause/resume and both jumps");
    mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
