/* Exercise the production SFX demo, script bus, PCM and 16+2 mixing limit. */
#define main unused_smoke_main
#include "smoke_test.c"
#undef main
#include <mgba/core/blip_buf.h>
#include "../src/graphics_profile.h"
#include "../src/save_runtime.h"
static unsigned mask_address, layer_address, graphics_address, nonzero, peak_ticks, voice_peak, measuring;
static void run(int count)
{
    short pcm[4096];
    while (count--)
    {
        frames(1);
        unsigned mask = core->busRead32(core, mask_address), voices = __builtin_popcount(mask);
        if (voices > voice_peak)
            voice_peak = voices;
        expect(voices <= 2, "at most two real Maxmod effects");
        if (measuring)
        {
            unsigned ticks = core->busRead32(core, graphics_address + offsetof(struct graphics_profile, core_ticks));
            if (ticks > peak_ticks)
            {
                peak_ticks = ticks;
                if (ticks > 4200)
                    fprintf(stderr, "peak=%u keys=%03x mask=%x\n", ticks, (~core->busRead16(core, 0x04000130)) & 1023,
                            mask);
            }
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            int n = blip_read_samples(core->getAudioChannel(core, ch), pcm, 4096, 0);
            for (int i = 0; i < n; ++i)
                nonzero += pcm[i] != 0;
        }
    }
}
static void key(unsigned keys)
{
    core->setKeys(core, keys);
    run(3);
    core->setKeys(core, 0);
    run(18);
}
static void select_cue(int* current, int index)
{
    while (*current + 8 <= index)
    {
        key(KEY_RIGHT);
        *current += 8;
    }
    while (*current > index)
    {
        key(KEY_UP);
        --*current;
    }
    while (*current < index)
    {
        key(KEY_DOWN);
        ++*current;
    }
}
int main(int argc, char** argv)
{
    expect(argc == 13, "ROM output mask layer graphics fire rain foot school phone_index ring_song old_song");
    mask_address = strtoul(argv[3], NULL, 16);
    layer_address = strtoul(argv[4], NULL, 16);
    graphics_address = strtoul(argv[5], NULL, 16);
    core = mCoreFind(argv[1]);
    expect(core && core->init(core), "init mGBA");
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
    debug_menu_navigate();
    for (int i = 0; i < 5; ++i)
        key(KEY_DOWN);
    key(KEY_A);
    run(20);
    int selected = 0;
    select_cue(&selected, atoi(argv[6]));
    unsigned before = nonzero;
    key(KEY_A);
    run(340);
    expect(nonzero - before > 1000, "fire native loop audible across boundary");
    expect(core->busRead32(core, mask_address), "fire remains playing");
    screenshot(argv[2], "sfx-fire-loop");
    select_cue(&selected, atoi(argv[7]));
    key(KEY_A);
    run(40);
    expect(__builtin_popcount(core->busRead32(core, mask_address)) == 2,
           "shared fire/rain aliases own separate voices");
    key(KEY_B);
    expect(!core->busRead32(core, mask_address), "B stops SFX without leaving demo");
    // Measure live music plus two continuous environment voices, then UI selection/sequence work.
    key(KEY_R);
    run(120);
    expect(core->busRead8(core, layer_address + 8), "R starts complete HirO");
    measuring = 1;
    select_cue(&selected, atoi(argv[6]));
    key(KEY_A);
    select_cue(&selected, atoi(argv[7]));
    key(KEY_A);
    run(600);
    expect(__builtin_popcount(core->busRead32(core, mask_address)) == 2, "music and two native loops coexist");
    screenshot(argv[2], "sfx-hiro-two-voices");
    expect(core->busRead16(core, layer_address + 56) == 960, "continuous ambience leaves music gain unchanged");
    key(KEY_B);
    select_cue(&selected, atoi(argv[8]));
    unsigned same_song = core->busRead32(core, layer_address + 24);
    key(KEY_A);
    expect(core->busRead16(core, layer_address + 56) == 678, "foreground footsteps duck full HirO by 3dB");
    expect(core->busRead32(core, layer_address + 24) == same_song, "ducking keeps the same music instance");
    run(380);
    expect(core->busRead16(core, layer_address + 56) == 960, "music gain returns after footsteps");
    screenshot(argv[2], "sfx-footsteps");
    select_cue(&selected, atoi(argv[9]));
    key(KEY_A);
    run(640);
    expect(!core->busRead32(core, mask_address), "finite bell sequence stops all its notes");
    measuring = 0;
    key(KEY_R);
    key(KEY_START);
    for (int i = 0; i < 4; ++i)
        key(KEY_UP);
    key(KEY_A);
    for (int i = 0; i < 3; ++i)
        key(KEY_DOWN);
    key(KEY_A);
    run(280);
    for (int i = 0; i < 15; ++i)
        key(KEY_A);
    expect(nonzero > 10000, "script sound bus emits PCM");
    screenshot(argv[2], "sfx-adv04");
    // Production ADV14: the ringtone owns the music bus, then restarts Bgm005.
    core->reset(core);
    debug_menu_navigate();
    key(KEY_DOWN);
    key(KEY_A);
    int index = atoi(argv[10]);
    for (int i = 0; i < index / 6; ++i)
        key(KEY_RIGHT);
    for (int i = 0; i < index % 6; ++i)
        key(KEY_DOWN);
    key(KEY_A);
    run(260);
    unsigned ring = strtoul(argv[11], NULL, 16), old_song = strtoul(argv[12], NULL, 16);
    expect(core->busRead32(core, layer_address + 24) == old_song, "ADV14 begins with original Bgm005");
    for (int i = 0; i < 200 && core->busRead32(core, layer_address + 24) != ring; ++i)
    {
        key(KEY_A);
        run(30);
    }
    expect(core->busRead32(core, layer_address + 24) == ring, "native Ambi_3_1 selects the ringtone XM");
    unsigned before_ring = nonzero;
    run(1620);
    expect(nonzero - before_ring > 1000, "ringtone loops with audible PCM");
    screenshot(argv[2], "sfx-phone-music");
    for (int i = 0; i < 100 && core->busRead32(core, layer_address + 24) != old_song; ++i)
    {
        key(KEY_A);
        run(30);
    }
    expect(core->busRead32(core, layer_address + 24) == old_song, "native stop ringtone replays original Bgm005");
    // The first nightmare's looping laughter must not leak into waking up.
    core->reset(core);
    debug_menu_navigate();
    key(KEY_DOWN);
    key(KEY_A);
    key(KEY_A);
    run(300);
    unsigned save = elf_symbol("save_runtime");
    expect(save, "save runtime symbol is exported");
    expect(core->busRead32(core, save + offsetof(save_runtime_profile, script)) == 0, "start ADV01");
    expect(core->busRead32(core, mask_address), "ADV01 laughter is playing");
    for (int i = 0; i < 80 && !core->busRead32(core, save + offsetof(save_runtime_profile, script)); ++i)
    {
        key(KEY_A);
        run(240);
    }
    expect(core->busRead32(core, save + offsetof(save_runtime_profile, script)) == 1,
           "reach ADV02 through script jump");
    expect(!core->busRead32(core, mask_address), "script jump cancels native looping laughter");
    run(600);
    expect(!core->busRead32(core, mask_address), "cancelled laughter does not resume");
    screenshot(argv[2], "sfx-prologue-boundary");
    printf("PASS: independent SFX demo, PCM, script flow, shared alias loops, finite notes, prologue loop cleanup, "
           "full HirO + two real SFX; peak_ticks=%u voice_peak=%u nonzero=%u\n",
           peak_ticks, voice_peak, nonzero);
    expect(peak_ticks < 4389, "SFX demo and 16+2 playback meet frame budget");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
