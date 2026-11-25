/* Full catalog audition and native ADV cue on the production player. */
#define main unused_navigation_main
#include "smoke_test.c"
#undef main
#include <mgba/core/blip_buf.h>

static unsigned layer, static_end, stack_low = 0x03007f00, peak, nonzero, total;
static unsigned canary(unsigned a)
{
    return a ^ 0xa55ac33cu;
}
static void audio(int count)
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
        blip_read_samples(core->getAudioChannel(core, 1), right, 4096, 0);
        for (int i = 0; i < n; ++i)
        {
            unsigned v = (unsigned)abs(left[i]);
            if (v > peak)
                peak = v;
            nonzero += left[i] != 0 || right[i] != 0;
            ++total;
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
static unsigned song(void)
{
    return core->busRead32(core, layer + 24);
}
static void audible(int count, const char* message)
{
    unsigned before = nonzero;
    audio(count);
    expect(nonzero - before > 1000, message);
}
int main(int argc, char** argv)
{
    expect(argc == 10 || argc == 14, "ROM output layer iwram_end plan default_index adv04_song adv08_index intro_song "
                                     "[trial_offset paired_song body_order graphics]");
    layer = strtoul(argv[3], NULL, 16);
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
        key(KEY_UP);
    FILE* plan = fopen(argv[5], "r");
    expect(plan != NULL, "open catalog plan");
    unsigned expected, index = 0, played = 0;
    char name[64];
    while (fscanf(plan, "%63s %x", name, &expected) == 2)
    {
        key(KEY_SELECT);
        key(KEY_A);
        if (expected)
        {
            expect(song() == expected, "DEMO selects the expected bank module");
            if (!strcmp(name, "Bgm003"))
                expect(core->busRead8(core, layer + 2) < 3, "003 enters its supplied Intro");
            if (!strcmp(name, "Bgm004"))
                expect(core->busRead8(core, layer + 2) >= 3, "004 enters the shared body directly");
            if (argc == 14 && !strcmp(name, "Bgm048"))
                expect(core->busRead8(core, layer + 2) >= atoi(argv[12]),
                       "ordinary 048 audition skips the optional 047 prefix");
            audible(420, "each produced catalog entry emits PCM");
            ++played;
            if (!strcmp(name, "Bgm003"))
            {
                audible(180, "003 Intro flows directly into the 004 body");
                expect(song() == expected && core->busRead8(core, layer + 2) >= 3,
                       "003 continues in the shared body without another module load");
            }
            if (!strcmp(name, "Bgm021") || !strcmp(name, "Bgm022"))
            {
                audible(600, "approved short texture remains audible over two PCM loops");
                expect(core->busRead8(core, layer + 8), "texture music remains active after two PCM loops");
            }
        }
        else
            expect(!core->busRead8(core, layer + 8), "unmade item does not start another song");
        if (index % 4 == 0)
            screenshot(argv[2], name);
        printf("catalog %u %s module=%08x pcm=%u\n", index, name, song(), nonzero);
        fflush(stdout);
        key(KEY_DOWN);
        ++index;
    }
    fclose(plan);
    // Enter ADV04 without enabling the HirO stress override.
    key(KEY_START);
    for (int i = 0; i < 3; ++i)
        key(KEY_UP);
    key(KEY_A);
    for (int i = 0; i < 3; ++i)
        key(KEY_DOWN);
    key(KEY_A);
    audio(300);
    expect(song() == strtoul(argv[7], NULL, 16), "ADV04 source cue selects Bgm026 without override");
    audible(120, "native ADV04 cue emits PCM");
    screenshot(argv[2], "native-adv04");
    key(KEY_START);
    unsigned before = core->busRead32(core, layer);
    audible(120, "pause menu keeps native BGM audible");
    expect(core->busRead32(core, layer) != before, "pause advances music");
    screenshot(argv[2], "native-pause");
    key(KEY_B);
    key(KEY_L);
    audible(120, "history keeps native BGM audible");
    key(KEY_B);
    key(KEY_SELECT);
    audible(180, "book keeps native BGM audible");
    key(KEY_SELECT);
    // This chapter requests the standalone eXi Intro rather than Intro+body.
    core->reset(core);
    for (unsigned a = static_end; a < 0x03007f00; a += 4)
        core->busWrite32(core, a, canary(a));
    debug_menu_navigate();
    key(KEY_DOWN);
    key(KEY_A);
    int offset = atoi(argv[8]);
    for (int i = 0; i < offset / 6; ++i)
        key(KEY_RIGHT);
    for (int i = 0; i < offset % 6; ++i)
        key(KEY_DOWN);
    key(KEY_A);
    unsigned intro_song = strtoul(argv[9], NULL, 16);
    for (int i = 0; i < 400 && song() != intro_song; ++i)
    {
        key(KEY_A);
        audio(30);
    }
    expect(song() == intro_song, "native ADV08 uses the extracted standalone Intro module");
    audible(900, "standalone Intro remains audible across repeated wraps");
    expect(song() == intro_song && core->busRead8(core, layer + 2) == 3 && core->busRead8(core, layer + 1) < 53,
           "standalone Intro repeats its 53-row piano motif without entering the full module");
    screenshot(argv[2], "native-adv08-intro");
    if (argc == 14)
    {
        // Actual final-chapter source command has a cross-number IntroBgmPath.
        core->reset(core);
        for (unsigned a = static_end; a < 0x03007f00; a += 4)
            core->busWrite32(core, a, canary(a));
        debug_menu_navigate();
        key(KEY_DOWN);
        key(KEY_DOWN);
        key(KEY_A);
        unsigned trial_offset = (unsigned)atoi(argv[10]);
        for (unsigned i = 0; i < trial_offset / 6; ++i)
            key(KEY_RIGHT);
        for (unsigned i = 0; i < trial_offset % 6; ++i)
            key(KEY_DOWN);
        key(KEY_A);
        unsigned paired_song = (unsigned)strtoul(argv[11], NULL, 16), body = (unsigned)atoi(argv[12]),
                 graphics = (unsigned)strtoul(argv[13], NULL, 16);
        for (int i = 0; i < 1500 && song() != paired_song; ++i)
            audio(1);
        expect(song() == paired_song && core->busRead8(core, layer + 2) < body,
               "Trial02 source command starts 047 prefix in the fused module");
        unsigned prefix_pcm = nonzero;
        for (int i = 0; i < 240 && core->busRead8(core, layer + 2) < body; ++i)
            audio(1);
        expect(song() == paired_song && core->busRead8(core, layer + 2) >= body,
               "047 flows into approved 048 with no second module load");
        expect(nonzero - prefix_pcm > 1000, "source prefix is audible");
        unsigned joint_peak = 0;
        for (int i = 0; i < 900; ++i)
        {
            audio(1);
            unsigned ticks = core->busRead32(core, graphics + 6 * 4);
            if (ticks > joint_peak)
                joint_peak = ticks;
        }
        expect(song() == paired_song && core->busRead8(core, layer + 8),
               "048 continues under real final-chapter debate rendering");
        screenshot(argv[2], "native-final-trial-047-048");
        printf("paired_intro body_order=%u trial_peak=%u ticks\n", body, joint_peak);
    }
    expect(stack_low > static_end + 1024, "PCM/UI retain stack margin");
    printf("PASS: catalog=%u played=%u native ADV04, pause/history/book; peak=%u samples=%u nonzero=%u stack_used=%u "
           "stack_gap=%u\n",
           index, played, peak, total, nonzero, 0x03007f00 - stack_low, stack_low - static_end);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
