/* Select the production presentation fixture; exercise shared native code/data. */
#define main navigation_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/trial_runtime.h"
#include "obj_pixels.h"
static unsigned address, peak;
static const char* directory;
static uint16_t expected[18][320 * 192];
static uint8_t forbidden[18][240 * 160];
static trial_runtime_profile metric(void)
{
    trial_runtime_profile p;
    uint32_t* v = (uint32_t*)&p;
    for (unsigned i = 0; i < sizeof(p) / 4; ++i)
        v[i] = core->busRead32(core, address + i * 4);
    return p;
}
static void sample(int count)
{
    while (count--)
    {
        frames(1);
        unsigned ticks = metric().core_ticks;
        if (ticks > peak)
            peak = ticks;
    }
}
static void tap(unsigned key)
{
    core->setKeys(core, key);
    sample(2);
    core->setKeys(core, 0);
    sample(12);
}
static void ready(void)
{
    for (int i = 0; i < 1200; ++i)
    {
        sample(1);
        if (metric().state == 1 && metric().interaction_ready)
        {
            sample(80);
            return;
        }
    }
    fprintf(stderr, "state=%u camera=%u frame=%u ready=%u bg=%u\n", metric().state, metric().camera,
            metric().camera_frame, metric().scene_ready, metric().background);
    screenshot(directory, "failure");
    expect(0, "presentation reaches a readable statement");
}
static uint32_t portrait_hash(void)
{
    uint32_t hash = 2166136261u;
    for (int y = 0; y < 88; ++y)
        for (int x = 0; x < 240; ++x)
            hash = (hash ^ pixels[y * 240 + x]) * 16777619u;
    return hash;
}
static void geometry(void)
{
    int background = (int)metric().background - 10;
    expect(background >= 0 && background < 18, "representative mirror uses its chapter's background atlas");
    unsigned errors = 0;
    for (int y = 0; y < 80; y += 2)
        for (int x = 0; x < 240; x += 2)
        {
            if (object_ink(x, y))
                continue;
            unsigned rgb = pixels[y * 240 + x];
            unsigned actual = ((rgb >> 3) & 31) | (((rgb >> 11) & 31) << 5) | (((rgb >> 19) & 31) << 10);
            errors += actual != expected[background][(y + 16) * 320 + x + 40];
        }
    if (errors)
        fprintf(stderr, "BG mismatch background=%d pixels=%u\n", background, errors);
    expect(!errors, "visible BG pixels match the corresponding offline original-model view");
    for (int y = 0; y < 160; ++y)
        for (int x = 0; x < 240; ++x)
            if (forbidden[background][y * 240 + x])
                expect(!object_ink(x, y), "poses never cover a nearer podium");
}
int main(int argc, char** argv)
{
    expect(argc == 9, "usage: test ROM output profile Court Final masks motions fixture_script");
    directory = argv[2];
    address = (unsigned)strtoul(argv[3], NULL, 16);
    for (int stage = 0; stage < 2; ++stage)
    {
        FILE* file = fopen(argv[4 + stage], "rb");
        expect(file != NULL, "open representative BG reference");
        expect(fread(expected + stage * 9, 1, 9 * sizeof(expected[0]), file) == 9 * sizeof(expected[0]),
               "read representative BG reference");
        fclose(file);
    }
    FILE* file = fopen(argv[6], "rb");
    expect(file != NULL, "open podium depth reference");
    expect(fread(forbidden, 1, sizeof(forbidden), file) == sizeof(forbidden), "read podium depth reference");
    fclose(file);
    file = fopen(argv[7], "r");
    expect(file != NULL, "open reduced motion cases");
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
    expect(mCoreLoadFile(core, argv[1]), "load production ROM");
    core->reset(core);
    debug_menu_navigate();
    tap(KEY_DOWN);
    tap(KEY_A);
    sample(80);
    tap(KEY_UP);
    tap(KEY_A);
    ready();
    expect(metric().script == (unsigned)atoi(argv[8]), "chapter list selects the explicitly named non-story fixture");
    uint32_t initial[2] = {0};
    unsigned initial_pose[2] = {0};
    for (int index = 0; index < 13; ++index)
    {
        ready();
        expect(metric().layout == (unsigned)(index < 6 ? 0 : 10), "ordinary views use 13/14-seat chapter layout");
        geometry();
        char name[80];
        snprintf(name, sizeof(name), "view-%02d-bg-%02u", index, metric().background - 1);
        screenshot(directory, name);
        if (index == 0 || index == 6)
        {
            int stage = index / 6;
            initial[stage] = portrait_hash();
            initial_pose[stage] = metric().pose;
        }
        if (index == 5 || index == 11)
        {
            int stage = index / 6;
            expect(metric().pose != initial_pose[stage] && portrait_hash() != initial[stage],
                   "source pose switch changes the visible upper body while sharing the same background");
        }
        if (index == 12)
            expect(metric().actor == 13, "fourteenth seat renders Yuki");
        tap(KEY_A);
    }
    int motion, duration, layout, background, cases = 0;
    while (fscanf(file, "%d %d %d %d", &motion, &duration, &layout, &background) == 4)
    {
        ready();
        tap(KEY_A);
        int paused = 0, middle = 0;
        for (int guard = 0; guard < duration + 600; ++guard)
        {
            trial_runtime_profile p = metric();
            if (p.state == 1 && p.interaction_ready && p.camera_frame == (unsigned)duration)
                break;
            if (!paused && p.camera_frame >= (unsigned)(duration / 3))
            {
                tap(KEY_START);
                unsigned frame = metric().camera_frame;
                sample(60);
                expect(metric().camera_frame == frame, "pause freezes reduced camera progress");
                tap(KEY_START);
                paused = 1;
            }
            if (!middle && p.scene_ready && p.camera_frame >= (unsigned)(duration / 2))
            {
                char name[80];
                snprintf(name, sizeof(name), "motion-%02d-middle", cases + 1);
                screenshot(directory, name);
                middle = 1;
            }
            sample(1);
        }
        ready();
        expect(metric().camera == (unsigned)motion + 1 && metric().camera_frame == (unsigned)duration,
               "reduced motion retains source identity and duration");
        expect(metric().layout == (unsigned)layout && metric().background == (unsigned)background + 1,
               "compound endpoint selects correct layout and distance/composition");
        geometry();
        char name[80];
        snprintf(name, sizeof(name), "motion-%02d-end", cases + 1);
        screenshot(directory, name);
        expect(paused && middle, "every reduced motion is observed during playback and pause");
        tap(KEY_A);
        ++cases;
    }
    fclose(file);
    expect(cases == 8, "all eight formerly absent compound motions are exercised");
    printf("PASS: reduced presentation views=13 motions=%d frames=%u peak_ticks=%u missed=%u\n", cases, metric().frame,
           peak, metric().missed);
    expect(!metric().missed, "presentation fits production frame budget including pose/background transitions");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
