/* Measure queued play -> real sequencer start -> audible PCM on the real ROM. */
#define main unused_navigation_main
#include "smoke_test.c"
#undef main
#include <mgba/core/blip_buf.h>

static unsigned layer, frame_peak;
static void audio(int count)
{
    short left[4096], right[4096];
    while (count--)
    {
        core->runFrame(core);
        frame_peak = 0;
        int n = blip_read_samples(core->getAudioChannel(core, 0), left, 4096, 0);
        blip_read_samples(core->getAudioChannel(core, 1), right, 4096, 0);
        for (int i = 0; i < n; ++i)
        {
            unsigned v = abs(left[i]) + abs(right[i]);
            if (v > frame_peak)
                frame_peak = v;
        }
    }
}
static void key(unsigned k)
{
    core->setKeys(core, k);
    audio(2);
    core->setKeys(core, 0);
    audio(18);
}
int main(int argc, char** argv)
{
    expect(argc == 4, "ROM layer catalog_plan");
    layer = strtoul(argv[2], NULL, 16);
    core = mCoreFind(argv[1]);
    expect(core && core->init(core), "initialize mGBA");
    mCoreInitConfig(core, NULL);
    mCoreConfigSetDefaultIntValue(&core->config, "skipBios", 1);
    mCoreConfigSetDefaultIntValue(&core->config, "logLevel", mLOG_FATAL | mLOG_ERROR);
    mCoreConfigSetDefaultIntValue(&core->config, "mute", 0);
    mCoreConfigSetDefaultIntValue(&core->config, "volume", 256);
    struct mStandardLogger logger;
    mStandardLoggerInit(&logger);
    mStandardLoggerConfig(&logger, &core->config);
    mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);
    core->setVideoBuffer(core, pixels, 240);
    expect(mCoreLoadFile(core, argv[1]), "load ROM");
    core->reset(core);
    for (int ch = 0; ch < 2; ++ch)
        blip_set_rates(core->getAudioChannel(core, ch), 16777216, 32768);
    debug_menu_navigate();
    for (int i = 0; i < 4; ++i)
        key(KEY_DOWN);
    key(KEY_A);
    for (int i = 0; i < 16; ++i)
        key(KEY_UP);
    FILE* plan = fopen(argv[3], "r");
    expect(plan != NULL, "catalog plan");
    char name[64];
    unsigned made;
    int maximum = 0;
    while (fscanf(plan, "%63s %u", name, &made) == 2)
    {
        key(KEY_SELECT);
        audio(120);
        if (made)
        {
            int start = -1, onset = -1;
            core->setKeys(core, KEY_A);
            for (int f = 0; f < 480; ++f)
            {
                if (f == 2)
                    core->setKeys(core, 0);
                audio(1);
                if (start < 0 && core->busRead8(core, layer + 8))
                    start = f;
                if (onset < 0 && frame_peak > 128)
                    onset = f;
                if (f > 30 && onset >= 0)
                    break;
            }
            expect(start >= 0 && start < 8, "new module starts within 8 native frames, without a loading delay");
            expect(onset >= 0, "audition emits PCM within 8 seconds");
            if (start > maximum)
                maximum = start;
            printf("%s start_frame=%d audible_frame=%d\n", name, start, onset);
            fflush(stdout);
        }
        key(KEY_DOWN);
    }
    fclose(plan);
    printf("PASS: maximum_backend_start_frame=%d\n", maximum);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
