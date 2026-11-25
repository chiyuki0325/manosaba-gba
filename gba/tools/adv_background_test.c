/* Actual ADV02 transition plus a controlled uncover/recover of its lower CG. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main

static unsigned profile;
static unsigned metric(int index)
{
    return core->busRead32(core, profile + index * 4);
}
static unsigned image_command(unsigned bank, unsigned line, unsigned* target)
{
    unsigned commands = core->busRead32(core, bank + 4), events = core->busRead32(core, bank + 12);
    for (unsigned i = 0; i < core->busRead16(core, bank + 20); ++i)
        if (core->busRead16(core, events + i * 6 + 4) == line)
        {
            unsigned first = core->busRead16(core, events + i * 6), count = core->busRead16(core, events + i * 6 + 2);
            for (unsigned n = 0; n < count; ++n)
            {
                unsigned command = commands + (first + n) * 12;
                if (!core->busRead8(core, command + 8))
                {
                    *target = core->busRead8(core, command + 9);
                    return core->busRead32(core, command);
                }
            }
        }
    expect(0, "find source background image command");
    return 0;
}
static void top_palette(unsigned assets, unsigned image, const char* message)
{
    unsigned palette = core->busRead32(core, assets + image * 36 + 8);
    int matches = 0;
    for (int i = 1; i < 112; ++i)
        matches += core->busRead16(core, 0x05000000 + (112 + i) * 2) == core->busRead16(core, palette + i * 2);
    fprintf(stderr, "top image=%u palette_matches=%d/111\n", image, matches);
    expect(matches == 111, message);
}
int main(int argc, char** argv)
{
    expect(argc == 4, "ROM output graphics_profile");
    profile = strtoul(argv[3], NULL, 16);
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
    press(KEY_DOWN);
    press(KEY_A);
    for (int i = 0; i < 80 && metric(5) < 35; ++i)
        press(KEY_A);
    expect(metric(4) == 1 && metric(5) == 35, "reach ADV02 prison dialogue at source line 80");
    screenshot(argv[2], "adv02-prison");
    unsigned banks = elf_symbol("_ZN8manosaba14graphics_banksE"), assets = elf_symbol("_ZN8manosaba14graphic_assetsE");
    expect(banks && assets, "resolve matching graphics descriptors");
    unsigned bank = banks + 24;
    unsigned room_slot, cg_slot, room = image_command(bank, 75, &room_slot), cg = image_command(bank, 28, &cg_slot);
    top_palette(assets, room, "new prison background replaces the old CG");
    unsigned commands = core->busRead32(core, bank + 4), keys = core->busRead32(core, bank + 8), stage = 0;
    for (unsigned at = 0x02000000; at < 0x0203fff8; at += 4)
        if (core->busRead32(core, at) == keys && core->busRead32(core, at + 4) == commands)
        {
            expect(!stage, "unique stage player");
            stage = at;
        }
    expect(stage, "locate stage player");
    // Hide only the covering background; the old CG must still be recoverable.
    core->busWrite32(core, stage + 8 + room_slot * 60 + 16, 0);
    frames(180);
    top_palette(assets, cg, "covered CG returns when the upper background is hidden");
    screenshot(argv[2], "adv02-uncovered-cg");
    core->busWrite32(core, stage + 8 + room_slot * 60 + 16, 256);
    frames(180);
    top_palette(assets, room, "room returns after covering the CG again");
    screenshot(argv[2], "adv02-prison-restored");
    uint32_t restored = frame_hash();
    press(KEY_START);
    press(KEY_B);
    expect(frame_hash() == restored, "pause return preserves the corrected background and dialogue");
    puts("PASS: real ADV02 background switch, covered-layer recovery and re-cover");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
