#include "music_player.h"
#include "music_backend.h"
#include "music_cues.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>

namespace
{
int starts, gain;
bool active, paused, loop_enabled, intro;
int module, order;
} // namespace
namespace manosaba::music_backend
{
void start(int m, int v, bool l, int o)
{
    ++starts;
    module = m;
    order = o;
    gain = v;
    loop_enabled = l;
    intro = o == 0;
    active = true;
}
void stop()
{
    active = false;
}
void set_volume(int v)
{
    gain = v;
}
void set_loop(bool l)
{
    loop_enabled = l;
}
void update()
{
}
void pause()
{
    paused = true;
}
void resume()
{
    paused = false;
}
bool playing()
{
    return active;
}
bool looping()
{
    return loop_enabled;
}
} // namespace manosaba::music_backend

int main()
{
    using namespace manosaba;
    namespace p = music_player;
    p::reset();
    music_command c;
    uint32_t generation = p::generation();
    c.path = "Bgm_037_002_Loop";
    c.intro = "Bgm_37_2_Intro";
    c.parameters = music_has_path | music_has_intro | music_has_volume | music_has_loop | music_has_fade_in;
    c.fade_in_frames = 10;
    assert(p::execute(c) == music_result::started && starts == 1 && gain == 0 && intro);
    assert(p::collection_key() != 0); // A script fade-in is a real play request.
    assert(p::generation() == generation + 1);
    generation = p::generation();
    for (int i = 0; i < 5; ++i)
        p::update();
    assert(gain == 480 && p::busy());
    p::pause();
    for (int i = 0; i < 20; ++i)
        p::update();
    assert(gain == 480 && paused);
    p::resume();
    for (int i = 0; i < 5; ++i)
        p::update();
    assert(gain == 960 && !p::busy() && !paused);

    c = {};
    c.path = "Bgm_37_2_Loop";
    c.parameters = music_has_path | music_has_volume | music_has_loop;
    c.volume = 512;
    c.loop = false;
    assert(p::execute(c) == music_result::modified && starts == 1 && gain == 480 && !loop_enabled);
    assert(p::generation() == generation); // Same-round retry must not look like a new song.
    p::set_master_volume(512);
    assert(gain == 240);

    c = {};
    c.parameters = music_has_volume;
    c.volume = 0;
    p::execute(c);
    assert(gain == 0 && p::playing() && starts == 1);
    assert(!p::collection_key()); // A silent pre-start is not a collected track.
    c.volume = 1024;
    p::execute(c);
    assert(gain == 480 && starts == 1);
    p::set_master_volume(0);
    assert(p::collection_key() != 0);
    p::set_master_volume(512);

    c = {};
    c.path = "Bgm_18_2_Loop";
    c.parameters = music_has_path;
    assert(p::execute(c) == music_result::not_packed && starts == 1 && !p::playing() && !p::busy());
    c.path = "Bgm_37_2_Alien";
    assert(p::execute(c) == music_result::invalid_alias);

    p::play_test_track();
    assert(p::playing() && starts == 2);

    c = {};
    c.stop = true;
    c.path = "Bgm_2_1_Loop";
    c.parameters = music_has_path;
    p::execute(c);
    assert(p::playing());
    c.path = "Bgm_37_2_Loop";
    c.duration_frames = 3;
    p::execute(c);
    assert(p::busy());
    for (int i = 0; i < 3; ++i)
        p::update();
    assert(!p::playing() && !p::busy() && !p::current_alias());

    p::reset();
    p::play_test_track(false);
    generation = p::generation();
    assert(!intro && loop_enabled && gain == 960);
    active = false;
    p::update();
    assert(!p::playing() && !p::current_alias());
    p::play_test_track(true);
    assert(intro && gain == 960);
    for (int i = 0; i < music_cue_count; ++i)
    {
        const auto& cue = music_cues[i];
        p::reset();
        c = {};
        c.path = cue.alias;
        c.parameters = music_has_path;
        assert(p::execute(c) == music_result::started);
        assert(module == cue.module && order == cue.story_order && gain == cue.volume);
    }
    // Numbered versions deliberately share the current arrangement.
    c = {};
    c.path = "Bgm_37_1_Loop";
    c.parameters = music_has_path;
    assert(p::execute(c) == music_result::started && order == 8);
    int hiro_module = module;
    c.path = "Bgm_37_2_Loop";
    assert(p::execute(c) == music_result::started && module == hiro_module && order == 8);
    p::reset();
    c.path = "Bgm_2_1_Intro";
    assert(p::execute(c) == music_result::started && order == 0);
    int intro_module = module;
    c.path = "Bgm_2_1_Loop";
    c.intro = "Bgm_2_1_Intro";
    c.parameters |= music_has_intro;
    assert(p::execute(c) == music_result::started && module != intro_module && order == 0);
    // Source 003 _Loop still plays its supplied Intro once, then native restart3
    // repeats only 004. An explicit 004 request enters the shared body directly.
    p::reset();
    c = {};
    c.path = "Bgm_3_1_Loop";
    c.parameters = music_has_path;
    assert(p::execute(c) == music_result::started && order == 0 && gain == 768);
    int sargedy_module = module;
    p::reset();
    c.path = "Bgm_4_1";
    assert(p::execute(c) == music_result::started && module == sargedy_module && order == 3 && gain == 768);
    // Ordinary 048 starts its body; its declared cross-number Intro starts 047.
    p::reset();
    c = {};
    c.path = "Bgm_48_1";
    c.parameters = music_has_path;
    assert(p::execute(c) == music_result::started && order == 1 && gain == 768);
    int final_module = module;
    p::reset();
    c.intro = "Bgm_47_1";
    c.parameters |= music_has_intro;
    assert(p::execute(c) == music_result::started && module == final_module && order == 0 && gain == 768);
    p::reset();
    c.path = "Bgm_048_001_Loop";
    c.intro = "Bgm_047_001";
    assert(p::execute(c) == music_result::started && module == final_module && order == 0);
    p::reset();
    c.intro = "Bgm_46_1";
    int rejected_starts = starts;
    assert(p::execute(c) == music_result::not_packed && starts == rejected_starts);
    p::reset();
    c = {};
    c.path = "Bgm_6_1_Loop";
    c.parameters = music_has_path;
    assert(p::execute(c) == music_result::started && order == 0);
    generation = p::generation();
    p::reset();
    p::reset();
    assert(p::generation() == generation);
    p::reset();
    c = {};
    c.path = "Bgm_5_1";
    c.parameters = music_has_path;
    assert(p::execute(c) == music_result::started && order == 0);
    // Safe game entry strips export silence; explicit Intro and DEMO retain it.
    p::reset();
    c = {};
    c.path = "Bgm_9_1";
    c.parameters = music_has_path;
    assert(p::execute(c) == music_result::started && order == 1);
    p::reset();
    c.parameters |= music_audition;
    assert(p::execute(c) == music_result::started && order == 0);
    p::reset();
    c = {};
    c.path = "Bgm_38_1_Loop";
    c.intro = "Bgm_38_1_Intro";
    c.parameters = music_has_path | music_has_intro;
    assert(p::execute(c) == music_result::started && order == 0);
    // The ringtone temporarily owns the single music bus. Replay the original
    // cue only after this VM batch had the chance to select a new track.
    p::reset();
    c = {};
    c.path = "Bgm_5_1";
    c.parameters = music_has_path | music_has_volume | music_has_loop;
    c.volume = 600;
    c.loop = false;
    p::execute(c);
    int prior_gain = gain;
    auto saved_bgm = p::checkpoint();
    c = {};
    c.path = "Ambi_3_1";
    c.parameters = music_has_path | music_has_loop;
    c.loop = true;
    assert(p::execute(c) == music_result::started);
    auto saved_ring = p::checkpoint();
    assert(saved_ring.key == saved_bgm.key && saved_ring.volume == 600 && !saved_ring.loop);
    int ringtone_starts = starts;
    c.stop = true;
    p::execute(c);
    assert(!p::playing() && starts == ringtone_starts);
    p::update();
    assert(p::playing() && starts == ringtone_starts + 1 && gain == prior_gain && !loop_enabled);
    c = {};
    c.path = "Ambi_3_1";
    c.parameters = music_has_path;
    p::execute(c);
    c.stop = true;
    p::execute(c);
    c = {};
    c.path = "Bgm_13_1_Loop";
    c.parameters = music_has_path;
    p::execute(c);
    int new_starts = starts;
    p::update();
    assert(starts == new_starts && p::checkpoint().key != saved_bgm.key);
    p::restore(saved_ring);
    assert(p::checkpoint().key == saved_bgm.key && gain == prior_gain && !loop_enabled);
    c = {};
    c.path = "Ambi_3_1";
    c.parameters = music_has_path;
    c.stop = true;
    p::execute(c);
    p::update();
    assert(gain == prior_gain && !loop_enabled && p::checkpoint().key == saved_bgm.key);
    // Saving music retains its cue/settings; loading always starts fresh.
    for (const char* alias : {"Bgm_6_1", "Bgm_2_1_Intro"})
    {
        p::reset();
        c = {};
        c.path = alias;
        c.parameters = music_has_path;
        p::execute(c);
        order = !strcmp(alias, "Bgm_2_1_Intro") ? 3 : 5;
        auto saved = p::checkpoint();
        int old_starts = starts;
        p::restore(saved);
        assert(starts == old_starts + 1 && intro && order == 0);
    }
    // Side-chain gain affects only the final output, never the scripted volume,
    // playback position, fade progress or the save checkpoint.
    p::reset();
    p::play_test_track(false);
    int duck_starts = starts;
    auto unattenuated = p::checkpoint();
    int before_order = order;
    generation = p::generation();
    for (int i = 0; i < 4; ++i)
        p::update(true);
    assert(gain == 960 * 724 / 1024 && starts == duck_starts && p::generation() == generation);
    assert(p::checkpoint().volume == unattenuated.volume && order == before_order && !p::busy());
    p::pause();
    int frozen_gain = gain;
    for (int i = 0; i < 20; ++i)
        p::update(false);
    assert(gain == frozen_gain);
    p::resume();
    for (int i = 0; i < 5; ++i)
        p::update(false);
    assert(gain == frozen_gain);
    for (int i = 0; i < 18; ++i)
        p::update(false);
    assert(gain == 960 && starts == duck_starts);
    for (int i = 0; i < 4; ++i)
        p::update(true);
    c = {};
    c.volume = 512;
    c.duration_frames = 10;
    c.parameters = music_has_volume;
    p::execute(c);
    for (int i = 0; i < 10; ++i)
        p::update(true);
    assert(!p::busy() && p::checkpoint().volume == 512 && gain == 480 * 724 / 1024);
    for (int i = 0; i < 23; ++i)
        p::update(false);
    assert(gain == 480);
    c.stop = true;
    c.duration_frames = 3;
    p::execute(c);
    assert(!p::checkpoint().key);
    for (int i = 0; i < 3; ++i)
        p::update(true);
    assert(!p::playing() && !p::busy());
    for (int i = 0; i < 23; ++i)
        p::update(false);
    std::puts("Music player passed: exact cue/version, Intro/Loop entry, in-place modification, zero-volume retention, "
              "fade/pause/master gain, targeted stop and unmade reporting");
}
