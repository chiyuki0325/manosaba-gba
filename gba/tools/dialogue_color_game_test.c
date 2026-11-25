/* Drive the released ROM through source dialogue; inspect actual BG glyph indices. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/trial_runtime.h"
#include "../src/dialogue_history_runtime.h"
#include "../src/witch_book_runtime.h"
#include "../src/save_runtime.h"
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/renderers/video-software.h>
#include <string.h>
static unsigned ta, ha, ba, sa;
static const char* directory;
static void read_words(unsigned at, void* out, unsigned size)
{
    for (unsigned i = 0; i < size / 4; ++i)
        ((uint32_t*)out)[i] = core->busRead32(core, at + i * 4);
}
static trial_runtime_profile trial(void)
{
    trial_runtime_profile p;
    read_words(ta, &p, sizeof(p));
    return p;
}
static dialogue_history_profile history(void)
{
    dialogue_history_profile p;
    read_words(ha, &p, sizeof(p));
    return p;
}
static witch_book_profile book(void)
{
    witch_book_profile p;
    read_words(ba, &p, sizeof(p));
    return p;
}
static save_runtime_profile save(void)
{
    save_runtime_profile p;
    read_words(sa, &p, sizeof(p));
    return p;
}
static void tap(unsigned key)
{
    core->setKeys(core, key);
    frames(2);
    core->setKeys(core, 0);
    frames(22);
}
static unsigned ui_pixels(uint8_t* out, unsigned* palette)
{
    unsigned bg = 0, display = core->busRead16(core, 0x04000000);
    for (; bg < 4; ++bg)
    {
        unsigned c = core->busRead16(core, 0x04000008 + bg * 2);
        if ((display & (256u << bg)) && !(c & 131))
            break;
    }
    expect(bg < 4, "find regular 4bpp UI BG");
    unsigned c = core->busRead16(core, 0x04000008 + bg * 2), used = 0;
    struct GBAVideoSoftwareRenderer* r = (struct GBAVideoSoftwareRenderer*)((struct GBA*)core->board)->video.renderer;
    for (unsigned y = 0; y < 160; ++y)
        for (unsigned x = 0; x < 240; ++x)
        {
            unsigned px = (x + r->bg[bg].x) & 255, py = (y + r->bg[bg].y) & 255;
            unsigned cell = core->busRead16(core, 0x06000000 + ((c >> 8) & 31) * 2048 + (py / 8 * 32 + px / 8) * 2);
            unsigned tx = (cell & 1024) ? 7 - (px & 7) : px & 7, ty = (cell & 2048) ? 7 - (py & 7) : py & 7;
            unsigned ink =
                (core->busRead8(core, 0x06000000 + ((c >> 2) & 3) * 16384 + (cell & 1023) * 32 + ty * 4 + tx / 2) >>
                 ((tx & 1) * 4)) &
                15;
            if (out)
                out[y * 240 + x] = ink;
            if (ink == 4 && y >= 22 && y < 158)
                ++used;
            *palette = 0x05000000 + (cell >> 12) * 32;
        }
    return used;
}
static void history_ready(void)
{
    for (unsigned i = 0; i < 1200; ++i)
    {
        frames(1);
        if (history().ready)
            return;
    }
    expect(0, "history finishes loading");
}
static void game_ready(void)
{
    for (unsigned i = 0; i < 1800; ++i)
    {
        frames(1);
        if ((int)save().view == -1 && !save().restoring)
        {
            frames(90);
            return;
        }
    }
    expect(0, "return to playback");
}
static void save_ready(unsigned view)
{
    for (unsigned i = 0; i < 1800; ++i)
    {
        frames(1);
        save_runtime_profile p = save();
        if (p.view == view && !p.busy && p.paint == 6)
            return;
    }
    expect(0, "save menu finishes drawing");
}
struct checkpoint
{
    unsigned script, pc, state, action, arg;
};
int main(int argc, char** argv)
{
    expect(argc == 13, "ROM directory trial history book save ordinal is_trial script target_pc color_index route");
    directory = argv[2];
    ta = strtoul(argv[3], 0, 16);
    ha = strtoul(argv[4], 0, 16);
    ba = strtoul(argv[5], 0, 16);
    sa = strtoul(argv[6], 0, 16);
    unsigned ordinal = atoi(argv[7]), is_trial = atoi(argv[8]), script = atoi(argv[9]), target = atoi(argv[10]),
             color_index = atoi(argv[11]);
    struct checkpoint route[4096];
    unsigned count = 0;
    FILE* f = fopen(argv[12], "r");
    expect(f != NULL, "open route");
    while (count < 4096 && fscanf(f, "%u %u %u %u %u", &route[count].script, &route[count].pc, &route[count].state,
                                  &route[count].action, &route[count].arg) == 5)
        ++count;
    fclose(f);
    core = mCoreFind(argv[1]);
    expect(core && core->init(core), "mGBA init");
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
    tap(KEY_DOWN);
    if (is_trial)
        tap(KEY_DOWN);
    tap(KEY_A);
    for (unsigned i = 0; i < ordinal / 6; ++i)
        tap(KEY_RIGHT);
    for (unsigned i = 0; i < ordinal % 6; ++i)
        tap(KEY_DOWN);
    tap(KEY_A);
    unsigned n, pulse = 0;
    for (n = 0; n < 80000; ++n)
    {
        trial_runtime_profile p = trial();
        witch_book_profile b = book();
        if (p.vm_script == script && p.pc == target && (p.vm_state == 1 || p.vm_state == 6))
            break;
        unsigned key = 0;
        if (pulse)
            --pulse;
        if (!pulse)
        {
            if (!is_trial && p.vm_state == 1)
                key = KEY_A;
            if (is_trial && !count && p.interaction_ready && (p.vm_state == 1 || p.vm_state == 6))
                key = KEY_A;
            for (unsigned i = 0; is_trial && i < count; ++i)
                if (p.vm_script == route[i].script && p.pc == route[i].pc && p.vm_state == route[i].state)
                {
                    struct checkpoint s = route[i];
                    if (b.open && b.ready)
                    {
                        if (b.source == s.arg)
                            key = KEY_A;
                        else if (b.mode)
                            key = KEY_B;
                        else
                            key = KEY_RIGHT;
                    }
                    else if (p.interaction_ready && !p.preview)
                    {
                        if (s.action < 2)
                            key = KEY_A;
                        else if (s.action == 2)
                            key = s.arg == 0 ? KEY_UP : s.arg == 1 ? KEY_LEFT : KEY_RIGHT;
                        else if (s.action == 3)
                            key = p.choice == s.arg ? KEY_A : KEY_DOWN;
                    }
                    break;
                }
            if (is_trial && p.preview && p.interaction_ready)
                key = KEY_B;
            if (key)
                pulse = 24;
        }
        core->setKeys(core, key);
        frames(1);
    }
    core->setKeys(core, 0);
    fprintf(stderr, "target frame=%u script=%u pc=%u actual=%u:%u state=%u\n", n, script, target, trial().vm_script,
            trial().pc, trial().vm_state);
    expect(n < 80000, "source colored dialogue reached by real input");
    unsigned palette = 0, used = 0;
    for (unsigned i = 0; i < 280 && trial().pc == target && !used; ++i)
    {
        frames(1);
        used = ui_pixels(NULL, &palette);
    }
    expect(used > 0, "colored glyphs appear through the production typewriter");
    unsigned colors[] = {31, 32307, 27149};
    expect(core->busRead16(core, palette + 8) == colors[color_index], "4bpp ink matches original RGB555 color");
    screenshot(directory, "typewriter");
    // Complete the same sentence; A must leave the VM on this utterance.
    if (trial().vm_state == 1)
    {
        tap(KEY_A);
        expect(trial().pc == target, "A completes colored sentence before advancing");
    }
    frames(trial().vm_state == 1 ? 180 : 14);
    if (getenv("MANOSABA_COLOR_SECOND_PAGE"))
    {
        tap(KEY_A);
        expect(trial().pc == target, "page turn keeps the colored utterance PC");
        frames(180);
        screenshot(directory, "second-page");
    }
    uint8_t before[240 * 160], after[240 * 160];
    used = ui_pixels(before, &palette);
    expect(used > 0, "completed text keeps its color");
    screenshot(directory, "dialogue");
    unsigned timed = trial().vm_state == 6;
    if (timed)
    {
        for (unsigned y = 0; y < 24; ++y)
            for (unsigned x = 176; x < 240; ++x)
                expect(before[y * 240 + x] != 3 && before[y * 240 + x] != 4 && before[y * 240 + x] != 5,
                       "clock pixels never reference speaker, inline-color or countdown-bar inks");
        expect(core->busRead16(core, palette + 10) == (31u | (16u << 5) | (21u << 10)),
               "Ema countdown bar remains pink while purple text and clock use separate colors");
    }
    if (!timed)
    {
        tap(KEY_START);
        save_ready(2);
        tap(KEY_B);
        game_ready();
        ui_pixels(after, &palette);
        screenshot(directory, "pause-return");
        expect(!memcmp(before + 22 * 240, after + 22 * 240, 136 * 240),
               "pause returns identical colored dialogue pixels");
        tap(KEY_SELECT);
        frames(260);
        tap(KEY_SELECT);
        game_ready();
        ui_pixels(after, &palette);
        expect(!memcmp(before + 22 * 240, after + 22 * 240, 136 * 240),
               "book returns identical colored dialogue pixels");
    }
    tap(KEY_L);
    history_ready();
    unsigned ink = 12 + color_index;
    ui_pixels(after, &palette);
    unsigned colored = 0;
    for (unsigned i = 0; i < 240 * 160; ++i)
        colored += after[i] == ink;
    expect(colored > 0 && core->busRead16(core, palette + ink * 2) == colors[color_index],
           "history preserves original inline color");
    screenshot(directory, "history");
    frames(2);
    uint32_t history_hash = frame_hash();
    tap(KEY_LEFT);
    history_ready();
    tap(KEY_RIGHT);
    history_ready();
    frames(2);
    screenshot(directory, "history-after-scroll");
    fprintf(stderr, "history after-scroll first=%u rows=%u beforehash=%u afterhash=%u\n", history().first,
            history().rows, history_hash, frame_hash());
    expect(frame_hash() == history_hash, "history page scrolling restores inline and speaker colors");
    tap(KEY_B);
    frames(24);
    expect(!history().open, "history closes");
    if (!timed)
    {
        frames(90);
        ui_pixels(after, &palette);
        expect(!memcmp(before + 22 * 240, after + 22 * 240, 136 * 240),
               "history returns identical mixed white/color text");
        tap(KEY_START);
        save_ready(2);
        tap(KEY_DOWN);
        tap(KEY_A);
        save_ready(0);
        tap(KEY_A);
        save_ready(0);
        tap(KEY_R);
        save_ready(1);
        tap(KEY_DOWN);
        save_ready(1);
        tap(KEY_A);
        save_ready(1);
        tap(KEY_RIGHT);
        tap(KEY_A);
        game_ready();
        expect(trial().pc == target && trial().vm_script == script, "Flash load restores colored utterance PC");
        ui_pixels(after, &palette);
        expect(!memcmp(before + 22 * 240, after + 22 * 240, 136 * 240),
               "Flash load reconstructs original colored glyphs without a saved style field");
        screenshot(directory, "loaded");
    }
    printf("PASS: source=%u pc=%u color=%u glyph_pixels=%u history_pixels=%u frames=%u%s\n", script, target,
           color_index, used, colored, n, timed ? " discussion" : " pause/book/Flash");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
