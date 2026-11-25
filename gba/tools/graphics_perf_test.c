/* Native hardware renderer integration/performance probe, executed on the emulated GBA CPU. */
#define main navigation_smoke_main
#include "smoke_test.c"
#undef main

static unsigned profile_address;
static uint32_t histogram[16384];
static unsigned samples, missed, scene_peak, graphics_peak, vblank_peak;
static int peak_event;
static unsigned read_metric(int index)
{
    return core->busRead32(core, profile_address + index * 4);
}
static void sample_frames(int count)
{
    while (count--)
    {
        frames(1);
        if ((core->busRead16(core, 0x04000000) & 7) > 1)
            continue;
        unsigned ticks = read_metric(6), graphics = read_metric(0);
        if (ticks > scene_peak)
        {
            scene_peak = ticks;
            peak_event = (int)read_metric(5);
        }
        if (graphics > graphics_peak)
            graphics_peak = graphics;
        if (ticks < 16384)
            ++histogram[ticks];
        ++samples;
        missed = read_metric(2);
        vblank_peak = read_metric(8);
        static unsigned spikes = 0;
        if (ticks > 3500 && spikes++ < 40)
            fprintf(stderr, "spike: core=%u graphics=%u ui=%u update=%u bank=%u event=%u\n", ticks, graphics,
                    read_metric(9), read_metric(10), read_metric(4), read_metric(5));
    }
}
static void step(void)
{
    core->setKeys(core, KEY_A);
    sample_frames(4);
    core->setKeys(core, 0);
    sample_frames(180);
}
int main(int argc, char** argv)
{
    expect(argc == 4, "usage: graphics_perf_test ROM screenshot_directory profile_address_hex");
    profile_address = (unsigned)strtoul(argv[3], NULL, 16);
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
    adv04_selector();
    core->setKeys(core, KEY_A);
    sample_frames(4);
    core->setKeys(core, 0);
    sample_frames(360);
    screenshot(argv[2], "native-first");
    expect((core->busRead16(core, 0x04000000) & 7) == 1, "native Mode 1 scene");
    unsigned first_bank = read_metric(4);
    unsigned last_event = read_metric(5), stalled = 0;
    for (int i = 0; i < 700 && read_metric(4) == first_bank; ++i)
    {
        step();
        unsigned event = read_metric(5);
        stalled = event == last_event ? stalled + 1 : 0;
        last_event = event;
        if (stalled > 25)
            break;
        unsigned mode = core->busRead16(core, 0x04000000) & 7;
        // Clear CGs deliberately switch to regular BG Mode 0. Detect an actual
        // assertion by its error screen, rather than treating Mode 0 as a crash.
        unsigned red = 0, black = 0;
        for (int pixel = 0; pixel < 240 * 160; ++pixel)
        {
            unsigned c = pixels[pixel] & 0xffffffu;
            black += c == 0;
            red += pixel < 240 * 45 && (c & 255) > 200 && ((c >> 8) & 255) < 90 && ((c >> 16) & 255) < 90;
        }
        if (mode > 1 || (red > 20 && black > 31000))
            screenshot(argv[2], "native-assertion");
        expect(mode <= 1 && !(red > 20 && black > 31000),
               "native renderer remains active without allocator/assert failure");
        static int captured_regular = 0;
        if (!mode && !captured_regular)
        {
            captured_regular = 1;
            screenshot(argv[2], "native-regular-cg");
            fprintf(stderr, "regular CG: bank=%u event=%u mode=0\n", read_metric(4), read_metric(5));
        }
        if (i == 4 || i == 19 || i == 49 || i == 99 || i == 199 || i == 299)
        {
            char name[40];
            snprintf(name, sizeof(name), "native-step-%d", i + 1);
            screenshot(argv[2], name);
        }
    }
    screenshot(argv[2], "native-next-chapter");
    if (read_metric(4) == first_bank)
    {
        fprintf(stderr, "stalled event=%u\n", read_metric(5));
        fprintf(stderr, "loading=%u clock=%u\n", read_metric(11), read_metric(12));
    }
    expect(read_metric(4) != first_bank, "ADV04 continues to the next native graphics bank");
    unsigned cumulative = 0, p50 = 0, p95 = 0, p99 = 0;
    for (unsigned ticks = 0; ticks < 16384; ++ticks)
    {
        cumulative += histogram[ticks];
        if (!p50 && cumulative >= samples / 2)
            p50 = ticks;
        if (!p95 && cumulative >= samples * 95 / 100)
            p95 = ticks;
        if (!p99 && cumulative >= samples * 99 / 100)
            p99 = ticks;
    }
    printf("native: samples=%u, core ticks p50=%u p95=%u p99=%u peak=%u(event %d), graphics peak=%u, vblank peak=%u, "
           "missed=%u, next bank=%u\n",
           samples, p50, p95, p99, scene_peak, peak_event, graphics_peak, vblank_peak, missed, read_metric(4));
    char path[512];
    snprintf(path, sizeof(path), "%s/performance.json", argv[2]);
    FILE* report = fopen(path, "w");
    expect(report != NULL, "write performance report");
    const char* sha = getenv("MANOSABA_ROM_SHA256");
    fprintf(report,
            "{\n  \"rom_sha256\": \"%s\",\n  \"ticks_per_frame\": 4389,\n  \"samples\": %u,\n  \"p50_ticks\": %u,\n  "
            "\"p95_ticks\": %u,\n  \"p99_ticks\": %u,\n  \"peak_ticks\": %u,\n  \"graphics_peak_ticks\": %u,\n  "
            "\"vblank_peak_ticks\": %u,\n  \"missed_frames\": %u,\n  \"next_bank\": %u\n}\n",
            sha ? sha : "", samples, p50, p95, p99, scene_peak, graphics_peak, vblank_peak, missed, read_metric(4));
    fclose(report);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
