/* Same artwork: a clear appearance, then a different script's real blur. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/save_runtime.h"
static unsigned profile, save_address;
static const char* output;
static unsigned metric(int i)
{
    return core->busRead32(core, profile + i * 4);
}
static save_runtime_profile saved(void)
{
    save_runtime_profile p;
    for (unsigned i = 0; i < sizeof(p) / 4; ++i)
        ((uint32_t*)&p)[i] = core->busRead32(core, save_address + 4 * i);
    return p;
}
static void tap(unsigned key)
{
    core->setKeys(core, key);
    frames(3);
    core->setKeys(core, 0);
    frames(40);
}
static void select_script(int script)
{
    debug_boot();
    tap(KEY_DOWN);
    tap(KEY_A);
    for (int i = 0; i < script / 6; ++i)
        tap(KEY_RIGHT);
    for (int i = 0; i < script % 6; ++i)
        tap(KEY_DOWN);
    tap(KEY_A);
    frames(600);
}
static void wait_ui(int view)
{
    for (int i = 0; i < 1800; ++i)
    {
        frames(1);
        save_runtime_profile p = saved();
        if ((int)p.view == view && p.paint == 6 && !p.busy)
        {
            frames(4);
            return;
        }
    }
    screenshot(output, "save-timeout");
    save_runtime_profile last = saved();
    fprintf(stderr, "SAVE_TIMEOUT wanted=%d view=%u paint=%u busy=%u operation=%u selected=%u confirm=%u progress=%u\n",
            view, last.view, last.paint, last.busy, last.operation, last.selected, last.confirm, last.progress);
    expect(0, "save UI settles");
}
static void wait_game(void)
{
    for (int i = 0; i < 1800; ++i)
    {
        frames(1);
        if ((int)saved().view == -1 && !saved().restoring)
        {
            frames(220);
            return;
        }
    }
    expect(0, "CG restore finishes");
}
int main(int argc, char** argv)
{
    expect(argc == 6, "ROM output profile clear_script blur_script");
    output = argv[2];
    profile = strtoul(argv[3], 0, 16);
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
    save_address = elf_symbol("save_runtime");
    select_script(atoi(argv[4]));
    expect(!(core->busRead16(core, 0x04000000) & 7), "a blur-capable artwork's clear appearance uses ordinary BG");
    screenshot(output, "clear-appearance");
    uint32_t original = frame_hash();
    tap(KEY_SELECT);
    frames(120);
    tap(KEY_B);
    frames(180);
    expect(frame_hash() == original, "book returns to the same clear appearance");
    tap(KEY_L);
    frames(120);
    tap(KEY_B);
    frames(180);
    expect(frame_hash() == original, "history returns to the same clear appearance");
    tap(KEY_START);
    wait_ui(2);
    tap(KEY_DOWN);
    tap(KEY_A);
    wait_ui(0);
    screenshot(output, "clear-save-preview");
    tap(KEY_A);
    wait_ui(0);
    tap(KEY_B);
    wait_ui(2);
    tap(KEY_B);
    wait_game();
    expect(frame_hash() == original, "save preview leaves clear CG unchanged");
    tap(KEY_START);
    wait_ui(2);
    tap(KEY_DOWN);
    tap(KEY_DOWN);
    tap(KEY_A);
    wait_ui(1);
    tap(KEY_DOWN);
    tap(KEY_A);
    wait_ui(1);
    expect(saved().confirm, "load confirmation");
    tap(KEY_RIGHT);
    tap(KEY_A);
    wait_game();
    expect(!(core->busRead16(core, 0x04000000) & 7) && frame_hash() == original,
           "Flash reload preserves the chosen ordinary backend and exact image");
    select_script(atoi(argv[5]));
    unsigned banks = elf_symbol("_ZN8manosaba14graphics_banksE");
    int found = 0;
    for (int i = 0; i < 80; ++i)
    {
        unsigned bank = banks + metric(4) * 24, events = core->busRead32(core, bank + 12),
                 line = core->busRead16(core, events + metric(5) * 6 + 4);
        if (line >= 47)
        {
            found = 1;
            break;
        }
        tap(KEY_A);
    }
    expect(found, "reach real CG300_1 blur");
    expect((core->busRead16(core, 0x04000000) & 7) == 1, "the appearance that actually blurs keeps affine Mode 1");
    screenshot(output, "blur-appearance");
    puts("PASS: per-appearance CG backend, actual blur, book/history and exact Flash save/load restore");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&log);
    return 0;
}
