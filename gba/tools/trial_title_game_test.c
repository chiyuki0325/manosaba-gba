/* Native title animation, pause/atlas restoration and music frame budget. */
#define main unused_navigation_main
#include "smoke_test.c"
#undef main
#include "../src/trial_runtime.h"
#include "../src/witch_book_runtime.h"
#include <stddef.h>

static unsigned address, book_address, layer;
static unsigned max_ticks, missed_start, observed;
static const char* output;
static trial_runtime_profile metric(void)
{
    trial_runtime_profile p;
    uint32_t* v = (uint32_t*)&p;
    for (unsigned i = 0; i < sizeof(p) / 4; ++i)
        v[i] = core->busRead32(core, address + i * 4);
    return p;
}
static void run(int count)
{
    while (count--)
    {
        frames(1);
        trial_runtime_profile p = metric();
        if (p.core_ticks > max_ticks)
            max_ticks = p.core_ticks;
        ++observed;
    }
}
static void key(unsigned keys)
{
    core->setKeys(core, keys);
    run(2);
    core->setKeys(core, 0);
    run(12);
}
static void wait_title(unsigned kind, unsigned frame)
{
    for (int n = 0; n < 2400; ++n)
    {
        run(1);
        trial_runtime_profile p = metric();
        if (p.state == 12 && p.title_kind == kind && p.title_stage == 3 && p.title_frame >= frame)
            return;
    }
    expect(0, "title reaches requested visible frame");
}
static void capture(const char* prefix)
{
    char name[80];
    snprintf(name, sizeof(name), "%s-%03u", prefix, metric().title_frame);
    screenshot(output, name);
}
static void book_ready(void)
{
    for (int i = 0; i < 1200; ++i)
    {
        run(1);
        if (core->busRead32(core, book_address + offsetof(witch_book_profile, ready)))
            return;
    }
    expect(0, "book opens during title");
}
int main(int argc, char** argv)
{
    expect(argc == 7, "ROM output trial_profile book_profile music_layer stress_music");
    output = argv[2];
    address = strtoul(argv[3], NULL, 16);
    book_address = strtoul(argv[4], NULL, 16);
    layer = strtoul(argv[5], NULL, 16);
    int stress_music = atoi(argv[6]);
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
    core->reset(core);
    debug_menu_navigate();
    key(KEY_DOWN);
    key(KEY_DOWN);
    key(KEY_A);
    key(KEY_DOWN);
    key(KEY_A);
    wait_title(0, 5);
    missed_start = metric().missed;
    max_ticks = 0;
    observed = 0;
    unsigned total = metric().total_left;
    expect(core->busRead8(core, layer + 8), "BGM already plays during the begin animation");
    unsigned begin_music = core->busRead32(core, layer);
    core->setKeys(core, KEY_A);
    run(30);
    core->setKeys(core, 0);
    expect(metric().state == 12 && metric().title_frame >= 35, "A does not skip the title animation");
    expect(metric().total_left == total, "begin animation freezes discussion time");
    expect(core->busRead32(core, layer) != begin_music, "BGM advances during begin title frames");
    while (metric().title_frame < 60)
    {
        if (metric().title_frame % 10 == 0)
            capture("begin");
        run(1);
    }
    capture("begin");
    key(KEY_START);
    unsigned frozen = metric().title_frame;
    unsigned music = core->busRead32(core, layer);
    run(90);
    expect(metric().title_frame == frozen, "pause freezes title animation");
    expect(core->busRead32(core, layer) != music, "BGM keeps advancing while title animation is paused");
    key(KEY_START);
    wait_title(0, frozen);
    expect(metric().title_frame <= frozen + 2, "resume restores current frame without replaying intro");
    capture("begin-resume");
    key(KEY_SELECT);
    book_ready();
    frozen = metric().title_frame;
    music = core->busRead32(core, layer);
    run(60);
    expect(metric().title_frame == frozen, "atlas freezes title animation");
    expect(core->busRead32(core, layer) != music, "BGM keeps advancing in the atlas during title animation");
    key(KEY_SELECT);
    wait_title(0, frozen);
    expect(metric().title_frame <= frozen + 2, "atlas close restores current title frame");
    unsigned frames_before = metric().frame, title_before = metric().title_frame;
    while (metric().state == 12)
    {
        if (metric().title_frame % 10 == 0)
            capture("begin");
        run(1);
    }
    expect(metric().state == 6, "begin automatically continues to the first debate utterance");
    expect(metric().frame - frames_before <= 123 - title_before, "remaining title frames run at 60Hz");
    expect(metric().total_left + 2 >= total, "title transitions do not consume discussion time");
    run(2);
    expect(core->busRead8(core, layer + 8), "discussion BGM plays after the begin animation");
    int cooldown = 0;
    for (int i = 0; i < 15000 && !(metric().state == 12 && metric().title_kind == 1); ++i)
    {
        trial_runtime_profile p = metric();
        if (!cooldown && p.interaction_ready && p.state == 6)
        {
            core->setKeys(core, KEY_A);
            cooldown = 24;
        }
        else
            core->setKeys(core, 0);
        if (cooldown)
            --cooldown;
        run(1);
    }
    core->setKeys(core, 0);
    wait_title(1, 3);
    total = metric().total_left;
    unsigned end_start = metric().frame, end_frame = metric().title_frame;
    while (metric().state == 12)
    {
        expect(metric().total_left == total, "end animation freezes discussion time");
        if (metric().title_frame % 8 == 0)
            capture("end");
        run(1);
    }
    expect(metric().frame - end_start <= 177 - end_frame, "end animation runs at 60Hz and advances automatically");
    for (int i = 0; i < 240 && metric().state != 1; ++i)
        run(1);
    expect(metric().state == 1, "end continues with the original following dialogue");
    run(120);
    capture("after");
    unsigned missed = metric().missed - missed_start;
    printf("Titles: frames=%u peak_ticks=%u missed=%u\n", observed, max_ticks, missed);
    expect(max_ticks < 4389 && !missed, "title/pause/atlas workflow remains within the real-music frame budget");
    printf("PASS: native begin/end, automatic continuation, A guard, frozen timers, pause/book restoration and "
           "continuing music (stress=%d)\n",
           stress_music);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
