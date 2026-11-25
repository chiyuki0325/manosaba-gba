/* Actual ADV input, checking that the compiled shake moves the rendered BG. */
#define main unused_navigation_main
#include "smoke_test.c"
#undef main
#include "../src/graphics_profile.h"
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/renderers/video-software.h>
static unsigned address;
static struct graphics_profile profile(void)
{
    struct graphics_profile p;
    uint32_t* v = (uint32_t*)&p;
    for (unsigned i = 0; i < sizeof(p) / 4; ++i)
        v[i] = core->busRead32(core, address + i * 4);
    return p;
}
static unsigned bg_x(int i)
{
    struct GBAVideoSoftwareRenderer* r = (struct GBAVideoSoftwareRenderer*)((struct GBA*)core->board)->video.renderer;
    return i == 2 ? (unsigned)r->bg[i].refx : r->bg[i].x;
}
static void tap(unsigned key)
{
    core->setKeys(core, key);
    frames(2);
    core->setKeys(core, 0);
    frames(14);
}
int main(int argc, char** argv)
{
    expect(argc == 7, "ROM output profile script_ordinal graphics_bank shake_event");
    address = strtoul(argv[3], NULL, 16);
    unsigned ordinal = atoi(argv[4]), bank = atoi(argv[5]), event = atoi(argv[6]);
    core = mCoreFind(argv[1]);
    expect(core && core->init(core), "mGBA init");
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
    for (unsigned i = 0; i < ordinal / 6; ++i)
        tap(KEY_RIGHT);
    for (unsigned i = 0; i < ordinal % 6; ++i)
        tap(KEY_DOWN);
    tap(KEY_A);
    unsigned frame = 0;
    for (; frame < 30000; ++frame)
    {
        struct graphics_profile p = profile();
        if (p.bank == (int)bank && p.event >= (int)event)
            break;
        core->setKeys(core, frame % 100 < 2 ? KEY_A : 0);
        frames(1);
    }
    core->setKeys(core, 0);
    if (frame >= 30000)
    {
        screenshot(argv[2], "unreached");
        struct ARMCore* cpu = ((struct GBA*)core->board)->cpu;
        fprintf(stderr, "stack=%08x\n", cpu->gprs[ARM_SP]);
        for (unsigned at = (unsigned)cpu->gprs[ARM_SP]; at < 0x03007f00; at += 4)
        {
            unsigned v = core->busRead32(core, at);
            if (v >= 0x08000000 && v < 0x0a000000)
                fprintf(stderr, "return=%08x\n", v);
        }
        fprintf(stderr, "wanted ordinal=%u bank=%u event=%u; actual bank=%d event=%d loading=%u clock=%u\n", ordinal,
                bank, event, profile().bank, profile().event, profile().loading, profile().clock);
    }
    expect(frame < 30000, "source shake is reached with normal input");
    for (unsigned n = 0; n < 1200 && profile().loading; ++n)
        frames(1);
    expect(!profile().loading, "shake scene finishes resource upload");
    fprintf(stderr, "reached frame=%u bank=%d event=%d target=%u clock=%u loading=%u\n", frame, profile().bank,
            profile().event, event, profile().clock, profile().loading);
    unsigned previous[4], moving = 0, peak = 0;
    for (int i = 0; i < 4; ++i)
        previous[i] = bg_x(i);
    for (unsigned n = 0; n < 72; ++n)
    {
        frames(1);
        struct graphics_profile p = profile();
        if (p.core_ticks > peak)
            peak = p.core_ticks;
        unsigned display = core->busRead16(core, 0x04000000);
        for (int bg = 0; bg < 4; ++bg)
            if (display & (0x100u << bg))
            {
                unsigned cnt = core->busRead16(core, 0x04000008 + bg * 2), value = bg_x(bg);
                if ((cnt & 3) > 0 && value != previous[bg])
                    ++moving;
                previous[bg] = value;
            }
        if (n == 10)
            screenshot(argv[2], "shake");
    }
    expect(moving > 1, "existing shake reaches BG hardware positions");
    printf("PASS: source shake ordinal=%u bank=%u event=%u moves=%u peak=%u ticks\n", ordinal, bank, event, moving,
           peak);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
