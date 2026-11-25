#ifndef STAGE_PLAYER_H
#define STAGE_PLAYER_H
#include "stage_format.h"
namespace manosaba
{
class save_archive;
struct stage_entity
{
    int32_t values[int(stage_property::count)] = {120 * 256, 80 * 256, 256, 0, 0, 256, 0, 256, 256, 256, 256, 256};
    int16_t image = -1, previous = -1, order = 0, mask = -1;
    bool maskable = true;
    bool regular_cg = false; // Uses existing entity padding; fixed for this visible appearance.
    int get(stage_property property) const
    {
        return int(values[int(property)]);
    }
};
class stage_player
{
  public:
    stage_player(const stage_key* keys = stage_keys, const stage_command* commands = stage_commands)
        : _keys(keys), _commands(commands)
    {
    }
    void reset(const stage_key* keys = stage_keys, const stage_command* commands = stage_commands);
    void archive(save_archive& data);
    void checkpoint(save_archive& data);
    void settle();
    void set_program(const stage_key* keys, const stage_command* commands)
    {
        _keys = keys;
        _commands = commands;
    }
    void apply(const stage_event& event);
    void apply(const stage_command& command);
    bool tick(int frames = 1, bool input = false);
    bool blocked() const;
    bool animating() const;
    bool geometry_animating(int entity) const;
    bool affine_animating(int entity) const
    {
        return _tracks[entity][int(stage_property::scale)].active || _tracks[entity][int(stage_property::angle)].active;
    }
    const stage_entity& entity(int index) const
    {
        return _entities[index];
    }
    bool printer_visible() const
    {
        return _printer;
    }
    unsigned clock() const
    {
        return _clock;
    }

  private:
    struct animation_track
    {
        int32_t from = 0, initial = 0;
        uint16_t first = 0, count = 0, key = 0, elapsed = 0;
        uint8_t loops = 0;
        bool active = false, blocking = false;
        bool snap_scale = false;
    };
    struct emote_state
    {
        uint16_t elapsed = 0, duration = 0;
        uint8_t kind = 0, loops = 0;
        int16_t x = 0, y = 0;
    };
    const stage_key* _keys;
    const stage_command* _commands;
    stage_entity _entities[stage_entity_count];
    animation_track _tracks[stage_entity_count][int(stage_property::count)];
    emote_state _emotes[stage_entity_count];
    unsigned _clock = 0;
    int _wait = 0;
    bool _wait_input = false, _wait_tracks = false, _printer = true;
    void start(const stage_command& command, uint8_t scale_policy);
    void apply_command(const stage_command& command, uint8_t scale_policy);
    uint8_t scale_policy(const stage_event* event = nullptr) const;
    int saved_value(int id, int property) const;
};
int stage_interpolate(int from, int to, int elapsed, int duration, stage_ease ease);
} // namespace manosaba
#endif
