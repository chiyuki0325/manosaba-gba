#define main navigation_smoke_main
#include "smoke_test.c"
#undef main
static unsigned address;
static unsigned metric(int i)
{
    return core->busRead32(core, address + 4 * i);
}
static void tap(unsigned key)
{
    core->setKeys(core, key);
    frames(4);
    core->setKeys(core, 0);
    frames(12);
}
int main(int argc, char** argv)
{
    expect(argc == 4 || argc == 6, "ROM directory profile_address [first end]");
    address = (unsigned)strtoul(argv[3], 0, 16);
    int first = argc == 6 ? atoi(argv[4]) : 0, end = argc == 6 ? atoi(argv[5]) : 230;
    core = mCoreFind(argv[1]);
    expect(core && core->init(core), "mGBA init");
    mCoreInitConfig(core, 0);
    mCoreConfigSetDefaultIntValue(&core->config, "skipBios", 1);
    mCoreConfigSetDefaultIntValue(&core->config, "logLevel", mLOG_FATAL | mLOG_ERROR);
    struct mStandardLogger log;
    mStandardLoggerInit(&log);
    mStandardLoggerConfig(&log, &core->config);
    mLogSetDefaultLogger(&log.d);
    mCoreLoadConfig(core);
    core->setVideoBuffer(core, pixels, 240);
    expect(mCoreLoadFile(core, argv[1]), "load ROM");
    for (int bank = first; bank < end; ++bank)
    {
        debug_boot();
        tap(KEY_DOWN);
        tap(KEY_A);
        frames(12);
        for (int i = 0; i < bank / 6; ++i)
            tap(KEY_RIGHT);
        for (int i = 0; i < bank % 6; ++i)
            tap(KEY_DOWN);
        tap(KEY_A);
        frames(240);
        for (int i = 0; i < 8; ++i)
        {
            tap(KEY_A);
            frames(140);
        }
        unsigned mode = core->busRead16(core, 0x04000000) & 7;
        if (mode != 1)
        {
            int red = 0;
            for (int p = 0; p < 240 * 40; ++p)
                if ((pixels[p] & 255) > 180 && ((pixels[p] >> 8) & 255) < 50 && ((pixels[p] >> 16) & 255) < 50)
                    ++red;
            if (red >= 10)
            {
                char name[60];
                snprintf(name, sizeof(name), "bank-failure-%03d", bank);
                screenshot(argv[2], name);
            }
            expect(red < 10, "chapter has no Butano assertion screen");
            char name[60];
            snprintf(name, sizeof(name), "bank-mode0-%03d", bank);
            screenshot(argv[2], name);
        }
        fprintf(stderr, "bank=%d runtime=%u event=%u mode=%u clock=%u\n", bank, metric(4), metric(5), mode, metric(12));
    }
    printf("PASS: chapter entrances [%d,%d) and initial dialogue/menu flow\n", first, end);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&log);
    return 0;
}
