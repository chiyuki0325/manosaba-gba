/* Host-side integration check. Requires libmgba-dev; never linked into the ROM. */
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct mCore* core;
static color_t pixels[240 * 160];

enum
{
    KEY_A = 1,
    KEY_B = 2,
    KEY_SELECT = 4,
    KEY_START = 8,
    KEY_RIGHT = 16,
    KEY_LEFT = 32,
    KEY_UP = 64,
    KEY_DOWN = 128,
    KEY_R = 256,
    KEY_L = 512
};

static void expect(int condition, const char* message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void frames(int count)
{
    while (count--)
        core->runFrame(core);
}

static void press(unsigned key)
{
    core->setKeys(core, key);
    frames(4);
    core->setKeys(core, 0);
    frames(260); // Finish bounded image uploads and the longest three-line text reveal.
}

// The cartridge exports this byte; the host pokes it to skip menu detours.
// Resolve it lazily from the ROM's ELF next to argv[1] (/proc/self/cmdline).
#define TEST_UNUSED __attribute__((unused))
static TEST_UNUSED const char* rom_argument(void)
{
    static char arguments[1024];
    static int loaded = 0;
    if (!loaded)
    {
        loaded = 1;
        FILE* file = fopen("/proc/self/cmdline", "rb");
        if (file)
        {
            size_t size = fread(arguments, 1, sizeof arguments - 2, file);
            fclose(file);
            arguments[size] = 0;
        }
    }
    const char* first = arguments;
    if (!*first)
        return 0;
    const char* second = first + strlen(first) + 1;
    return *second ? second : 0;
}

static TEST_UNUSED unsigned elf_symbol(const char* name)
{
    static char elf[768];
    if (!elf[0])
    {
        const char* rom = rom_argument();
        if (!rom || strlen(rom) >= sizeof elf - 8)
            return 0;
        strcpy(elf, rom);
        char* dot = strrchr(elf, '.');
        if (dot)
            strcpy(dot, ".elf");
        else
            strcat(elf, ".elf");
    }
    char command[900];
    snprintf(command, sizeof command,
             "/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm '%s' 2>/dev/null", elf);
    FILE* pipe = popen(command, "r");
    if (!pipe)
        return 0;
    char line[512], symbol[256];
    unsigned address = 0, found = 0;
    while (fgets(line, sizeof line, pipe))
        if (sscanf(line, "%x %*c %255s", &address, symbol) == 2 && !strcmp(symbol, name))
        {
            found = 1;
            break;
        }
    pclose(pipe);
    return found ? address : 0;
}

// Wait for actual startup completion, including the notice, upload and title fade.
static TEST_UNUSED unsigned home_profile_address(void)
{
    static unsigned address;
    if (!address)
        address = elf_symbol("home_runtime");
    expect(address, "home runtime profile is exported");
    return address;
}
static TEST_UNUSED void wait_for_title(void)
{
    unsigned address = home_profile_address();
    for (int i = 0; i < 700; ++i)
    {
        frames(1);
        if (!core->busRead32(core, address) && core->busRead32(core, address + 12))
        {
            frames(2);
            return;
        }
    }
    expect(0, "startup notice and title finish");
}

// Arm the one-shot hotkey; the next Start consumes it and clears it.
// 1 opens the debug menu, 2 returns to the current chapter selector.
static unsigned hotkey_address;
static TEST_UNUSED void hotkey_arm(unsigned request)
{
    if (core->busRead32(core, home_profile_address()))
        wait_for_title();
    if (!hotkey_address)
        hotkey_address = elf_symbol("debug_hotkey");
    expect(hotkey_address, "debug hotkey symbol is exported");
    core->busWrite8(core, hotkey_address, request);
    frames(2);
}
static TEST_UNUSED void debug_hotkey(void)
{
    hotkey_arm(1);
}
static TEST_UNUSED void chapters_hotkey(void)
{
    hotkey_arm(2);
}

// Start the debug menu directly from the title or any playing scene.
// The 150-frame wait lets the title picture upload, the global Flash read and the
// 32-frame black fade finish; opening debug earlier leaks the BG fade into later
// screens and dims every pixel the visual probes compare.
static void debug_menu_navigate(void)
{
    wait_for_title();
    debug_hotkey();
    core->setKeys(core, KEY_START);
    frames(4);
    core->setKeys(core, 0);
    frames(10);
}

static void debug_boot(void)
{
    core->reset(core);
    debug_menu_navigate();
}

// Open the normal story selector at ADV04; leave the final A to each probe.
static void adv04_selector(void)
{
    press(KEY_DOWN);
    press(KEY_A);
    for (int i = 0; i < 3; ++i)
        press(KEY_DOWN);
}

static uint32_t frame_hash(void)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 240 * 160; ++i)
        hash = (hash ^ (pixels[i] & 0xffffffu)) * 16777619u;
    return hash;
}

