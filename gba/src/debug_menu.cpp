#include "debug_menu.h"
#include "text_canvas.h"
#include "bn_keypad.h"
#include "sfx_ui.h"

namespace manosaba
{
void debug_menu::draw(text_canvas& canvas) const
{
    canvas.clear();
    canvas.text(12, 8, "调试跳转", text_canvas::accent);
    canvas.rectangle(12, 25, 216, 1, text_canvas::selection);
    for(int row = 0; row < scene_count; ++row)
    {
        int index = row;
        int y = 30 + row * 12;
        if(index == _selected)
        {
            canvas.rectangle(10, y - 1, 220, 13, text_canvas::selection);
            canvas.text(12, y, ">", text_canvas::accent);
        }
        canvas.text(24, y, scenes[index].name);
        bool implemented = true;
        canvas.text(198, y, implemented ? "测试" : "占位",
                    implemented ? text_canvas::accent : text_canvas::muted);
    }
    canvas.text(12,145,_has_scene ? "上下选择 A进入 Start/B返回" : "上下选择 A进入",text_canvas::muted);
    canvas.present();
}
menu_action debug_menu::enter()
{
    _scene = scene_id(_selected);
    _has_scene = true;
    _open = false;
    return menu_action::enter;
}
menu_action debug_menu::update(bool scene_buttons)
{
    if(_open)
    {
        if(bn::keypad::start_pressed() || bn::keypad::b_pressed())
        {
            if(_has_scene)
            {
                _open = false;
                return menu_action::resume;
            }
            return menu_action::none;
        }
    }
    else if(scene_buttons && (bn::keypad::start_pressed() || bn::keypad::b_pressed()))
    {
        _selected = int(_scene);
        _open = true;
        return menu_action::pause;
    }
    if(_open)
    {
        int previous = _selected;
        if(bn::keypad::up_pressed()) _selected = (_selected + scene_count - 1) % scene_count;
        else if(bn::keypad::down_pressed()) _selected = (_selected + 1) % scene_count;
        if(bn::keypad::a_pressed()) return enter();
        if(_selected!=previous)play_ui_sound(ui_sound::move);
        return _selected != previous ? menu_action::redraw : menu_action::none;
    }
    if(scene_buttons && (bn::keypad::l_pressed() || bn::keypad::r_pressed()))
    {
        _selected = (int(_scene) + (bn::keypad::r_pressed() ? 1 : scene_count - 1)) % scene_count;
        return enter();
    }
    return menu_action::none;
}
}
