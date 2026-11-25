/* Walk each production execution entry; hold/decay, pause, audio and continuation. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/execution_runtime.h"
#include "../src/save_runtime.h"
#include <mgba/core/blip_buf.h>
#include <stddef.h>
static unsigned address, save, nonzero;
static const char* output;
static execution_runtime_profile metric(void)
{
    execution_runtime_profile p;
    uint32_t* w = (uint32_t*)&p;
    for (unsigned i = 0; i < sizeof(p) / 4; ++i)
        w[i] = core->busRead32(core, address + 4 * i);
    return p;
}
static void run(int count)
{
    short samples[4096];
    while (count--)
    {
        frames(1);
        for (int ch = 0; ch < 2; ++ch)
        {
            int n = blip_read_samples(core->getAudioChannel(core, ch), samples, 4096, 0);
            for (int i = 0; i < n; ++i)
                nonzero += samples[i] != 0;
        }
    }
}
static void key(unsigned k)
{
    core->setKeys(core, k);
    run(2);
    core->setKeys(core, 0);
    run(14);
}
static void ready(void)
{
    for (int i = 0; i < 600; ++i)
    {
        run(1);
        if (metric().ready)
            return;
    }
    screenshot(output, "not-ready");
    expect(0, "execution UI becomes ready");
}
int main(int argc, char** argv)
{
    expect(argc == 6, "ROM output execution_runtime save_runtime script_index");
    output = argv[2];
    address = strtoul(argv[3], NULL, 16);
    save = strtoul(argv[4], NULL, 16);
    int index = atoi(argv[5]);
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
    key(KEY_DOWN);
    key(KEY_A);
    for (int i = 0; i < index / 6; ++i)
        key(KEY_RIGHT);
    for (int i = 0; i < index % 6; ++i)
        key(KEY_DOWN);
    key(KEY_A);
    for (int i = 0; i < 40000 && !metric().active; ++i)
    {
        core->setKeys(core, i % 8 < 2 ? KEY_A : 0);
        run(1);
    }
    core->setKeys(core, 0);
    expect(metric().active, "source script reaches real execution UI");
    ready();
    run(5);
    screenshot(output, "idle");
    unsigned before = nonzero;
    core->setKeys(core, KEY_A);
    run(120);
    expect(metric().charge >= 560 && metric().charge < 620, "A charges at the original rate");
    expect(nonzero - before > 1000, "charging produces audible PCM");
    screenshot(output, "charging");
    core->setKeys(core, 0);
    unsigned charged = metric().charge;
    run(20);
    expect(metric().charge + 100 < charged, "releasing A drains progress");
    key(KEY_B);
    key(KEY_L);
    key(KEY_SELECT);
    expect(metric().active && !metric().complete, "B/L/Select cannot confirm or escape");
    key(KEY_START);
    unsigned frozen = metric().charge;
    run(60);
    expect(metric().charge == frozen, "pause freezes the partial fill");
    key(KEY_START);
    ready();
    run(5);
    expect(metric().charge <= frozen, "resume does not finish or add progress");
    unsigned first_pc = core->busRead32(core, save + offsetof(save_runtime_profile, pc));
    core->setKeys(core, KEY_A);
    unsigned held = 0;
    while (!metric().complete && held < 450)
    {
        run(1);
        ++held;
    }
    expect(metric().complete && held >= 300 && held < 410, "continuous hold reaches full charge");
    core->setKeys(core, 0);
    run(8);
    expect(metric().active && metric().delay > 70, "confirmation keeps the original 1.6-second dwell");
    screenshot(output, "confirmed");
    before = nonzero;
    run(50);
    expect(nonzero - before > 1000, "completion produces audible PCM");
    expect(metric().active, "script stays blocked during confirmation dwell");
    for (int n = 0; n < 160 && metric().active; ++n)
        run(1);
    expect(!metric().active, "execution completes automatically after its dwell");
    run(150);
    expect(core->busRead32(core, save + offsetof(save_runtime_profile, pc)) != first_pc,
           "ExecutionConfirmed resumes the original source commands");
    screenshot(output, "after");
    printf("script=%d peak=%u draw_peak=%u missed=%u hold=%u pcm=%u\n", index, metric().max_ticks, metric().max_draw,
           metric().missed, held, nonzero);
    expect(metric().max_ticks < 4389 && !metric().missed, "execution remains within the frame budget");
    puts("PASS: formal execution, A hold/release, ignored shortcuts, frozen pause, both audible cues and source "
         "continuation");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
