#include "stage_player.h"
#include "save_archive.h"
namespace manosaba
{
namespace {int presentation_value(int value,int target,int property,bool relative,bool snap_scale);}
void stage_player::archive(save_archive& a)
{
    for(int id=0;id<stage_entity_count;++id)
    {
        auto& e=_entities[id];
        for(int property=0;property<int(stage_property::count);++property)
        {
            int value=e.values[property];
            if(!a.reading())
            {
                const auto& track=_tracks[id][property];
                if(track.active)
                {
                    value=track.loops ? track.initial : track.from;
                    for(int key=track.loops ? 0 : track.key;key<track.count;++key)
                    {
                        const auto& k=_keys[track.first+key];
                        int target=presentation_value(k.value,id,property,k.relative,track.snap_scale);
                        value=k.relative ? value+target : target;
                    }
                }
                else if(property<2)value-=property ? _emotes[id].y : _emotes[id].x;
            }
            a.field(value);if(a.reading())e.values[property]=value;
        }
        a.field(e.image);a.field(e.previous);a.field(e.order);a.field(e.mask);
        uint8_t presentation=uint8_t(e.maskable)|(uint8_t(e.regular_cg)<<1);
        a.field(presentation);
        if(a.reading()){e.maskable=presentation&1;e.regular_cg=presentation&2;}
    }
    a.field(_printer);
    if(a.reading())
    {
        for(auto& row:_tracks)for(auto& track:row)track={};
        for(auto& emote:_emotes)emote={};
        _clock=0;_wait=0;_wait_input=_wait_tracks=false;
    }
}
namespace
{
int presentation_value(int value,int target,int property,bool relative=false,bool snap_scale=true)
{
#if defined(MANOSABA_CLOSEUP_LIMIT) || defined(MANOSABA_ACTOR_SCALE_SNAP)
    if(snap_scale && target<stage_actor_count && property==int(stage_property::scale) && !relative && value>512)
        return 512;
#endif
#ifdef MANOSABA_ACTOR_SCALE_SNAP
    bool common=value==282;
#ifdef MANOSABA_ACTOR_SCALE_SNAP_115
    common|=value==294;
#endif
    if(snap_scale && target<stage_actor_count && property==int(stage_property::scale) && !relative && common)
        return 256;
#else
    (void)target;(void)property;(void)relative;(void)snap_scale;
#endif
    return value;
}
}
int stage_interpolate(int from, int to, int elapsed, int duration, stage_ease ease)
{
    if(! duration || elapsed >= duration) return to;
    int t = elapsed * 4096 / duration;
    int t2 = t*t/4096;
    switch(ease)
    {
    case stage_ease::smooth: t = t2*(12288-2*t)/4096; break;
    case stage_ease::in_quad: t = t2; break;
    case stage_ease::out_quad: t = 8192*t/4096-t2; break;
    case stage_ease::in_out_quad: t = t < 2048 ? 2*t2 : 4096-2*(4096-t)*(4096-t)/4096; break;
    case stage_ease::in_cubic: t = t2*t/4096; break;
    case stage_ease::out_cubic: t = 4096-(4096-t)*(4096-t)/4096*(4096-t)/4096; break;
    case stage_ease::sine:
    {
        static constexpr int16_t curve[] = {0,2,10,22,39,61,88,120,156,197,242,291,345,403,465,531,600,673,749,828,910,995,1083,1172,1264,1358,1453,1550,1648,1747,1847,1948,2048,2148,2249,2349,2448,2546,2643,2738,2832,2924,3013,3101,3186,3268,3347,3423,3496,3565,3631,3693,3751,3805,3854,3899,3940,3976,4008,4035,4057,4074,4086,4094,4096};
        int index = t/64, fraction = t%64;
        t = curve[index]+(curve[index+1]-curve[index])*fraction/64;
        break;
    }
    case stage_ease::out_back:
    { int u = t-4096; t = 4096 + u*u/4096*(int(2.70158*4096)*u/4096+int(1.70158*4096))/4096; break; }
    default: break;
    }
    return from + int(int64_t(to-from)*t/4096);
}
void stage_player::reset(const stage_key* keys,const stage_command* commands)
{
    _keys=keys;_commands=commands;
    for(auto& entity:_entities)entity={};
    for(auto& row:_tracks)for(auto& track:row)track={};
    for(auto& emote:_emotes)emote={};
    _clock=0;_wait=0;_wait_input=_wait_tracks=false;_printer=true;
    auto& camera = _entities[stage_camera];
    camera.values[int(stage_property::x)] = camera.values[int(stage_property::y)] = 0;
}
bool stage_player::animating() const
{
    for(const auto& row : _tracks) for(const auto& track : row) if(track.active) return true;
    for(const auto& emote : _emotes) if(emote.duration) return true;
    return false;
}
bool stage_player::geometry_animating(int entity) const
{
    for(int prop=0;prop<4;++prop)if(_tracks[entity][prop].active)return true;
    return _emotes[entity].duration!=0;
}
void stage_player::start(const stage_command& command,uint8_t scale_policy)
{
    auto& entity = _entities[command.target];
    auto& track = _tracks[command.target][command.property];
    track = {};
    track.first = command.first; track.count = command.count;
    const auto& emote = _emotes[command.target];
    int additive = command.property == 0 ? emote.x : command.property == 1 ? emote.y : 0;
    track.from = track.initial = entity.values[command.property]-additive;
    track.loops = command.flags & 127;
    track.active = true;
    track.snap_scale=scale_policy&(1u<<command.target);
    // Immediate keys are consumed here, without adding a phantom frame/wait.
    while(track.active && ! _keys[track.first+track.key].frames)
    {
        auto key = _keys[track.first+track.key];
        key.value=presentation_value(key.value,command.target,command.property,key.relative,track.snap_scale);
        track.from = key.relative ? track.from+key.value : key.value;
        entity.values[command.property] = track.from+additive;
        if(++track.key == track.count) track.active = false;
    }
    track.blocking = command.flags & 128;
}
bool stage_player::blocked() const
{
    if(_wait != 0 || (_wait_tracks && animating())) return true;
    for(const auto& row : _tracks) for(const auto& track : row)
        if(track.active && track.blocking) return true;
    return false;
}
void stage_player::apply(const stage_event& event)
{
    uint8_t policy=scale_policy(&event);
    for(int index = 0; index < event.count; ++index) apply_command(_commands[event.first+index],policy);
}
void stage_player::apply(const stage_command& command)
{
    apply_command(command,scale_policy());
}
uint8_t stage_player::scale_policy(const stage_event* event) const
{
#ifdef MANOSABA_ACTOR_SCALE_SNAP
    // Read the whole atomic event, including rotation commands after Scale.
    // Rotation keeps its authored scale; shrinking it can reveal more source tiles.
    bool rotating[stage_entity_count]={};
    for(int id=0;id<stage_entity_count;++id)
        rotating[id]=_entities[id].get(stage_property::angle)!=0 || _tracks[id][int(stage_property::angle)].active;
    if(event)for(int i=0;i<event->count;++i)
    {
        const auto& c=_commands[event->first+i];
        if(c.property!=int(stage_property::angle) || (c.target>=stage_actor_count && c.target!=stage_camera))continue;
        if(c.kind==stage_kind::set)rotating[c.target]=c.value!=0;
        else if(c.kind==stage_kind::tween)
        {
            bool zero=true,immediate=true;
            for(int k=0;k<c.count;++k){const auto& key=_keys[c.first+k];zero&=key.value==0 && !key.relative;immediate&=key.frames==0;}
            rotating[c.target]=!zero || (rotating[c.target] && !immediate);
        }
    }
    uint8_t result=0;
    if(!rotating[stage_camera])for(int id=0;id<stage_actor_count;++id)if(!rotating[id])result|=1u<<id;
    return result;
#else
    (void)event;return 0;
#endif
}
void stage_player::apply_command(const stage_command& command,uint8_t scale_policy)
{
    auto& entity = _entities[command.target];
    switch(command.kind)
    {
    case stage_kind::image:
        if(command.flags && entity.image >= 0) break;
        if(entity.image != command.value) { entity.previous = entity.image; entity.image = int16_t(command.value);entity.regular_cg=false; }
        break;
    case stage_kind::tween: start(command,scale_policy); break;
    case stage_kind::set:
        _tracks[command.target][command.property] = {};
        entity.values[command.property] = presentation_value(command.value,command.target,command.property,false,scale_policy&(1u<<command.target));
        break;
    case stage_kind::wait:
        _wait = command.value; _wait_input = command.flags & 1;
        _wait_tracks = command.flags & 2;
        break;
    case stage_kind::clear:
        for(int target = command.first; target < command.first+command.count; ++target)
        {
            _entities[target] = {};
            for(auto& track : _tracks[target]) track = {};
            _emotes[target] = {};
        }
        break;
    case stage_kind::order: entity.order = int16_t(command.value); break;
    case stage_kind::mask: entity.mask = int16_t(command.value); break;
    case stage_kind::maskable: entity.maskable = command.value; break;
    case stage_kind::printer: _printer = command.value; break;
    case stage_kind::cg_backend: entity.regular_cg = command.value; break;
    case stage_kind::emote:
        entity.values[0] -= _emotes[command.target].x;
        entity.values[1] -= _emotes[command.target].y;
        _emotes[command.target] = {0,command.count,uint8_t(command.value),command.flags,0,0};
        break;
    default: break;
    }
}
bool stage_player::tick(int frames, bool input)
{
    bool changed = false;
    for(int frame = 0; frame < frames; ++frame)
    {
        ++_clock;
        if(_wait > 0 && (--_wait == 0 || (_wait_input && input))) _wait = 0;
        for(int target = 0; target < stage_entity_count; ++target)
        {
            auto& entity = _entities[target];
            auto& emote = _emotes[target];
            entity.values[0] -= emote.x; entity.values[1] -= emote.y;
            for(int prop = 0; prop < int(stage_property::count); ++prop)
            {
                auto& track = _tracks[target][prop];
                if(! track.active) continue;
                const auto& key = _keys[track.first+track.key];
                int target_value=presentation_value(key.value,target,prop,key.relative,track.snap_scale);
                entity.values[prop] = stage_interpolate(track.from,key.relative ? track.from+target_value : target_value,
                                                       ++track.elapsed,key.frames,key.easing);
                changed = true;
                if(track.elapsed >= key.frames)
                {
                    track.from = entity.values[prop]; track.elapsed = 0;
                    if(++track.key == track.count)
                    {
                        if(track.loops) { if(track.loops != 127) --track.loops; track.key = 0; track.from = track.initial; }
                        else track.active = false;
                    }
                }
            }
            emote.x = emote.y = 0;
            if(emote.duration)
            {
                int phase = ++emote.elapsed*256/emote.duration;
                if(emote.kind == 2) emote.x = (phase/24%2 ? -1 : 1)*3*256;
                else
                {
                    int arc = (phase*(emote.kind == 1 ? 2 : 1))%256;
                    emote.y = -4*arc*(256-arc)/256 * (emote.kind == 3 ? -2 : 6);
                }
                if(emote.elapsed >= emote.duration)
                {
                    emote.x = emote.y = 0;
                    if(emote.loops) { if(emote.loops != 127) --emote.loops; emote.elapsed = 0; }
                    else emote.duration = 0;
                }
                changed = true;
            }
            entity.values[0] += emote.x; entity.values[1] += emote.y;
        }
    }
    if(! animating()) _wait_tracks = false;
    return changed;
}
}
