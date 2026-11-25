/* Real PCM and player progress while the production story/UI remain active. */
#define main unused_navigation_main
#include "smoke_test.c"
#undef main
#include <mgba/core/blip_buf.h>
#include <string.h>

static unsigned layer_address, static_end;
static unsigned stack_low = 0x03007f00, peak, nonzero, samples_total;
static FILE* capture;
static unsigned canary(unsigned address)
{
    return address ^ 0xa55ac33cu;
}
static void audio_frames(int count)
{
    short left[4096], right[4096];
    while (count--)
    {
        core->runFrame(core);
        for (unsigned a = static_end; a < stack_low; a += 4)
            if (core->busRead32(core, a) != canary(a))
            {
                stack_low = a;
                break;
            }
        int n = blip_read_samples(core->getAudioChannel(core, 0), left, 4096, 0);
        int r = blip_read_samples(core->getAudioChannel(core, 1), right, 4096, 0);
        expect(n == r, "stereo sample clocks remain aligned");
        for (int i = 0; i < n; ++i)
        {
            unsigned value = (unsigned)abs(left[i]);
            if (value > peak)
                peak = value;
            nonzero += left[i] != 0 || right[i] != 0;
            ++samples_total;
            if (capture)
            {
                fwrite(&left[i], 2, 1, capture);
                fwrite(&right[i], 2, 1, capture);
            }
        }
    }
}
static void key(unsigned value)
{
    core->setKeys(core, value);
    audio_frames(4);
    core->setKeys(core, 0);
    audio_frames(24);
}
static unsigned position(void)
{
    return core->busRead32(core, layer_address) & 0xffffffu;
}
static void audible(int count, const char* message)
{
    unsigned before = nonzero;
    audio_frames(count);
    expect(nonzero - before > 1000, message);
}
int main(int argc, char** argv)
{
    expect(argc == 7, "ROM output layer_address iwram_end native_trial_offset demo_hiro_offset");
    layer_address = strtoul(argv[3], NULL, 16);
    static_end = strtoul(argv[4], NULL, 16);
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
    for (unsigned a = static_end; a < 0x03007f00; a += 4)
        core->busWrite32(core, a, canary(a));
    for (int ch = 0; ch < 2; ++ch)
        blip_set_rates(core->getAudioChannel(core, ch), 16777216, 32768);
    debug_menu_navigate();
    for (int i = 0; i < 4; ++i)
        key(KEY_DOWN);
    key(KEY_A);
    for (int i = 0; i < atoi(argv[6]); ++i)
        key(KEY_DOWN);
    key(KEY_A);
    char path[512];
    snprintf(path, sizeof(path), "%s/hiro-stereo.raw", argv[2]);
    capture = fopen(path, "wb");
    expect(capture != NULL, "open PCM capture");
    audible(900, "HirO audition produces real PCM");
    fclose(capture);
    capture = NULL;
    screenshot(argv[2], "hiro-music-page");
    key(KEY_START);
    unsigned before = position();
    audible(120, "debug menu keeps music audible");
    expect(position() != before, "debug menu keeps music sequencer advancing");
    key(KEY_B);
    key(KEY_START);
    key(KEY_DOWN);
    key(KEY_DOWN);
    key(KEY_A);
    screenshot(argv[2], "hiro-stress-entry");
    key(KEY_A);
    for (int i = 0; i < 3; ++i)
        key(KEY_DOWN);
    key(KEY_A);
    audio_frames(240);
    audible(120, "ADV04 plays HirO while drawing the real scene");
    screenshot(argv[2], "hiro-adv04");
    key(KEY_START);
    before = position();
    audible(120, "internal pause keeps music audible");
    expect(position() != before, "internal pause keeps the sequencer advancing");
    screenshot(argv[2], "hiro-pause");
    key(KEY_B);
    key(KEY_L);
    audible(120, "history keeps music audible");
    screenshot(argv[2], "hiro-history");
    key(KEY_B);
    key(KEY_SELECT);
    audible(180, "witch book keeps music audible");
    screenshot(argv[2], "hiro-book");
    key(KEY_SELECT);
    // Leave the separate stress entry and enter an ordinary Trial selector.
    debug_hotkey();
    key(KEY_START);
    audio_frames(60);
    key(KEY_DOWN);
    key(KEY_A);
    int offset = atoi(argv[5]);
    for (int i = 0; i < offset / 6; ++i)
        key(KEY_RIGHT);
    for (int i = 0; i < offset % 6; ++i)
        key(KEY_DOWN);
    key(KEY_A);
    for (int i = 0; i < 80 && !core->busRead8(core, layer_address + 8); ++i)
    {
        audio_frames(60);
        key(KEY_A);
    }
    expect(core->busRead8(core, layer_address + 8), "native Trial28 source PlayBgm starts the packed HirO cue");
    audible(180, "native source cue is audible without the stress override");
    screenshot(argv[2], "hiro-native-trial28");
    printf("PASS: real stereo PCM, ADV/history/book, music continues through both menus, native Trial28 cue; peak=%u "
           "samples=%u nonzero=%u stack_used=%u stack_gap=%u\n",
           peak, samples_total, nonzero, 0x03007f00 - stack_low, stack_low - static_end);
    expect(stack_low > static_end + 1024, "tested music/UI paths retain more than 1KiB untouched stack margin");
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
