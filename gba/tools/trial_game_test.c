/* Production integration: actual UI input and VM state, never linked into ROM. */
#define main navigation_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/trial_runtime.h"
#include "../src/witch_book_runtime.h"
#include "obj_pixels.h"
static unsigned address, book_address, sfx_address, sfx_counter;
static const char* directory;
static unsigned phase_max[8], phase_frames[8], phase_missed[8];
static uint16_t panorama[2048 * 192];
static uint8_t podium_forbidden[18][240 * 160];
static int camera_end;
static trial_runtime_profile metric(void)
{
    trial_runtime_profile p;
    uint32_t* v = (uint32_t*)&p;
    for (unsigned i = 0; i < sizeof(p) / 4; ++i)
        v[i] = core->busRead32(core, address + i * 4);
    return p;
}
static void monitored_frames(int count)
{
    static unsigned last_missed = 0;
    while (count--)
    {
        frames(1);
        trial_runtime_profile p = metric();
        if (p.missed > last_missed)
        {
            fprintf(stderr,
                    "drop: +%u frame=%u state=%u preview=%u movie=%u ticks=%u UI=%u init=%u clear=%u text=%u hud=%u\n",
                    p.missed - last_missed, p.frame, p.state, p.preview, p.movie_frame, p.core_ticks, p.ui_work,
                    p.scene_init, p.clear_work, p.text_work, p.hud_work);
            last_missed = p.missed;
        }
    }
}
#define frames monitored_frames
static void sample(int count, int phase)
{
    unsigned missed = metric().missed;
    for (int i = 0; i < count; ++i)
    {
        frames(1);
        trial_runtime_profile p = metric();
        if (p.core_ticks > phase_max[phase])
            phase_max[phase] = p.core_ticks;
        ++phase_frames[phase];
    }
    phase_missed[phase] += metric().missed - missed;
}
static void tap(unsigned key)
{
    core->setKeys(core, key);
    frames(2);
    core->setKeys(core, 0);
    frames(12);
}
static witch_book_profile book_metric(void)
{
    witch_book_profile p;
    uint32_t* v = (uint32_t*)&p;
    for (unsigned i = 0; i < sizeof(p) / 4; ++i)
        v[i] = core->busRead32(core, book_address + i * 4);
    return p;
}
static void wait_inventory(int mode)
{
    for (int i = 0; i < 1800; ++i)
    {
        frames(1);
        witch_book_profile p = book_metric();
        if (p.open && p.ready && (int)p.mode == mode)
        {
            frames(12);
            return;
        }
    }
    expect(0, "shared book reaches requested selection level");
}
static void chapter_list(void)
{
    chapters_hotkey();
    tap(KEY_START);
    frames(40);
}
static void debug_jump(void)
{
    debug_hotkey();
    tap(KEY_START);
}
static void submit_book(void)
{
    wait_inventory(0);
    tap(KEY_A);
    wait_inventory(1);
    tap(KEY_A);
}
static void wait_for(int state, int view, int ready)
{
    for (int i = 0; i < 1800; ++i)
    {
        frames(1);
        trial_runtime_profile p = metric();
        if ((int)p.state == state && (view < 0 || (int)p.view == view) && (!ready || p.interaction_ready))
        {
            frames(20);
            return;
        }
    }
    trial_runtime_profile p = metric();
    fprintf(stderr, "timeout state=%u view=%u scene=%u canvas=%u frame=%u movie=%u preview=%u\n", p.state, p.view,
            p.scene_ready, p.canvas_ready, p.frame, p.movie_frame, p.preview);
    screenshot(directory, "trial-game-failure");
    expect(0, "trial production player reaches requested state");
}
static void next_text(void)
{
    tap(KEY_A);
    frames(120);
    tap(KEY_A);
}
static void reveal_debate(void)
{
    trial_runtime_profile before = metric();
    expect(before.state == 6 && before.line_left > 120, "A reveal regression begins on a fresh timed statement");
    core->setKeys(core, KEY_A);
    frames(32);
    trial_runtime_profile after = metric();
    expect(after.state == 6 && after.actor == before.actor && after.duration == before.duration &&
               after.links == before.links,
           "first A and held A only complete the current statement's typewriter");
    expect(after.line_left < before.line_left && before.line_left - after.line_left <= 32 &&
               before.total_left - after.total_left <= 32,
           "revealing text keeps both timers running without charging the skipped tail");
    core->setKeys(core, 0);
    frames(12);
}
static void no_legs(void)
{
    if (metric().background >= 10)
    {
        const uint8_t* mask = podium_forbidden[metric().background - 10];
        for (int y = 0; y < 160; ++y)
            for (int x = 0; x < 240; ++x)
                if (mask[y * 240 + x])
                    expect(!object_ink(x, y), "source-distance close view never paints actors over a nearer podium");
    }
    else
        for (int y = 106; y < 158; y += 2)
            for (int x = 0; x < 240; ++x)
                expect(!object_ink(x, y), "wide podium hides all lower-body OBJ pixels, including outside its sides");
}
static void no_missing_objects(void)
{
    unsigned mismatches = 0;
    for (int y = 0; y < 160; ++y)
        for (int x = 0; x < 240; ++x)
        {
            unsigned expected;
            if (!object_pixel(x, y, &expected))
                continue;
            unsigned rgb = pixels[y * 240 + x];
            unsigned actual = ((rgb >> 3) & 31) | (((rgb >> 11) & 31) << 5) | (((rgb >> 19) & 31) << 10);
            mismatches += actual != expected;
        }
    if (mismatches)
        fprintf(stderr, "cut-in OBJ pixels missing or corrupted: %u\n", mismatches);
    expect(!mismatches, "particles preserve every visible hero/glass OBJ pixel");
}
static void clock_frozen(int count, const char* reason)
{
    unsigned left = metric().total_left;
    frames(count);
    expect(metric().total_left == left, reason);
}
static int white_ink(unsigned pixel)
{
    return (pixel & 255) >= 240 && ((pixel >> 8) & 255) >= 240 && ((pixel >> 16) & 255) >= 230;
}
static void selected_shadow(int left, int top, int width)
{
    unsigned edges = 0;
    for (int y = top; y < top + 12; ++y)
        for (int x = left; x < left + width; ++x)
            if (white_ink(pixels[y * 240 + x]) && !white_ink(pixels[(y + 1) * 240 + x + 1]))
            {
                expect(!(pixels[(y + 1) * 240 + x + 1] & 0xffffffu),
                       "selected bubble label has a black shadow one pixel down/right");
                ++edges;
            }
    expect(edges > 20, "shadow checks cover the selected white label");
}
static uint32_t clock_pixels(int x, int y, int width, int height)
{
    uint32_t hash = 2166136261u;
    for (int row = y; row < y + height; ++row)
        for (int col = x; col < x + width; ++col)
            hash = (hash ^ pixels[row * 240 + col]) * 16777619u;
    return hash;
}
static void clock_separator(void)
{
    for (int y = 10; y <= 15; y += 5)
    {
        unsigned rgb = pixels[y * 240 + 214];
        expect((rgb & 255) >= 224 && ((rgb >> 8) & 255) >= 224 && ((rgb >> 16) & 255) >= 224,
               "MM:SS separator has both visible white dots");
    }
}
static void movie(int kind, const char* name)
{
    int reached = 0;
    for (int i = 0; i < 1800; ++i)
    {
        frames(1);
        trial_runtime_profile p = metric();
        if (p.state == 8 && p.movie_kind == (unsigned)kind && p.movie_frame > 0 && p.movie_frame <= 3)
        {
            reached = 1;
            break;
        }
    }
    expect(reached, "serial cut-in begins with requested original kind");
    unsigned previous = 0, counter = 0;
    int still = 0, count = 0, book_pause = 0, sounds = 0;
    for (int guard = 0; guard < 400; ++guard)
    {
        trial_runtime_profile p = metric();
        if (p.state != 8 || p.movie_kind != (unsigned)kind)
            break;
        if (!book_pause && kind == 0 && p.movie_frame >= 32)
        {
            tap(KEY_SELECT);
            wait_inventory(0);
            unsigned frozen = metric().movie_frame;
            if (p.script == 396)
            {
                tap(KEY_R);
                wait_inventory(0);
                expect(!book_metric().count, "empty book category remains usable");
                screenshot(directory, "trial-game-empty-book");
                tap(KEY_L);
                wait_inventory(0);
            }
            frames(90);
            expect(metric().movie_frame == frozen, "Select book freezes the cut-in frame");
            tap(KEY_SELECT);
            for (int n = 0; n < 600 && metric().movie_frame <= frozen + 1; ++n)
                frames(1);
            expect(metric().state == 8 && metric().movie_frame > frozen && metric().movie_frame < frozen + 8,
                   "closing book resumes cut-in without restarting");
            book_pause = 1;
            continue;
        }
        if (p.movie_frame != previous && p.movie_frame > 0)
        {
            previous = p.movie_frame;
            if (previous == 44 || previous == 74)
                counter = core->busRead8(core, sfx_counter);
            if (previous == 48 || previous == 78)
            {
                unsigned voices = (unsigned)__builtin_popcount(core->busRead32(core, sfx_address));
                expect(voices >= 1 && voices <= 2 && ((core->busRead8(core, sfx_counter) - counter) & 255) == 1,
                       "original crack/crash audio tracks play at their cut-in frames");
                ++sounds;
            }
            if ((previous - 1) % 3 == 0)
            {
                char frame[160];
                snprintf(frame, sizeof(frame), "%s-frame-%03u", name, previous);
                screenshot(directory, frame);
                ++count;
            }
            if (!still && previous >= 90)
            {
                screenshot(directory, name);
                still = 1;
            }
            if (previous >= 67 && previous <= 163 && (previous - 67) % 16 == 0)
                no_missing_objects();
        }
        sample(1, 2 + kind);
    }
    expect(count >= 65 && still, "full cut-in animation is captured, not only a still");
    expect(sounds == 2, "both glass sound events were observed in each hero's cut-in");
}
static void camera_sweep(void)
{
    expect(metric().camera == 1 && metric().camera_duration == 1500, "formal Trial00 starts original 25-second sweep");
    unsigned first = metric().camera_scroll, previous = 0;
    int paused = 0;
    while (metric().camera_frame < 1500)
    {
        trial_runtime_profile p = metric();
        if (p.camera_frame != previous && p.camera_frame % 3 == 0)
        {
            char name[80];
            snprintf(name, sizeof(name), "trial-formal-sweep-%04u", p.camera_frame);
            screenshot(directory, name);
            previous = p.camera_frame;
        }
        if (!paused && p.camera_frame >= 420)
        {
            tap(KEY_START);
            unsigned frame = metric().camera_frame;
            frames(120);
            expect(metric().camera_frame == frame, "pause freezes camera time");
            tap(KEY_START);
            paused = 1;
        }
        sample(1, 7);
        if (metric().camera_frame % 120 == 0)
        {
            no_legs();
            int matched = 0;
            // The profile may describe the CPU's next frame; sample the current
            // viewport or its immediate neighbour against the offline reference.
            for (int delta = -1; delta <= 1 && !matched; ++delta)
            {
                int scroll = (int)metric().camera_scroll + delta;
                unsigned errors = 0;
                for (int y = 0; y < 80; y += 2)
                    for (int x = 0; x < 240; x += 2)
                    {
                        if (object_ink(x, y))
                            continue;
                        unsigned rgb = pixels[y * 240 + x];
                        unsigned actual = ((rgb >> 3) & 31) | (((rgb >> 11) & 31) << 5) | (((rgb >> 19) & 31) << 10);
                        errors += actual != panorama[(y + 16) * 2048 + ((x + scroll) & 2047)];
                    }
                matched = !errors;
            }
            if (!matched)
                screenshot(directory, "panorama-mismatch");
            expect(matched, "streamed panorama matches original background pixels across cache wraps");
        }
    }
    sample(12, 7);
    expect(metric().camera_scroll == (unsigned)camera_end && metric().camera_scroll > first + 400,
           "sweep reaches final seat and stops");
    uint32_t end = frame_hash();
    sample(120, 7);
    expect(frame_hash() == end, "finished sweep holds without looping");
    screenshot(directory, "trial-formal-sweep-end");
}
static void scrolling_choices(void)
{
    /* A real four-option menu, reached through chapter selection and the pink link. */
    debug_jump();
    tap(KEY_DOWN);
    tap(KEY_A);
    frames(80);
    for (int i = 0; i < 13; ++i)
        tap(KEY_RIGHT);
    for (int i = 0; i < 5; ++i)
        tap(KEY_DOWN);
    tap(KEY_A);
    int found = 0, cooldown = 0;
    for (int n = 0; n < 9000; ++n)
    {
        trial_runtime_profile p = metric();
        if (p.script == 313 && p.state == 6 && p.actor == 7 && p.duration == 562 && p.links == 1)
        {
            found = 1;
            break;
        }
        if (!cooldown && p.interaction_ready && (p.state == 1 || p.state == 3 || p.state == 6))
        {
            core->setKeys(core, KEY_A);
            cooldown = 24;
        }
        else
            core->setKeys(core, 0);
        if (cooldown)
            --cooldown;
        frames(1);
    }
    expect(found, "formal Trial12 hallucination point reached");
    core->setKeys(core, 0);
    frames(180);
    screenshot(directory, "trial-phantom-dialogue");
    clock_separator();
    uint32_t digits = clock_pixels(200, 6, 31, 14), chrome = clock_pixels(181, 7, 9, 9);
    unsigned before = metric().total_left;
    frames(61);
    expect(before - metric().total_left == 61 && metric().duration == 562,
           "clock redraw preserves the running statement and total time");
    expect(clock_pixels(200, 6, 31, 14) != digits, "four-digit countdown changes after a second");
    expect(clock_pixels(181, 7, 9, 9) == chrome, "round clock ornament remains stable while digits change");
    tap(KEY_UP);
    wait_for(2, -1, 1);
    expect(metric().choice_first == 0 && metric().choice_rows == 2, "four-option menu begins with its first two rows");
    screenshot(directory, "trial-phantom-choice-0");
    uint32_t initial = frame_hash();
    clock_frozen(60, "scrolling bubble pauses discussion clock");
    clock_separator();
    expect(frame_hash() == initial, "paused bubble clock and its ornaments remain pixel-identical");
    for (int i = 1; i <= 3; ++i)
    {
        tap(KEY_DOWN);
        wait_for(2, -1, 1);
        expect(metric().choice == (unsigned)i && metric().choice_first == 1 && metric().choice_rows == 3,
               "selecting second option proactively reveals successor and Return");
        char name[64];
        snprintf(name, sizeof(name), "trial-phantom-choice-%d", i);
        screenshot(directory, name);
    }
    tap(KEY_UP);
    wait_for(2, -1, 1);
    expect(metric().choice == 2 && metric().choice_first == 1, "up keeps nearby choices visible");
    uint32_t selected = frame_hash();
    tap(KEY_START);
    clock_frozen(60, "pause freezes scrolling bubble");
    tap(KEY_START);
    wait_for(2, -1, 1);
    expect(frame_hash() == selected, "pause rebuilds the whole scrolled viewport");
    tap(KEY_DOWN);
    wait_for(2, -1, 1);
    tap(KEY_DOWN);
    wait_for(2, -1, 1);
    expect(metric().choice == 0 && frame_hash() == initial, "wraparound restores exact original option pixels");
    tap(KEY_B);
    wait_for(6, -1, 1);
    expect(metric().actor == 7, "bubble cancel restores the same long statement");
}

