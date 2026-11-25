#include "stage_player.h"
#include "stage_raster.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace manosaba;
static void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
static const stage_event& source(int line)
{
    for (int i = 0; i < stage_event_count; ++i)
        if (stage_events[i].source_line == line)
            return stage_events[i];
    std::fprintf(stderr, "Missing source line %d\n", line);
    std::exit(1);
}
static void set(stage_player& player, int target, stage_property property, int value)
{
    player.apply(stage_command{value, 0, 0, stage_kind::set, uint8_t(target), uint8_t(property), 0});
}
static unsigned hash(const std::vector<uint8_t>& pixels)
{
    unsigned result = 2166136261u;
    for (auto pixel : pixels)
        result = (result ^ pixel) * 16777619u;
    return result;
}
int main()
{
    stage_player player;
    player.reset();
    player.apply(source(17));
    expect(player.blocked(), "real timed wait blocks before dialogue");
    player.tick(53);
    expect(player.blocked(), "0.9-second wait is not rounded down");
    player.tick();
    expect(!player.blocked(), "wait expires at 54 GBA frames");
    player.apply(source(747));
    player.tick(2);
    expect(player.blocked(), "input wait has a real timer");
    player.tick(1, true);
    expect(!player.blocked(), "fresh input interrupts i2.4 wait");
    player.apply(source(63));
    expect(player.animating(), "camera command starts a task");
    player.tick(27);
    int camera = player.entity(stage_camera).get(stage_property::x);
    expect(camera > 0 && camera < 20 * 256, "camera has an intermediate position");
    player.tick(27);
    expect(player.entity(stage_camera).get(stage_property::x) == 4915, "camera reaches the requested offset");
    expect(player.entity(stage_camera).get(stage_property::scale) == 640, "camera reaches 2.5x zoom");
    player.apply(source(69));
    player.tick(54);
    expect(player.entity(stage_camera).get(stage_property::x) == 0, "camera returns to its origin");
    player.reset();
    player.apply(source(192));
    player.tick(5);
    bool jumped = false;
    for (int i = 0; i < 3; ++i)
        jumped |= player.entity(i).get(stage_property::y) != 80 * 256;
    expect(jumped, "double jump changes actor position before its endpoint");
    player.tick(50);
    for (int i = 0; i < 3; ++i)
        expect(player.entity(i).get(stage_property::y) == 80 * 256, "emote removes additive offset on completion");
    expect(stage_interpolate(0, 256, 1, 2, stage_ease::out_quad) == 192, "quadratic easing");
    expect(stage_interpolate(0, 256, 1, 2, stage_ease::smooth) == 128, "smooth-step easing");
    // Render actual resources, compare intermediate images and check all indices.
    std::vector<uint8_t> pixels(240 * 160), unchanged;
    player.reset();
    player.apply(source(15));
    player.tick(60);
    stage_render(player, pixels.data());
    unsigned hallway = hash(pixels);
    unchanged = pixels;
    std::vector<uint8_t> sliced(240 * 160);
    for (int y = 0; y < 160; y += 40)
        stage_render(player, sliced.data(), y, 40);
    expect(sliced == pixels, "sliced rendering matches the full frozen frame");
    player.apply(source(16));
    stage_render(player, pixels.data());
    unsigned black = hash(pixels);
    expect(black != hallway, "opaque overlay hides the scene");
    player.tick(30);
    stage_render(player, pixels.data());
    expect(hash(pixels) != black && hash(pixels) != hallway, "original custom mask has a real intermediate reveal");
    player.tick(30);
    stage_render(player, pixels.data());
    expect(hash(pixels) == hallway, "masked dissolve completes without residual black");
    player.apply(source(63));
    player.tick(25);
    stage_render(player, pixels.data());
    expect(hash(pixels) != hallway, "camera changes raster sampling before endpoint");
    player.apply(source(69));
    player.tick(54);
    player.apply(source(82));
    player.tick(60);
    stage_render(player, pixels.data());
    unsigned sharp = hash(pixels);
    player.apply(source(83));
    stage_render(player, pixels.data());
    expect(hash(pixels) != sharp, "blur samples the prefiltered texture");
    for (auto pixel : pixels)
        expect(pixel < 240, "scene effects cannot consume UI palette entries");
    // Verify the exact pixels revealed by layer order and a transparent mask.
    player.reset();
    player.apply(source(15));
    player.tick(60);
    stage_render(player, pixels.data());
    auto baseline = pixels;
    int background = -1;
    for (int i = 3; i < 7; ++i)
        if (player.entity(i).image >= 0)
            background = i;
    expect(background >= 3, "active background plane");
    int solid = stage_texture_count - 1;
    player.apply(stage_command{solid, 0, 0, stage_kind::image, 0, 0, 0});
    set(player, 0, stage_property::x, 120 * 256);
    set(player, 0, stage_property::y, 80 * 256);
    set(player, 0, stage_property::opacity, 256);
    set(player, 0, stage_property::scale, 256);
    player.apply(stage_command{-10, 0, 0, stage_kind::order, 0, 0, 0});
    stage_render(player, pixels.data());
    expect(pixels == baseline, "opaque scenery occludes a rear character");
    player.apply(stage_command{10, 0, 0, stage_kind::order, 0, 0, 0});
    stage_render(player, pixels.data());
    expect(std::all_of(pixels.begin(), pixels.end(), [](uint8_t p) { return p == 1; }),
           "foreground occludes the entire viewport");
    unsigned foreground = hash(pixels);
    set(player, 0, stage_property::angle, 25 * 256);
    stage_render(player, pixels.data());
    expect(hash(pixels) != foreground, "rotation changes coverage and sampling");
    set(player, 0, stage_property::angle, 0);
    player.apply(stage_command{0, 0, 0, stage_kind::maskable, uint8_t(background), 0, 0});
    player.apply(stage_command{solid, 0, 0, stage_kind::image, stage_mask_first, 0, 0});
    player.apply(stage_command{-1, 0, 0, stage_kind::order, stage_mask_first, 0, 0});
    set(player, stage_mask_first, stage_property::scale, 128);
    set(player, stage_mask_first, stage_property::opacity, 256);
    set(player, stage_mask_first, stage_property::fill, 0);
    // Fill opacity zero, clip opacity one: the mask still cuts the character.
    stage_render(player, pixels.data());
    expect(pixels[80 * 240 + 120] == 1, "inside a transparent rectangle keeps the foreground");
    expect(pixels[20 * 240 + 20] == baseline[20 * 240 + 20], "outside a transparent rectangle reveals the background");
    player.apply(stage_command{0, 0, 0, stage_kind::maskable, 0, 0, 0});
    stage_render(player, pixels.data());
    expect(hash(pixels) == foreground, "maskability can be disabled independently");
    std::printf(
        "PASS: %d source events, camera, waits, input interrupt, jump, blur, masks, rotation and palette isolation\n",
        stage_event_count);
}