static TEST_UNUSED void key_frames(unsigned key, int count)
{
    core->setKeys(core, key);
    frames(count);
}

static TEST_UNUSED int playback_arrows(int x)
{
    const int offsets[] = {5, 9, 12};
    int count = 0;
    for (int i = 0; i < 3; ++i)
    {
        int white = 0;
        for (int y = 5; y <= 13; ++y)
        {
            unsigned pixel = pixels[y * 240 + x + offsets[i]];
            white += (pixel & 255) >= 240 && ((pixel >> 8) & 255) >= 240 && ((pixel >> 16) & 255) >= 230;
        }
        count += white == 9;
    }
    return count;
}

static TEST_UNUSED void double_tap_r(void)
{
    key_frames(KEY_R, 4);
    key_frames(0, 4);
    key_frames(KEY_R, 4);
    key_frames(0, 4);
}

static void screenshot(const char* directory, const char* name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    FILE* file = fopen(path, "wb");
    expect(file != NULL, "open screenshot");
    fputs("P6\n240 160\n255\n", file);
    for (int i = 0; i < 240 * 160; ++i)
    {
        fputc(pixels[i] & 255, file);
        fputc((pixels[i] >> 8) & 255, file);
        fputc((pixels[i] >> 16) & 255, file);
    }
    fclose(file);
}