static int finish(int quick)
{
    for (int i = 0; i < 8; ++i)
        if (phase_frames[i])
            printf("phase %d: frames=%u max_ticks=%u missed=%u\n", i, phase_frames[i], phase_max[i], phase_missed[i]);
    printf("%s; frames=%u missed=%u peak_ticks=%u\n",
           quick ? "Quick formal trial entry, camera/pause and A/timer fairness"
                 : "Production cameras, three cut-ins, evidence/selector, bubbles, pause/cancel and actual Trial01",
           metric().frame, metric().missed, metric().max_ticks);
    for (int phase = 0; phase < 8; ++phase)
        expect(!phase_missed[phase], "continuous trial playback stays within its frame budget");
    puts("PASS: production trial workflow and frame budget");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    return 0;
}

int main(int argc, char** argv)
{
    expect(argc == 10, "usage: trial_game_test ROM output profile_address panorama_reference camera_end "
                       "podium_reference book_address sfx_address sfx_counter");
    camera_end = atoi(argv[5]);
    directory = argv[2];
    address = (unsigned)strtoul(argv[3], NULL, 16);
    book_address = (unsigned)strtoul(argv[7], NULL, 16);
    sfx_address = (unsigned)strtoul(argv[8], NULL, 16);
    sfx_counter = (unsigned)strtoul(argv[9], NULL, 16);
    FILE* reference = fopen(argv[4], "rb");
    expect(reference != NULL, "open offline panorama reference");
    expect(fread(panorama, 1, sizeof(panorama), reference) == sizeof(panorama), "load complete panorama reference");
    fclose(reference);
    reference = fopen(argv[6], "rb");
    expect(reference != NULL, "open source-distance podium masks");
    expect(fread(podium_forbidden, 1, sizeof(podium_forbidden), reference) == sizeof(podium_forbidden),
           "read all 18 source-distance podium masks");
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
    expect(mCoreLoadFile(core, argv[1]), "load production ROM");
    debug_boot();
    for (int i = 0; i < 2; ++i)
        tap(KEY_DOWN);
    tap(KEY_A);
    frames(80);
    uint32_t first_page = frame_hash();
    screenshot(directory, "trial-chapter-list");
    tap(KEY_UP);
    frames(40);
    screenshot(directory, "trial-chapter-list-last");
    tap(KEY_DOWN);
    frames(40);
    expect(frame_hash() == first_page, "trial list wraps within canonical chapters");
    tap(KEY_RIGHT);
    frames(40);
    screenshot(directory, "trial-chapter-list-page2");
    tap(KEY_LEFT);
    frames(40);
    expect(frame_hash() == first_page, "trial list pages independently of global script IDs");
    tap(KEY_DOWN);
    tap(KEY_A);
    wait_for(12, -1, 0);
    expect(metric().script == 231, "chapter selector directly starts selected Trial01 title animation");
    chapter_list();
    tap(KEY_UP);
    tap(KEY_A);
    wait_for(1, -1, 1);
    expect(metric().script == 230 && metric().actor == 7, "trial entrance starts canonical Trial00 with Leia");
    no_legs();
    screenshot(directory, "trial-formal-entry");
    camera_sweep();
    tap(KEY_A);
    wait_for(1, -1, 1);
    expect(metric().actor == 2, "wide-shot fallback follows Sherry's next statement");
    frames(140);
    no_legs();
    screenshot(directory, "trial-formal-speaker");
    double_tap_r();
    expect(playback_arrows(216) + playback_arrows(152) == 1, "trial dialogue shows the autoplay arrow");
    for (int i = 0; i < 4800 && metric().script == 230; ++i)
        frames(1);
    expect(metric().script == 231, "trial dialogue autoplays into Trial01");
    if (metric().state == 3)
    {
        wait_for(3, -1, 1);
        tap(KEY_A);
    }
    wait_for(6, -1, 1);
    expect(!playback_arrows(152), "entering discussion hides the autoplay arrow");
    expect(metric().links == 0, "formal first discussion statement has no objection point");
    reveal_debate();
    screenshot(directory, "trial-formal-discussion");
    unsigned ordinary_total = metric().total_left, ordinary_left = metric().line_left;
    tap(KEY_A);
    wait_for(6, -1, 1);
    expect(metric().total_left <= ordinary_total - ordinary_left &&
               ordinary_total - ordinary_left - metric().total_left < 80,
           "second A advances a completed ordinary statement and charges its remaining time");
    for (int i = 0; i < 3800 && metric().state != 1; ++i)
    {
        frames(1);
        if (metric().preview && metric().interaction_ready)
            tap(KEY_B);
    }
    wait_for(1, -1, 1);
    expect(!playback_arrows(216) && !playback_arrows(152), "autoplay does not resume after leaving discussion");
    if (getenv("MANOSABA_TRIAL_QUICK"))
    {
        int result = finish(1);
        mLogSetDefaultLogger(NULL);
        mStandardLoggerDeinit(&logger);
        return result;
    }
    /* The operation demo remains an explicitly selected script, outside the formal entrance. */
    debug_jump();
    tap(KEY_UP);
    tap(KEY_A);
    frames(40);
    tap(KEY_UP);
    tap(KEY_UP);
    tap(KEY_A);
    wait_for(1, 0, 1);
    frames(140);
    screenshot(directory, "trial-game-court");
    sample(600, 0);
    tap(KEY_A);
    wait_for(1, 3, 1);
    frames(140);
    screenshot(directory, "trial-game-pan");
    sample(1800, 1);
    tap(KEY_A);
    wait_for(1, 1, 1);
    frames(140);
    screenshot(directory, "trial-game-close");
    tap(KEY_A);
    movie(0, "trial-game-cutin-ema");
    movie(1, "trial-game-cutin-hiro");
    movie(2, "trial-game-cutin-witch-hiro");
    wait_inventory(0);
    frames(40);
    screenshot(directory, "trial-game-inventory");
    clock_frozen(120, "selector pauses clock");
    tap(KEY_RIGHT);
    wait_inventory(0);
    expect(book_metric().selected == 1, "right selects second inventory item");
    tap(KEY_A);
    wait_inventory(1);
    tap(KEY_R);
    wait_inventory(2);
    screenshot(directory, "trial-game-evidence");
    clock_frozen(120, "evidence pauses clock");
    tap(KEY_B);
    wait_inventory(1);
    tap(KEY_B);
    wait_inventory(0);
    expect(book_metric().selected == 1, "evidence returns to same selection");
    tap(KEY_A);
    wait_inventory(1);
    tap(KEY_A);
    wait_for(1, -1, 1);
    expect(metric().chosen > 0, "selected evidence stored");
    frames(160);
    tap(KEY_A);
    frames(180);
    expect(metric().preview != 0, "script evidence opens full screen");
    tap(KEY_A);
    wait_for(1, -1, 1);
    frames(140);
    tap(KEY_A);
    wait_for(6, -1, 1);
    reveal_debate();
    frames(200);
    expect(metric().links == 3, "three pink links survive reflow");
    screenshot(directory, "trial-game-debate");
    unsigned total = metric().total_left, left = metric().line_left;
    frames(30);
    expect(total - metric().total_left == 30 && left - metric().line_left == 30,
           "line and global timers tick together");
    double_tap_r();
    expect(!playback_arrows(152), "double-tapping R in discussion leaves normal playback");
    key_frames(KEY_R, 4);
    expect(playback_arrows(152) == 2, "holding R in discussion still shows fast-forward");
    key_frames(0, 4);
    const unsigned old_keys[] = {KEY_B, KEY_R};
    for (int i = 0; i < 2; ++i)
    {
        tap(old_keys[i]);
        expect(metric().state == 6, "old B/R no longer enter objection points");
    }
    const unsigned keys[] = {KEY_UP, KEY_LEFT, KEY_RIGHT};
    const char* names[] = {"trial-game-bubble-ema", "trial-game-bubble-approval", "trial-game-bubble-hiro"};
    for (int i = 0; i < 3; ++i)
    {
        tap(keys[i]);
        wait_for(2, -1, 1);
        frames(50);
        expect(metric().choice == 0, "Up/Left/Right each opens its point");
        screenshot(directory, names[i]);
        clock_frozen(120, "opaque bubble pauses clock");
        selected_shadow(56, 56, 72);
        tap(KEY_DOWN);
        expect(metric().choice == 1, "bubble down chooses return");
        tap(KEY_UP);
        expect(metric().choice == 0, "bubble up restores first choice");
        tap(KEY_DOWN);
        left = metric().line_left;
        total = metric().total_left;
        tap(KEY_A);
        wait_for(6, -1, 1);
        expect(metric().line_left <= left && left - metric().line_left < 40,
               "cancel restores remaining utterance time");
        expect(total - metric().total_left < 40, "cancel preserves total clock");
    }
    tap(KEY_START);
    clock_frozen(120, "pause menu freezes trial");
    tap(KEY_START);
    wait_for(6, -1, 1);
    total = metric().total_left;
    left = metric().line_left;
    tap(KEY_A);
    wait_for(1, -1, 1);
    expect(metric().total_left == total - left || metric().total_left + 1 == total - left,
           "A charges all skipped tail to total clock");
    screenshot(directory, "trial-game-complete");
    chapter_list();
    tap(KEY_A);
    wait_for(1, 0, 1);
    next_text();
    wait_for(1, 3, 1);
    next_text();
    wait_for(1, 1, 1);
    next_text();
    submit_book();
    wait_for(1, -1, 1);
    next_text();
    frames(120);
    tap(KEY_A);
    wait_for(1, -1, 1);
    next_text();
    wait_for(6, -1, 1);
    frames(180);
    tap(KEY_UP);
    wait_for(2, -1, 1);
    tap(KEY_A);
    wait_for(1, -1, 1);
    expect(metric().links == 3, "bubble confirmation reaches its target");
    chapter_list();
    tap(KEY_A);
    wait_for(1, 0, 1);
    next_text();
    wait_for(1, 3, 1);
    next_text();
    wait_for(1, 1, 1);
    next_text();
    submit_book();
    wait_for(1, -1, 1);
    next_text();
    frames(120);
    tap(KEY_A);
    wait_for(1, -1, 1);
    next_text();
    wait_for(6, -1, 1);
    total = metric().total_left;
    left = metric().line_left;
    sample((int)left + 80, 5);
    expect(metric().state == 1, "pink utterance expires automatically");
    expect(metric().total_left == total - left, "automatic expiry charges full duration exactly");
    /* Enter real Trial01 from the production script list (stable appended IDs). */
    chapter_list();
    for (int i = 0; i < 27; ++i)
        tap(KEY_LEFT);
    for (int i = 0; i < 3; ++i)
        tap(KEY_UP);
    tap(KEY_A);
    wait_for(6, -1, 1);
    expect(metric().script == 231 && metric().duration > 60,
           "real Trial01 automatically finishes the title and uses original voice duration");
    frames(100);
    no_legs();
    screenshot(directory, "trial-game-real-trial01");
    unsigned actor = metric().actor;
    left = metric().line_left;
    sample((int)left + 40, 6);
    expect(metric().state == 6 && metric().actor != actor,
           "ordinary real utterance automatically advances with camera");
    for (int i = 0; i < 20 && (!metric().links || metric().preview); ++i)
    {
        if (metric().preview)
            tap(KEY_B);
        else
            tap(KEY_A);
        wait_for(6, -1, 1);
    }
    expect(metric().links == 1, "real objection point reached");
    frames(120);
    no_legs();
    screenshot(directory, "trial-game-real-pink");
    tap(KEY_UP);
    wait_for(2, -1, 1);
    screenshot(directory, "trial-game-real-objection");
    tap(KEY_A);
    for (int i = 0; i < 20 && !(metric().state == 6 && metric().links && !metric().preview); ++i)
    {
        if (metric().preview)
            tap(KEY_B);
        else if (metric().state == 8)
            frames(240);
        else
            next_text();
    }
    expect(metric().hp == 10 && metric().state == 6, "actual wrong objection returns to debate without deducting HP");
    screenshot(directory, "trial-game-wrong-feedback");
    /* Correct source branch: reject the pen, not the blood. */
    for (int i = 0; i < 40 && !(metric().state == 6 && metric().links && !metric().preview); ++i)
    {
        if (metric().preview)
            tap(KEY_B);
        else if (metric().state == 6)
            tap(KEY_A);
        else
            next_text();
        frames(30);
    }
    expect(metric().state == 6 && metric().links == 1, "retry reaches first source point");
    frames(80); // Let the first point's typewriter finish before the skip press.
    tap(KEY_A);
    wait_for(6, -1, 1);
    frames(140);
    screenshot(directory, "trial-formal-correct-point");
    tap(KEY_UP);
    wait_for(2, -1, 1);
    screenshot(directory, "trial-formal-correct-choice");
    tap(KEY_A);
    movie(0, "trial-formal-cutin-ema");
    wait_for(1, -1, 1);
    expect(metric().hp == 10, "correct source cut-in preserves HP");
    int cooldown = 0;
    for (int guard = 0; guard < 5000 && metric().state != 2; ++guard)
    {
        trial_runtime_profile p = metric();
        if (!cooldown && p.interaction_ready && (p.state == 1 || p.state == 3))
        {
            core->setKeys(core, KEY_A);
            cooldown = 24;
        }
        else
            core->setKeys(core, 0);
        if (cooldown)
            --cooldown;
        frames(1);
    }
    core->setKeys(core, 0);
    wait_for(2, -1, 1);
    clock_separator();
    screenshot(directory, "trial-clock-ordinary-choice-0");
    clock_frozen(60, "ordinary choice pauses clock");
    tap(KEY_DOWN);
    wait_for(2, -1, 1);
    clock_separator();
    screenshot(directory, "trial-clock-ordinary-choice-1");
    uint32_t ordinary = frame_hash();
    frames(60);
    expect(frame_hash() == ordinary, "ordinary choice redraw retains its clock frame and digits");
    scrolling_choices();
    int result = finish(0);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return result;
}
