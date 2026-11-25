#include "music_player.h"
#include "music_backend.h"
#include "music_cues.h"
#include "save_archive.h"

namespace manosaba
{
void music_checkpoint::archive(save_archive& a)
{
    a.fixed(key);
    a.fixed(volume);
    a.fixed(loop);
}
void music_checkpoint::legacy_archive(save_archive& a)
{
    int start = 0, target = 0, total = 0, left = 0, return_volume = 0;
    bool stopping = false, return_loop = false, pending = false;
    uint32_t return_key = 0;
    a.field(key);
    a.field(volume);
    a.field(start);
    a.field(target);
    a.field(total);
    a.field(left);
    a.field(loop);
    a.field(stopping);
    a.field(return_key);
    a.field(return_volume);
    a.field(return_loop);
    a.field(pending);
    if (return_key)
    {
        key = return_key;
        volume = return_volume;
        loop = return_loop;
    }
    else if (stopping || (left ? target : volume) == 0)
    {
        key = 0;
        volume = 0;
    }
    else if (left)
        volume = target;
}
} // namespace manosaba

namespace manosaba::music_player
{
namespace
{
struct state
{
    const char* alias;
    const music_cue* cue;
    uint32_t generation;
    int volume;
    int fade_start;
    int fade_target;
    int fade_total;
    int fade_left;
    int master_trim;
    bool paused;
    bool stop_on_fade;
};
__attribute__((section(".ewram_bss"))) state data;
constexpr unsigned ringtone_key = (1u << 26) | (1u << 12) | 3;
struct return_track
{
    const music_cue* cue;
    int volume;
    bool loop, pending;
};
__attribute__((section(".ewram_bss"))) return_track previous;
struct mix_envelope
{
    int reduction, hold;
};
__attribute__((section(".ewram_bss"))) mix_envelope mix;

struct cue
{
    unsigned id;
    unsigned part; // 0: full/default, 1: Intro, 2: Loop.
    const music_cue* packed;
};

bool prefix(const char*& p, const char* expected)
{
    const char* scan = p;
    while (*expected)
        if (*scan++ != *expected++)
            return false;
    p = scan;
    return true;
}

unsigned number(const char*& p)
{
    unsigned value = 0;
    while (*p >= '0' && *p <= '9')
        value = value * 10 + unsigned(*p++ - '0');
    return value;
}

cue resolve(const char* alias)
{
    if (!alias || !*alias)
        return {};
    const char* p = alias;
    bool song = false, ringtone = false;
    if (!prefix(p, "Bgm"))
    {
        p = alias;
        if (prefix(p, "Song"))
            song = true;
        else
        {
            p = alias;
            if (!prefix(p, "Ambi"))
                return {};
            ringtone = true;
        }
    }
    if (*p == '_')
        ++p;
    unsigned id = number(p);
    unsigned variant = 1;
    if (*p == '_' && p[1] >= '0' && p[1] <= '9')
    {
        ++p;
        variant = number(p);
    }
    unsigned part = 0;
    if (*p)
    {
        if (prefix(p, "_Intro"))
            part = 1;
        else if (prefix(p, "_Loop"))
            part = 2;
        else
            return {};
    }
    if (*p || !id || id > 999 || !variant || variant > 255)
        return {};
    if (ringtone && (id != 3 || variant != 1 || part))
        return {};
    unsigned key = id | (variant << 12) | (song ? 1u << 24 : 0) | (ringtone ? 1u << 26 : 0);
    const music_cue* packed = part == 1 ? find_music_cue(key | (1u << 25)) : nullptr;
    return {key, part, packed ? packed : find_music_cue(key)};
}

int bounded(int value)
{
    return value < 0 ? 0 : value > 1024 ? 1024 : value;
}
int effective_volume()
{
    int volume = data.volume * (1024 - data.master_trim) / 1024 * data.cue->volume / 1024;
    return volume * (1024 - mix.reduction) / 1024;
}

void clear_current()
{
    if (data.cue && data.cue->key == ringtone_key && previous.cue)
        previous.pending = true;
    data.alias = nullptr;
    data.cue = 0;
    data.fade_left = 0;
    data.stop_on_fade = false;
}

void fade_to(int target, int frames, bool stopping = false)
{
    data.fade_start = data.volume;
    data.fade_target = target;
    data.fade_total = frames;
    data.fade_left = frames;
    data.stop_on_fade = stopping;
    if (!frames)
    {
        data.volume = target;
        if (stopping)
        {
            music_backend::stop();
            clear_current();
        }
        else
            music_backend::set_volume(effective_volume());
    }
}
} // namespace

music_result execute(const music_command& command)
{
    bool has_path = (command.parameters & music_has_path) && command.path && *command.path;
    cue requested = has_path ? resolve(command.path) : cue{};
    if (has_path && !requested.id)
        return music_result::invalid_alias;
    if (command.stop)
    {
        if (previous.cue && has_path && requested.id == (previous.cue->key & 0x1ffffffu))
        {
            previous = {};
            return music_result::stopped;
        }
        if (data.cue && (!has_path || requested.id == (data.cue->key & ~(1u << 25))))
            fade_to(0, command.duration_frames, true);
        return music_result::stopped;
    }
    if (has_path && !requested.packed)
    {
        previous = {};
        if (data.cue)
        {
            music_backend::stop();
            clear_current();
        }
        return music_result::not_packed;
    }
    if (!has_path && !data.cue)
        return music_result::not_packed;
    if (has_path && (data.cue != requested.packed || !music_backend::playing()))
    {
        if (requested.id == ringtone_key)
        {
            if (data.cue && data.cue->key != ringtone_key)
                previous = {data.cue, data.fade_left ? data.fade_target : data.volume, music_backend::looping(), false};
        }
        else
            previous = {};
        bool with_intro = requested.part != 2;
        bool explicit_intro = requested.part == 1;
        int linked_intro = -1;
        if ((command.parameters & music_has_intro) && command.intro && *command.intro)
        {
            cue intro = resolve(command.intro);
            linked_intro = music_intro_order(requested.id, intro.id);
            if (!intro.packed || (linked_intro < 0 && (intro.id != requested.id || intro.part != 1)))
                return music_result::not_packed;
            with_intro = true;
            explicit_intro = true;
        }
        data.alias = command.path;
        data.cue = requested.packed;
        ++data.generation;
        data.volume = 0;
        int target = (command.parameters & music_has_volume) ? bounded(command.volume) : 1024;
        int frames = (command.parameters & music_has_fade_in) ? command.fade_in_frames : command.duration_frames;
        if (!frames)
            data.volume = target;
        int intro_order =
            explicit_intro || (command.parameters & music_audition) ? data.cue->intro_order : data.cue->story_order;
        music_backend::start(data.cue->module, effective_volume(), command.loop,
                             linked_intro >= 0 ? linked_intro
                             : with_intro      ? intro_order
                                               : data.cue->loop_order);
        if (data.paused)
            music_backend::pause();
        fade_to(target, frames);
        return music_result::started;
    }
    if (command.parameters & music_has_loop)
        music_backend::set_loop(command.loop);
    if (command.parameters & music_has_volume)
        fade_to(bounded(command.volume), command.duration_frames);
    return music_result::modified;
}
music_checkpoint checkpoint()
{
    music_checkpoint result;
    if (previous.cue && (!data.cue || data.cue->key == ringtone_key))
        return {previous.cue->key, previous.volume, previous.loop};
    int volume = data.fade_left ? data.fade_target : data.volume;
    if (playing() && !data.stop_on_fade && volume > 0)
        result = {data.cue->key, volume, music_backend::looping()};
    return result;
}
void restore(const music_checkpoint& saved)
{
    int trim = data.master_trim;
    reset();
    data.master_trim = trim;
    data.cue = saved.key ? find_music_cue(saved.key) : nullptr;
    if (!data.cue)
        return;
    data.alias = data.cue->alias;
    ++data.generation;
    data.volume = saved.volume;
    music_backend::start(data.cue->module, effective_volume(), saved.loop, data.cue->story_order);
}

void update(bool foreground_sfx)
{
    music_backend::update();
    int old_reduction = mix.reduction;
    if (!data.paused)
    {
        if (foreground_sfx)
            mix.hold = 6;
        else if (mix.hold)
            --mix.hold;
        int target = foreground_sfx || mix.hold ? 300 : 0;
        if (mix.reduction < target)
        {
            mix.reduction += 75;
            if (mix.reduction > target)
                mix.reduction = target;
        }
        else if (mix.reduction > target)
        {
            mix.reduction -= 17;
            if (mix.reduction < target)
                mix.reduction = target;
        }
    }
    if (!data.paused && previous.pending)
    {
        auto old = previous;
        previous = {};
        music_command command;
        command.path = old.cue->alias;
        command.volume = old.volume;
        command.loop = old.loop;
        command.parameters = music_has_path | music_has_volume | music_has_loop;
        execute(command);
    }
    if (data.paused || !data.cue)
        return;
    if (!music_backend::playing())
    {
        clear_current();
        return;
    }
    bool volume_changed = old_reduction != mix.reduction;
    if (data.fade_left)
    {
        --data.fade_left;
        data.volume = data.fade_target + (data.fade_start - data.fade_target) * data.fade_left / data.fade_total;
        if (!data.fade_left && data.stop_on_fade)
        {
            music_backend::stop();
            clear_current();
            return;
        }
        volume_changed = true;
    }
    if (volume_changed)
        music_backend::set_volume(effective_volume());
}

void reset()
{
    music_backend::stop();
    uint32_t generation = data.generation;
    data = {};
    previous = {};
    data.generation = generation;
}
void pause()
{
    data.paused = true;
    if (data.cue)
        music_backend::pause();
}
void resume()
{
    data.paused = false;
    if (data.cue)
        music_backend::resume();
}
bool busy()
{
    return data.fade_left != 0;
}
bool playing()
{
    return data.cue && music_backend::playing();
}
const char* current_alias()
{
    return data.alias;
}
uint32_t generation()
{
    return data.generation;
}
uint32_t collection_key()
{
    return data.cue && (data.fade_left ? data.fade_target : data.volume) > 0 && !data.stop_on_fade ? data.cue->key : 0;
}
void set_master_volume(int volume_q10)
{
    data.master_trim = 1024 - bounded(volume_q10);
    if (data.cue)
        music_backend::set_volume(effective_volume());
}
void play_test_track(bool with_intro)
{
    music_command command;
    command.path = "Bgm_37_2_Loop";
    command.intro = with_intro ? "Bgm_37_2_Intro" : nullptr;
    command.parameters = music_has_path | music_has_volume | music_has_loop | music_has_fade_in;
    if (with_intro)
        command.parameters |= music_has_intro;
    execute(command);
}
} // namespace manosaba::music_player
