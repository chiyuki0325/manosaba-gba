#ifndef STORY_VM_H
#define STORY_VM_H
#include "story_format.h"
#include "story_layout.h"
#include "witch_book_data.h"
#include "music_command.h"
#include "sfx_command.h"
#include "screen_effect.h"
#include "court_effect.h"

namespace manosaba
{
class save_archive;
enum class story_state
{
    running,
    text,
    menu,
    pause,
    end,
    stage,
    debate,
    inventory,
    cutin,
    failure,
    camera,
    audio,
    title,
    reenact,
    screen,
    credits,
    execution,
    map
};
struct story_link
{
    uint32_t pc;
    uint16_t script, first, last;
};
struct story_option
{
    char text[story_option_capacity];
    uint32_t pc;
    uint16_t script;
    uint8_t value;
};

// No heap or Naninovel parsing on the GBA. One cache shared by the current script.
class story_vm
{
  public:
    using music_sink = bool (*)(void*, const music_command&);
    void bind_music(music_sink sink, void* context)
    {
        _music_sink = sink;
        _music_context = context;
    }
    using sfx_sink = void (*)(void*, const sfx_command&);
    void bind_sfx(sfx_sink sink, void* context)
    {
        _sfx_sink = sink;
        _sfx_context = context;
    }
    void start(int script);
    void archive(save_archive& data);
    uint8_t checkpoint(save_archive& data, uint8_t flags = 0);
    void legacy_anchors();
    unsigned source_position() const
    {
        return _source_pc;
    }
    void advance();
    void choose(int index);
    void object(int index);
    bool tick_debate(int frames, bool autoplay = true);
    void skip_debate();
    void choose_item(int index);
    void cancel_inventory();
    story_state state() const
    {
        return _state;
    }
    int script() const
    {
        return _script;
    }
    unsigned position() const
    {
        return _pc;
    }
    const char* text() const
    {
        return _text;
    }
    const story_text_style* text_style() const
    {
        return find_text_style(_script, _pc);
    }
    const story_text_style* history_style(int index) const
    {
        return find_text_style(_script, _history[index].pc, true);
    }
    int toast() const
    {
        return _toast_left ? _toast : 0;
    }
    void tick_toast(int frames)
    {
        _toast_left = uint8_t(frames < _toast_left ? _toast_left - frames : 0);
    }
    const char* speaker() const
    {
        return story_speakers[_speaker];
    }
    int history_count() const
    {
        return _history_count;
    }
    const story_history_entry& history(int index) const
    {
        return _history[index];
    }
    void history_text(int index, char* destination);
    const char* backdrop() const
    {
        return _backdrop;
    }
    const char* character() const
    {
        return _character;
    }
    const char* music() const
    {
        return story_music_aliases[_music_alias];
    }
    int option_count() const
    {
        return _option_count;
    }
    const story_option& option(int index) const
    {
        return _options[index];
    }
    bool option_bad(int index) const
    {
        return !_map && _options[index].value;
    }
    unsigned area_flags() const
    {
        return _flags;
    }
    int graphical_bank() const
    {
        return story_scripts[_script].graphics_bank == 65535 ? -1 : story_scripts[_script].graphics_bank;
    }
    int stage_event() const
    {
        return _stage_event;
    }
    int visual_frame() const
    {
        return _visual_frame;
    }
    bool trial() const
    {
        return story_scripts[_script].mode != 0;
    }
    bool bad_end() const
    {
        return _state == story_state::end && _bad_end;
    }
    bool objection_menu() const
    {
        return _menu_mode == 2;
    }
    int link_count() const
    {
        return _link_count;
    }
    const story_link& link(int index) const
    {
        return _links[index];
    }
    unsigned statement_duration() const
    {
        return _duration & 0x7fff;
    }
    bool debate_stopped() const
    {
        return _duration & 0x8000;
    }
    unsigned statement_left() const
    {
        return _line_left;
    }
    unsigned trial_left() const
    {
        return _total_left;
    }
    int hp() const
    {
        return _hp;
    }
    int bad_id() const
    {
        return _bad_id;
    }
    int trial_actor() const
    {
        return _trial_actor;
    }
    int trial_view() const
    {
        return _trial_view;
    }
    int trial_layout() const
    {
        return _trial_layout;
    }
    const uint16_t* trial_poses() const
    {
        return _trial_poses;
    }
    int trial_camera() const
    {
        return _trial_camera;
    }
    unsigned camera_frame() const
    {
        return _camera_frame;
    }
    unsigned camera_duration() const
    {
        return _camera_duration;
    }
    unsigned camera_wait() const
    {
        return _camera_wait;
    }
    bool witch_hiro() const
    {
        return _witch_hiro;
    }
    void tick_camera(unsigned frames)
    {
        unsigned left = _camera_duration - _camera_frame;
        _camera_frame += frames < left ? frames : left;
        _camera_wait = frames < _camera_wait ? _camera_wait - frames : 0;
    }
    int evidence() const
    {
        return _picture;
    }
    int inventory_count() const
    {
        return _candidate_count;
    }
    int inventory_kind() const
    {
        return _inventory_kind;
    }
    int item_id(int index) const
    {
        return _candidates[index];
    }
    int chosen_item() const
    {
        return _chosen_item;
    }
    bool inventory_back() const
    {
        return _inventory_back;
    }
    int book_version(int entry) const
    {
        return _book_versions[entry];
    }
    bool map_menu() const
    {
        return _state == story_state::menu && _map;
    }
    int cutin_kind() const
    {
        return _cutin_kind;
    }
    int cutin_variant() const
    {
        return _cutin_variant;
    }
    bool restored_debate() const
    {
        return _restored;
    }
    bool simple_trial_text() const
    {
        return _printer_style != 0;
    }
    bool live() const
    {
        return _printer_style == 2;
    }
    bool reenact() const
    {
        return _reenact;
    }
    bool reenact_closed() const
    {
        return _reenact_closed;
    }
    int reenact_scene() const
    {
        return _reenact_scene;
    }
    int reenact_action() const
    {
        return _reenact_action;
    }
    int credits_act() const
    {
        return _credits_act;
    }
    int map_segment() const
    {
        return _map_segment;
    }
    bool map_wait() const
    {
        return _map_wait;
    }
    int reenact_wait() const
    {
        return _reenact_wait;
    }
    palette_effect screen_color() const
    {
        return _screen.value();
    }
    void tick_screen(int frames)
    {
        _screen.tick(frames);
        _screen_wait = uint16_t(frames < _screen_wait ? _screen_wait - frames : 0);
    }
    bool screen_waiting() const
    {
        return _screen_wait != 0;
    }
    bool court_fire() const
    {
        return _court_fire;
    }
    int shake_x() const
    {
        return _shake.x();
    }
    int shake_y() const
    {
        return _shake.y();
    }
    void tick_shake(unsigned frames)
    {
        _shake.tick(frames);
        _actor_shake.tick(frames);
    }
    int shake_actor() const
    {
        return _actor_shake_id;
    }
    int actor_shake_x() const
    {
        return _actor_shake.x();
    }
    int actor_shake_y() const
    {
        return _actor_shake.y();
    }
    unsigned actor_shake_left() const
    {
        return _actor_shake.left();
    }
    const dialogue_layout& debate_layout() const
    {
        return _debate_layout;
    }

