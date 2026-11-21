#ifndef SCENES_H
#define SCENES_H

namespace manosaba
{
class text_canvas;

enum class scene_id
{
    title, script, trial, ending, music, sound, music_stress, gallery, gallery_demo,
    count
};

struct scene_entry
{
    const char* name;
    const char* description;
};

constexpr scene_entry scenes[] = {
    { "标题画面", "故事、审判与画廊。" },
    { "剧本", "中文故事剧本选择与播放。" },
    { "魔女审判", "选择正式Trial章节并播放。" },
    { "片尾名单", "纯文字片尾自动播放，B返回。" },
    { "音乐 DEMO", "完整音乐DEMO与曲目试听。" },
    { "音效 DEMO", "剧情音效、循环、音序与音乐联合试听。" },
    { "音乐压力测试", "固定HirO播放故事或审判章节。" },
    { "画廊", "查看已见过的插画及差分。" },
    { "画廊(demo)", "浏览全部插画，不修改解锁进度。" },
};

constexpr int scene_count = int(scene_id::count);
static_assert(sizeof(scenes) / sizeof(scenes[0]) == scene_count);

void draw_scene(text_canvas& canvas, scene_id scene);
}

#endif
