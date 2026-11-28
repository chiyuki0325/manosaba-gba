/* Flash128K regression for the reported Act01 Chapter01 Adv24 dialogue. */
#define MANOSABA_SAVE_LIBRARY
#include "save_game_test.c"
#include <mgba-util/crc32.h>

static unsigned u16(const uint8_t* p)
{
    return p[0] | p[1] << 8;
}
static unsigned u32(const uint8_t* p)
{
    return u16(p) | (unsigned)u16(p + 2) << 16;
}
static void check_record(int slot, unsigned script, const char* summary)
{
    const struct GBASavedata* flash = &((struct GBA*)core->board)->memory.savedata;
    expect(flash->type == SAVEDATA_FLASH1M, "inspect both physical banks");
    const uint8_t* latest = NULL;
    for (int i = 0; i < 30; ++i)
    {
        const uint8_t* p = flash->data + 4096 * i;
        if (!memcmp(p, "MNSV", 4) && p[63] == 0xa5 && p[22] == slot && (!latest || u32(p + 4) > u32(latest + 4)))
            latest = p;
    }
    expect(latest && u32(latest + 24) == doCrc32(latest, 24), "latest committed header");
    unsigned size = 192 + u16(latest + 18) + u16(latest + 20);
    expect(size <= 4096 - 64 && u32(latest + 12) == doCrc32(latest + 64, size), "latest record CRC");
    expect(u16(latest + 72) == script, "latest record contains the expected script");
    if (summary)
        expect(!strcmp((const char*)latest + 86, summary), "exact reported dialogue");
}
static void select_row(int view, int row)
{
    while (state().selected != (unsigned)row)
    {
        tap(state().selected < (unsigned)row ? KEY_DOWN : KEY_UP);
        wait_ui(view);
    }
}
static void save_day(int slot, unsigned script, const char* summary)
{
    open_list(0);
    select_row(0, slot);
    tap(KEY_A);
    wait_ui(0);
    if (state().confirm)
    {
        tap(KEY_RIGHT);
        tap(KEY_A);
        wait_ui(0);
    }
    check_record(slot, script, summary);
    tap(KEY_B);
    wait_ui(2);
    tap(KEY_B);
    wait_game();
}
int main(int argc, char** argv)
{
    expect(argc == 4, "usage: save_adv24_game_test ROM output-dir direct|route (direct requires debug ROM)");
    int route = !strcmp(argv[3], "route");
    expect(route || !strcmp(argv[3], "direct"), "select run mode");
    output = argv[2];
    save_address = elf_symbol("save_runtime");
    trial_address = elf_symbol("trial_runtime");
    graphics_address = elf_symbol("_ZN8manosaba20graphics_performanceE");
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
    if (route)
    {
        core->reset(core);
        wait_for_title();
        press(KEY_A);
    }
    else
    {
        debug_boot();
        press(KEY_DOWN);
        press(KEY_A);
        for (int i = 0; i < 23; ++i)
            press(KEY_DOWN);
    }
    press(KEY_A);
    const char* report = "汉娜她不可能是凶手！";
    unsigned saved = 0;
    for (int guard = 0;; ++guard)
    {
        unsigned day = state().script + 1, pc = state().pc, vm = trial().vm_state;
        if (guard > 100000 || day > 24)
        {
            fprintf(stderr, "unexpected route at day=%u pc=%u vm=%u\n", day, pc, vm);
            screenshot(output, "route-stopped");
            return 1;
        }
        if (day == 24 && pc == 5546 && vm == 1)
        {
            key_frames(0, 220);
            if (route)
                for (int slot = 23; slot < MANUAL_SLOTS; ++slot)
                    save_day(slot, 23, report);
            save_day(0, 23, report);
            check_record(27, 23, NULL);
            if (route)
                for (int slot = 1; slot < 23; ++slot)
                    check_record(slot, slot, NULL);
            break;
        }
        if (route && day < 24 && !(saved & (1u << day)) && (vm == 1 || (day == 11 && vm == 2)))
        {
            key_frames(0, 220);
            save_day(day - 1, day - 1, NULL);
            saved |= 1u << day;
            fprintf(stderr, "saved Adv%02u\n", day);
        }
        else if (vm == 2)
        {
            key_frames(0, 180);
            if (day != 11 && day != 19)
                tap(KEY_DOWN);
            tap(KEY_A);
        }
        else
            key_frames(KEY_R, 4);
    }
    if (route)
        expect(saved == (1u << 24) - 2, "saved every preceding daily segment");
    char path[1024];
    snprintf(path, sizeof(path), "%s/adv24-%s.sav", output, argv[3]);
    FILE* file = fopen(path, "wb");
    expect(file && fwrite(((struct GBA*)core->board)->memory.savedata.data, 1, 131072, file) == 131072,
           "export Flash image for cartridge comparison");
    expect(!fclose(file), "flush exported Flash");
    for (int manual = 0; manual <= 1; ++manual)
    {
        core->reset(core);
        wait_for_title();
        press(KEY_A);
        press(KEY_DOWN);
        press(KEY_A);
        // The title menu does not publish the story renderer's paint counter.
        expect(state().view == 1 && state().selected == 0, "title load list opens on auto");
        if (manual)
            press(KEY_DOWN);
        press(KEY_A);
        expect(state().confirm != 0, "confirm cold load");
        press(KEY_RIGHT);
        press(KEY_A);
        wait_game();
        expect(state().script == 23, "cold-loaded Adv24");
        if (manual)
            expect(state().pc == 5546, "cold-loaded exact reported line from slot 01");
    }
    printf("PASS: %s Adv24 slot 01, auto CRC and cold-load PC 5546\n", argv[3]);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
}
