/* Real input, real audio and persistent collection; never linked into the ROM. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include "../src/music_room_runtime.h"
#include "../src/home_runtime.h"
#include "../src/save_runtime.h"
#include <mgba/core/blip_buf.h>
#include <mgba/internal/gba/gba.h>
static unsigned room_address, home_address, save_address, music_address, graphics_address, pcm, peak, room_frames,
    missed, last_missed;
static const char* output;
static music_room_profile room(void)
{
    music_room_profile p;
    for (unsigned i = 0; i < sizeof(p) / 4; ++i)
        ((uint32_t*)&p)[i] = core->busRead32(core, room_address + i * 4);
    return p;
}
static home_runtime_profile home(void)
{
    home_runtime_profile p;
    for (unsigned i = 0; i < sizeof(p) / 4; ++i)
        ((uint32_t*)&p)[i] = core->busRead32(core, home_address + i * 4);
    return p;
}
static void run(unsigned n)
{
    short samples[1024];
    while (n--)
    {
        frames(1);
        for (int c = 0; c < 2; ++c)
        {
            int count = blip_read_samples(core->getAudioChannel(core, c), samples, 1024, 0);
            for (int i = 0; i < count; ++i)
                pcm += samples[i] != 0;
        }
        if (room().active)
        {
            unsigned ticks = core->busRead32(core, graphics_address + 24);
            if (ticks > peak)
            {
                peak = ticks;
                music_room_profile p = room();
                fprintf(stderr, "peak=%u phase=%u dirty=%u selected=%u playing=%u ui=%u update=%u ready=%u\n", ticks,
                        p.phase, p.dirty, p.selected, p.playing, core->busRead32(core, graphics_address + 36),
                        core->busRead32(core, graphics_address + 40), p.ready);
            }
            unsigned current = room().missed;
            missed += current >= last_missed ? current - last_missed : current;
            last_missed = current;
            ++room_frames;
        }
        else
            last_missed = 0;
    }
}
static void tap(unsigned keys)
{
    core->setKeys(core, keys);
    run(3);
    core->setKeys(core, 0);
    run(32);
}
static void ready(void)
{
    for (int i = 0; i < 240; ++i)
    {
        run(1);
        if (room().active && room().ready)
            return;
    }
    screenshot(output, "timeout");
    expect(0, "music room finishes bounded background/row drawing");
}
static void open_room(void)
{
    tap(KEY_A);
    for (int i = 0; i < 3; ++i)
        tap(KEY_DOWN);
    tap(KEY_A);
    ready();
}
static void cold(void)
{
    core->setKeys(core, 0);
    core->reset(core);
    wait_for_title();
}
static uint32_t crc(const uint8_t* p, unsigned n)
{
    uint32_t c = ~0u;
    while (n--)
    {
        c ^= *p++;
        for (int i = 0; i < 8; ++i)
            c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
    }
    return ~c;
}
static unsigned global_page(const uint8_t* flash)
{
    unsigned best = 0, sequence = 0;
    for (unsigned at = 122880; at < 131072; at += 4096)
    {
        uint32_t value;
        memcpy(&value, flash + at + 4, 4);
        if (flash[at + 63] == 0xa5 && (!best || (int32_t)(value - sequence) > 0))
        {
            best = at;
            sequence = value;
        }
    }
    expect(best, "global COW page exists");
    return best;
}
static void source(void)
{
    debug_hotkey();
    tap(KEY_START);
    tap(KEY_DOWN);
    tap(KEY_A);
    for (int i = 0; i < 3; ++i)
        tap(KEY_DOWN);
    tap(KEY_A);
    run(900);
    expect(core->busRead32(core, save_address + 44) == 3, "actual ADV04 entered");
}
static void select_track(unsigned index)
{
    while (room().selected != index)
    {
        tap(KEY_DOWN);
        ready();
    }
}
int main(int argc, char** argv)
{
    expect(argc == 12, "ROM output room home save music graphics source_index source_bit hiro_index count");
    output = argv[2];
    room_address = strtoul(argv[3], 0, 16);
    home_address = strtoul(argv[4], 0, 16);
    save_address = strtoul(argv[5], 0, 16);
    music_address = strtoul(argv[6], 0, 16);
    graphics_address = strtoul(argv[7], 0, 16);
    unsigned source_index = atoi(argv[8]), source_bit = atoi(argv[9]);
    core = mCoreFind(argv[1]);
    expect(core && core->init(core), "mGBA init");
    mCoreInitConfig(core, 0);
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
    cold();
    open_room();
    expect(!room().unlocked && !room().playing, "title music does not unlock the empty music room");
    screenshot(output, "empty");
    tap(KEY_A);
    ready();
    expect(!room().playing && !core->busRead8(core, music_address + 8), "locked A cannot play music");
    tap(KEY_UP);
    ready();
    expect(room().selected == (unsigned)atoi(argv[11]) - 1 && room().page == ((unsigned)atoi(argv[11]) - 1) / 5,
           "up crosses to the last page");
    tap(KEY_DOWN);
    ready();
    expect(!room().selected && !room().page, "down wraps to the first page");
    tap(KEY_R);
    ready();
    expect(room().selected == 5, "R turns one page");
    tap(KEY_LEFT);
    ready();
    expect(!room().selected, "left turns back");
    tap(KEY_START);
    ready();
    expect(room().active, "Start does not open the debug menu from the formal room");
    tap(KEY_B);
    expect(home().page == 1 && home().selected == 3, "B returns to the start menu's music entry");
    source();
    cold();
    open_room();
    expect(room().unlocked > 0, "actual script playback unlocks music across a cold reset");
    unsigned collected = room().unlocked;
    select_track(source_index);
    tap(KEY_A);
    ready();
    run(120);
    expect(room().playing == source_index + 1, "collected music plays");
    unsigned song = core->busRead32(core, music_address + 24), before = pcm;
    expect(song, "real music module installed");
    screenshot(output, "playing");
    tap(KEY_DOWN);
    ready();
    expect(core->busRead32(core, music_address + 24) == song, "moving the cursor does not change songs");
    run(120);
    expect(pcm - before > 1000, "music room produces real stereo PCM");
    tap(KEY_A);
    ready();
    expect(room().playing == source_index + 1, "locked row A leaves current playback intact");
    screenshot(output, "locked-playing");
    select_track(source_index);
    tap(KEY_A);
    ready();
    expect(!room().playing, "A on playing row stops it");
    tap(KEY_B);
    // The full music DEMO is outside collection progress.
    debug_hotkey();
    tap(KEY_START);
    for (int i = 0; i < 4; ++i)
        tap(KEY_DOWN);
    tap(KEY_A);
    tap(KEY_A);
    run(120);
    expect(core->busRead8(core, music_address + 8), "full music DEMO is retained");
    cold();
    open_room();
    expect(room().unlocked == collected, "demo audition does not collect another track");
    tap(KEY_B);
    run(600);
    uint8_t* flash = malloc(131072);
    expect(flash != 0, "host Flash snapshot");
    memcpy(flash, ((struct GBA*)core->board)->memory.savedata.data, 131072);
    unsigned at = global_page(flash);
    expect(flash[at + 18] == 120, "music extends global payload from 112B to 120B");
    expect(flash[at + 256 + 112 + source_bit / 8] & (1u << (source_bit % 8)),
           "source music bit persists in upper-bank Flash");
    // Emulate a later collected library, then test the full HirO arrangement.
    uint8_t* stress = malloc(131072);
    expect(stress != 0, "host later-progress fixture");
    memcpy(stress, flash, 131072);
    stress[at + 256 + 112 + 36 / 8] |= 1u << (36 % 8);
    uint32_t checksum = crc(stress + at + 64, 256 + 120 - 64);
    memcpy(stress + at + 12, &checksum, 4);
    checksum = crc(stress + at, 24);
    memcpy(stress + at + 24, &checksum, 4);
    memcpy(((struct GBA*)core->board)->memory.savedata.data, stress, 131072);
    free(stress);
    cold();
    open_room();
    select_track(atoi(argv[10]));
    tap(KEY_A);
    ready();
    run(1200);
    for (int i = 0; i < 120; ++i)
    {
        tap(KEY_DOWN);
        ready();
    }
    expect(room().playing == (unsigned)atoi(argv[10]) + 1, "full HirO continues through scrolling and page changes");
    screenshot(output, "hiro-library");
    tap(KEY_B);
    // A real old-format global prefix with a different story signature must migrate.
    for (unsigned length = 112; length >= 92; length -= 20)
    {
        uint8_t legacy[131072];
        memcpy(legacy, flash, 131072);
        unsigned other = at == 122880 ? 126976 : 122880;
        memset(legacy + other, 255, 4096);
        uint16_t size = length, volume = 768;
        memcpy(legacy + at + 18, &size, 2);
        memcpy(legacy + at + 256, &volume, 2);
        legacy[at + 258] = 1;
        legacy[at + 259] = 2;
        legacy[at + 256 + 92] |= 1;
        uint32_t signature = 0x13572468;
        memcpy(legacy + at + 8, &signature, 4);
        uint32_t sum = crc(legacy + at + 64, 256 + length - 64);
        memcpy(legacy + at + 12, &sum, 4);
        sum = crc(legacy + at, 24);
        memcpy(legacy + at + 24, &sum, 4);
        memcpy(((struct GBA*)core->board)->memory.savedata.data, legacy, 131072);
        cold();
        expect(home().speed == 1 && home().music == 3 && home().sound == 2,
               "legacy globals retain options despite changed story signature");
        open_room();
        expect(!room().unlocked, "old globals initialize new music flags to zero");
        tap(KEY_B);
        if (length == 112)
        {
            source();
            cold();
            open_room();
            expect(room().unlocked > 0, "legacy global upgrades when another source song is heard");
            tap(KEY_B);
            run(600);
            uint8_t* now = ((struct GBA*)core->board)->memory.savedata.data;
            unsigned page = global_page(now);
            expect(now[page + 18] == 120 && (now[page + 256 + 92] & 1),
                   "global upgrade preserves existing gallery bits");
        }
    }
    // The Trial player shares the same collection path (Trial01 starts Bgm007).
    cold();
    debug_hotkey();
    tap(KEY_START);
    tap(KEY_DOWN);
    tap(KEY_DOWN);
    tap(KEY_A);
    tap(KEY_DOWN);
    tap(KEY_A);
    run(600);
    for (int i = 0; i < 1800 && core->busRead32(core, save_address + 28); ++i)
        run(1);
    run(4);
    cold();
    open_room();
    expect(room().unlocked > 0, "actual Trial playback collects music");
    select_track(6);
    tap(KEY_A);
    ready();
    expect(room().playing == 7, "Trial's Bgm007 is available after cold reset");
    screenshot(output, "trial-collected");
    free(flash);
    printf("PASS: collected-track formal room, real script unlock, global Flash migration92/112->120, old-signature "
           "options/gallery, cold reset, audition isolation and full HirO; peak_ticks=%u room_frames=%u missed=%u "
           "PCM=%u\n",
           peak, room_frames, missed, pcm);
    expect(peak < 4389 && !missed, "music room stays in frame budget");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(0);
    mStandardLoggerDeinit(&logger);
    return 0;
}
