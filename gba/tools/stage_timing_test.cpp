#include "stage_player.h"
#include <cstdio>
#include <cstdlib>
namespace manosaba
{
const stage_key stage_keys[]={
    {256,10,stage_ease::linear,0},{512,10,stage_ease::linear,0},
    {-512,5,stage_ease::linear,1},{512,5,stage_ease::linear,1},
    {256,0,stage_ease::linear,0},{768,5,stage_ease::out_quad,0},
    {1024,20,stage_ease::linear,0}
};
const stage_command stage_commands[]={{0,0,2,stage_kind::tween,0,0,0}};
}
using namespace manosaba;
static void expect(bool ok,const char* message)
{
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1); }
}
int main()
{
    stage_player p; p.reset();
    p.apply(stage_command{0,0,0,stage_kind::set,0,0,0});
    p.apply(stage_command{0,0,2,stage_kind::tween,0,0,0});
    p.tick(5); expect(p.entity(0).get(stage_property::x)==128,"first keyframe interpolates");
    p.tick(5); expect(p.entity(0).get(stage_property::x)==256,"first keyframe boundary");
    p.tick(5); expect(p.entity(0).get(stage_property::x)==384,"second keyframe inherits previous endpoint");
    p.tick(5); expect(p.entity(0).get(stage_property::x)==512 && !p.animating(),"sequence completes");
    p.apply(stage_command{0,2,2,stage_kind::tween,0,0,1});
    p.tick(5); expect(p.entity(0).get(stage_property::x)==0,"relative first key");
    p.tick(5); expect(p.entity(0).get(stage_property::x)==512 && p.animating(),"finite loop repeats");
    p.tick(10); expect(p.entity(0).get(stage_property::x)==512 && !p.animating(),"loop terminates after repeated sequence");
    p.apply(stage_command{0,4,2,stage_kind::tween,0,0,128});
    expect(p.entity(0).get(stage_property::x)==256 && p.blocked(),"instant key precedes a blocking tween");
    p.apply(stage_command{0,6,1,stage_kind::tween,1,1,0});
    p.tick(5); expect(!p.blocked() && p.animating(),"wait only blocks its own task, not unrelated parallel animation");
    expect(p.entity(0).get(stage_property::x)==768,"eased track endpoint");
    p.apply(stage_command{0,0,0,stage_kind::wait,0,0,2});
    expect(p.blocked(),"await all tracks");
    p.tick(15); expect(!p.blocked(),"await all resolves after the final parallel task");
    p.apply(stage_command{0,2,2,stage_kind::tween,0,0,127});
    p.tick(110); expect(p.animating(),"infinite loops continue across many cycles");
    p.apply(stage_command{0,0,1,stage_kind::clear,0,0,0});
    expect(!p.animating(),"recycling an actor cancels its old tracks");
    std::puts("PASS: multiple keys, relative movement, finite/infinite loops, parallel tasks and scoped waits");
}
