/* Actual ADV08 Intro repeats, pause, scripted fade, and full-track re-entry. */
#define main unused_navigation_main
#include "smoke_test.c"
#undef main
#include <mgba/core/blip_buf.h>

static unsigned layer, music_state, intro_song, total_frames, wraps, last_wrap;
static unsigned shortest = 9999, longest, peak, pcm_frames;
static FILE* pcm;
static int recording;

static unsigned song(void)
{
    return core->busRead32(core, layer + 24);
}
static void audio(int count)
{
    short left[4096], right[4096], stereo[8192];
    while (count--)
    {
        unsigned previous_row = core->busRead8(core, layer + 1);
        unsigned previous_order = core->busRead8(core, layer + 2);
        core->runFrame(core);
        ++total_frames;
        unsigned row = core->busRead8(core, layer + 1), order = core->busRead8(core, layer + 2);
        if (recording && song() == intro_song && previous_order == 3 && order == 3 && row < previous_row)
        {
            if (wraps)
            {
                unsigned interval = total_frames - last_wrap;
                if (interval < shortest)
                    shortest = interval;
                if (interval > longest)
                    longest = interval;
            }
            last_wrap = total_frames;
            ++wraps;
        }
        int n = blip_read_samples(core->getAudioChannel(core, 0), left, 4096, 0);
        expect(n == blip_read_samples(core->getAudioChannel(core, 1), right, 4096, 0), "stereo clocks");
        for (int i = 0; i < n; ++i)
        {
            stereo[i * 2] = left[i];
            stereo[i * 2 + 1] = right[i];
            unsigned v = (unsigned)abs(left[i]);
            if (v > peak)
                peak = v;
        }
        if (recording)
        {
            expect(fwrite(stereo, sizeof(short) * 2, n, pcm) == (size_t)n, "record native PCM");
            pcm_frames += n;
        }
    }
}
static void key(unsigned value)
{
    core->setKeys(core, value);
    audio(4);
    core->setKeys(core, 0);
    audio(24);
}

int main(int argc, char** argv)
{
    expect(argc == 8, "ROM output layer music_state adv08_index intro_song full_song");
    layer = strtoul(argv[3], NULL, 16);
    music_state = strtoul(argv[4], NULL, 16);
    intro_song = strtoul(argv[6], NULL, 16);
    unsigned full_song = strtoul(argv[7], NULL, 16);
    core = mCoreFind(argv[1]);
    expect(core && core->init(core), "initialize mGBA");
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
    for (int ch = 0; ch < 2; ++ch)
        blip_set_rates(core->getAudioChannel(core, ch), 16777216, 32768);
    debug_menu_navigate();
    key(KEY_DOWN);
    key(KEY_A);
    int offset = atoi(argv[5]);
    for (int i = 0; i < offset / 6; ++i)
        key(KEY_RIGHT);
    for (int i = 0; i < offset % 6; ++i)
        key(KEY_DOWN);
    key(KEY_A);
    for (int i = 0; i < 500 && song() != intro_song; ++i)
    {
        key(KEY_A);
        audio(30);
    }
    expect(song() == intro_song, "ADV08 selects the standalone Intro");
    expect(core->busRead8(core, layer + 2) < 3, "fresh request plays the opening before the motif");
    char path[1024];
    snprintf(path, sizeof path, "%s/native-intro.s16le", argv[2]);
    pcm = fopen(path, "wb");
    expect(pcm != NULL, "open PCM recording");
    recording = 1;
    audio(1800);
    recording = 0;
    fclose(pcm);
    expect(wraps >= 15, "native Maxmod repeats the motif many times");
    expect(shortest >= 65 && longest <= 67, "53-row loop stays near1.104 seconds at the GBA frame rate");
    expect(song() == intro_song && core->busRead8(core, layer + 2) == 3 && core->busRead8(core, layer + 1) < 53,
           "repeats never enter the full song");
    screenshot(argv[2], "native-intro-loop");
    key(KEY_START);
    unsigned old_row = core->busRead8(core, layer + 1);
    audio(90);
    expect(song() == intro_song && core->busRead8(core, layer + 1) != old_row, "pause menu keeps the motif playing");
    key(KEY_B);
    // The checked-in music_player::state holds fade_total at24 and stopping
    // at37. Observe actual VM events, without injecting a synthetic command.
    for (int i = 0; i < 250 && !core->busRead8(core, music_state + 37); ++i)
        key(KEY_A);
    unsigned fade = core->busRead32(core, music_state + 24);
    expect(core->busRead8(core, music_state + 37) && fade >= 178 && fade <= 181,
           "source217 starts the three-second fade");
    audio(fade + 4);
    expect(!core->busRead8(core, layer + 8), "Intro stops after its source fade");
    for (int i = 0; i < 100 && song() != full_song; ++i)
        key(KEY_A);
    expect(song() == full_song && core->busRead8(core, layer + 8), "source238 starts the normal fused song");
    expect(core->busRead8(core, layer + 2) < 3, "normal song starts with its original opening");
    audio(480);
    expect(song() == full_song && core->busRead8(core, layer + 2) >= 3, "normal song proceeds into its body");
    screenshot(argv[2], "native-full-song");
    printf("PASS wraps=%u shortest=%u longest=%u fade=%u peak=%u pcm_frames=%u\n", wraps, shortest, longest, fade, peak,
           pcm_frames);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
