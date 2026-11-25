#include "stage_player.h"
#include <cstdio>
#include <cstdlib>
namespace manosaba
{
const stage_key stage_keys[] = {{768, 10, stage_ease::linear, 0}};
const stage_command stage_commands[] = {{294, 0, 0, stage_kind::set, 0, uint8_t(stage_property::scale), 0},
                                        {10 * 256, 0, 0, stage_kind::set, 0, uint8_t(stage_property::angle), 0}};
} // namespace manosaba
using namespace manosaba;
static void expect(bool ok, const char* text)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", text);
        std::exit(1);
    }
}
static void set(stage_player& p, int id, stage_property prop, int value)
{
    p.apply(stage_command{value, 0, 0, stage_kind::set, uint8_t(id), uint8_t(prop), 0});
}
int main()
{
    stage_player p;
    p.reset();
    p.apply(stage_event{0, 2, 0});
    expect(p.entity(0).get(stage_property::scale) == 294 && p.entity(0).get(stage_property::angle) == 10 * 256,
           "small rotation and authored scale stay intact");
    set(p, 0, stage_property::angle, 0);
    set(p, 0, stage_property::scale, 256);
    p.apply(stage_command{0, 0, 1, stage_kind::tween, 0, uint8_t(stage_property::scale), 0});
    p.tick(5);
    expect(p.entity(0).get(stage_property::scale) == 384, "close-up target capped before interpolation");
    p.tick(5);
    expect(p.entity(0).get(stage_property::scale) == 512, "upright actor close-up reaches 2x");
    set(p, stage_camera, stage_property::scale, 768);
    expect(p.entity(stage_camera).get(stage_property::scale) == 768, "camera scale keeps original value");
    set(p, 3, stage_property::scale, 768);
    expect(p.entity(3).get(stage_property::scale) == 768, "background scale keeps original value");
    std::puts("PASS: rotated scale preserved, close-up interpolation and independent backgrounds");
}
