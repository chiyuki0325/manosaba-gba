/* Host-only state edits exercise shared blocks returning to color and masking. */
#define main unused_navigation_main
#include "smoke_test.c"
#undef main
#include "obj_pixels.h"
static unsigned profile;
static unsigned metric(int i)
{
    return core->busRead32(core, profile + i * 4);
}
static void tap(unsigned k)
{
    core->setKeys(core, k);
    frames(2);
    core->setKeys(core, 0);
    frames(24);
}
static int obj_bytes(void)
{
    static const int widths[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
    static const int heights[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};
    unsigned char seen[1024] = {0};
    for (int i = 0; i < 128; ++i)
    {
        unsigned a = core->busRead16(core, 0x07000000 + i * 8), b = core->busRead16(core, 0x07000002 + i * 8),
                 c = core->busRead16(core, 0x07000004 + i * 8);
        if ((!(a & 256) && (a & 512)) || (a >> 14) == 3)
            continue;
        int count = widths[a >> 14][b >> 14] * heights[a >> 14][b >> 14] / 64;
        for (int t = 0; t < count; ++t)
            seen[(c + t) & 1023] = 1;
    }
    int count = 0;
    for (int t = 0; t < 1024; ++t)
        count += seen[t];
    return count * 32;
}
int main(int argc, char** argv)
{
    expect(argc == 7, "ROM output profile banks rectangle_id warden_id");
    profile = strtoul(argv[3], NULL, 16);
    unsigned banks = strtoul(argv[4], NULL, 16);
    int rectangle = atoi(argv[5]);
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
    debug_boot();
    tap(KEY_DOWN);
    tap(KEY_A);
    tap(KEY_DOWN);
    tap(KEY_A);
    unsigned cooldown = 140;
    for (unsigned f = 0; f < 30000 && !(metric(4) == 1 && metric(5) >= 75); ++f)
    {
        if (!cooldown)
        {
            core->setKeys(core, KEY_A);
            cooldown = 166;
        }
        else if (cooldown == 164)
            core->setKeys(core, 0);
        --cooldown;
        frames(1);
    }
    core->setKeys(core, 0);
    frames(240);
    expect(metric(4) == 1 && metric(5) == 75, "reach stationary Warden dialogue");
    unsigned commands = core->busRead32(core, banks + 24 + 4), keys = core->busRead32(core, banks + 24 + 8), player = 0;
    for (unsigned a = 0x02000000; a < 0x02040000 - 960; a += 4)
        if (core->busRead32(core, a) == keys && core->busRead32(core, a + 4) == commands)
        {
            expect(!player, "unique active stage player");
            player = a;
        }
    expect(player != 0, "locate the actual production stage state");
    unsigned actor = 0;
    for (int i = 0; i < 3; ++i)
        if (core->busRead16(core, player + 8 + i * 60 + 48) == (unsigned)atoi(argv[6]))
            actor = player + 8 + i * 60;
    expect(actor != 0, "find Warden entity");
    // Expose the three equal-alpha body blocks together, without a second actor.
    core->busWrite32(core, actor, 120 * 256);
    core->busWrite32(core, actor + 4, (uint32_t)(-80 * 256));
    core->busWrite32(core, actor + 8, 256);
    frames(240);
    color_t before[240 * 104];
    memcpy(before, pixels, sizeof(before));
    unsigned char ink[240 * 104];
    for (int y = 0; y < 104; ++y)
        for (int x = 0; x < 240; ++x)
            ink[y * 240 + x] = object_ink(x, y) != 0;
    int colored = obj_bytes();
    screenshot(argv[2], "color-before");
    core->busWrite32(core, actor + 20, 0);
    frames(240);
    int shared = obj_bytes();
    fprintf(stderr, "OBJ bytes colored=%d shared=%d\n", colored, shared);
    expect(shared < colored, "black silhouette reuses tile blocks");
    for (int y = 0; y < 104; ++y)
        for (int x = 0; x < 240; ++x)
            expect(ink[y * 240 + x] == (object_ink(x, y) != 0), "sharing preserves alpha geometry");
    screenshot(argv[2], "black-shared");
    unsigned mask = player + 8 + 13 * 60;
    core->busWrite16(core, mask + 48, rectangle);
    core->busWrite32(core, mask + 16, 256);
    // Put the rectangle in the current actor's mask group.
    core->busWrite16(core, mask + 52, (int16_t)core->busRead16(core, actor + 52) - 1);
    frames(240);
    int masked = obj_bytes();
    expect(masked > shared, "masking gets independent writable blocks");
    screenshot(argv[2], "black-masked");
    core->busWrite16(core, mask + 48, 0xffff);
    frames(240);
    expect(obj_bytes() == shared, "removing the mask restores sharing");
    core->busWrite32(core, actor + 20, 256);
    frames(240);
    expect(obj_bytes() == colored, "restoring color restores independent source indices");
    expect(memcmp(before, pixels, sizeof(before)) == 0, "return to original color pixels");
    screenshot(argv[2], "color-restored");
    printf("PASS: color/black/mask/black/color; OBJ bytes color=%d shared=%d masked=%d\n", colored, shared, masked);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    return 0;
}
