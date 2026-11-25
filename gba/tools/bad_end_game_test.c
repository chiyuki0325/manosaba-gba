/* Real bad-ending scripts and an actual discussion timeout use the same black UI. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/trial_runtime.h"
#include "../src/witch_book_runtime.h"
static unsigned trial_address, book_address;
static uint8_t expected[240 * 160];
static trial_runtime_profile trial(void)
{
    trial_runtime_profile p;
    uint32_t* words = (uint32_t*)&p;
    for (unsigned i = 0; i < sizeof(p) / 4; ++i)
        words[i] = core->busRead32(core, trial_address + i * 4);
    return p;
}
static witch_book_profile book(void)
{
    witch_book_profile p;
    uint32_t* words = (uint32_t*)&p;
    for (unsigned i = 0; i < sizeof(p) / 4; ++i)
        words[i] = core->busRead32(core, book_address + i * 4);
    return p;
}
static int bad_screen(void)
{
    for (int i = 0; i < 240 * 160; ++i)
        if (!!(pixels[i] & 0xffffffu) != expected[i])
            return 0;
    return 1;
}
int main(int argc, char** argv)
{
    expect(argc == 8, "ROM output trial_address bad_script_index expected_mask common_script_index book_address");
    trial_address = strtoul(argv[3], NULL, 16);
    unsigned bad = atoi(argv[4]);
    unsigned common = atoi(argv[6]);
    book_address = strtoul(argv[7], NULL, 16);
    FILE* reference = fopen(argv[5], "rb");
    expect(reference && fread(expected, 1, sizeof(expected), reference) == sizeof(expected),
           "load independent Unifont mask");
    fclose(reference);
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
    press(KEY_DOWN);
    press(KEY_A);
    for (unsigned i = 0; i < bad / 6; ++i)
        press(KEY_RIGHT);
    for (unsigned i = 0; i < bad % 6; ++i)
        press(KEY_DOWN);
    press(KEY_A);
    for (int n = 0; n < 150 && !bad_screen(); ++n)
        press(KEY_A);
    frames(24);
    expect(bad_screen(), "ProcessBadEnd has only Unifont BAD END and the chevron, with exact margins");
    screenshot(argv[2], "bad-end-story");
    press(KEY_START);
    press(KEY_START);
    expect(bad_screen(), "pause restores the black bad-ending screen");
    press(KEY_A);
    expect(!bad_screen(), "A returns from the bad ending to chapter selection");
    for (int run = 0; run < 2; ++run)
    {
        unsigned offset = run ? 72 : 1, ending = common + (run ? 6 : 0);
        debug_boot();
        press(KEY_DOWN);
        press(KEY_DOWN);
        press(KEY_A);
        for (unsigned i = 0; i < offset / 6; ++i)
            press(KEY_RIGHT);
        for (unsigned i = 0; i < offset % 6; ++i)
            press(KEY_DOWN);
        press(KEY_A);
        for (int n = 0; n < 100 && trial().state != 6; ++n)
            press(KEY_A);
        expect(trial().state == 6, "formal trial reaches its discussion");
        for (int n = 0; n < 500 && book().script < common; ++n)
        {
            unsigned state = trial().state;
            if (!trial().preview && (state == 6 || state == 10))
                frames(1024);
            else
                press(KEY_A);
        }
        frames(120);
        fprintf(stderr, "failure dialogue: script=%u expected=%u state=%u\n", book().script, ending, book().state);
        expect(book().script == ending && book().state != 4 && !bad_screen(),
               "actual discussion timeout enters the protagonist's original failure dialogue before BAD END");
        screenshot(argv[2], run ? "trial-failure-hiro-dialogue" : "trial-failure-ema-dialogue");
        for (int n = 0; n < 400 && !bad_screen(); ++n)
            press(KEY_A);
        frames(24);
        expect(bad_screen(), "failure dialogue completes before the same exact black ending screen");
        screenshot(argv[2], run ? "bad-end-trial-hiro" : "bad-end-trial-ema");
        press(KEY_A);
        expect(!bad_screen(), "A returns from failure to the trial selector");
    }
    puts("PASS: story BAD END, trial timeout plays original dialogue first, exact Unifont pixels, chevron/margins, "
         "pause and A return");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
