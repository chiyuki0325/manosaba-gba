#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/save_runtime.h"
#include "../src/trial_runtime.h"
#include <stddef.h>
static unsigned saves, vm;
static unsigned field(unsigned base, unsigned offset)
{
    return core->busRead32(core, base + offset);
}
static void ui(int view)
{
    for (int n = 0; n < 1800; ++n)
    {
        frames(1);
        if ((int)field(saves, offsetof(save_runtime_profile, view)) == view &&
            field(saves, offsetof(save_runtime_profile, paint)) == 6 &&
            !field(saves, offsetof(save_runtime_profile, busy)))
            return;
    }
    expect(0, "save UI becomes ready");
}
static void game(void)
{
    for (int n = 0; n < 1800; ++n)
    {
        frames(1);
        if ((int)field(saves, offsetof(save_runtime_profile, view)) == -1 &&
            !field(saves, offsetof(save_runtime_profile, restoring)))
        {
            frames(260);
            return;
        }
    }
    expect(0, "retry reconstruction completes");
}
static int state(void)
{
    return (int)field(vm, offsetof(trial_runtime_profile, vm_state));
}
static unsigned script(void)
{
    return field(saves, offsetof(save_runtime_profile, script));
}
int main(int argc, char** argv)
{
    expect(argc == 3, "ROM output");
    core = mCoreFind(argv[1]);
    expect(core && core->init(core), "init mGBA");
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
    saves = elf_symbol("save_runtime");
    vm = elf_symbol("trial_runtime");
    expect(saves && vm, "resolve runtime");
    debug_boot();
    press(KEY_DOWN);
    press(KEY_A);
    for (int i = 0; i < 4; ++i)
        press(KEY_DOWN);
    press(KEY_A);
    for (int n = 0; n < 120 && state() != 2; ++n)
        press(KEY_A);
    expect(script() == 4 && state() == 2, "reach ADV05 choice");
    expect(field(saves, offsetof(save_runtime_profile, seed_size)) > 0, "story choice captures retry seed");
    uint32_t choice = frame_hash();
    unsigned pc = field(saves, offsetof(save_runtime_profile, pc));
    screenshot(argv[2], "story-retry-choice");
    void* original = malloc(core->stateSize(core));
    expect(original && core->saveState(core, original), "save independent choice fixture");
    press(KEY_START);
    ui(2);
    press(KEY_DOWN);
    press(KEY_A);
    ui(0);
    press(KEY_A);
    ui(0);
    press(KEY_B);
    ui(2);
    press(KEY_B);
    game();
    expect(frame_hash() == choice, "manual save keeps the exact choice pixels");
    press(KEY_A);
    for (int n = 0; n < 200 && state() != 4; ++n)
        press(KEY_A);
    expect(state() == 4 && script() != 4, "bad choice plays its full ending");
    press(KEY_A);
    ui(2);
    screenshot(argv[2], "story-retry-actions");
    press(KEY_A);
    game();
    expect(script() == 4 && state() == 2 && field(saves, offsetof(save_runtime_profile, pc)) == pc,
           "retry lands directly on the existing choice");
    expect(frame_hash() == choice, "retry restores every choice pixel without replaying dialogue");
    screenshot(argv[2], "story-retry-restored");
    // A loaded manual record must carry this same choice checkpoint.
    press(KEY_START);
    ui(2);
    press(KEY_DOWN);
    press(KEY_DOWN);
    press(KEY_A);
    ui(1);
    press(KEY_DOWN);
    press(KEY_A);
    ui(1);
    press(KEY_RIGHT);
    press(KEY_A);
    game();
    expect(script() == 4 && state() == 2 && frame_hash() == choice, "load restores choice and its attached retry seed");
    press(KEY_A);
    for (int n = 0; n < 200 && state() != 4; ++n)
        press(KEY_A);
    expect(state() == 4, "loaded record reaches bad ending");
    press(KEY_A);
    ui(2);
    press(KEY_A);
    game();
    expect(state() == 2 && frame_hash() == choice, "loaded record retries to its own choice");
    press(KEY_DOWN);
    press(KEY_A);
    uint32_t continued = frame_hash();
    expect(script() == 4 && state() != 2, "a different choice continues after retry");
    expect(core->loadState(core, original), "restore independent choice fixture");
    free(original);
    press(KEY_DOWN);
    press(KEY_A);
    expect(frame_hash() == continued, "continuation equals choosing the good option without a bad branch");
    puts("PASS: ordinary bad ending retries directly to choice; exact pixels, independent state, save/load seed and "
         "good continuation");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
