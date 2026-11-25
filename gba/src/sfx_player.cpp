#include "sfx_player.h"
#include "sfx_backend.h"
#include "sfx_data.h"
#include "save_archive.h"
#include <cstring>
namespace manosaba::sfx_player
{
namespace
{
// Silent loop requests retain their alias and resume after foreground effects.
// Only these two voice records can submit to Maxmod's two SFX channels.
struct stream
{
    int cue = -1, frame = 0, next = 0, volume = 0, fade_start = 0, fade_target = 0, fade_total = 0, fade_left = 0;
    unsigned serial = 0;
    bool loop = false, stopping = false;
};
struct voice
{
    uint16_t handle = 0;
    int stream = -1, left = 0, level = 0;
    unsigned serial = 0;
};
struct player_state
{
    stream streams[6];
    voice voices[2];
    unsigned serial = 0;
    int master = 1024;
};
__attribute__((section(".ewram_bss"))) player_state state;
bool initialized = false;
void init()
{
    if (!initialized)
    {
        state = {};
        initialized = true;
    }
}
void remove_voice(voice& v)
{
    if (v.handle)
        sfx_backend::stop(v.handle);
    v = {};
}
void remove(int index)
{
    for (auto& v : state.voices)
        if (v.stream == index)
            remove_voice(v);
    state.streams[index] = {};
}
int priority(int index)
{
    return sfx_cues[state.streams[index].cue].ambient ? 0 : state.streams[index].loop ? 1 : 2;
}
int gain(int index, int note)
{
    const auto& s = state.streams[index];
    int value = s.volume;
    const auto& cue = sfx_cues[s.cue];
    if (cue.envelope)
    {
        if (s.frame < 60)
            value = value * s.frame / 60;
        int left = cue.duration - s.frame;
        if (left < 107)
            value = value * (left > 0 ? left : 0) / 107;
    }
    return value * note / 1024 * state.master / 1024;
}
void play(int index, int sample, int level, int rate, int hold, bool interrupt = true)
{
    voice* target = nullptr;
    for (auto& v : state.voices)
        if (v.stream < 0)
        {
            target = &v;
            break;
        }
    if (!target && interrupt)
    {
        for (auto& v : state.voices)
            if (priority(v.stream) <= priority(index) &&
                (!target || priority(v.stream) < priority(target->stream) ||
                 (priority(v.stream) == priority(target->stream) && v.serial < target->serial)))
                target = &v;
    }
    if (!target)
        return;
    remove_voice(*target);
    target->handle = sfx_backend::play(sample, gain(index, level), rate);
    if (target->handle)
    {
        target->stream = index;
        target->left = hold;
        target->level = level;
        target->serial = ++state.serial;
    }
}
void fade(stream& s, int target, int frames, bool stopping)
{
    s.fade_start = s.volume;
    s.fade_target = target;
    s.fade_total = s.fade_left = frames;
    s.stopping = stopping;
    if (!frames)
        s.volume = target;
}
} // namespace
int find(const char* alias)
{
    if (!alias)
        return -1;
    for (int i = 0; i < sfx_cue_count; ++i)
    {
        const char* a = alias;
        const char* b = sfx_cues[i].alias;
        while (*a && *a == *b)
        {
            ++a;
            ++b;
        }
        if (*a == *b)
            return i;
    }
    return -1;
}
void execute(const sfx_command& command)
{
    init();
    int index = -1;
    if (command.stop)
    {
        for (int i = 0; i < 6; ++i)
            if (state.streams[i].cue >= 0 && (command.cue < 0 || command.cue == state.streams[i].cue) &&
                (command.loops_only ? state.streams[i].loop || sfx_cues[state.streams[i].cue].ambient
                                    : bool(sfx_cues[state.streams[i].cue].ambient) == command.ambient))
            {
                if (command.fade_frames)
                    fade(state.streams[i], 0, command.fade_frames, true);
                else
                    remove(i);
            }
        return;
    }
    if (command.cue < 0)
        return;
    const auto& cue = sfx_cues[command.cue];
    if (cue.sample < 0 && !cue.count)
        return;
    for (int i = 0; i < 6; ++i)
        if (state.streams[i].cue == command.cue)
        {
            index = i;
            break;
        }
    if (index >= 0 && cue.ambient)
    {
        auto& s = state.streams[index];
        s.loop = command.loop;
        fade(s, command.volume, command.fade_frames, false);
        return;
    }
    if (index < 0)
        for (int i = 0; i < 6; ++i)
            if (state.streams[i].cue < 0)
            {
                index = i;
                break;
            }
    if (index < 0)
    {
        index = 0;
        for (int i = 1; i < 6; ++i)
            if (state.streams[i].serial < state.streams[index].serial)
                index = i;
    }
    remove(index);
    auto& s = state.streams[index];
    s.cue = command.cue;
    s.loop = command.loop;
    s.serial = ++state.serial;
    s.volume = command.fade_frames ? 0 : command.volume;
    fade(s, command.volume, command.fade_frames, false);
    if (cue.sample >= 0)
        play(index, cue.sample, 1024, 1024, cue.native_loop ? -1 : cue.duration);
    else
        while (s.next < cue.count && !sfx_notes[cue.first + s.next].frame)
        {
            const auto& n = sfx_notes[cue.first + s.next++];
            play(index, n.sample, n.volume, n.rate, n.hold);
        }
}
void update(int frames)
{
    init();
    for (auto& v : state.voices)
        if (v.stream >= 0)
        {
            bool expired = v.left > 0 && (v.left -= frames) <= 0;
            if (!sfx_backend::active(v.handle) || expired)
                remove_voice(v);
        }
    for (int i = 0; i < 6; ++i)
    {
        auto& s = state.streams[i];
        if (s.cue < 0)
            continue;
        const auto& cue = sfx_cues[s.cue];
        s.frame += frames;
        if (s.fade_left)
        {
            s.fade_left = s.fade_left > frames ? s.fade_left - frames : 0;
            s.volume = s.fade_target + (s.fade_start - s.fade_target) * s.fade_left / s.fade_total;
            if (!s.fade_left && s.stopping)
            {
                remove(i);
                continue;
            }
        }
        if (s.frame >= cue.duration)
        {
            if (!s.loop)
            {
                remove(i);
                continue;
            }
            s.frame %= cue.duration;
            s.next = 0;
            // Native loop samples keep their phase. Finite WAVs restart here.
        }
        if (cue.sample < 0)
        {
            while (s.next < cue.count && sfx_notes[cue.first + s.next].frame <= s.frame)
            {
                const auto& n = sfx_notes[cue.first + s.next++];
                int left = n.hold - (s.frame - n.frame);
                if (left > 0)
                    play(i, n.sample, n.volume, n.rate, left);
            }
        }
        else
        {
            bool held = false;
            for (auto& v : state.voices)
                held |= v.stream == i;
            if (!held && (s.loop || cue.ambient || cue.envelope))
                play(i, cue.sample, 1024, 1024, cue.native_loop ? -1 : cue.duration - s.frame, false);
        }
        for (auto& v : state.voices)
            if (v.stream == i)
                sfx_backend::volume(v.handle, gain(i, v.level));
    }
}
void reset()
{
    init();
    for (int i = 0; i < 6; ++i)
        remove(i);
}
int active_voices()
{
    init();
    int count = 0;
    for (auto& v : state.voices)
        count += v.stream >= 0;
    return count;
}
bool foreground_active()
{
    init();
    for (const auto& v : state.voices)
        if (v.stream >= 0 && gain(v.stream, v.level) > 0)
        {
            const auto& cue = sfx_cues[state.streams[v.stream].cue];
            if (cue.ambient)
                continue;
            const char* alias = cue.alias;
            const char* system = "Sfx_System_";
            while (*system && *alias == *system)
            {
                ++alias;
                ++system;
            }
            if (*system)
                return true;
        }
    return false;
}
int current_cue()
{
    init();
    int cue = -1;
    unsigned serial = 0;
    for (auto& s : state.streams)
        if (s.cue >= 0 && s.serial >= serial)
        {
            serial = s.serial;
            cue = s.cue;
        }
    return cue;
}
void set_master_volume(int value)
{
    init();
    state.master = value;
}

sfx_checkpoint checkpoint()
{
    init();
    sfx_checkpoint result;
    int chosen[2] = {-1, -1}, count = 0;
    auto eligible = [](int id)
    {
        const auto& s = state.streams[id];
        return s.cue >= 0 && (s.loop || sfx_cues[s.cue].ambient) && !s.stopping &&
               (s.fade_left ? s.fade_target : s.volume) > 0;
    };
    for (const auto& v : state.voices)
        if (v.stream >= 0 && eligible(v.stream) && v.stream != chosen[0] && count < 2)
            chosen[count++] = v.stream;
    while (count < 2)
    {
        int best = -1;
        for (int i = 0; i < 6; ++i)
            if (i != chosen[0] && i != chosen[1] && eligible(i) &&
                (best < 0 || priority(i) > priority(best) ||
                 (priority(i) == priority(best) && state.streams[i].serial > state.streams[best].serial)))
                best = i;
        if (best < 0)
            break;
        chosen[count++] = best;
    }
    for (int i = 0; i < count; ++i)
    {
        const auto& s = state.streams[chosen[i]];
        result.sounds[i] = {s.cue, s.fade_left ? s.fade_target : s.volume, s.loop};
    }
    return result;
}
void restore(const sfx_checkpoint& saved)
{
    reset();
    for (const auto& s : saved.sounds)
        if (s.cue >= 0)
        {
            sfx_command c;
            c.cue = s.cue;
            c.volume = s.volume;
            c.loop = s.loop;
            execute(c);
        }
}
} // namespace manosaba::sfx_player
namespace manosaba
{
void sfx_checkpoint::archive(save_archive& a)
{
    for (auto& s : sounds)
    {
        a.fixed(s.cue);
        a.fixed(s.volume);
        a.fixed(s.loop);
        if (a.reading() && s.cue >= sfx_cue_count)
            a.reject();
    }
}
void sfx_checkpoint::legacy_archive(save_archive& a)
{
    *this = {};
    int count = 0;
    for (int i = 0; i < 6; ++i)
    {
        int cue = -1;
        a.field(cue);
        if (cue < 0)
            continue;
        int volume = 0, left = 0, target = 0;
        bool loop = false, stopping = false;
        a.field(volume);
        a.field(loop);
        a.field(left);
        a.field(target);
        a.field(stopping);
        if (cue >= sfx_cue_count)
        {
            a.reject();
            return;
        }
        int gain = left ? target : volume;
        if (stopping || gain <= 0 || (!loop && !sfx_cues[cue].ambient))
            continue;
        sound value{cue, gain, loop};
        if (count < 2)
            sounds[count++] = value;
        else
        {
            auto rank = [](const sound& s) { return sfx_cues[s.cue].ambient ? 0 : int(s.loop); };
            int replace = rank(sounds[0]) < rank(sounds[1]) ? 0 : 1;
            if (rank(value) >= rank(sounds[replace]))
                sounds[replace] = value;
        }
    }
}
} // namespace manosaba