  private:
    alignas(4) uint8_t _cache[story_block_size];
    char _text[story_text_capacity] = {};
    char _backdrop[96] = {};
    char _character[96] = {};
    sfx_sink _sfx_sink = nullptr;
    void* _sfx_context = nullptr;
    music_sink _music_sink = nullptr;
    void* _music_context = nullptr;
    uint16_t _music_alias = 0;
    uint8_t _toast = 0, _toast_left = 0;
    story_option _options[story_max_options];
    uint32_t _pc = 0;
    uint32_t _source_pc = 0;
    story_history_entry _history[story_history_capacity];
    uint16_t _history_count = 0;
    int _script = 0;
    int _cached_block = -1;
    int _option_count = 0;
    int _visual_frame = -1, _stage_event = -1;
    uint16_t _flags = 0;
    uint8_t _area = 0;
    uint8_t _speaker = 0;
    bool _direct = true;
    bool _map = false;
    story_state _state = story_state::end;
    story_link _links[3];
    uint16_t _candidates[book_entry_count];
    uint8_t _book_versions[book_entry_count] = {};
    uint8_t _book_chapter = 0;
    uint64_t _trial_flags = 0;
    uint32_t _duration = 0, _line_left = 0, _total_left = 0;
    int _link_count = 0, _menu_mode = 0, _hp = 10, _bad_id = 0, _trial_actor = 0, _trial_view = 0, _picture = 0;
    int _trial_camera = -1;
    uint16_t _trial_poses[17];
    uint8_t _trial_layout = 0;
    unsigned _camera_frame = 0, _camera_duration = 0, _camera_wait = 0;
    int _candidate_count = 0, _chosen_item = 0, _inventory_kind = 0, _cutin_kind = 0, _cutin_variant = 0;
    bool _inventory_back = false, _return_valid = false, _restored = false, _witch_hiro = false;
    uint8_t _printer_style = 0;
    bool _reenact = false, _reenact_closed = true, _bad_end = false;
    int16_t _reenact_scene = -1;
    uint16_t _reenact_wait = 0;
    uint8_t _reenact_action = 0;
    uint8_t _credits_act = 0;
    int16_t _map_segment = -1;
    bool _map_wait = false;
    screen_effect _screen;
    uint16_t _screen_wait = 0;
    court_shake _shake;
    court_shake _actor_shake;
    uint8_t _actor_shake_id = 255;
    bool _court_fire = false;
    dialogue_layout _debate_layout{};
    struct debate_return
    {
        char text[story_text_capacity];
        story_link links[3];
        uint32_t pc, source_pc, duration, left, camera_frame, camera_duration;
        int script, speaker, link_count, actor, view, picture, camera;
        uint16_t poses[17];
        uint8_t trial_layout;
        bool witch_hiro;
        dialogue_layout layout;
    } _return{};

    uint8_t byte();
    uint16_t word();
    uint32_t offset();
    void string(char* out);
    void jump(int script, uint32_t pc);
    void fail();
    void remember(uint16_t pc);
    bool restore_payload(bool returning = false);
    unsigned anchor_before(int script, unsigned end, bool* curtain = nullptr);
};
} // namespace manosaba
#endif
