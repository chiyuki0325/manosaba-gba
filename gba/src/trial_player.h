#ifndef TRIAL_PLAYER_H
#define TRIAL_PLAYER_H
#include "story_vm.h"
#include "trial_scene.h"
#include "trial_movie.h"
#include "reenact_scene.h"
#include "dialogue_presenter.h"
#include "text_canvas.h"
#include "trial_map_scene.h"
#include "image_scene.h"
namespace manosaba {
class trial_player
{
public:
    trial_player(story_vm& vm,image_scene& images) : _vm(vm),_images(images) {}
    void reset();
    void dialogue_speed(int value){_dialogue_speed=value;}
    void archive(save_archive& data);
    bool rebuild_ready(bool display_ready);
    bool rebuild_draw() const;
    void suspend();
    void external_progress(){progressed();}
    bool release() {return _scene.release() && _movie.release() && _reenact_stage.release();}
    bool update(bool display_ready);
    void draw(text_canvas& canvas);
    void render(text_canvas& canvas);
    int commit(int bytes);
    bool exited() const {return _exited;}
private:
    story_vm& _vm;
    image_scene& _images;
    int _picture_bank=-1,_picture_event=-1;
    bool _pictures=false,_picture_canvas=false;
    trial_scene _scene;
    trial_movie _movie;
    reenact_scene _reenact_stage;
    trial_map_scene _map_stage;
    dialogue_presenter _dialogue;
    dialogue_page _return_page;
    advance_button _advance;
    int _dialogue_speed=0;
    int _choice=0,_first_choice=0;
    int _painted_first=0,_painted_y=0,_painted_count=0,_painted_choice=0;
    int _selector_row=0;
    int _observed_evidence=0,_preview=0,_bar=-1,_seconds=-1,_resume_movie=0;
    int _title_frame=0,_title_change=0,_title_stage=0;
    int _reenact_wait=0;
    bool _reenact_started=false;
    bool _map_settle=false;
    bool _prepared=false,_full=true,_hud=true,_exited=false,_preview_ready=false;
    bool _canvas_blank=false;
    bool _base_prepared=false;
    bool _keep_canvas=false,_pause_repaint=false;
    bool _choice_loading=false;
    bool show_pictures() const;
    void progressed();
    const dialogue_layout& layout() const;
    void dialogue(text_canvas& canvas);
    void hud(text_canvas& canvas,bool force=false);
    void choices(text_canvas& canvas);
};
}
#endif
