#include "scenes.h"
#include "text_canvas.h"

namespace manosaba
{

void draw_scene(text_canvas& canvas, scene_id scene)
{
    canvas.clear();
    const auto& entry = scenes[int(scene)];
    canvas.text(12, 10, entry.name, text_canvas::accent);
    canvas.rectangle(12, 27, 216, 1, text_canvas::selection);
    canvas.text(12, 44, "占位画面", text_canvas::warning);
    canvas.text(12, 66, entry.description);
    canvas.text(12, 102, "已接入统一画面跳转。", text_canvas::muted);
    canvas.rectangle(12, 133, 216, 1, text_canvas::selection);
    canvas.text(12, 136, "Start/B 菜单   L/R 切换画面", text_canvas::muted);
    canvas.present();
}

}
