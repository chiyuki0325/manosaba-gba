/* Load a real save through the production UI, save v5 and cold-load it again. */
#define MANOSABA_SAVE_LIBRARY
#include "save_game_test.c"
#include <mgba-util/vfs.h>
static unsigned half(const uint8_t* p)
{
    return p[0] | p[1] << 8;
}
static uint32_t word32(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t story_picture(void)
{
    uint32_t hash = 2166136261u;
    // The live trial clock legitimately advances between pausing and saving.
    for (int i = (trial().duration != 0 ? 24 * 240 : 0); i < 240 * 160; ++i)
        hash = (hash ^ pixels[i]) * 16777619u;
    return hash;
}

int main(int argc, char** argv)
{
    expect(argc == 7, "ROM output save_address trial_address graphics_address input_save");
    output = argv[2];
    save_address = strtoul(argv[3], NULL, 16);
    trial_address = strtoul(argv[4], NULL, 16);
    graphics_address = strtoul(argv[5], NULL, 16);
    core = mCoreFind(argv[1]);
    expect(core && core->init(core), "initialize mGBA");
    mCoreInitConfig(core, NULL);
    mCoreConfigSetDefaultIntValue(&core->config, "skipBios", 1);
    mCoreConfigSetDefaultIntValue(&core->config, "logLevel", mLOG_FATAL | mLOG_ERROR);
    struct mStandardLogger logger;
    mStandardLoggerInit(&logger);
    mStandardLoggerConfig(&logger, &core->config);
    mLogSetDefaultLogger(&logger.d);
    mCoreConfigSetDefaultIntValue(&core->config, "mute", 0);
    mCoreConfigSetDefaultIntValue(&core->config, "volume", 256);
    mCoreLoadConfig(core);
    core->setVideoBuffer(core, pixels, 240);
    expect(mCoreLoadFile(core, argv[1]), "load ROM");
    FILE* file = fopen(argv[6], "rb");
    expect(file != NULL, "open original save");
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    uint8_t* original = malloc(size);
    expect(original && fread(original, 1, size, file) == (size_t)size, "read original save");
    fclose(file);
    expect(core->loadSave(core, VFileMemChunk(original, size)), "mount original save in the actual backend");
    free(original);
    for (int ch = 0; ch < 2; ++ch)
        blip_set_rates(core->getAudioChannel(core, ch), 16777216, 32768);
    debug_boot();
    adv04_selector();
    press(KEY_A);
    open_list(1);
    tap(KEY_DOWN);
    wait_ui(1);
    tap(KEY_A);
    wait_ui(1);
    tap(KEY_RIGHT);
    tap(KEY_A);
    wait_game();
    unsigned script = state().script, pc = state().pc;
    uint32_t picture = story_picture();
    screenshot(output, "migrated-story");
    if (state().seed_size)
        expect(state().seed_size == 1254, "attached legacy retry was converted to the fixed v5 body");
    open_list(0);
    while (state().selected)
    {
        tap(KEY_UP);
        wait_ui(0);
    }
    tap(KEY_A);
    wait_ui(0);
    screenshot(output, "overwrite-check");
    expect(state().confirm, "overwrite mounted manual record");
    tap(KEY_RIGHT);
    tap(KEY_A);
    wait_ui(0);
#if !MANOSABA_TEST_SRAM
    if (getenv("MANOSABA_EXPAND_LEGACY"))
    {
        for (int slot = 12; slot < MANUAL_SLOTS; ++slot)
        {
            while (state().selected != (unsigned)slot)
            {
                tap(KEY_DOWN);
                wait_ui(0);
            }
            save_slot();
            expect(state().script == script && state().pc == pc,
                   "legacy compaction leaves the current story anchor untouched");
        }
        fprintf(stderr, "stage: filled all new manual slots over the full legacy directory\n");
    }
#endif
    size_t count = MANOSABA_TEST_SRAM ? 32768u : 131072u;
    uint8_t* bytes = malloc(count);
    expect(bytes != NULL, "copy physical save capacity");
    if (MANOSABA_TEST_SRAM)
    {
        for (unsigned i = 0; i < count; ++i)
            bytes[i] = core->busRead8(core, 0x0e000000 + i);
    }
    else
    {
        uint8_t first[8];
        for (int i = 0; i < 8; ++i)
            first[i] = core->busRead8(core, 0x0e000000 + i);
        for (unsigned bank = 0; bank < 2; ++bank)
        {
            core->busWrite8(core, 0x0e005555, 0xaa);
            core->busWrite8(core, 0x0e002aaa, 0x55);
            core->busWrite8(core, 0x0e005555, 0xb0);
            core->busWrite8(core, 0x0e000000, bank);
            for (unsigned i = 0; i < 65536; ++i)
                bytes[bank * 65536 + i] = core->busRead8(core, 0x0e000000 + i);
        }
        core->busWrite8(core, 0x0e005555, 0xaa);
        core->busWrite8(core, 0x0e002aaa, 0x55);
        core->busWrite8(core, 0x0e005555, 0xb0);
        core->busWrite8(core, 0x0e000000, memcmp(first, bytes, 8) != 0);
    }
    const uint8_t* latest = NULL;
    for (int page = 0; page < (MANOSABA_TEST_SRAM ? 13 : 32); ++page)
    {
        const uint8_t* h = (uint8_t*)bytes + record_address(page);
        if (!memcmp(h, "MNSV", 4) && h[63] == 0xa5 && h[22] == 0 &&
            (!latest || (int32_t)(word32(h + 4) - word32(latest + 4)) > 0))
            latest = h;
    }
    expect(latest && half(latest + 16) == 5 && half(latest + 18) == 1254,
           "normal save upgrades only the selected slot to v5");
    char path[1024];
    snprintf(path, sizeof(path), "%s/upgraded.sav", output);
    file = fopen(path, "wb");
    expect(file != NULL, "export upgraded save");
    fwrite(bytes, 1, count, file);
    fclose(file);
    free(bytes);
    debug_boot();
    adv04_selector();
    press(KEY_A);
    open_list(1);
    tap(KEY_DOWN);
    wait_ui(1);
    tap(KEY_A);
    wait_ui(1);
    tap(KEY_RIGHT);
    tap(KEY_A);
    wait_game();
    screenshot(output, "cold-reloaded");
    expect(state().script == script && state().pc == pc && story_picture() == picture,
           "cold v5 load preserves the mounted story anchor and target picture");
    printf("PASS: mounted input, lazy v5 write, exact cold reload; backend=%s body=1254 PCM=%u\n",
           MANOSABA_TEST_SRAM ? "SRAM32K" : "Flash128K", nonzero);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
