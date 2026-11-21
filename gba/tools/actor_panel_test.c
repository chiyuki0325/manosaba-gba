/* Reconstruct visible OBJ pixels from OAM/VRAM and verify panel-over-character alpha. */
#define main navigation_smoke_main
#include "smoke_test.c"
#undef main

static int object_pixel(int x,int y,int* actor) {
    static const int widths[3][4]={{8,16,32,64},{16,32,32,64},{8,8,16,32}};
    static const int heights[3][4]={{8,16,32,64},{8,8,16,32},{16,32,32,64}};
    int result=-1,priority=4;
    for(int i=0;i<128;++i) {
        unsigned base=0x07000000+i*8,a=core->busRead16(core,base),b=core->busRead16(core,base+2),c=core->busRead16(core,base+4);
        if((!(a&256) && (a&512)) || ((a>>10)&3)!=0 || (a>>14)==3 || (int)((c>>10)&3)>=priority)continue;
        int w=widths[a>>14][b>>14],h=heights[a>>14][b>>14],bw=(a&768)==768 ? w*2 : w,bh=(a&768)==768 ? h*2 : h;
        int left=b&511,top=a&255;if(left>=256)left-=512;if(top>=160)top-=256;
        if(x<left || x>=left+bw || y<top || y>=top+bh)continue;
        int u=x-left,v=y-top;
        if(a&256){
            unsigned mat=0x07000000+((b>>9)&31)*32;
            int dx=u-bw/2,dy=v-bh/2;
            u=(w*128+(int16_t)core->busRead16(core,mat+6)*dx+(int16_t)core->busRead16(core,mat+14)*dy)>>8;
            v=(h*128+(int16_t)core->busRead16(core,mat+22)*dx+(int16_t)core->busRead16(core,mat+30)*dy)>>8;
        }else{
            if(b&4096)u=w-1-u;
            if(b&8192)v=h-1-v;
        }
        if(u<0 || u>=w || v<0 || v>=h)continue;
        unsigned tile=(c&1023)+(v/8)*(w/8)+u/8;
        unsigned packed=core->busRead8(core,0x06010000+tile*32+(v%8)*4+(u%8)/2);
        unsigned ink=(packed>>((u&1)*4))&15;if(!ink)continue;
        // image_scene allocates four palette banks per actor, including regular OBJ.
        priority=(c>>10)&3;result=core->busRead16(core,0x05000200+((c>>12)*16+ink)*2);*actor=c>>14;
    }
    return result;
}
static int ui_ink(int x,int y) {
    for(int id=0;id<2;++id) {
        unsigned cnt=core->busRead16(core,0x04000008+id*2);
        if((cnt&128) || (cnt&3))continue;
        int u=x,v=y; // The UI canvas is fixed at (8,48): hardware scroll is zero.
        unsigned cell=core->busRead16(core,0x06000000+((cnt>>8)&31)*2048+(v/8*32+u/8)*2);
        unsigned byte=core->busRead8(core,0x06000000+((cnt>>2)&3)*16384+(cell&1023)*32+(v%8)*4+(u%8)/2);
        return (byte>>((u&1)*4))&15;
    }
    return 0;
}
static int actors(void) {
    unsigned seen=0;
    for(int i=0;i<128;++i) {
        unsigned a=core->busRead16(core,0x07000000+i*8),c=core->busRead16(core,0x07000004+i*8);
        if(!(!(a&256) && (a&512)) && (a>>14)!=3 && ((a>>10)&3)==0)seen|=1u<<(c>>14);
    }
    return __builtin_popcount(seen);
}
static void check_body(int count,const char* directory,const char* name) {
    unsigned samples[32]={0},matches[32]={0};int tested=0,edge_samples=0,edge_matches=0;
    static const int bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    for(int y=104;y<160;++y)for(int x=8;x<224;++x) {
        if(ui_ink(x,y))continue;
        int matrix=0,rgb=object_pixel(x,y,&matrix);if(rgb<0)continue;
        int r=rgb&31,g=(rgb>>5)&31,b=(rgb>>10)&31;if(r<12 && g<12 && b<12)continue;
        unsigned actual=pixels[y*240+x];
        int covered=y>=120 || bayer[y&3][x&3]<y-103;
        int expected[3]={covered ? (r*6+10)/16 : r,covered ? (g*6+10)/16 : g,covered ? (b*6+10)/16 : b};
        int got[3]={(actual&255)>>3,((actual>>8)&255)>>3,((actual>>16)&255)>>3};
        int matches_pixel=abs(expected[0]-got[0])<=1 && abs(expected[1]-got[1])<=1 && abs(expected[2]-got[2])<=1;
        if(y<120){++edge_samples;edge_matches+=matches_pixel;continue;}
        ++samples[matrix];if(matches_pixel)++matches[matrix];
        else {static int printed=0;if(printed++<8)fprintf(stderr,"mismatch %d,%d raw=%x wanted=%d,%d,%d got=%d,%d,%d\n",x,y,rgb,expected[0],expected[1],expected[2],got[0],got[1],got[2]);}
    }
    screenshot(directory,name);
    for(int m=0;m<32;++m)if(samples[m]>=20){
        fprintf(stderr,"cast=%d actor=%d body=%u correct=%u\n",count,m,samples[m],matches[m]);
        expect(matches[m]*100>=samples[m]*98,"character remains visible through the panel below y=120");++tested;
    }
    expect(tested==count,"sample the lower body of every visible character");
    fprintf(stderr,"cast=%d original dither edge=%d/%d\n",count,edge_matches,edge_samples);
    expect(edge_samples>20 && edge_matches*100>=edge_samples*98,"preserve the original 4x4 dither edge and 10/16 panel weight");
}
int main(int argc,char** argv) {
    expect(argc==3,"usage: actor_panel_test ROM screenshot_directory");
    core=mCoreFind(argv[1]);expect(core&&core->init(core),"initialize mGBA");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");
    unsigned checked=0;const int banks[]={2,11};
    for(int test=0;test<2;++test) {
        debug_boot();press(KEY_DOWN);press(KEY_A);
        for(int i=0;i<banks[test];++i)press(KEY_DOWN);
        press(KEY_A);
        for(int step=0;step<70;++step) {
            int count=actors();
            if(count>=1 && count<=3 && !(checked&(1u<<count))){char name[40];snprintf(name,sizeof(name),"actor-panel-%d",count);check_body(count,argv[2],name);checked|=1u<<count;}
            if(checked==14)break;
            press(KEY_A);
        }
    }
    expect(checked==14,"verify single, dual and triple character scenes");
    // Adv04's zoomed/blurred Middle background puts the lower BG at (0,0).
    // A reversed HBE scroll delta samples the solid tile instead of the edge.
    debug_boot();adv04_selector();press(KEY_A);
    for(int step=0;step<27;++step)press(KEY_A);
    expect(actors()==1,"reach Alisa in front of the zoomed background");
    check_body(1,argv[2],"actor-panel-zoomed-background");
    press(KEY_START);press(KEY_B);
    check_body(1,argv[2],"actor-panel-zoomed-resumed");
    mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);
    puts("PASS: all three cast sizes retain lower-body OBJ pixels through the original dialogue panel");return 0;
}
