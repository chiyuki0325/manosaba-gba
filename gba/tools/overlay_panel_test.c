/* Full-screen overlays must retain the lower scanlines used by the panel. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main

static int ui_ink(int x, int y)
{
    for (int id = 0; id < 2; ++id)
    {
        unsigned cnt = core->busRead16(core, 0x04000008 + id * 2);
        if ((cnt & 128) || (cnt & 3))
            continue;
        unsigned cell = core->busRead16(core, 0x06000000 + ((cnt >> 8) & 31) * 2048 + (y / 8 * 32 + x / 8) * 2);
        unsigned byte = core->busRead8(core, 0x06000000 + ((cnt >> 2) & 3) * 16384 + (cell & 1023) * 32 + (y % 8) * 4 +
                                                 (x % 8) / 2);
        return (byte >> ((x & 1) * 4)) & 15;
    }
    return 0;
}

int main(int argc, char** argv)
{
    expect(argc == 3, "ROM output");
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
    frames(4);
    core->setKeys(core, 0);
    frames(9);
    screenshot(argv[2], "adv04-overlay");
    int samples = 0, matches = 0;
    for (int x = 40; x < 200; ++x)
    {
        unsigned upper = pixels[80 * 240 + x] & 0xffffffu;
        if ((upper & 255) < 192 || ((upper >> 8) & 255) < 192)
            continue;
        for (int y = 104; y < 136; ++y)
            if (!ui_ink(x, y))
            {
                ++samples;
                matches += (pixels[y * 240 + x] & 0xffffffu) == upper;
            }
    }
    expect(samples > 1000 && matches == samples, "Adv04 opening dissolve covers the dialogue area too");

    debug_boot();
    press(KEY_DOWN);
    press(KEY_A);
    // Act01 Chapter01 Bad06 is selectable story 41.
    for (int i = 0; i < 41 / 6; ++i)
        press(KEY_RIGHT);
    for (int i = 0; i < 41 % 6; ++i)
        press(KEY_DOWN);
    press(KEY_A);
    for (int i = 0; i < 62; ++i)
        press(KEY_A);
    screenshot(argv[2], "bad06-black-dialogue");
    int text = 0;
    for (int y = 104; y < 160; ++y)
        for (int x = 8; x < 232; ++x)
        {
            unsigned rgb = pixels[y * 240 + x] & 0xffffffu;
            if (ui_ink(x, y))
            {
                text += (rgb & 255) > 230;
                continue;
            }
            expect(!rgb, "Bad06 black dialogue cannot reveal old scenery under its panel");
        }
    expect(text > 100, "Bad06 retains its dialogue over the black scene");
    uint32_t black = frame_hash();
    press(KEY_START);
    press(KEY_B);
    expect(frame_hash() == black, "pause restores the same full-screen black overlay and text");
    chapters_hotkey();
    press(KEY_START);
    press(KEY_A);
    screenshot(argv[2], "bad06-panel-restored");
    int scenery = 0;
    for (int y = 104; y < 144; ++y)
        for (int x = 32; x < 208; ++x)
            scenery += !ui_ink(x, y) && !!(pixels[y * 240 + x] & 0xffffffu);
    expect(scenery > 1000, "normal translucent dialogue returns after restarting the chapter");
    puts("PASS: Adv04 full-height dissolve, Bad06 black dialogue, pause/resume and normal panel restoration");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
