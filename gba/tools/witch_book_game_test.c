/* Real key input, screenshots and pause/submit regressions on the production ROM. */
#define main navigation_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/witch_book_runtime.h"
#include "../src/graphics_profile.h"
#include "../src/trial_runtime.h"
#include <mgba/internal/gba/gba.h>
static unsigned book_address, graphics_address, trial_address, peak, missed, updates, monitor, peak_vblank;
static const char* directory;
static uint8_t map_reference[4][384 * 224];
static void read_words(unsigned address, void* target, unsigned size)
{
    uint32_t* out = target;
    for (unsigned i = 0; i < size / 4; ++i)
        out[i] = core->busRead32(core, address + i * 4);
}
static witch_book_profile book(void)
{
    witch_book_profile p;
    read_words(book_address, &p, sizeof(p));
    return p;
}
static trial_runtime_profile trial(void)
{
    trial_runtime_profile p;
    read_words(trial_address, &p, sizeof(p));
    return p;
}
static void measured_frames(int count)
{
    static unsigned last_ui = 0, last_paint = 0, last_cpu = 0, last_update = 0;
    while (count--)
    {
        unsigned before = book().missed;
        frames(1);
        struct graphics_profile p;
        read_words(graphics_address, &p, sizeof(p));
        if (!monitor)
            continue;
        if (p.max_vblank_ticks > peak_vblank)
            peak_vblank = p.max_vblank_ticks;
        if (p.core_ticks > peak)
        {
            peak = p.core_ticks;
            if (peak > 3500)
                fprintf(stderr,
                        "slow frame ticks=%u book=%u mode=%u tab=%u loading=%u ready=%u UI=%u previous_UI=%u "
                        "previous_paint=%u previous_scene=%u previous_update=%u\n",
                        peak, book().open, book().mode, book().tab, book().loading, book().ready, p.ui_ticks, last_ui,
                        last_paint, last_cpu, last_update);
        }
        last_ui = p.ui_ticks;
        last_paint = book().paint;
        last_cpu = p.cpu_ticks;
        last_update = p.update_ticks;
        missed += book().missed - before;
        ++updates;
    }
}
#define frames measured_frames
static void tap(unsigned key)
{
    core->setKeys(core, key);
    frames(4);
    core->setKeys(core, 0);
    frames(20);
}
static void ready(void)
{
    for (int n = 0; n < 300; ++n)
    {
        frames(1);
        if (book().ready)
        {
            frames(2);
            return;
        }
    }
    screenshot(directory, "book-timeout");
    fprintf(stderr, "book open=%u mode=%u tab=%u count=%u loading=%u state=%u\n", book().open, book().mode, book().tab,
            book().count, book().loading, book().state);
    expect(0, "book finishes loading");
}
static void reset_rom(void)
{
    monitor = 0;
    debug_boot();
}
static void enter_trial(int offset)
{
    for (int i = 0; i < 2; ++i)
        tap(KEY_DOWN);
    tap(KEY_A);
    frames(20);
    for (int i = 0; i < offset / 6; ++i)
        tap(KEY_RIGHT);
    for (int i = 0; i < offset % 6; ++i)
        tap(KEY_DOWN);
    tap(KEY_A);
    frames(90);
}
static void full_profile(void)
{
    unsigned selected = book().selected;
    tap(KEY_A);
    ready();
    tap(KEY_A);
    ready();
    expect(book().mode == 2, "every profile supports its full image");
    tap(KEY_B);
    ready();
    tap(KEY_B);
    ready();
    expect(book().mode == 0 && book().selected == selected, "full image restores profile selection");
}
static unsigned map_palette(void)
{
    unsigned display = core->busRead16(core, 0x04000000);
    for (int i = 0; i < 4; ++i)
        if (display & (256u << i))
        {
            unsigned ctrl = core->busRead16(core, 0x04000008 + i * 2);
            expect(!(ctrl & 128), "map canvas uses 4bpp BG tiles");
            unsigned cell = core->busRead16(core, 0x06000000 + ((ctrl >> 8) & 31) * 2048);
            return 0x05000000 + (cell >> 12) * 32;
        }
    expect(0, "map has a visible background");
    return 0;
}
static unsigned map_background(void)
{
    unsigned display = core->busRead16(core, 0x04000000);
    for (unsigned i = 0; i < 4; ++i)
        if (display & (256u << i))
        {
            unsigned ctrl = core->busRead16(core, 0x04000008 + i * 2);
            if ((ctrl & 0xc000) == 0x4000)
            {
                expect(!(ctrl & 128), "resident map is a 512x256 4bpp BG");
                return i;
            }
        }
    expect(0, "resident map has a hardware BG");
    return 0;
}
static uint32_t memory_hash(unsigned address, unsigned count)
{
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < count; i += 2)
        hash = (hash ^ core->busRead16(core, address + i)) * 16777619u;
    return hash;
}
static void map_geometry(void)
{
    unsigned map_bg = map_background(), ctrl = core->busRead16(core, 0x04000008 + map_bg * 2);
    // Scroll registers are write-only on GBA; use mGBA's stored register state.
    unsigned left = ((struct GBA*)core->board)->memory.io[(0x10 + map_bg * 4) / 2] & 511,
             top = ((struct GBA*)core->board)->memory.io[(0x12 + map_bg * 4) / 2] & 255;
    unsigned cells = 0x06000000 + ((ctrl >> 8) & 31) * 2048;
    unsigned ui_bg = 0, display = core->busRead16(core, 0x04000000);
    for (unsigned i = 0; i < 4; ++i)
        if (i != map_bg && (display & (256u << i)))
            ui_bg = i;
    unsigned ui_ctrl = core->busRead16(core, 0x04000008 + ui_bg * 2);
    unsigned ui_cells = 0x06000000 + ((ui_ctrl >> 8) & 31) * 2048, ui_tiles = 0x06000000 + ((ui_ctrl >> 2) & 3) * 16384;
    int floor = book().map_floor, image = floor == 0 ? 0 : floor < 3 ? 1 : floor - 1;
    static const unsigned counts[] = {709, 567, 370, 657};
    unsigned checked = 0;
    for (unsigned y = 25; y < 143; y += 3)
        for (unsigned x = 4; x < 236; x += 3)
        {
            unsigned ui_cell = core->busRead16(core, ui_cells + (y / 8 * 32 + x / 8) * 2);
            unsigned ui_word = core->busRead32(core, ui_tiles + (ui_cell & 1023) * 32 + (y % 8) * 4);
            if ((ui_word >> ((x % 8) * 4)) & 15)
                continue;
            unsigned sx = (x + left) & 511, sy = (y + top) & 255;
            expect(sx < 384 && sy < 224, "hardware scroll stays within the authored map plane");
            unsigned cell = core->busRead16(core, cells + (sx / 256 * 1024 + sy / 8 * 32 + (sx / 8) % 32) * 2);
            if ((cell & 1023) >= counts[image])
                continue;
            unsigned index = map_reference[image][sy * 384 + sx],
                     expected = core->busRead16(core, 0x05000000 + (cell >> 12) * 32 + index * 2);
            unsigned rgb = pixels[y * 240 + x],
                     actual = ((rgb >> 3) & 31) | (((rgb >> 11) & 31) << 5) | (((rgb >> 19) & 31) << 10);
            if (actual != expected)
                fprintf(stderr,
                        "map mismatch paint=%u floor=%d x=%u y=%u scroll=%u,%u source=%u,%u cell=%x index=%u "
                        "expected=%x actual=%x ui=%x ui_bg=%u map_bg=%u\n",
                        book().paint, floor, x, y, left, top, sx, sy, cell, index, expected, actual, ui_cell, ui_bg,
                        map_bg);
            expect(actual == expected, "hardware map pixels match the offline plane at every camera position");
            ++checked;
        }
    expect(checked > 200, "geometry checks cover the visible map beyond UI and highlight");
}
static uint32_t map_fixed_hash(void)
{
    uint32_t hash = 2166136261u;
    for (int y = 0; y < 160; ++y)
        for (int x = 0; x < 240; ++x)
            if (y < 24 || y >= 144 || (x >= 7 && x < 63 && y >= 28 && y < 44))
                hash = (hash ^ pixels[y * 240 + x]) * 16777619u;
    return hash;
}
static void map_browse(void)
{
    expect(book().map_floor == 0 && book().count == 22, "first floor uses original 22 regions");
    expect(core->busRead16(core, map_palette() + 6) == (31 | (18 << 5) | (21 << 10)), "Act01 map uses Ema pink");
    map_geometry();
    unsigned map_bg = map_background(), ctrl = core->busRead16(core, 0x04000008 + map_bg * 2);
    uint32_t base_tiles = memory_hash(0x06000000 + ((ctrl >> 2) & 3) * 16384, 709 * 32);
    unsigned moving = 0, positions = 0, last_position = ~0u, scroll_peak = 0;
    uint32_t moving_cells = 0;
    uint32_t fixed = map_fixed_hash();
    unsigned start_position = (((struct GBA*)core->board)->memory.io[(0x10 + map_bg * 4) / 2] & 511) |
                              ((((struct GBA*)core->board)->memory.io[(0x12 + map_bg * 4) / 2] & 255) << 16);
    core->setKeys(core, KEY_RIGHT);
    for (int i = 0; i < 100; ++i)
    {
        frames(1);
        if (i == 3)
            core->setKeys(core, 0);
        expect(map_fixed_hash() == fixed, "map redraw retains tabs, floor badge and toolbar on every frame");
        if (book().paint == 620)
        {
            unsigned current = (((struct GBA*)core->board)->memory.io[(0x10 + map_bg * 4) / 2] & 511) |
                               ((((struct GBA*)core->board)->memory.io[(0x12 + map_bg * 4) / 2] & 255) << 16);
            // CPU preparation can reach phase 620 while the old picture still holds.
            // Check resident-map stability once the hardware starts the actual motion.
            if (!moving && current == start_position)
                continue;
            positions += current != last_position;
            last_position = current;
            uint32_t cell_hash = memory_hash(0x06000000 + ((ctrl >> 8) & 31) * 2048, 4096);
            if (moving)
                expect(cell_hash == moving_cells, "scrolling only changes coordinates, not the resident map");
            moving_cells = cell_hash;
            expect(memory_hash(0x06000000 + ((ctrl >> 2) & 3) * 16384, 709 * 32) == base_tiles,
                   "scrolling never rewrites source map tiles");
            map_geometry();
            struct graphics_profile perf;
            read_words(graphics_address, &perf, sizeof(perf));
            if (perf.core_ticks > scroll_peak)
                scroll_peak = perf.core_ticks;
            if (moving < 12)
            {
                char name[32];
                snprintf(name, sizeof(name), "map-scroll-%02u", moving);
                screenshot(directory, name);
            }
            ++moving;
        }
        if (i >= 4 && book().ready)
            break;
    }
    core->setKeys(core, 0);
    ready();
    expect(book().selected == 1, "map view changes without rebuilding fixed UI");
    expect(moving >= 8 && positions >= 8, "same-floor camera presents smooth intermediate positions");
    printf("Hardware map scroll: frames=%u positions=%u peak_ticks=%u base_tiles_unchanged=1 cells_unchanged=1\n",
           moving, positions, scroll_peak);
    for (int i = 1; i < 16; ++i)
    {
        tap(KEY_RIGHT);
        ready();
    }
    expect(book().selected == 16 && book().map_area == 16, "right selects the narrow Undine chamber");
    screenshot(directory, "map-small-room");
    for (int i = 0; i < 4; ++i)
    {
        tap(KEY_RIGHT);
        ready();
    }
    expect(book().map_area == 20 && book().map_left == 0, "outside Lake arrow remains reachable");
    screenshot(directory, "map-outside-lake");
    unsigned prior = frame_hash(), last_view = prior, swaps = 0;
    core->setKeys(core, KEY_DOWN);
    for (int i = 0; i < 150; ++i)
    {
        frames(1);
        if (i == 3)
            core->setKeys(core, 0);
        if (book().paint == 630 || book().paint == 602 || book().paint == 615 || book().paint == 616 ||
            book().paint == 617)
        {
            if (frame_hash() != prior)
            {
                screenshot(directory, "map-floor-handoff-failure");
                fprintf(stderr, "handoff frame=%d paint=%u\n", i, book().paint);
            }
            expect(frame_hash() == prior, "floor upload retains the exact previous view");
        }
        unsigned current = frame_hash();
        if (current != last_view)
            ++swaps;
        last_view = current;
        if (i >= 4 && book().ready)
            break;
    }
    core->setKeys(core, 0);
    ready();
    expect(swaps == 1, "floor and UI switch together in exactly one visible frame");
    expect(book().map_floor == 1 && book().count == 4, "down switches to second floor v1");
    map_geometry();
    for (int i = 0; i < 3; ++i)
    {
        tap(KEY_RIGHT);
        ready();
    }
    expect(book().map_area == 25, "v1 has UnknownArea");
    screenshot(directory, "map-unknown-v1");
    tap(KEY_DOWN);
    ready();
    expect(book().map_floor == 3 && book().count == 3, "down switches to basement");
    screenshot(directory, "map-basement");
    tap(KEY_UP);
    ready();
    expect(book().map_area == 25, "up restores the floor and its selected room");
    tap(KEY_UP);
    ready();
    expect(book().map_area == 20, "first floor retains its selection");
    unsigned idle = frame_hash();
    frames(90);
    expect(frame_hash() == idle, "idle map is stable");
    tap(KEY_A);
    ready();
    expect(book().mode == 1, "map A enters detail");
    screenshot(directory, "map-detail");
    tap(KEY_A);
    ready();
    expect(book().mode == 2, "browse detail opens panning mode");
    int left = book().map_left;
    tap(KEY_RIGHT);
    ready();
    expect(book().map_left > left, "map viewer pans without scaling");
    tap(KEY_B);
    ready();
    expect(book().mode == 0 && book().map_area == 20, "B restores map focus");
    expect(frame_hash() == idle, "modal and pan return restore the exact map and minimap frame");
    core->setKeys(core, KEY_LEFT);
    frames(4);
    core->setKeys(core, 0);
    for (int i = 0; i < 100 && book().paint != 620; ++i)
        frames(1);
    expect(book().paint == 620, "pause regression reaches an active hardware pan");
    tap(KEY_START);
    expect(book().paused, "Start pauses the map during motion");
    unsigned display = core->busRead16(core, 0x04000000);
    for (unsigned i = 0; i < 4; ++i)
        if (display & (256u << i))
            expect((core->busRead16(core, 0x04000008 + i * 2) & 0xc000) != 0x4000,
                   "pause releases the resident map background");
    tap(KEY_START);
    ready();
    expect(book().map_area == 19 && book().mode == 0, "resume restores the selected room after interrupting motion");
    map_geometry();
    tap(KEY_RIGHT);
    ready();
    expect(book().map_area == 20, "map remains navigable after pause and resource rebuild");
}
static void map_versions(void)
{
    reset_rom();
    enter_trial(33);
    monitor = 1;
    tap(KEY_SELECT);
    ready();
    tap(KEY_R);
    ready();
    tap(KEY_R);
    ready();
    for (int i = 0; i < 3; ++i)
    {
        tap(KEY_DOWN);
        ready();
    }
    expect(book().map_floor == 4 && !book().count, "chapter-three trial snapshot unlocks non-selectable blueprint");
    screenshot(directory, "map-blueprint");
    tap(KEY_A);
    ready();
    expect(book().mode == 2, "blueprint A enters free panning");
    tap(KEY_B);
    ready();
    reset_rom();
    enter_trial(72);
    monitor = 1;
    tap(KEY_SELECT);
    ready();
    tap(KEY_R);
    ready();
    tap(KEY_R);
    ready();
    expect(core->busRead16(core, map_palette() + 6) == (29 | (9 << 5) | (10 << 10)), "Act02 map uses Hiro red");
    tap(KEY_DOWN);
    ready();
    expect(book().map_floor == 2, "Act02 trial uses second floor v2");
    for (int i = 0; i < 3; ++i)
    {
        tap(KEY_RIGHT);
        ready();
    }
    expect(book().map_area == 29, "v2 replaces UnknownArea with NoahStudio");
    screenshot(directory, "map-studio-v2-hiro");
    tap(KEY_L);
    ready();
    screenshot(directory, "book-hiro-theme");
}
static unsigned reader_background(void)
{
    unsigned display = core->busRead16(core, 0x04000000);
    for (unsigned i = 0; i < 4; ++i)
        if ((display & (256u << i)) && !(core->busRead16(core, 0x04000008 + i * 2) & 128))
            return i;
    expect(0, "reader has a separate 4bpp text background");
    return 0;
}
static unsigned reader_position(unsigned bg)
{
    return ((struct GBA*)core->board)->memory.io[(0x12 + bg * 4) / 2] & 255;
}
static void long_detail(void)
{
    reset_rom();
    enter_trial(156);
    monitor = 1;
    tap(KEY_SELECT);
    ready();
    tap(KEY_R);
    ready();
    tap(KEY_A);
    ready();
    unsigned bg = reader_background(), start = reader_position(bg);
    uint32_t top = frame_hash();
    screenshot(directory, "book-long-profile-top");
    for (int i = 0; i < 5; ++i)
    {
        tap(KEY_RIGHT);
        ready();
    }
    unsigned distance = (reader_position(bg) - start) & 255;
    uint32_t bottom = frame_hash();
    expect(distance > 0, "later chapter exposes a scrollable character biography");
    screenshot(directory, "book-long-profile-bottom");
    for (int i = 0; i < 5; ++i)
    {
        tap(KEY_LEFT);
        ready();
    }
    expect(frame_hash() == top, "detail page jumps restore its header, footer and text");
    core->setKeys(core, KEY_DOWN);
    frames(250);
    core->setKeys(core, 0);
    ready();
    expect(frame_hash() == bottom, "seven-line detail ring matches page rendering after held scroll");
    tap(KEY_B);
    ready();
    expect(book().mode == 0, "scrolling biography returns to its character list");
    tap(KEY_L);
    ready();
    tap(KEY_L);
    ready();
    expect(book().tab == 4 && book().count == 18, "late-game Notes exposes all eighteen unlocked records");
    tap(KEY_A);
    ready();
    screenshot(directory, "book-notes-updated");
    for (int i = 0; i < 3; ++i)
    {
        tap(KEY_RIGHT);
        ready();
    }
    expect(book().first == 15, "the same witch record now displays all twenty-two rows of its third version");
    tap(KEY_B);
    ready();
    tap(KEY_RIGHT);
    ready();
    for (int i = 0; i < 3; ++i)
    {
        tap(KEY_DOWN);
        ready();
    }
    expect(book().selected == 9 && book().source == 159,
           "full-width Notes selects the original prisoner's letter entry");
    tap(KEY_A);
    ready();
    screenshot(directory, "book-notes-letter");
    uint32_t letter_top = frame_hash();
    for (int i = 0; i < 8; ++i)
    {
        tap(KEY_RIGHT);
        ready();
    }
    expect(book().first == 35, "all forty-two rows of the prisoner's letter are reachable");
    for (int i = 0; i < 8; ++i)
    {
        tap(KEY_LEFT);
        ready();
    }
    expect(frame_hash() == letter_top, "Notes restores exact text after crossing the reader ring boundary");
    tap(KEY_B);
    ready();
    expect(book().selected == 9, "returning from a long letter retains its list selection");
}
static void reader_scroll(uint32_t top)
{
    unsigned bg = reader_background(), ctrl = core->busRead16(core, 0x04000008 + bg * 2);
    unsigned cells = 0x06000000 + ((ctrl >> 8) & 31) * 2048;
    unsigned tiles = 0x06000000 + ((ctrl >> 2) & 3) * 16384 + (core->busRead16(core, cells) & 1023) * 32;
    unsigned base = 0;
    for (unsigned i = 0; i < 4; ++i)
        if (i != bg && (core->busRead16(core, 0x04000000) & (256u << i)))
            base = i;
    unsigned base_ctrl = core->busRead16(core, 0x04000008 + base * 2);
    unsigned base_tiles = 0x06000000 + ((base_ctrl >> 2) & 3) * 16384;
    uint32_t paper = memory_hash(base_tiles, 540 * 64), ring[16];
    for (int y = 0; y < 16; ++y)
        ring[y] = memory_hash(tiles + y * 28 * 32, 28 * 32);
    unsigned start = reader_position(bg), last = start, moved = 0, changed = 0, scroll_peak = 0;
    core->setKeys(core, KEY_DOWN);
    for (int n = 0; n < 20; ++n)
    {
        frames(1);
        if (n == 3)
            core->setKeys(core, 0);
        unsigned position = reader_position(bg);
        if (position != last)
        {
            expect(((position - last) & 255) == 2, "reader moves two pixels every displayed frame");
            ++moved;
            last = position;
            char name[40];
            snprintf(name, sizeof(name), "text-scroll-%02u", moved);
            screenshot(directory, name);
        }
        expect(memory_hash(base_tiles, 540 * 64) == paper, "scroll retains uploaded paper, tabs and headings");
        for (int y = 0; y < 16; ++y)
        {
            uint32_t hash = memory_hash(tiles + y * 28 * 32, 28 * 32);
            if (hash != ring[y])
            {
                ++changed;
                ring[y] = hash;
            }
        }
        struct graphics_profile p;
        read_words(graphics_address, &p, sizeof(p));
        if (p.core_ticks > scroll_peak)
            scroll_peak = p.core_ticks;
    }
    ready();
    expect(moved == 7 && ((last - start) & 255) == 14, "one line has seven distinct hardware scroll frames");
    expect(changed <= 3, "one scroll updates at most three text tile rows");
    printf("Hardware text scroll: frames=%u positions=%u tile_rows=%u peak_ticks=%u\n", moved, moved, changed,
           scroll_peak);
    tap(KEY_UP);
    ready();
    expect(frame_hash() == top, "hardware scroll restores original pixels");
    // Reverse before reaching a line boundary; the circular cache must recover the old top.
    core->setKeys(core, KEY_DOWN);
    frames(4);
    core->setKeys(core, KEY_UP);
    frames(4);
    core->setKeys(core, 0);
    ready();
    expect(book().first == 0 && frame_hash() == top, "mid-line reversal restores exact pixels");
    // A page jump and held scrolling must produce the same text after wrapping the 128px ring.
    tap(KEY_RIGHT);
    ready();
    tap(KEY_RIGHT);
    ready();
    uint32_t page16 = frame_hash();
    tap(KEY_LEFT);
    ready();
    tap(KEY_LEFT);
    ready();
    expect(frame_hash() == top, "page cache returns to top");
    core->setKeys(core, KEY_DOWN);
    for (int n = 0; n < 180 && book().first < 16; ++n)
        frames(1);
    core->setKeys(core, 0);
    ready();
    expect(book().first == 16 && frame_hash() == page16, "held scrolling crosses ring wrap and matches page jump");
    core->setKeys(core, KEY_UP);
    for (int n = 0; n < 180 && book().first > 0; ++n)
        frames(1);
    core->setKeys(core, 0);
    ready();
    expect(frame_hash() == top, "held reverse scrolling crosses ring wrap exactly");
    core->setKeys(core, KEY_DOWN);
    frames(4);
    core->setKeys(core, 0);
    tap(KEY_START);
    expect(book().paused, "pause interrupts active text motion");
    tap(KEY_START);
    ready();
    expect(book().first == 1, "resume restores the selected reader line");
    tap(KEY_UP);
    ready();
    expect(frame_hash() == top, "pause rebuild preserves exact text and paper");
}
static void browse(void)
{
    reset_rom();
    adv04_selector();
    tap(KEY_A);
    frames(120);
    monitor = 1;
    unsigned old_picture = frame_hash(), opening_hashes[300], opening_count = 0;
    core->setKeys(core, KEY_SELECT);
    do
    {
        frames(1);
        opening_hashes[opening_count++] = frame_hash();
        if (opening_count == 4)
            core->setKeys(core, 0);
    } while (opening_count < 300 && (opening_count < 6 || !book().ready));
    core->setKeys(core, 0);
    ready();
    unsigned book_picture = frame_hash();
    for (unsigned i = 0; i < opening_count; ++i)
        expect(opening_hashes[i] == old_picture || opening_hashes[i] == book_picture,
               "book publishes only a complete old or new picture");
    expect(book().open && book().tab == 0, "Select opens book during normal story");
    screenshot(directory, "book-clue");
    tap(KEY_R);
    ready();
    expect(book().tab == 1 && book().count == 15, "person tab uses original unlocked profiles");
    screenshot(directory, "book-profile");
    unsigned profile_frame = frame_hash();
    tap(KEY_RIGHT);
    ready();
    tap(KEY_LEFT);
    ready();
    expect(book().selected == 0 && frame_hash() == profile_frame,
           "preview round trip restores exact pixels and palette");
    tap(KEY_DOWN);
    ready();
    tap(KEY_RIGHT);
    ready();
    screenshot(directory, "book-ema");
    tap(KEY_LEFT);
    ready();
    tap(KEY_UP);
    ready();
    full_profile();
    for (unsigned i = 1; i < 15; ++i)
    {
        tap(KEY_RIGHT);
        ready();
        expect(book().selected == i, "all profile entries remain reachable");
        full_profile();
        if (i == 13)
        {
            expect(book().first == 12, "odd final row stays visible from its preceding row");
            screenshot(directory, "book-profile-scroll");
        }
    }
    tap(KEY_SELECT);
    frames(120);
    tap(KEY_SELECT);
    ready();
    expect(book().tab == 1 && book().selected == 14 && book().first == 12,
           "reopening book retains a visible last selection");
    tap(KEY_R);
    ready();
    tap(KEY_L);
    ready();
    expect(book().selected == 14 && book().first == 12, "switching tabs restores the selected viewport");
    tap(KEY_RIGHT);
    ready();
    if (book().selected != 0 || book().first != 0)
        fprintf(stderr, "wrap selected=%u first=%u tab=%u count=%u paint=%u ready=%u mode=%u frames=%u paused=%u\n",
                book().selected, book().first, book().tab, book().count, book().paint, book().ready, book().mode,
                updates, book().paused);
    expect(book().selected == 0 && book().first == 0, "profile list wraps to first row");
    unsigned item = book().selected;
    tap(KEY_RIGHT);
    ready();
    expect(book().selected != item, "D-pad selects next shared item");
    tap(KEY_A);
    ready();
    expect(book().mode == 1, "A opens details without submitting");
    screenshot(directory, "book-profile-detail");
    tap(KEY_A);
    ready();
    expect(book().mode == 2, "browse detail can open full image");
    screenshot(directory, "book-profile-full");
    tap(KEY_B);
    ready();
    expect(book().mode == 1, "B returns from full image to detail");
    tap(KEY_B);
    ready();
    tap(KEY_R);
    ready();
    expect(book().tab == 2 && book().count >= 3, "map tab shows unlocked floor plans");
    screenshot(directory, "book-map");
    map_browse();
    tap(KEY_R);
    ready();
    expect(book().tab == 3 && book().count == 5, "rule tab shows original rules");
    expect(book().mode == 0 && book().first == 0, "rules open directly as a full-width document");
    uint32_t rule_top = frame_hash();
    tap(KEY_A);
    ready();
    expect(book().mode == 0 && frame_hash() == rule_top, "rules have no second level");
    screenshot(directory, "book-rules-top");
    reader_scroll(rule_top);
    tap(KEY_DOWN);
    ready();
    expect(book().first == 1 && frame_hash() != rule_top, "rules scroll down one line");
    tap(KEY_UP);
    ready();
    expect(book().first == 0 && frame_hash() == rule_top, "rules restore the exact top line");
    tap(KEY_RIGHT);
    ready();
    expect(book().first == 8, "right turns one page of the same document");
    screenshot(directory, "book-rules-page2");
    tap(KEY_LEFT);
    ready();
    expect(book().first == 0 && frame_hash() == rule_top, "left restores the exact prior page");
    for (int i = 0; i < 30; ++i)
    {
        tap(KEY_RIGHT);
        ready();
    }
    expect(book().first > 40 && book().mode == 0, "all unlocked rules are reachable without a list");
    screenshot(directory, "book-rules-last");
    tap(KEY_DOWN);
    ready();
    tap(KEY_START);
    frames(40);
    expect(book().paused, "Start opens pause while reading book");
    screenshot(directory, "book-pause");
    tap(KEY_START);
    ready();
    expect(book().mode == 0 && book().tab == 3, "pause restores the rules document");
    tap(KEY_R);
    ready();
    expect(book().tab == 4 && book().count == 7 && book().mode == 0,
           "Notes lists only the seven unlocked chapter-one records");
    screenshot(directory, "book-notes-list");
    tap(KEY_RIGHT);
    ready();
    expect(book().selected == 6 && book().first == 1, "Notes pages through a full-width six-row list");
    tap(KEY_DOWN);
    ready();
    expect(book().selected == 0 && book().first == 0, "Notes wraps back to the first record");
    tap(KEY_A);
    ready();
    expect(book().mode == 1, "Notes opens a text-only detail");
    screenshot(directory, "book-notes-detail");
    uint32_t note_top = frame_hash();
    tap(KEY_A);
    ready();
    expect(book().mode == 1 && frame_hash() == note_top, "Notes has no image or evidence-submission level");
    tap(KEY_DOWN);
    ready();
    expect(book().first == 1, "long Notes uses the shared hardware text reader");
    uint32_t note_scrolled = frame_hash();
    tap(KEY_START);
    frames(40);
    expect(book().paused, "Notes can pause during reading");
    tap(KEY_START);
    ready();
    expect(book().tab == 4 && book().first == 1 && frame_hash() == note_scrolled,
           "pause restores the exact Notes text and scroll");
    tap(KEY_B);
    ready();
    expect(book().mode == 0 && book().selected == 0, "B returns to the same Notes list item");
    tap(KEY_R);
    ready();
    expect(book().tab == 0, "five-category tabs wrap from Notes to evidence");
    tap(KEY_L);
    ready();
    expect(book().tab == 4, "L wraps from evidence to Notes");
    tap(KEY_SELECT);
    frames(140);
    expect(!book().open, "Select closes book from detail");
    screenshot(directory, "book-story-resumed");
    long_detail();
}
static void timed_pause(void)
{
    reset_rom();
    enter_trial(1);
    for (int n = 0; n < 100 && trial().state != 6; ++n)
    {
        tap(KEY_A);
        frames(12);
    }
    expect(trial().state == 6, "formal trial reaches discussion");
    monitor = 1;
    tap(KEY_SELECT);
    ready();
    unsigned left = book().line_left, total = book().total_left, camera = book().camera_frame;
    frames(150);
    expect(book().line_left == left && book().total_left == total && book().camera_frame == camera,
           "book pauses both clocks and camera");
    tap(KEY_START);
    frames(45);
    expect(book().paused && book().line_left == left, "pause menu also freezes discussion");
    tap(KEY_START);
    ready();
    tap(KEY_SELECT);
    frames(100);
    expect(!book().open && trial().state == 6, "closing book restores discussion");
    expect(trial().line_left < left && trial().total_left < total, "discussion clocks resume");
    screenshot(directory, "book-trial-resumed");
}
static void forced_choice(void)
{
    reset_rom();
    enter_trial(14);
    monitor = 1;
    for (int n = 0; n < 1300 && !book().open; ++n)
    {
        trial_runtime_profile p = trial();
        if (p.interaction_ready && p.state == 6 && p.links)
        {
            frames(100);
            tap(KEY_UP);
        }
        else if (p.interaction_ready && (p.state == 1 || p.state == 2 || p.state == 3 || p.state == 6))
            tap(KEY_A);
        else
            frames(8);
    }
    expect(book().open && book().state == 7, "formal Trial14 enters forced evidence book");
    ready();
    expect(book().mode == 0 && book().count > 1, "forced picker starts in shared item list");
    screenshot(directory, "book-trial-evidence-list");
    unsigned left = book().line_left, total = book().total_left;
    core->setKeys(core, KEY_A);
    frames(90);
    core->setKeys(core, 0);
    frames(20);
    ready();
    expect(book().open && book().mode == 1, "one held A opens detail without also submitting");
    screenshot(directory, "book-trial-evidence-detail");
    tap(KEY_R);
    ready();
    expect(book().mode == 2, "R opens evidence full screen from detail");
    screenshot(directory, "book-trial-evidence-full");
    tap(KEY_B);
    ready();
    expect(book().mode == 1, "full evidence returns to same detail");
    tap(KEY_B);
    ready();
    tap(KEY_B);
    tap(KEY_SELECT);
    expect(book().open && book().mode == 0, "Back=false cannot be bypassed with B or Select");
    expect(book().line_left == left && book().total_left == total, "forced list/detail/full screen keep clocks frozen");
    tap(KEY_A);
    ready();
    tap(KEY_A);
    frames(80);
    expect(!book().open && trial().chosen > 0, "second independent A submits item ID to formal trial");
    screenshot(directory, "book-trial-submitted");
}
static void forced_map(void)
{
    reset_rom();
    enter_trial(14);
    monitor = 1;
    for (int n = 0; n < 1300 && !book().open; ++n)
    {
        trial_runtime_profile p = trial();
        if (p.interaction_ready && p.state == 6 && p.links)
        {
            frames(100);
            tap(KEY_UP);
        }
        else if (p.interaction_ready && (p.state == 1 || p.state == 2 || p.state == 3 || p.state == 6))
            tap(KEY_A);
        else
            frames(8);
    }
    ready();
    expect(book().tab == 0, "map question starts with original evidence question");
    for (int i = 0; i < 6; ++i)
    {
        tap(KEY_RIGHT);
        ready();
    }
    tap(KEY_A);
    ready();
    tap(KEY_A);
    frames(50);
    for (int n = 0; n < 1300 && !book().open; ++n)
    {
        trial_runtime_profile p = trial();
        if (p.interaction_ready && p.state == 2)
        {
            tap(KEY_DOWN);
            tap(KEY_A);
        }
        else if (p.interaction_ready && (p.state == 1 || p.state == 3 || p.state == 6))
            tap(KEY_A);
        else
            frames(8);
    }
    ready();
    expect(book().tab == 2 && book().count == 1, "formal Trial14 map restricts first floor to Lounge");
    expect(book().map_floor == 0 && book().map_area == 0, "first forced answer is original Lounge");
    screenshot(directory, "map-trial-restricted");
    unsigned left = book().line_left, total = book().total_left;
    tap(KEY_RIGHT);
    ready();
    expect(book().map_area == 0, "non-candidate first-floor rooms cannot be selected");
    tap(KEY_DOWN);
    ready();
    expect(book().map_floor == 1 && book().map_area == 24, "down chooses allowed RecreationRoom on second floor");
    tap(KEY_DOWN);
    ready();
    expect(book().map_floor == 3 && book().map_area == 30, "next floor chooses original BasementHallway");
    tap(KEY_B);
    tap(KEY_SELECT);
    tap(KEY_R);
    expect(book().open && book().tab == 2 && book().mode == 0, "forced map cannot close or change category");
    core->setKeys(core, KEY_A);
    frames(90);
    core->setKeys(core, 0);
    frames(20);
    ready();
    expect(book().open && book().mode == 1, "held A opens map detail without submitting");
    screenshot(directory, "map-trial-confirm");
    expect(book().line_left == left && book().total_left == total, "map selection and confirmation freeze both timers");
    tap(KEY_B);
    ready();
    expect(book().mode == 0 && book().map_area == 30, "B restores same forced map choice");
    tap(KEY_A);
    ready();
    tap(KEY_A);
    frames(80);
    expect(!book().open && trial().chosen > 0, "independent second A submits formal map answer");
    screenshot(directory, "map-trial-submitted");
}
int main(int argc, char** argv)
{
    expect(argc == 7,
           "usage: witch_book_game_test ROM output book_address graphics_address trial_address map_reference");
    FILE* reference = fopen(argv[6], "rb");
    expect(reference != NULL, "open offline map planes");
    expect(fread(map_reference, 1, sizeof(map_reference), reference) == sizeof(map_reference),
           "read all four offline map planes");
    fclose(reference);
    directory = argv[2];
    book_address = strtoul(argv[3], NULL, 16);
    graphics_address = strtoul(argv[4], NULL, 16);
    trial_address = strtoul(argv[5], NULL, 16);
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
    browse();
    timed_pause();
    forced_choice();
    forced_map();
    map_versions();
    printf("VBlank peak_ticks=%u\n", peak_vblank);
    expect(peak_vblank < 4389, "book uploads complete within one hardware frame");
    printf("PASS: book browsing, portraits/maps/rules/Notes, version updates, two-level view, pause/resume; frames=%u "
           "peak_ticks=%u missed=%u\n",
           updates, peak, missed);
    core->deinit(core);
    return 0;
}
