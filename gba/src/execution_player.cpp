#include "execution_player.h"
#include "execution_runtime.h"
#include "text_canvas.h"
#include "sfx_player.h"
#include "bn_core.h"
#include "bn_algorithm.h"
extern "C"
{
    BN_DATA_EWRAM_BSS execution_runtime_profile execution_runtime = {};
}
namespace manosaba
{
void execution_player::charging(bool enabled)
{
    if (enabled == _sound)
        return;
    _sound = enabled;
    sfx_command command;
    command.cue = sfx_player::find("Sfx_System_ExecuteButton_001");
    command.loop = true;
    command.stop = !enabled;
    sfx_player::execute(command);
}
void execution_player::suspend()
{
    charging(false);
    _opened = _shown = _pressed = _armed = false;
}
void execution_player::begin()
{
    suspend();
    _charge = 0;
    _delay = 96;
    _checked = false;
    execution_runtime = {};
    execution_runtime.active = 1;
}
bool execution_player::update(bool held, bool ready)
{
    auto& p = execution_runtime;
    ++p.frames;
    p.charge = unsigned(_charge);
    p.pressed = unsigned(_pressed);
    p.complete = unsigned(_charge == 2000);
    p.delay = unsigned(_delay);
    p.ready = unsigned(ready && _shown);
    p.core_ticks = unsigned(bn::core::last_cpu_ticks());
    p.max_ticks = bn::max(p.max_ticks, p.core_ticks);
    p.missed += unsigned(bn::core::last_missed_frames());
    if (!_shown)
        return true;
    if (!ready)
        return false;
    int frames = 1 + bn::core::last_missed_frames();
    if (_charge == 2000)
    {
        _delay = bn::max(0, _delay - frames);
        if (!_delay)
            p.active = 0;
        return false;
    }
    if (!held)
        _armed = true;
    bool pressing = _armed && held;
    int before = _charge;
    bool changed = pressing != _pressed;
    _pressed = pressing;
    charging(pressing);
    _charge = bn::clamp(_charge + (pressing ? 5 : -8) * frames, 0, 2000);
    if (_charge == 2000)
    {
        charging(false);
        _pressed = false;
        sfx_command sound;
        sound.cue = sfx_player::find("Sfx_System_ExecuteButton_002");
        sfx_player::execute(sound);
    }
    return changed || _charge / 50 != before / 50;
}
void execution_player::draw(text_canvas& canvas)
{
    if (!_opened)
    {
        canvas.execution_screen();
        _opened = true;
        _shown = false;
        _draw_level = 0;
        _pose = 0;
        _checked = false;
    }
    unsigned start = unsigned(bn::core::current_cpu_ticks());
    int level = _charge / 50, pose = int(_pressed);
    if (_charge == 2000 && !_checked)
    {
        canvas.execution_complete();
        _checked = true;
    }
    else if (!_checked)
    {
        if (pose != _pose)
        {
            canvas.execution_button(pose, level);
            _pose = pose;
        }
        else
            canvas.execution_fill(pose, _draw_level, level);
    }
    _draw_level = level;
    if (!_shown && canvas.ready())
    {
        canvas.title_show();
        _shown = true;
    }
    execution_runtime.draw_ticks = unsigned(bn::core::current_cpu_ticks()) - start;
    execution_runtime.max_draw = bn::max(execution_runtime.max_draw, execution_runtime.draw_ticks);
}
} // namespace manosaba
