#include "sfx_player.h"
#include "sfx_backend.h"
#include "sfx_data.h"
#include "save_archive.h"
#include "story_vm.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
namespace {
struct sound {bool active=false;int volume=0,sample=0,rate=0;};
sound voices[4096];int next=1,started=0,peak=0;
int count(){int n=0;for(auto& v:voices)n+=v.active;return n;}
void tick(int n){while(n--)manosaba::sfx_player::update();}
void play(const char* alias,bool loop=false,int fade=0){manosaba::sfx_command c;c.cue=manosaba::sfx_player::find(alias);assert(c.cue>=0);c.loop=loop;c.fade_frames=fade;manosaba::sfx_player::execute(c);}
void stop(const char* alias,bool ambient){manosaba::sfx_command c;c.cue=alias ? manosaba::sfx_player::find(alias) : -1;c.stop=true;c.ambient=ambient;manosaba::sfx_player::execute(c);}
}
namespace manosaba::sfx_backend {
uint16_t play(int sample,int volume,int rate){assert(next<4096);int id=next++;voices[id]={true,volume,sample,rate};++started;int n=count();assert(n<=2);if(n>peak)peak=n;return id;}
void stop(uint16_t h){voices[h].active=false;}
bool active(uint16_t h){return voices[h].active;}
void volume(uint16_t h,int v){assert(v>=0 && v<=1024);voices[h].volume=v;}
}
int main()
{
    using namespace manosaba;
    sfx_player::reset();play("Ambi_4_1",true);assert(count()==1 && !sfx_player::foreground_active());int before=started;
    tick(900);assert(started==before && count()==1); // The loop never re-submits at its boundary.
    play("Ambi_8_1",true);assert(count()==2);stop("Ambi_4_1",true);assert(count()==1);assert(sfx_player::current_cue()==sfx_player::find("Ambi_8_1"));
    play("Sfx_Common_1");play("Sfx_Common_2");assert(count()==2);tick(100);assert(count()>=1);stop(nullptr,false);assert(sfx_player::current_cue()==sfx_player::find("Ambi_8_1"));stop(nullptr,true);assert(count()==0);
    for(const char* name:{"Sfx_49","Sfx_51","Sfx_71","Sfx_173","Sfx_33","Sfx_40","Sfx_57"})
    {
        sfx_player::reset();int id=sfx_player::find(name),old=started;const auto& cue=sfx_cues[id];play(name);tick(cue.duration+2);assert(started-old==cue.count);assert(count()==0);
    }
    sfx_player::reset();play("Sfx_154");assert(count()==1 && voices[next-1].volume==0);tick(60);assert(voices[next-1].volume==1024);tick(sfx_cues[sfx_player::find("Sfx_154")].duration);assert(count()==0);
    play("Ambi_11_1",true,60);tick(29);assert(voices[next-1].volume>400 && voices[next-1].volume<600);
    uint8_t bytes[18];auto saved=sfx_player::checkpoint();save_archive out(bytes,sizeof(bytes),false);saved.archive(out);assert(out.good() && out.size()==18);
    sfx_player::reset();sfx_checkpoint restored;save_archive in(bytes,out.size(),true);restored.archive(in);sfx_player::restore(restored);
    assert(in.good() && in.size()==out.size() && count()==1 && voices[next-1].volume==1024);
    sfx_player::reset();play("Ambi_4_1",true);play("Ambi_8_1",true);play("Sfx_Common_1");play("Sfx_Common_2");
    saved=sfx_player::checkpoint();assert(saved.sounds[0].cue>=0 && saved.sounds[1].cue>=0);
    for(const auto& s:saved.sounds)assert(sfx_cues[s.cue].ambient);
    sfx_player::restore(saved);assert(count()==2);
    uint8_t legacy[128];save_archive old(legacy,sizeof(legacy),false);
    for(const char* name:{"Sfx_Common_1","Sfx_Common_2","Ambi_4_1", "", "", ""})
    {
        int cue=*name ? sfx_player::find(name) : -1;old.field(cue);if(cue<0)continue;
        int gain=800,left=0,target=800;bool loop=true,stopping=false;
        old.field(gain);old.field(loop);old.field(left);old.field(target);old.field(stopping);
    }
    save_archive old_reader(legacy,old.size(),true);restored.legacy_archive(old_reader);assert(old_reader.good());
    for(const auto& sound:restored.sounds)assert(sound.cue>=0 && !sfx_cues[sound.cue].ambient);
    sfx_player::reset();play("Ambi_1_1",true);assert(sfx_player::find("Sfx_87")==-1);assert(count()==0);
    play("Sfx_System_Move_001");assert(!sfx_player::foreground_active());
    sfx_player::reset();play("Sfx_Common_1");assert(sfx_player::foreground_active());
    stop(nullptr,false);assert(!sfx_player::foreground_active());
    sfx_command muted;muted.cue=sfx_player::find("Sfx_Common_1");muted.volume=0;
    sfx_player::execute(muted);assert(!sfx_player::foreground_active());sfx_player::reset();
    play("Ambi_11_1",true);play("Sfx_Common_1");
    sfx_command boundary;boundary.stop=boundary.loops_only=true;sfx_player::execute(boundary);
    assert(count()==1 && sfx_player::current_cue()==sfx_player::find("Sfx_Common_1"));
    int after_boundary=started;tick(900);assert(count()==0 && started==after_boundary);
    // A deferred loop without a live voice must also be removed.
    play("Ambi_11_1",true);play("Sfx_Common_1");play("Sfx_Common_2");
    sfx_player::execute(boundary);stop(nullptr,false);tick(900);assert(count()==0);
    play("Sfx_Common_1",true);sfx_player::execute(boundary);assert(count()==0);
    // Walk the real prologue across ADV01 -> ADV02 through the production VM.
    story_vm vm;int first=0;
    while(std::strcmp(story_scripts[first].path,"Act01_Chapter01/Act01_Chapter01_Adv01"))++first;
    vm.bind_sfx([](void*,const sfx_command& c){sfx_player::execute(c);},nullptr);
    vm.bind_music([](void*,const music_command& c){
        if(c.path && !std::strcmp(c.path,"Ambi_11_1"))play(c.path,c.loop);
        return false;
    },nullptr);
    vm.start(first);bool heard=false;
    for(int i=0;i<200 && vm.script()==first;++i)
    {
        if(sfx_player::current_cue()==sfx_player::find("Ambi_11_1"))heard=true;
        if(heard)assert(count()==1);
        vm.advance();
    }
    assert(heard && vm.script()!=first && count()==0);
    after_boundary=started;tick(900);assert(count()==0 && started==after_boundary);
    printf("PASS: two-voice limit, native loop continuity, independent shared aliases, 69 sequence notes, envelopes, stop groups, script loop cleanup and save restore; peak=%d\n",peak);
}
