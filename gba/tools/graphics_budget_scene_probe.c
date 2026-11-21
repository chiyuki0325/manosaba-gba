/* Host-only scene inspection. The ROM is unchanged; compare the same saved
 * native frame with and without mGBA's OBJ scanline limit. */
#define main unused_navigation_main
#include "smoke_test.c"
#undef main
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/renderers/video-software.h>
#include <string.h>
#include "obj_bounds_probe.h"

static unsigned profile;
static void (*native_scanline)(struct GBAVideoRenderer*,int);
static unsigned char object_override[1024+32768+512];
static int override_unlimited;
static unsigned metric(int i){return core->busRead32(core,profile+4*i);}
static void tap(unsigned k){core->setKeys(core,k);frames(2);core->setKeys(core,0);frames(24);}
static void unlimited_scanline(struct GBAVideoRenderer* base,int y){
    struct GBAVideoSoftwareRenderer* r=(struct GBAVideoSoftwareRenderer*)base;
    if(r->oamDirty){r->oamMax=GBAVideoRendererCleanOAM(r->d.oam->obj,r->sprites,r->objOffsetY);r->oamDirty=false;}
    for(int i=0;i<r->oamMax;++i)r->sprites[i].cycles=0;
    for(int i=0;i<5;++i)r->scanlineDirty[i]=0xffffffffu;
    native_scanline(base,y);
}
static void overridden_scanline(struct GBAVideoRenderer* base,int y){
    if(!y){
        for(int i=0;i<1024;i+=2)core->busWrite16(core,0x07000000+i,object_override[i]|(object_override[i+1]<<8));
        for(int i=0;i<32768;i+=4){unsigned v;memcpy(&v,object_override+1024+i,4);core->busWrite32(core,0x06010000+i,v);}
        struct GBAVideoSoftwareRenderer* r=(struct GBAVideoSoftwareRenderer*)base;r->oamDirty=true;
        for(int i=0;i<5;++i)r->scanlineDirty[i]=0xffffffffu;
    }
    if(override_unlimited)unlimited_scanline(base,y);else native_scanline(base,y);
}
static void dump_objects(const char* directory,const char* name){
    char path[512];snprintf(path,sizeof(path),"%s/%s-objects.bin",directory,name);
    FILE* f=fopen(path,"wb");expect(f!=NULL,"open OBJ diagnostic snapshot");
    static const unsigned addresses[]={0x07000000,0x06010000,0x05000200},sizes[]={1024,32768,512};
    for(int region=0;region<3;++region)for(unsigned i=0;i<sizes[region];++i){unsigned char b=core->busRead8(core,addresses[region]+i);fwrite(&b,1,1,f);}
    fclose(f);
}
static void compare(const char* directory,const char* name){
    void* saved=malloc(core->stateSize(core));expect(saved && core->saveState(core,saved),"save diagnostic state");
    core->runFrame(core);
    char label[100];snprintf(label,sizeof(label),"%s-native",name);screenshot(directory,label);
    color_t original[240*160];memcpy(original,pixels,sizeof(original));
    unsigned cost[160]={0},objects=0;struct GBA* gba=core->board;
    struct GBAVideoRendererSprite sprites[128];
    int n=GBAVideoRendererCleanOAM(gba->video.oam.obj,sprites,0);
    for(int y=0;y<160;++y){
        int last=0;
        for(int i=0;i<n;++i){
            cost[y]+=2*(sprites[i].index-last);last=sprites[i].index;
            if((y>=sprites[i].y && y<sprites[i].endY) || (sprites[i].endY>256 && y<sprites[i].endY-256))cost[y]+=sprites[i].cycles;
        }
    }
    unsigned peak=0,row=0;for(unsigned y=0;y<160;++y)if(cost[y]>peak){peak=cost[y];row=y;}objects=n;
    unsigned limit=core->busRead16(core,0x04000000)&32?954:1210;
    unsigned bounds_above=0,bounds_clipped=object_bounds_clipping(&bounds_above),wrap_ink=object_y_wrap_ink();
    dump_objects(directory,name);
    expect(core->loadState(core,saved),"restore the exact native frame");
    struct GBAVideoRenderer* renderer=gba->video.renderer;native_scanline=renderer->drawScanline;
    renderer->drawScanline=unlimited_scanline;core->runFrame(core);renderer->drawScanline=native_scanline;
    snprintf(label,sizeof(label),"%s-unlimited",name);screenshot(directory,label);
    unsigned changed=0,above=0;
    for(int i=0;i<240*160;++i)if((original[i]&0xffffffu)!=(pixels[i]&0xffffffu)){++changed;above+=i<240*104;}
    printf("scene=%s bank=%u event=%u cycles=%u limit=%u y=%u objects=%u changed=%u above_dialogue=%u bounds_clipped=%u bounds_above=%u wrap_ink=%u\n",name,metric(4),metric(5),peak,limit,row,objects,changed,above,bounds_clipped,bounds_above,wrap_ink);fflush(stdout);
    if(getenv("MANOSABA_ASSERT_COMPLETE_OBJECTS")){
        expect(!changed,"no pixels lost to OBJ scanline exhaustion");
        expect(!bounds_clipped,"no pixels lost to OBJ output boxes");
        expect(!wrap_ink,"no bottom-edge OBJ ink wraps to the top");
    }
    const char* overrides=getenv("MANOSABA_OBJ_OVERRIDES");
    if(overrides){
        char path[512];snprintf(path,sizeof(path),"%s/%s-override.bin",overrides,name);FILE* f=fopen(path,"rb");
        if(f){
            expect(fread(object_override,1,sizeof(object_override),f)==sizeof(object_override),"read counterfactual OBJ state");fclose(f);
            color_t reference[240*160],padded[240*160];memcpy(reference,pixels,sizeof(reference));
            expect(core->loadState(core,saved),"restore before padded native frame");override_unlimited=0;renderer->drawScanline=overridden_scanline;core->runFrame(core);renderer->drawScanline=native_scanline;
            memcpy(padded,pixels,sizeof(padded));snprintf(label,sizeof(label),"%s-padded-native",name);screenshot(directory,label);
            expect(core->loadState(core,saved),"restore before padded reference frame");override_unlimited=1;renderer->drawScanline=overridden_scanline;core->runFrame(core);renderer->drawScanline=native_scanline;
            unsigned gaps=0,difference=0,gaps_above=0;
            for(int p=0;p<240*160;++p){int gap=(padded[p]&0xffffffu)!=(pixels[p]&0xffffffu);gaps+=gap;gaps_above+=gap && p<240*104;difference+=(reference[p]&0xffffffu)!=(pixels[p]&0xffffffu);}
            snprintf(label,sizeof(label),"%s-padded-unlimited",name);screenshot(directory,label);
            printf("padding scene=%s pixel_difference=%u native_missing=%u above_dialogue=%u\n",name,difference,gaps,gaps_above);fflush(stdout);
        }
    }
    expect(core->loadState(core,saved),"restore normal OBJ accounting after the comparison");free(saved);
    struct GBAVideoSoftwareRenderer* software=(struct GBAVideoSoftwareRenderer*)gba->video.renderer;
    software->oamDirty=true;
    for(int i=0;i<5;++i)software->scanlineDirty[i]=0xffffffffu;
    core->runFrame(core);
}
#ifndef MANOSABA_SCENE_PROBE_LIBRARY
int main(int argc,char** argv){
    expect(argc==5,"ROM output profile plan");profile=strtoul(argv[3],NULL,16);
    core=mCoreFind(argv[1]);expect(core&&core->init(core),"initialize mGBA");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);expect(mCoreLoadFile(core,argv[1]),"load ROM");
    debug_boot();compare(argv[2],"control-menu");
    FILE* plan=fopen(argv[4],"r");expect(plan!=NULL,"open scene plan");
    unsigned bank,event,offset;char name[80],row[256];
    while(fgets(row,sizeof(row),plan)){
        unsigned ordinal;
        int fields=sscanf(row,"%79s %u %u %u %u",name,&bank,&event,&offset,&ordinal);
        expect(fields==4 || fields==5,"scene plan has bank, event, offset and optional script ordinal");
        if(fields==4)ordinal=bank;
        debug_boot();tap(KEY_DOWN);tap(KEY_A);
        for(unsigned i=0;i<ordinal/6;++i)tap(KEY_RIGHT);
        for(unsigned i=0;i<ordinal%6;++i)tap(KEY_DOWN);
        tap(KEY_A);
        int reached=0,cooldown=140;
        for(unsigned frame=0;frame<100000;++frame){
            if(metric(4)==bank && metric(5)==event){reached=1;break;}
            if(!cooldown){core->setKeys(core,KEY_A);cooldown=166;}
            else if(cooldown==164)core->setKeys(core,0);
            --cooldown;frames(1);
        }
        core->setKeys(core,0);
        if(!reached){printf("UNREACHED scene=%s bank=%u event=%u last_bank=%u last_event=%u\n",name,bank,event,metric(4),metric(5));fflush(stdout);expect(0,"planned source scene is reached");}
        unsigned start=metric(12);
        for(unsigned guard=0;guard<3000 && (metric(11) || metric(12)<start+offset);++guard)frames(1);
        frames(2);compare(argv[2],name);
        frames(90);char later[100];snprintf(later,sizeof(later),"%s-settled",name);compare(argv[2],later);
    }
    fclose(plan);mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
#endif