int main(int argc, char** argv)
{
    expect(argc == 3, "usage: smoke_test ROM screenshot_directory");
    core = mCoreFind(argv[1]);
    expect(core != NULL && core->init(core), "initialize mGBA");
    mCoreInitConfig(core, NULL);
    mCoreConfigSetDefaultIntValue(&core->config, "skipBios", 1);
    mCoreConfigSetDefaultIntValue(&core->config, "logLevel", mLOG_FATAL | mLOG_ERROR | mLOG_WARN);
    struct mStandardLogger logger;
    mStandardLoggerInit(&logger);
    mStandardLoggerConfig(&logger, &core->config);
    mLogSetDefaultLogger(&logger.d);
    mCoreLoadConfig(core);
    core->setVideoBuffer(core, pixels, 240);
    expect(mCoreLoadFile(core, argv[1]), "load ROM");
    core->reset(core);
    wait_for_title();
    expect((core->busRead16(core, 0x04000000) & 7) == 0, "Butano Mode 0");
    uint32_t boot = frame_hash();
    screenshot(argv[2], "title");
    press(KEY_B);
    press(KEY_START);
    expect(frame_hash() == boot, "B and Start stay on the title instead of opening debug");
    press(KEY_A);
    uint32_t menu = frame_hash();
    expect(menu != boot, "A opens the start menu");
    screenshot(argv[2], "start-menu");
    press(KEY_DOWN);
    expect(frame_hash() != menu, "down moves the start-menu selection");
    press(KEY_UP);
    expect(frame_hash() == menu, "up restores the start-menu selection");
    press(KEY_B);
    expect(frame_hash() == boot, "B returns from the start menu to the title");
    press(KEY_A);
    for (int i = 0; i < 4; ++i)
        press(KEY_DOWN);
    press(KEY_A);
    uint32_t options = frame_hash();
    expect(options != menu, "options page differs from the start menu");
    screenshot(argv[2], "options");
    for (int i = 0; i < 4; ++i)
        press(KEY_DOWN);
    press(KEY_A);
    expect(frame_hash() != options && frame_hash() != boot, "debug entry opens the debug menu");
    screenshot(argv[2], "debug-menu");

    uint32_t scenes[6];
    for (int i = 0; i < 6; ++i)
    {
        debug_boot();
        for (int j = 0; j < i; ++j)
            press(KEY_DOWN);
        press(KEY_A);
        scenes[i] = frame_hash();
        for (int j = 0; j < i; ++j)
            expect(scenes[i] != scenes[j], "six remaining scene entrances stay distinct");
    }
    debug_boot();
    press(KEY_DOWN);
    press(KEY_A);
    expect(frame_hash() == scenes[1], "script entrance opens selector");
    screenshot(argv[2], "story-selector");
    press(KEY_RIGHT);
    expect(frame_hash() != scenes[1], "script selector pages forward");
    press(KEY_LEFT);
    expect(frame_hash() == scenes[1], "script selector returns to first page");
    press(KEY_UP);
    screenshot(argv[2], "story-selector-last");
    press(KEY_DOWN);
    expect(frame_hash() == scenes[1], "script selector wraps across all scripts");
    debug_boot();
    adv04_selector();
    press(KEY_A);
    expect((core->busRead16(core, 0x04000000) & 7) == 1, "ADV04 uses native Mode 1 BG/OBJ hardware");
    uint32_t graphic_first = frame_hash();
    screenshot(argv[2], "adv04-first");
    int white = 0, dark = 0;
    for (int y = 120; y < 144; ++y)
        for (int x = 25; x < 230; ++x)
        {
            unsigned rgb = pixels[y * 240 + x];
            if ((rgb & 255) > 230 && ((rgb >> 8) & 255) > 230)
                ++white;
            if ((rgb & 255) < 100 && ((rgb >> 8) & 255) < 100)
                ++dark;
        }
    expect(white > 10 && dark > 100, "opaque dot-matrix text over translucent decorated panel");
    uint16_t ui_palette[16];
    for (int i = 0; i < 16; ++i)
        ui_palette[i] = core->busRead16(core, 0x050001e0 + i * 2);
    press(KEY_START);
    press(KEY_B);
    expect(frame_hash() == graphic_first, "graphical story pauses and resumes without reloading");
    key_frames(KEY_R, 4);
    expect(playback_arrows(216) == 2, "R shows two fast-forward arrows while held");
    key_frames(0, 36);
    key_frames(KEY_R, 12);
    key_frames(0, 48);
    expect(frame_hash() == graphic_first, "a second R press inside the window does not count if released too late");
    double_tap_r();
    expect(playback_arrows(216) == 1, "quick R taps switch to one autoplay arrow on the second release");
    press(KEY_START);
    press(KEY_B);
    expect(frame_hash() == graphic_first, "closing the pause menu does not resume autoplay");
    double_tap_r();
    key_frames(KEY_R, 4);
    expect(playback_arrows(216) == 2, "R interrupts autoplay and shows fast-forward immediately");
    key_frames(0, 4);
    expect(frame_hash() == graphic_first, "releasing the interrupting R leaves normal playback");
    key_frames(KEY_R, 4);
    key_frames(0, 120);
    expect(frame_hash() == graphic_first, "the interrupting R does not count toward another double-tap");
    double_tap_r();
    uint32_t auto_first = frame_hash();
    frames(90);
    expect(frame_hash() == auto_first, "autoplay does not use the 48-frame fast-forward interval");
    frames(30);
    expect(frame_hash() != auto_first && playback_arrows(216) == 1, "autoplay advances without R held");
    key_frames(KEY_R, 4);
    key_frames(0, 120);
    uint32_t graphic_second = frame_hash();
    press(KEY_A);
    uint32_t graphic_third = frame_hash();
    debug_boot();
    adv04_selector();
    press(KEY_A);
    screenshot(argv[2], "adv04-restart");
    expect(frame_hash() == graphic_first, "graphical story restarts deterministically");
    core->setKeys(core, KEY_A);
    frames(1);
    core->setKeys(core, 0);
    frames(12);
    uint32_t partial = frame_hash();
    expect(partial != graphic_first && partial != graphic_second, "new dialogue begins partially revealed");
    screenshot(argv[2], "adv04-typewriter-partial");
    frames(8);
    expect(frame_hash() != partial, "text reveals without advancing the script");
    core->setKeys(core, KEY_A);
    frames(180);
    expect(frame_hash() == graphic_second, "A while revealing fills this page only");
    frames(120);
    expect(frame_hash() == graphic_second, "holding A does not skip subsequent dialogue");
    screenshot(argv[2], "adv04-typewriter-filled");
    core->setKeys(core, 0);
    frames(1);
    core->setKeys(core, KEY_A);
    frames(20);
    expect(frame_hash() == graphic_second, "short release bounce cannot advance");
    core->setKeys(core, 0);
    frames(12);
    core->setKeys(core, KEY_A);
    frames(260);
    expect(frame_hash() == graphic_third, "a fresh A press advances exactly one dialogue");
    frames(120);
    expect(frame_hash() == graphic_third, "long A press remains on the new dialogue");
    core->setKeys(core, 0);
    frames(12);
    for (int i = 0; i < 15; ++i)
        press(KEY_A);
    screenshot(argv[2], "adv04-lounge");
    for (int i = 0; i < 13; ++i)
        if (i != 3)
            expect(core->busRead16(core, 0x050001e0 + i * 2) == ui_palette[i],
                   "background changes preserve fixed UI colors");
    press(KEY_SELECT);
    screenshot(argv[2], "adv04-return-list");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    puts("PASS: story flow, translucent UI, gradual text, A hold/bounce isolation, pause/resume and native graphics "
         "navigation");
    return 0;
}
