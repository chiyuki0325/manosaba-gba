/* Positive/negative control: ensure budget warnings and real missing pixels are
 * distinguished. Synthetic OAM is confined to this host diagnostic, not patrol. */
#define MANOSABA_TRIAL_WALK_LIBRARY
#include "trial_walk_test.c"

static unsigned visible_last;
static void synthetic_objects(void* context)
{
    (void)context;
    core->busWrite16(core,0x04000000,0x1000);
    for(unsigned i=0;i<128;++i)core->busWrite16(core,0x07000000+i*8,0x0200);
    for(unsigned i=0;i<20;++i)
    {
        core->busWrite16(core,0x07000000+i*8,24);
        core->busWrite16(core,0x07000002+i*8,0xc000 | ((visible_last && i==19) ? 120 : 0));
        core->busWrite16(core,0x07000004+i*8,0);
    }
    for(unsigned i=0;i<2048;i+=4)core->busWrite32(core,0x06010000+i,0x11111111);
    core->busWrite16(core,0x05000202,0x7fff);
}
int main(int argc,char** argv)
{
    expect(argc==5,"probe_test ROM output profile book_profile");
    directory=argv[2];profile=strtoul(argv[3],NULL,16);book_profile=strtoul(argv[4],NULL,16);
    core=mCoreFind(argv[1]);expect(core && core->init(core),"initialize probe core");mCoreInitConfig(core,NULL);
    mCoreConfigSetDefaultIntValue(&core->config,"skipBios",1);mCoreConfigSetDefaultIntValue(&core->config,"logLevel",mLOG_FATAL|mLOG_ERROR);
    struct mStandardLogger logger;mStandardLoggerInit(&logger);mStandardLoggerConfig(&logger,&core->config);mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);core->setVideoBuffer(core,pixels,240);
    expect(mCoreLoadFile(core,argv[1]),"load probe ROM");core->reset(core);frames(30);
    diagnostic_state=malloc(core->stateSize(core));events=tmpfile();expect(diagnostic_state && events,"allocate host probe state");
    struct mCoreCallbacks callbacks={0};callbacks.videoFrameStarted=synthetic_objects;core->addCoreCallbacks(core,&callbacks);
    tick();tick();expect(risk_frames && checked_cases && !missing_cases,"overlapping duplicate sprites exceed budget but have no visible gaps");
    visible_last=1;tick();tick();expect(missing_cases>0,"a late visible sprite exceeds budget and paired native rendering detects missing pixels");
    printf("PASS: budget-only and actual missing OBJ pixels distinguished (%u verified missing cases)\n",missing_cases);
    fclose(events);free(diagnostic_state);mCoreConfigDeinit(&core->config);core->deinit(core);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
