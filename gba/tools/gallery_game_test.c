/* Real user paths and Flash persistence. Not linked into the cartridge. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/gallery_runtime.h"
#include "../src/graphics_profile.h"
#include <mgba/core/blip_buf.h>
#include <mgba/internal/gba/gba.h>
#include <string.h>
static unsigned gallery_address, graphics_address, music_address, pcm, peak, samples;
static const char* output;
static gallery_runtime_profile state(void);
static void native_pixels(void)
{
    unsigned directory = elf_symbol("_ZN8manosaba14gallery_imagesE"),
             assets = elf_symbol("_ZN8manosaba14graphic_assetsE");
    unsigned data = elf_symbol("_ZN8manosaba22graphic_regular_imagesE"),
             payload = elf_symbol("_ZN8manosaba14graphics_bytesE");
    unsigned image = core->busRead16(core, directory + state().image * 12), at = 0;
    for (unsigned i = 0; i < 157; ++i)
        if (core->busRead16(core, data + i * 12 + 4) == image)
        {
            at = data + i * 12;
            break;
        }
    expect(at != 0, "native view has a shared regular descriptor");
    unsigned asset = assets + image * 36, variants = core->busRead32(core, asset + 4),
             tiles = core->busRead32(core, variants);
    unsigned palette = core->busRead32(core, asset + 8), map = core->busRead32(core, at),
             columns = core->busRead8(core, at + 8);
    int left = (core->busRead16(core, asset + 16) - 240) / 2 + core->busRead8(core, at + 9) + (int)state().pan;
    unsigned errors = 0;
    for (unsigned y = 0; y < 160; ++y)
        for (unsigned x = 0; x < 240; ++x)
        {
            unsigned sx = (unsigned)(left + (int)x), cell = core->busRead16(core, map + (y / 8 * columns + sx / 8) * 2);
            unsigned tx = cell & 1024 ? 7 - sx % 8 : sx % 8, ty = cell & 2048 ? 7 - y % 8 : y % 8;
            unsigned offset = core->busRead32(core, tiles + (cell & 1023) * 4) & 0x7fffffffu;
            unsigned color = core->busRead8(core, payload + offset + ty * 8 + tx),
                     rgb = core->busRead16(core, palette + color * 2);
            unsigned r = rgb & 31, g = (rgb >> 5) & 31, b = (rgb >> 10) & 31;
            unsigned expected = ((r << 3) | (r >> 2)) | (((g << 3) | (g >> 2)) << 8) | (((b << 3) | (b >> 2)) << 16);
            errors += (pixels[y * 240 + x] & 0xffffffu) != expected;
        }
    if (errors)
        fprintf(stderr, "native CG %u image %u pixel errors %u left %d\n", state().image, image, errors, left);
    expect(!errors, "ordinary BG H/V output matches every encoded native pixel");
    expect((core->busRead16(core, 0x04000000) & 7) == 0, "clear native CG uses Mode 0");
}
static gallery_runtime_profile state(void)
{
    gallery_runtime_profile p;
    for (unsigned i = 0; i < sizeof(p) / 4; ++i)
        ((uint32_t*)&p)[i] = core->busRead32(core, gallery_address + 4 * i);
    return p;
}
static void measured(int n)
{
    short audio[1024];
    while (n--)
    {
        frames(1);
        for (int c = 0; c < 2; ++c)
        {
            int count = blip_read_samples(core->getAudioChannel(core, c), audio, 1024, 0);
            for (int i = 0; i < count; ++i)
                pcm += audio[i] != 0;
        }
        if (state().active)
        {
            unsigned ticks = core->busRead32(core, graphics_address + 6 * 4);
            if (ticks > peak)
            {
                peak = ticks;
                gallery_runtime_profile p = state();
                fprintf(stderr, "peak %u mode=%u page=%u ready=%u upload=%u seen=%u phase=%u cell=%u row=%u ui=%u\n",
                        peak, p.mode, p.page, p.ready, p.upload, p.unlocked, p.phase, p.cell, p.row,
                        core->busRead32(core, graphics_address + 9 * 4));
            }
            ++samples;
        }
    }
}
static void tap(unsigned key)
{
    core->setKeys(core, key);
    measured(3);
    core->setKeys(core, 0);
    measured(32);
}
static void ready(void)
{
    for (int i = 0; i < 600; ++i)
    {
        measured(1);
        if (state().active && state().ready)
        {
            measured(5);
            return;
        }
    }
    screenshot(output, "gallery-timeout");
    gallery_runtime_profile p = state();
    fprintf(stderr, "gallery active=%u mode=%u ready=%u image=%u upload=%u\n", p.active, p.mode, p.ready, p.image,
            p.upload);
    expect(0, "gallery bounded loading succeeds");
}
static void title_gallery(void)
{
    wait_for_title();
    tap(KEY_A);
    tap(KEY_DOWN);
    tap(KEY_DOWN);
    tap(KEY_A);
    ready();
}
// Any scene -> debug menu at the current scene row, then walk down to 画廊.
static void gallery_debug(void)
{
    debug_hotkey();
    tap(KEY_START);
}
static void story_gallery(int trial)
{
    debug_hotkey();
    tap(KEY_START);
    for (int i = 0; i < (trial ? 5 : 6); ++i)
        tap(KEY_DOWN);
    tap(KEY_A);
    ready();
}
static uint32_t crc(const uint8_t* p, unsigned size)
{
    uint32_t c = ~0u;
    while (size--)
    {
        c ^= *p++;
        for (int i = 0; i < 8; ++i)
            c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
    }
    return ~c;
}
static unsigned latest_global(const uint8_t* flash)
{
    unsigned best = 0, seq = 0;
    for (unsigned at = 122880; at < 131072; at += 4096)
    {
        uint32_t s;
        memcpy(&s, flash + at + 4, 4);
        if (flash[at + 63] == 0xa5 && (!best || (int32_t)(s - seq) > 0))
        {
            best = at;
            seq = s;
        }
    }
    expect(best != 0, "global record written in upper Flash bank");
    return best;
}
int main(int argc, char** argv)
{
    expect(argc == 9, "ROM output gallery save graphics music trial_mode hiro_steps");
    output = argv[2];
    gallery_address = strtoul(argv[3], 0, 16);
    graphics_address = strtoul(argv[5], 0, 16);
    music_address = strtoul(argv[6], 0, 16);
    int trial = atoi(argv[7]);
    core = mCoreFind(argv[1]);
    expect(core && core->init(core), "mGBA init");
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
    for (int c = 0; c < 2; ++c)
        blip_set_rates(core->getAudioChannel(core, c), 16777216, 32768);
    measured(150);
    title_gallery();
    expect(state().unlocked == 0, "fresh Flash hides all CG");
    screenshot(output, "gallery-locked");
    tap(KEY_A);
    expect(state().mode == 0, "locked CG cannot open");
    tap(KEY_R);
    ready();
    expect(state().page == 1, "next grid page");
    tap(KEY_L);
    ready();
    expect(state().page == 0, "previous grid page");
    tap(KEY_LEFT);
    ready();
    expect(state().page == 9 && state().selected == 56, "left edge crosses to previous page at same row");
    tap(KEY_RIGHT);
    ready();
    expect(state().page == 0 && state().selected == 0, "right edge crosses to next page at same row");
    tap(KEY_UP);
    ready();
    expect(state().page == 9 && state().selected == 57, "top edge crosses to bottom row of previous page");
    tap(KEY_DOWN);
    ready();
    expect(state().page == 0 && state().selected == 0, "bottom edge crosses to top row of next page");
    tap(KEY_B);
    measured(150);
    screenshot(output, "main-menu");
    expect(!state().active, "B returns from gallery to title");
    tap(KEY_A);
    ready();
    expect(state().active, "gallery is a real main-menu entry");
    gallery_debug();
    for (int i = 0; i < (trial ? 5 : 6); ++i)
        tap(KEY_UP);
    tap(KEY_A);
    if (trial)
        for (int i = 0; i < 3; ++i)
            tap(KEY_DOWN);
    tap(KEY_A);
    measured(900);
    screenshot(output, "source-first");
    expect((core->busRead16(core, 0x04000000) & 7) == (unsigned)(trial ? 1 : 0),
           "story static CG uses Mode 0; rotated Trial CG retains affine Mode 1");
    if (!trial)
    {
        uint32_t original = frame_hash();
        tap(KEY_START);
        tap(KEY_B);
        measured(180);
        expect(frame_hash() == original && !(core->busRead16(core, 0x04000000) & 7),
               "pause restores the clear story CG and decorated panel exactly");
        tap(KEY_L);
        measured(180);
        tap(KEY_B);
        measured(180);
        expect(frame_hash() == original && !(core->busRead16(core, 0x04000000) & 7),
               "history restores the clear story CG and decorated panel exactly");
    }
    if (!trial)
        for (int i = 0; i < 58; ++i)
            tap(KEY_A);
    screenshot(output, "source-later");
    story_gallery(trial);
    unsigned seen = state().unlocked;
    fprintf(stderr, "unlocked=%u trial=%d\n", seen, trial);
    expect(seen > 0, "actual formal playback unlocks CG");
    screenshot(output, "gallery-unlocked");
    if (trial)
    {
        tap(KEY_RIGHT);
        tap(KEY_DOWN);
        ready();
        expect(state().selected == 4, "select trial corpse CG group");
    }
    tap(KEY_A);
    ready();
    expect(state().mode == 1, "unlocked CG opens");
    screenshot(output, "gallery-fit");
    unsigned image = state().image;
    tap(KEY_A);
    ready();
    expect(state().native, "A toggles native size");
    screenshot(output, "gallery-native");
    core->setKeys(core, KEY_RIGHT);
    measured(100);
    core->setKeys(core, 0);
    expect((int)state().pan > 0, "native image pans within its real width");
    screenshot(output, "gallery-pan");
    tap(KEY_R);
    ready();
    expect(state().mode == 1, "difference switching keeps viewer active");
    expect(trial ? state().image == image : state().image != image, "viewer cycles only actually seen differences");
    screenshot(output, "gallery-variant");
    tap(KEY_B);
    ready();
    expect(!state().mode, "B returns to grid");
    debug_hotkey();
    tap(KEY_START);
    tap(KEY_START);
    ready();
    expect(state().active, "debug return rebuilds gallery palette and resources");
    // Enter music demo without disturbing global unlock state, then start menu -> gallery.
    tap(KEY_B);
    tap(KEY_DOWN);
    tap(KEY_A);
    tap(KEY_A);
    measured(120);
    tap(KEY_B);
    tap(KEY_UP);
    tap(KEY_A);
    ready();
    unsigned song = core->busRead32(core, music_address + 24), before = pcm;
    expect(song != 0, "music remains installed entering gallery");
    measured(300);
    tap(KEY_A);
    if (state().mode)
        ready();
    else
    {
        tap(KEY_LEFT);
        tap(KEY_UP);
        tap(KEY_A);
        ready();
    }
    measured(300);
    expect(core->busRead32(core, music_address + 24) == song, "gallery never restarts or switches current music");
    expect(pcm - before > 1000, "real audio is audible during grid sampling and viewer upload");
    tap(KEY_B);
    ready();
    tap(KEY_B);
    measured(600);
    size_t length = 131072;
    void* flash = malloc(length);
    expect(flash != NULL, "allocate Flash fixture");
    expect(((struct GBA*)core->board)->memory.savedata.type == SAVEDATA_FLASH1M, "Flash device is 128K");
    memcpy(flash, ((struct GBA*)core->board)->memory.savedata.data, length);
    unsigned at = latest_global(flash);
    uint8_t* bytes = flash;
    expect(bytes[at + 18] == 120, "global gallery and music payload is 120B");
    expect(bytes[at + 256 + 92 + image / 8] & (1u << (image % 8)), "seen variant persisted, not just RAM cache");
    core->reset(core);
    measured(150);
    title_gallery();
    expect(state().unlocked == seen, "cold reset preserves each seen variant");
    screenshot(output, "gallery-cold-reset");
    // Mount a genuine old 92B global prefix with fresh CRCs.
    uint16_t old_length = 92;
    memcpy(bytes + at + 18, &old_length, 2);
    uint32_t sum = crc(bytes + at + 64, 256 + 92 - 64);
    memcpy(bytes + at + 12, &sum, 4);
    sum = crc(bytes + at, 24);
    memcpy(bytes + at + 24, &sum, 4);
    unsigned other = at == 122880 ? 126976 : 122880;
    memset(bytes + other, 255, 4096);
    memcpy(((struct GBA*)core->board)->memory.savedata.data, flash, length);
    core->reset(core);
    measured(150);
    title_gallery();
    expect(state().unlocked == 0, "old 92B globals load with empty new gallery flags");
    if (!trial)
    {
        tap(KEY_B);
        tap(KEY_DOWN);
        tap(KEY_A);
        for (int i = 0; i < atoi(argv[8]); ++i)
            tap(KEY_DOWN);
        tap(KEY_A);
        measured(120);
        debug_hotkey();
        tap(KEY_START);
        for (int i = 0; i < 4; ++i)
            tap(KEY_DOWN);
        tap(KEY_A);
        ready();
        unsigned hiro_song = core->busRead32(core, music_address + 24);
        expect(hiro_song != 0, "full HirO installed for complete gallery sweep");
        unsigned hiro_pcm = pcm;
        expect(state().unlocked == 157, "demo entry exposes every image without progress writes");
        screenshot(output, "gallery-demo");
        unsigned count = 0;
        for (unsigned group = 0; group < 59; ++group)
        {
            if (state().selected != group)
            {
                fprintf(stderr, "expected group=%u actual=%u page=%u\n", group, state().selected, state().page);
            }
            expect(state().selected == group, "all groups reachable through normal controls");
            tap(KEY_A);
            ready();
            expect(state().mode == 1, "every unlocked group loads");
            tap(KEY_A);
            ready();
            native_pixels();
            unsigned first = state().image;
            ++count;
            for (unsigned i = 0; i < 32; ++i)
            {
                tap(KEY_R);
                ready();
                if (state().image == first)
                    break;
                native_pixels();
                ++count;
                expect(i < 31, "variant cycle bounded");
            }
            tap(KEY_B);
            ready();
            if (group == 58)
                break;
            unsigned target = group + 1;
            if (state().page != target / 6)
            {
                tap(KEY_R);
                ready();
            }
            for (int move = 0; move < 3 && state().selected % 3 != target % 3; ++move)
                tap(state().selected % 3 < target % 3 ? KEY_RIGHT : KEY_LEFT);
            if (state().selected % 6 / 3 != target % 6 / 3)
                tap(target % 6 / 3 ? KEY_DOWN : KEY_UP);
            ready();
        }
        expect(count == 157, "all 157 reused CG variants upload without VRAM errors");
        expect(hiro_song == core->busRead32(core, music_address + 24) && pcm - hiro_pcm > 100000,
               "full HirO continues through every CG and variant");
        fprintf(stderr, "all-CG variants=%u\n", count);
        tap(KEY_B);
        tap(KEY_UP);
        tap(KEY_A);
        ready();
        expect(state().unlocked == 0, "demo browsing never grants formal unlocks");
    }
    free(flash);
    printf("PASS: %s CG unlocks=%u, hidden differences, main-menu entry, grid paging, fit/native/pan, debug return, "
           "music continuity, Flash cold reset and 92B legacy; frames=%u peak=%u PCM=%u\n",
           trial ? "Trial03" : "ADV01", seen, samples, peak, pcm);
    expect(peak < 4389, "gallery with actual music stays within one frame");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
