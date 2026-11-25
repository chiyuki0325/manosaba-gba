/* Host-only rendering of offline peak states through the production renderer.
 * Scene injection validates geometry/resources; it does not prove route reachability. */
#define MANOSABA_SCENE_PROBE_LIBRARY
#include "graphics_budget_scene_probe.c"

static unsigned stage;
static void write_bytes(unsigned address, const void* source, unsigned count)
{
    const unsigned char* p = source;
    for (unsigned i = 0; i < count; ++i)
        core->busWrite8(core, address + i, p[i]);
}
int main(int argc, char** argv)
{
    expect(argc == 8, "ROM output profile banks snapshots track-offset tail-offset");
    profile = strtoul(argv[3], NULL, 16);
    unsigned banks = strtoul(argv[4], NULL, 16);
    unsigned track = strtoul(argv[6], NULL, 0), tail = strtoul(argv[7], NULL, 0);
    core = mCoreFind(argv[1]);
    expect(core && core->init(core), "initialize mGBA");
    mCoreInitConfig(core, NULL);
    mCoreConfigSetDefaultIntValue(&core->config, "skipBios", 1);
    mCoreConfigSetDefaultIntValue(&core->config, "logLevel", mLOG_FATAL | mLOG_ERROR);
    struct mStandardLogger logger;
    mStandardLoggerInit(&logger);
    mStandardLoggerConfig(&logger, &core->config);
    mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);
    core->setVideoBuffer(core, pixels, 240);
    expect(mCoreLoadFile(core, argv[1]), "load ROM");
    debug_boot();
    tap(KEY_DOWN);
    tap(KEY_A);
    for (int i = 0; i < 21 / 6; ++i)
        tap(KEY_RIGHT);
    for (int i = 0; i < 21 % 6; ++i)
        tap(KEY_DOWN);
    tap(KEY_A);
    unsigned cooldown = 140;
    for (unsigned f = 0; f < 30000 && !(metric(4) == 21 && metric(5) >= 39); ++f)
    {
        if (!cooldown)
        {
            core->setKeys(core, KEY_A);
            cooldown = 166;
        }
        else if (cooldown == 164)
            core->setKeys(core, 0);
        --cooldown;
        frames(1);
    }
    core->setKeys(core, 0);
    frames(240);
    expect(metric(4) == 21 && metric(5) == 39, "reach stationary production renderer");
    unsigned commands = core->busRead32(core, banks + 21 * 24 + 4), keys = core->busRead32(core, banks + 21 * 24 + 8);
    for (unsigned a = 0x02000000; a < 0x02040000 - tail; a += 4)
        if (core->busRead32(core, a) == keys && core->busRead32(core, a + 4) == commands)
        {
            expect(!stage, "unique stage player");
            stage = a;
        }
    expect(stage != 0, "locate production stage player");
    for (unsigned i = track; i < tail; ++i)
        core->busWrite8(core, stage + i, 0);
    for (int i = 0; i < 3; ++i)
        core->busWrite32(core, stage + 8 + i * 60 + 16, 0);
    frames(180); // Fresh loads exercise cache admission, not an already-resident pose.
    void* baseline = malloc(core->stateSize(core));
    expect(baseline && core->saveState(core, baseline), "save stationary baseline");
    FILE* input = fopen(argv[5], "rb");
    expect(input != NULL, "open peak snapshots");
    int h[16];
    unsigned char states[960];
    uint64_t masks[3];
    while (fread(h, sizeof(h), 1, input) == 1)
    {
        expect(fread(states, sizeof(states), 1, input) == 1 && fread(masks, sizeof(masks), 1, input) == 1,
               "read peak snapshot");
        expect(core->loadState(core, baseline), "restore independent snapshot baseline");
        /* ARM stage_player ends after its final bool, rounded to four bytes.
         * image_scene stores the active graphics_bank immediately after it. */
        core->busWrite32(core, stage + ((tail + 4) & ~3u), banks + h[0] * 24);
        for (int s = 0; s < 3; ++s)
            if (h[10 + s] >= 0 && h[13 + s] < 3)
            {
                short image = h[10 + s];
                memcpy(states + h[13 + s] * 60 + 48, &image, 2);
            }
        write_bytes(stage + 8, states, sizeof(states));
        frames(180);
        char name[80];
        snprintf(name, sizeof(name), "bank%03d-event%03d", h[0], h[1]);
        printf("source scene=%s line=%d offset=%d offline_cycles=%d\n", name, h[2], h[5], h[7]);
        if ((core->busRead16(core, 0x04000000) & 7) > 1)
        {
            printf("REJECTED scene=%s injected-state assertion\n", name);
            screenshot(argv[2], name);
            continue;
        }
        compare(argv[2], name);
        if (getenv("MANOSABA_CACHE_TRANSLATION_TEST"))
        {
            unsigned initial = core->busRead32(core, stage + 8);
            const unsigned shifts[] = {64, 128, 192, 0};
            for (unsigned shift = 0; shift < 4; ++shift)
            {
                core->busWrite32(core, stage + 8, initial + shifts[shift]);
                for (int frame = 0; frame < 60; ++frame)
                {
                    frames(1);
                    expect(!metric(11), "translation does not reload a cached pose");
                }
                unsigned objects = 0, affine = 0;
                for (int i = 0; i < 128; ++i)
                {
                    unsigned a = core->busRead16(core, 0x07000000 + i * 8);
                    if (!(a & 256) && (a & 512))
                        continue;
                    ++objects;
                    affine += !!(a & 256);
                }
                expect(objects && !affine, "fractional translation retains ordinary OBJ cache");
            }
            puts("PASS: cached pose follows fractional translations without reloading");
        }
    }
    free(baseline);
    fclose(input);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
