#include "stage_player.h"
#include <cstdio>
#include <cstdlib>

namespace manosaba
{
const stage_key stage_keys[]={
    {282,10,stage_ease::linear,0},
    {282,10,stage_ease::linear,1},
    {0,0,stage_ease::linear,0},
    {256,10,stage_ease::linear,0},
    {0,21,stage_ease::linear,0}
};
// Scale followed by rotation must keep the same result as the reverse order.
const stage_command stage_commands[]={
    {282,0,0,stage_kind::set,0,uint8_t(stage_property::scale),0},
    {8*256,0,0,stage_kind::set,0,uint8_t(stage_property::angle),0},
    {0,0,0,stage_kind::set,0,uint8_t(stage_property::angle),0},
    {282,0,0,stage_kind::set,0,uint8_t(stage_property::scale),0},
    {282,0,0,stage_kind::set,0,uint8_t(stage_property::scale),0},
    {8*256,0,0,stage_kind::set,stage_camera,uint8_t(stage_property::angle),0},
    {0,0,1,stage_kind::clear,0,0,0},
    {282,0,0,stage_kind::set,0,uint8_t(stage_property::scale),0},
    {0,4,1,stage_kind::tween,0,uint8_t(stage_property::angle),0}
};
}
using namespace manosaba;
static void expect(bool ok,const char* message)
{
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
static void set(stage_player& p,int target,stage_property prop,int value)
{
    p.apply(stage_command{value,0,0,stage_kind::set,uint8_t(target),uint8_t(prop),0});
}
int main()
{
    stage_player p;p.reset();
    const int targets[]={stage_actor_count-1,stage_actor_count,stage_camera};
    for(int target:targets)
    {
        set(p,target,stage_property::scale,282);
        expect(p.entity(target).get(stage_property::scale)==(target<stage_actor_count ? 256 : 282),"only actor scale snaps");
    }
    p.reset();set(p,0,stage_property::scale,294);
#ifdef MANOSABA_ACTOR_SCALE_SNAP_115
    expect(p.entity(0).get(stage_property::scale)==256,"1.15x snaps");
#else
    expect(p.entity(0).get(stage_property::scale)==294,"1.15x control is preserved");
#endif
    set(p,0,stage_property::scale,320);
    expect(p.entity(0).get(stage_property::scale)==320,"1.25x is preserved");
    p.apply(stage_command{0,0,1,stage_kind::tween,0,uint8_t(stage_property::scale),0});
    p.tick(5);expect(p.entity(0).get(stage_property::scale)==288,"tween targets snap before interpolation");
    p.tick(5);expect(p.entity(0).get(stage_property::scale)==256,"no endpoint discontinuity");
    p.apply(stage_command{0,1,1,stage_kind::tween,0,uint8_t(stage_property::scale),0});
    p.tick(10);expect(p.entity(0).get(stage_property::scale)==538,"relative scale delta is preserved");
    p.reset();p.apply(stage_event{0,2,0});
    expect(p.entity(0).get(stage_property::scale)==282,"later rotation preserves authored scale");
    p.apply(stage_event{2,2,0});
    expect(p.entity(0).get(stage_property::scale)==256,"immediate rotation reset permits snap");
    p.reset();p.apply(stage_event{4,2,0});
    expect(p.entity(0).get(stage_property::scale)==282,"camera rotation preserves authored scale");
    p.reset();set(p,0,stage_property::angle,8*256);
    p.apply(stage_command{0,2,2,stage_kind::tween,0,uint8_t(stage_property::angle),0});
    set(p,0,stage_property::scale,282);
    expect(p.entity(0).get(stage_property::scale)==282,"pending rotation preserves scale");
    p.apply(stage_event{6,3,0});
    expect(p.entity(0).get(stage_property::scale)==256,"reused slot discards the previous actor's rotation before scale selection");
    std::printf("PASS: actor-only scale snap, interpolation, relative keys and rotation policy; stage_player=%zu bytes\n",sizeof(p));
}
