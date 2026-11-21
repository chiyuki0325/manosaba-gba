/* Production chapter selection and complete representative camera paths. */
#define main navigation_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/trial_runtime.h"
#include "obj_pixels.h"
static unsigned address;
static uint16_t panorama[2][2048*192];
static uint16_t representative[18][320*192];
static trial_runtime_profile metric(void){trial_runtime_profile p;uint32_t* v=(uint32_t*)&p;for(unsigned i=0;i<sizeof(p)/4;++i)v[i]=core->busRead32(core,address+i*4);return p;}
static void tap(unsigned key){core->setKeys(core,key);frames(2);core->setKeys(core,0);frames(12);}
static int ui_ink(int x,int y){
    unsigned enabled=core->busRead16(core,0x04000000);
    for(int bg=0;bg<4;++bg)if(enabled&(0x100u<<bg)){
        unsigned cnt=core->busRead16(core,0x04000008+bg*2);if((cnt&3)!=0 || (cnt&128))continue;
        unsigned cell=core->busRead16(core,0x06000000+((cnt>>8)&31)*2048+(y/8*32+x/8)*2);
        unsigned at=0x06000000+((cnt>>2)&3)*16384+(cell&1023)*32+(y&7)*4+(x&7)/2;
        if((core->busRead8(core,at)>>((x&1)*4))&15)return 1;
    }
    return 0;
}
static void viewport(int stage){
    if(metric().background>=10){
        int matched=0,background=(int)metric().background-10,scroll=(int)metric().window_x;
        for(int delta=-2;delta<=2 && !matched;++delta){
            unsigned errors=0;
            for(int y=0;y<80;y+=2)for(int x=0;x<240;x+=2){
                if(object_ink(x,y) || ui_ink(x,y))continue;
                unsigned rgb=pixels[y*240+x];unsigned actual=((rgb>>3)&31)|(((rgb>>11)&31)<<5)|(((rgb>>19)&31)<<10);
                errors+=actual!=representative[background][(y+16)*320+x+scroll+delta];
            }
            matched=!errors;
        }
        expect(matched,"short yaw stays in its representative close view and streams the correct source columns");
        return;
    }
    int matched=0;
    for(int delta=-2;delta<=2 && !matched;++delta){
        int scroll=(int)metric().camera_scroll+delta;unsigned errors=0;
        for(int y=0;y<80;y+=2)for(int x=0;x<240;x+=2){
            if(object_ink(x,y))continue;
            unsigned rgb=pixels[y*240+x];
            unsigned actual=((rgb>>3)&31)|(((rgb>>11)&31)<<5)|(((rgb>>19)&31)<<10);
            errors+=actual!=panorama[stage][(y+16)*2048+((x+scroll)&2047)];
        }
        matched=!errors;
    }
    expect(matched,"circular streamed BG pixels match correct Court/Court_Final reference");
    for(int y=106;y<158;y+=2)for(int x=0;x<240;x+=2)expect(!object_ink(x,y),"no legs escape any podium during movement");
}
int main(int argc,char** argv){
    expect(argc==9,"usage: trial_camera_game_test ROM output profile_address specs Court_reference Final_reference CloseCourt CloseFinal");
    address=(unsigned)strtoul(argv[3],NULL,16);
    for(int stage=0;stage<2;++stage){FILE* f=fopen(argv[5+stage],"rb");expect(f!=NULL,"open panorama reference");expect(fread(panorama[stage],1,sizeof(panorama[stage]),f)==sizeof(panorama[stage]),"read panorama reference");fclose(f);}
    for(int stage=0;stage<2;++stage){FILE* f=fopen(argv[7+stage],"rb");expect(f!=NULL,"open close-view reference");expect(fread(representative+stage*9,1,9*sizeof(representative[0]),f)==9*sizeof(representative[0]),"read close-view reference");fclose(f);}
    FILE* specs=fopen(argv[4],"r");expect(specs!=NULL,"open source camera cases");
    int ordinal,motion,duration,end,stage,cases=0;unsigned total_frames=0,max_ticks=0;
    while(fscanf(specs,"%d %d %d %d %d",&ordinal,&motion,&duration,&end,&stage)==5){
        core=mCoreFind(argv[1]);expect(core&&core->init(core),"initialize mGBA");mCoreInitConfig(core,NULL);
        mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
        struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
        mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load production ROM");debug_boot();
        for(int i=0;i<2;++i)tap(KEY_DOWN);
        tap(KEY_A);frames(80);
        for(int i=0;i<ordinal/6;++i)tap(KEY_RIGHT);
        for(int i=0;i<ordinal%6;++i)tap(KEY_DOWN);
        tap(KEY_A);int found=0,cooldown=0;
        for(int guard=0;guard<12000;++guard){
            trial_runtime_profile p=metric();
            if(p.camera==(unsigned)motion+1 && p.scene_ready && p.camera_frame>0){found=1;break;}
            if(!cooldown && p.interaction_ready && (p.state==1 || p.state==2 || p.state==3 || p.state==6)){core->setKeys(core,KEY_A);cooldown=16;}
            else core->setKeys(core,0);
            if(cooldown)--cooldown;
            frames(1);
        }
        if(!found)fprintf(stderr,"unreached case=%d ordinal=%d camera=%u state=%u script=%u\n",cases,ordinal,metric().camera,metric().state,metric().script);
        expect(found,"selected formal chapter reaches requested source camera");core->setKeys(core,0);
        unsigned peak=0,previous=0;int snapshots=0;
        while(metric().camera_frame<(unsigned)duration){
            trial_runtime_profile p=metric();expect(p.camera==(unsigned)motion+1,"motion persists across its whole source duration");
            if(p.core_ticks>peak)peak=p.core_ticks;
            if(!snapshots || (p.camera_frame>=previous+(unsigned)duration/4 && snapshots<4)){
                char name[80];snprintf(name,sizeof(name),"camera-layout-%02d-frame-%04u",cases+1,p.camera_frame);screenshot(argv[2],name);
                viewport(stage);previous=p.camera_frame;++snapshots;
            }
            frames(1);++total_frames;
        }
        viewport(stage);expect(metric().camera_scroll==(unsigned)end,"all camera endpoints match source trajectory");
        expect(!metric().missed,"all chapter camera paths retain frame budget");
        if(peak>max_ticks)max_ticks=peak;
        printf("camera case %d: ordinal=%d motion=%d duration=%d stage=%d max_ticks=%u missed=%u\n",cases,ordinal,motion,duration,stage,peak,metric().missed);
        mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);++cases;
    }
    fclose(specs);expect(cases==11,"all eleven chapter layouts are exercised");
    printf("PASS: chapter cameras cases=%d frames=%u peak_ticks=%u missed=0\n",cases,total_frames,max_ticks);return 0;
}
