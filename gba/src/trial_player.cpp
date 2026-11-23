#include "video_upload.h"
#include "trial_player.h"
#include "trial_ui_data.h"
#include "trial_title_data.h"
#include "trial_runtime.h"
#include "story_ui_data.h"
#include "choice_window.h"
#include "book_theme.h"
#include "bn_keypad.h"
#include "bn_core.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include <cstring>
#include "save_archive.h"
#include "trial_special_runtime.h"
extern "C" {BN_DATA_EWRAM_BSS trial_runtime_profile trial_runtime={};}
namespace manosaba {

namespace {
int lines(const char* text){int count=1;for(;*text;++text)count+=*text=='\n';return count;}
int width(const char* first,const char* last)
{
    int value=0;while(first<last){unsigned c=uint8_t(*first);value+=c<128 ? 6 : 12;first+=c<128 ? 1 : c<224 ? 2 : c<240 ? 3 : 4;}return value;
}
const ui_picture* proof(int id)
{
    if(id<=0)return nullptr;
    const char* key=story_items[id].id;
    for(int i=0;i<trial_evidence_count;++i)if(bn::string_view(key)==trial_evidence_pictures[i].id)return &trial_evidence_pictures[i].image;
    return nullptr;
}
void centered(text_canvas& canvas,int left,int right,int top,const char* text,text_canvas::ink ink,bool shadow)
{
    while(*text)
    {
        const char* end=text;while(*end && *end!='\n')++end;
        text_canvas::cursor cursor{(left+right-width(text,end))/2,top};
        canvas.text(cursor,dialogue_layout{left,top,right,top+13},text,end,ink,shadow);
        text=*end ? end+1 : end;top+=16;
    }
}
}
void trial_player::reset()
{
    _scene.reset();_movie.reset();_reenact_stage.reset();_map_stage.reset();_choice=_first_choice=_observed_evidence=_preview=_resume_movie=_selector_row=0;
    _prepared=_preview_ready=false;_full=_hud=true;_exited=false;_dialogue.reset();_advance.reset();
    _canvas_blank=_keep_canvas=_pause_repaint=_choice_loading=_base_prepared=false;
    _title_frame=_title_change=_title_stage=0;
    _reenact_started=_settled_reenact=false;_reenact_wait=0;
    _return_page={};
    _map_settle=false;_pictures=_picture_canvas=false;_picture_bank=_picture_event=-1;
}
void trial_player::archive(save_archive& a)
{
    if(a.reading())reset();
    if(a.reading())_map_settle=true;
    a.field(_picture_bank);a.field(_picture_event);_images.archive(a);
    if(a.reading())_pictures=_images.has_content();
    a.field(_choice);a.field(_first_choice);a.field(_observed_evidence);a.field(_preview);
    _dialogue.archive(a,_vm.text());_return_page.archive(a,_vm.text());
}
void trial_player::restored()
{
    _picture_bank=_vm.graphical_bank();_picture_event=_vm.state()==story_state::stage ? _vm.stage_event() : -1;_pictures=_images.has_content();_map_settle=true;
    _settled_reenact=_reenact_started=true;_reenact_wait=0;
    _dialogue.begin(_vm.text(),layout());_dialogue.finish();
}
bool trial_player::show_pictures() const
{
    auto state=_vm.state();
    return _pictures && !_preview && !_vm.reenact() && _vm.map_segment()<0 &&
        (state==story_state::stage || state==story_state::text || state==story_state::debate || state==story_state::pause ||
         state==story_state::running || state==story_state::camera || state==story_state::screen || state==story_state::audio);
}
bool trial_player::rebuild_ready(bool display_ready)
{
    if(show_pictures()){_images.update(0);return display_ready && _images.ready() && _prepared && !_dialogue.redrawing();}
    if(_scene.active())_scene.update();
    if(_movie.active())_movie.update(false);
    if(_reenact_stage.active())_reenact_stage.update(false,0);
    if(_vm.map_segment()>=0 || _map_stage.active())return display_ready && _map_stage.ready() && _prepared && !_dialogue.redrawing();
    if(!display_ready)return false;
    if(_preview)return _preview_ready;
    if(_vm.state()==story_state::title)return _title_stage==3;
    if(_vm.state()==story_state::cutin)return _movie.active() && _movie.ready();
    if(_vm.reenact())return _prepared && (_vm.reenact_scene()<0 || _reenact_stage.ready()) && !_dialogue.redrawing();
    return _prepared && (!_scene.active() || _scene.ready()) && !_dialogue.redrawing();
}
bool trial_player::rebuild_draw() const
{
    if(_preview)return !_preview_ready;
    if(_vm.state()==story_state::title)return _title_stage<3;
    if(_vm.state()==story_state::cutin)return !_prepared;
    return !_prepared || _full || _dialogue.redrawing();
}
void trial_player::suspend()
{
    if(_movie.active()){_resume_movie=_movie.frame();_prepared=false;}
    if(_vm.state()==story_state::title)_title_stage=0;
    _scene.visible(false);_movie.visible(false);_full=true;_base_prepared=false;_dialogue.repaint();_advance.reset();
    _reenact_stage.visible(false);
    _map_stage.suspend();_images.set_visible(false);_picture_canvas=false;
    _pause_repaint=_scene.active();
    if(_vm.state()==story_state::inventory || _vm.state()==story_state::menu){_prepared=false;_selector_row=0;_choice_loading=false;}
}
void trial_player::progressed()
{
    _reenact_started=_settled_reenact=false;_reenact_wait=0;
    _resume_movie=0;_base_prepared=false;
    _title_frame=_title_change=_title_stage=0;
    _choice=_first_choice=_selector_row=0;_prepared=_choice_loading=false;_dialogue.reset();_full=_hud=true;_keep_canvas=false;_bar=_seconds=-1;_advance.reset();
    if(_vm.evidence()!=_observed_evidence)
    {
        _observed_evidence=_vm.evidence();if(_observed_evidence && proof(_observed_evidence)){_preview=_observed_evidence;_preview_ready=false;}
    }
}
const dialogue_layout& trial_player::layout() const {return _vm.state()==story_state::debate ? _vm.debate_layout() : graphical_dialogue;}
bool trial_player::update(bool display_ready)
{
    _dialogue.speed(_vm.state()==story_state::debate ? 1 : _dialogue_speed);
    int frames=1+bn::core::last_missed_frames();
    if(_picture_bank!=_vm.graphical_bank())
    {_images.reset();_picture_bank=_vm.graphical_bank();_picture_event=-1;_pictures=false;_images.set_bank(_picture_bank,false);_images.request(0);}
    if(_picture_bank>=0)
    {
        bool running=_vm.state()!=story_state::menu && _vm.state()!=story_state::inventory && !_preview;
        _images.update(running ? frames : 0,bn::keypad::a_pressed());
        for(int n=0;n<8 && _vm.state()==story_state::stage;++n)
        {
            if(_picture_event<0){_picture_event=_vm.stage_event();_images.apply(_picture_event);}
            _pictures=_images.has_content();
            if(_images.blocked())return true;
            _picture_event=-1;_vm.advance();progressed();
        }
        bool pictures=_images.has_content();
        if(pictures!=_pictures){_pictures=pictures;_prepared=_base_prepared=false;_full=true;_dialogue.repaint();}
        if(!_pictures || !running){_images.set_visible(false);_picture_canvas=false;}
        if(_vm.state()==story_state::stage)return true;
    }
    trial_special_runtime.picture=unsigned(_pictures);

    if(_map_stage.active())_map_stage.update(display_ready,frames);
    if(_map_stage.closed())
    {_map_stage.reset();_prepared=_base_prepared=false;_full=true;}
    if(_vm.reenact() && _reenact_stage.active())_reenact_stage.update(display_ready,frames);
    trial_runtime.state=unsigned(_vm.state());trial_runtime.script=unsigned(_vm.script());trial_runtime.actor=unsigned(_vm.trial_actor());trial_runtime.view=unsigned(_vm.trial_view());
    trial_runtime.line_left=_vm.statement_left();trial_runtime.duration=_vm.statement_duration();trial_runtime.total_left=_vm.trial_left();trial_runtime.hp=unsigned(_vm.hp());trial_runtime.links=unsigned(_vm.link_count());
    trial_runtime.choice=unsigned(_choice);trial_runtime.items=unsigned(_vm.inventory_count());trial_runtime.chosen=unsigned(_vm.chosen_item());trial_runtime.preview=unsigned(_preview);
    trial_runtime.scene_ready=show_pictures() ? _images.ready() : _scene.ready();trial_runtime.canvas_ready=display_ready;trial_runtime.movie_frame=unsigned(_movie.frame());++trial_runtime.frame;
    trial_runtime.movie_kind=unsigned(_vm.cutin_kind());
    trial_runtime.layout=unsigned(_vm.trial_layout());trial_runtime.pose=_vm.trial_actor()==255 ? 65535 : _vm.trial_poses()[_vm.trial_actor()];trial_runtime.background=unsigned(_scene.background()+1);
    trial_runtime.window_x=unsigned(_scene.window_x());
    trial_runtime.missed+=unsigned(bn::core::last_missed_frames());trial_runtime.core_ticks=unsigned(bn::core::last_cpu_ticks());trial_runtime.max_ticks=bn::max(trial_runtime.max_ticks,trial_runtime.core_ticks);
    trial_runtime.camera=unsigned(_vm.trial_camera()+1);trial_runtime.camera_frame=_vm.camera_frame();trial_runtime.camera_duration=_vm.camera_duration();
    trial_runtime.title_kind=unsigned(_vm.cutin_kind());trial_runtime.title_frame=unsigned(_title_frame);trial_runtime.title_stage=unsigned(_title_stage);
    if(_vm.reenact())
    {
        trial_runtime.scene_ready=_reenact_stage.ready();trial_runtime.background=unsigned(_vm.reenact_scene()+1);
        trial_runtime.movie_frame=unsigned(_reenact_stage.clock());
    }
    if(show_pictures() && display_ready && _images.ready())_vm.tick_camera(unsigned(frames));
    bool camera_waiting=_vm.camera_wait()!=0;
    auto state=_vm.state();
    if(!_preview && display_ready && _prepared && !_full && !_dialogue.redrawing() && _scene.ready() &&
       (state==story_state::text || state==story_state::debate || state==story_state::camera || state==story_state::screen))
        _vm.tick_shake(unsigned(frames));
    _scene.shake(_vm.shake_x(),_vm.shake_y());
    bool actor_shaking=_vm.trial_actor()==_vm.shake_actor() && _vm.actor_shake_left();
    _scene.actor_shake(actor_shaking ? _vm.actor_shake_x() : 0,actor_shaking ? _vm.actor_shake_y() : 0);
    trial_special_runtime.shake_actor=actor_shaking ? unsigned(_vm.shake_actor()+1) : 0;
    if(_scene.active())
    {
        if(!show_pictures() && _vm.trial_camera()>=0 && !_preview && (_vm.state()==story_state::text || _vm.state()==story_state::debate || _vm.state()==story_state::camera || _vm.state()==story_state::screen))
        {
            _scene.request_pan(_vm.trial_camera(),_vm.camera_frame(),_vm.witch_hiro(),_vm.trial_poses(),_vm.trial_actor());
            if(_scene.ready())_vm.tick_camera(1+bn::core::last_missed_frames());
        }
        _scene.update();trial_runtime.camera_scroll=unsigned(_scene.scroll());
    }
    // Full-screen evidence owns input while the underlying VM is suspended,
    // including camera waits whose timer resumes only after this overlay closes.
    if(_preview)
    {
        bool readable=display_ready && _preview_ready;
        trial_runtime.interaction_ready=readable;
        bool action=_advance.update(bn::keypad::a_held(),readable,1+bn::core::last_missed_frames());
        if(readable && (action || bn::keypad::b_pressed()))
        {
            _preview=0;_preview_ready=false;
            if(_vm.state()==story_state::inventory || _vm.state()==story_state::menu){_prepared=false;_selector_row=0;}
            _full=true;_advance.reset();return true;
        }
        return _full;
    }
    bool screen_ready=display_ready && (!show_pictures() || _images.ready()) && _prepared && !_full && !_dialogue.redrawing() &&
        (!_scene.active() || _scene.ready()) && (!_vm.reenact() || _vm.reenact_scene()<0 || _reenact_stage.ready());
    if(screen_ready)_vm.tick_screen(frames);
    if(_vm.state()==story_state::screen)
    {
        trial_runtime.interaction_ready=0;
        if(screen_ready && !_vm.screen_waiting()){_vm.advance();progressed();return true;}
        return !_prepared || _full || _dialogue.redrawing();
    }
    if(_vm.state()==story_state::reenact)
    {
        trial_runtime.interaction_ready=0;
        if(!display_ready || (_vm.reenact_scene()>=0 && (_reenact_stage.scene()!=_vm.reenact_scene() || !_reenact_stage.ready())))return true;
        if(!_reenact_started)
        {
            _reenact_started=true;_reenact_wait=_vm.reenact_wait();
            if(_vm.reenact_action()==0)_reenact_stage.open();
            else if(_vm.reenact_action()==1)_reenact_stage.close();
            return true;
        }
        if(_vm.reenact_action()==2)_reenact_wait=bn::max(0,_reenact_wait-frames);
        if((_vm.reenact_action()==2 && !_reenact_wait) || (_vm.reenact_action()!=2 && !_reenact_stage.transitioning()))
        {_vm.advance();progressed();}
        return true;
    }
    if(_vm.state()==story_state::camera)
    {
        if(_scene.ready() && !camera_waiting)
        {_vm.advance();progressed();return true;}
        return _full;
    }
    if(_vm.state()==story_state::cutin && _movie.active() && _prepared)
    {
        if(_movie.update(display_ready)){_vm.advance();progressed();return true;}
        return false;
    }
    if(_vm.state()==story_state::title)
    {
        trial_runtime.interaction_ready=0;
        if(_title_stage<3)return true;
        const auto& sequence=trial_titles[_vm.cutin_kind()];
        if(_title_change<sequence.frames[_title_frame].count)return true;
        if(!display_ready)return false;
        if(_vm.cutin_kind()==2)_vm.tick_screen(1);
        if(++_title_frame==sequence.count){_vm.advance();progressed();return true;}
        _title_change=0;return true;
    }
    if(_vm.state()==story_state::map)
    {
        bool done=_vm.map_segment()>=0 ? _map_stage.done() : !_map_stage.active();
        if(_prepared && (!_vm.map_wait() || done)){_vm.advance();progressed();return true;}
        return true;
    }
    // The base pass and the text pass are separate frames. Finish the pending
    // text pass even when A already completed the typewriter and tick is idle.
    if(_map_stage.active() && !_map_stage.ready())return true;
    if(_dialogue.redrawing() || _full){trial_runtime.interaction_ready=0;return true;}
    bool readable=display_ready && (!show_pictures() || _images.ready()) && (!_scene.active() || _scene.ready()) && (!_map_stage.active() || _map_stage.ready()) && (!_vm.reenact() || _vm.reenact_scene()<0 || _reenact_stage.ready()) && _prepared;
    trial_runtime.interaction_ready=readable;
    _dialogue.frame(readable);
    bool action=_advance.update(bn::keypad::a_held(),readable,1+bn::core::last_missed_frames());
    if(!readable)return _full;
    if(_full && _canvas_blank)return true;
    if(_vm.state()==story_state::failure || _vm.state()==story_state::end)
    {
        if(action){_exited=true;return true;}return false;
    }
    if(_vm.state()==story_state::running){_vm.advance();progressed();return true;}
    if(_vm.state()==story_state::menu)
    {
        int count=_vm.option_count();int before=_choice;
        if(bn::keypad::up_pressed())_choice=(_choice+count-1)%count;
        else if(bn::keypad::down_pressed())_choice=(_choice+1)%count;
        if(bn::keypad::b_pressed())
        {
            if(_vm.objection_menu())
            {
                for(int i=0;i<count;++i)if(_vm.option(i).value&128){_vm.choose(i);progressed();return true;}
            }
        }
        if(action)
        {
            _vm.choose(_choice);
            progressed();return true;
        }
        return _choice!=before;
    }
    if(action && _dialogue.complete() && !_dialogue.can_advance())action=false;
    if(_vm.state()==story_state::debate)
    {
        for(int point=0;point<_vm.link_count();++point)
        {
            bool pressed=point==0 ? bn::keypad::up_pressed() : point==1 ? bn::keypad::left_pressed() : bn::keypad::right_pressed();
            if(pressed && _vm.link(point).first>=_dialogue.page().start() && _vm.link(point).last<=_dialogue.page().visible())
            {_return_page=_dialogue.page();_vm.object(point);progressed();return true;}
        }
        if(action && _dialogue.complete())
        {
            if(!_vm.debate_stopped()){_vm.skip_debate();progressed();return true;}
            action=false;
        }
        if(_vm.tick_debate(1+bn::core::last_missed_frames())){progressed();return true;}
        if(action){_dialogue.finish();_full=_keep_canvas=true;return true;}
        int bar=int(_vm.statement_left()*240/_vm.statement_duration()),seconds=int((_vm.trial_left()+59)/60);
        _hud=_hud || bar!=_bar || seconds!=_seconds;
        if(_dialogue.complete() && _vm.text()[_dialogue.page().end()]){_dialogue.next_page(_vm.text(),layout());_full=true;return true;}
    }
    else if(action)
    {
        auto result=_dialogue.advance(_vm.text(),layout());
        if(result==dialogue_advance::script){_vm.advance();progressed();}
        else {_full=true;_keep_canvas=result==dialogue_advance::finish;}
        return true;
    }
    return _dialogue.tick() || _hud;
}
void trial_player::dialogue(text_canvas& canvas)
{
    bool debate=_vm.state()==story_state::debate;
    _dialogue.draw(canvas,_vm.text(),layout(),_full,24,debate ? &_vm.link(0) : nullptr,debate ? _vm.link_count() : 0,_vm.text_style());
}
void trial_player::render(text_canvas& canvas)
{
    if(show_pictures()){if(video_upload::finishing || bn::core::current_cpu_ticks()<2100)_images.render(canvas);_images.tint(_vm.screen_color());}
    _scene.heat(_vm.court_fire() && !_vm.reenact() && !_preview && _vm.state()!=story_state::menu && _vm.state()!=story_state::inventory,
                _prepared && !_full);
    auto effect=_vm.screen_color();
    _scene.tint(_movie.active() ? _movie.background_effect(effect) : effect);
    _movie.tint(effect);_reenact_stage.tint(effect);
}
void trial_player::hud(text_canvas& canvas,bool force)
{
    if(!_vm.trial_left())return;
    int seconds=int((_vm.trial_left()+59)/60);
    if(force || _full || seconds!=_seconds)
    {
        const uint8_t* inks=_vm.state()==story_state::menu && !_vm.objection_menu() ? trial_clock_choice_inks : trial_label_inks;
        canvas.clock(seconds,trial_clock,inks);
        _seconds=seconds;
    }
    if(_vm.state()==story_state::debate)
    {
        int bar=int(_vm.statement_left()*240/_vm.statement_duration());
        if(_full || bar!=_bar)
        {
            // Keep the protagonist's timer independent of pink objection links
            // and the current speaker's nameplate palette entry.
            uint16_t color=hiro_book_theme(_vm) ? uint16_t(27|7<<5|11<<10) : uint16_t(31|16<<5|21<<10);
            canvas.palette_color(text_canvas::warning,color);
            canvas.rectangle(0,158,240,2,text_canvas::background);canvas.rectangle(0,158,bar,2,text_canvas::warning);_bar=bar;
        }
    }
    _hud=false;
}
void trial_player::choices(text_canvas& canvas)
{
    unsigned begin=unsigned(bn::core::current_cpu_ticks());
    if(!_vm.objection_menu())
    {
        if(!_choice_loading)
        {
            canvas.choice_screen();_choice_loading=true;_selector_row=0;_prepared=false;_full=true;
            trial_runtime.ui_work=unsigned(bn::core::current_cpu_ticks())-begin;return;
        }
        int heights[8];
        for(int i=0;i<_vm.option_count();++i)heights[i]=40+(lines(_vm.option(i).text)-1)*16;
        auto window=visible_choices(heights,_vm.option_count(),_choice,_first_choice,0,144);_first_choice=window.first;
        trial_runtime.choice_first=unsigned(window.first);trial_runtime.choice_rows=unsigned(window.count);
        int y=(168-window.height)/16*8;
        int end=bn::min(window.count,_selector_row+1);
        for(int row=0;row<end;++row)
        {
            int i=window.first+row;
            if(row==_selector_row)canvas.choice_button(y,_vm.option(i).text,i==_choice,false,heights[i]);
            y+=heights[i];
        }
        _selector_row=end;
        if(end==window.count){_prepared=true;_full=_choice_loading=false;}
        trial_runtime.ui_work=unsigned(bn::core::current_cpu_ticks())-begin;return;
    }
    int who=1;for(int i=0;i<_vm.option_count();++i)if(!(_vm.option(i).value&128)){who=(_vm.option(i).value&32) ? 2 : 1;break;}
    if(!canvas.picture(trial_choice_pictures[who]))return;
    int x=8;
    int heights[8];for(int i=0;i<_vm.option_count();++i)heights[i]=(_vm.option(i).value&128) ? 24 : 16*(lines(_vm.option(i).text)+1);
    auto window=visible_choices(heights,_vm.option_count(),_choice,_first_choice,16,120);_first_choice=window.first;
    trial_runtime.choice_first=unsigned(window.first);trial_runtime.choice_rows=unsigned(window.count);
    int y=bn::max(32,(172-window.height)/16*8);
    bool clearing=_prepared;
    if(_prepared)
    {
        int old_y=_painted_y;
        for(int i=_painted_first;i<_painted_first+_painted_count;++i)
        {
            bool cancel=_vm.option(i).value&128;int height=heights[i];
            if(cancel)canvas.restore(48,old_y,128,24);
            else {canvas.restore(8,old_y,168,height);canvas.restore(16,old_y-16,40,32);canvas.restore(0,old_y+height/2-8,8,16);}
            old_y+=height+16;
        }
    }
    _painted_first=window.first;_painted_y=y;_painted_count=window.count;
    if(!_choice_loading)
    {_choice_loading=true;_selector_row=0;_prepared=false;_full=true;}
    // Retire the old rows first, then draw one bubble and its label per frame.
    if(clearing){trial_runtime.ui_work=unsigned(bn::core::current_cpu_ticks())-begin;return;}
    int first=_selector_row,end=bn::min(window.count,first+1);
    for(int row=0;row<end;++row)
    {
        int i=window.first+row;
        const auto& option=_vm.option(i);int height=heights[i];bool cancel=option.value&128;
        if(row<first){y+=height+16;continue;}
        int left=x+(cancel ? 48 : 0),right=x+168;
        if(cancel)canvas.blit(left,y,120,24,trial_cancel_bubbles[int(i==_choice)]);
        else {int size=(height-32)/16;canvas.blit(left,y,168,height,trial_bubbles[size*2+int(i==_choice)]);int badge=option.value&31;canvas.category_label(x+8,y-16,trial_badges[badge],trial_label_inks,trial_badge_colors[badge]);}
        centered(canvas,left+12,right-12,y+(cancel ? 4 : 8),option.text,i==_choice ? text_canvas::white : text_canvas::dark,i==_choice);
        if(i==_choice)canvas.text(left-7,y+height/2-6,">",text_canvas::white,left+5,y+height);
        y+=height+16;
    }
    if(_choice_loading)
    {
        _selector_row=end;
        if(end<window.count){trial_runtime.ui_work=unsigned(bn::core::current_cpu_ticks())-begin;return;}
    }
    _painted_choice=_choice;_prepared=true;_full=_choice_loading=false;
    trial_runtime.ui_work=unsigned(bn::core::current_cpu_ticks())-begin;
}
void trial_player::draw(text_canvas& canvas)
{
    _dialogue.speed(_vm.state()==story_state::debate ? 1 : _dialogue_speed);
    // A VM cache miss can decode 4 KiB before this pass. Leave enough time
    // for the bounded UI step and VBlank uploads; retry with the canvas intact.
    if(!video_upload::finishing && (_full || !_prepared) && bn::core::current_cpu_ticks()>1400)return;
    if(!show_pictures()){_images.set_visible(false);_picture_canvas=false;}
    if(_vm.map_segment()>=0 || _map_stage.active() || _vm.state()==story_state::map)
    {
        if(!_scene.release() || !_movie.release() || !_reenact_stage.release())return;
        if(_vm.map_segment()>=0)_map_stage.request(_vm.map_segment(),_map_settle);
        else if(_map_stage.active())_map_stage.close();
        _map_settle=false;
        if(_map_stage.active())
        {
            _map_stage.draw(canvas);
            if(!_map_stage.ready())return;
        }
        if(_vm.state()==story_state::map){_prepared=true;_full=false;return;}
        if(!_prepared){if(!_dialogue.prepared())_dialogue.begin(_vm.text(),layout());_prepared=true;}
        if(_full)canvas.decoration(story_ui_tiles);
        dialogue_presenter::speaker(canvas,_vm.speaker());dialogue(canvas);
        if(!_dialogue.redrawing())_full=false;
        return;
    }
    if(!_vm.reenact() && !_reenact_stage.release())return;
    if(_preview)
    {
        if(!_scene.release() || !_movie.release() || !_reenact_stage.release())return;
        const auto* image=proof(_preview);
        if(!canvas.picture(*image))return;
        if(*story_items[_preview].label)canvas.text(12,1,story_items[_preview].label,text_canvas::white,228,14);
        _preview_ready=true;_full=false;return;
    }
    if(_vm.state()==story_state::title)
    {
        if(!_movie.release())return;
        if(!_scene.active())
        {
            canvas.release();video_upload::reclaim();_scene.init();
            _scene.request(_vm.trial_actor(),_vm.trial_view(),story_scripts[_vm.script()].mode==2,_vm.trial_layout(),_vm.trial_poses());
        }
        _scene.visible(true);
        if(!_scene.ready())return;
        const auto& sequence=trial_titles[_vm.cutin_kind()];
        if(!_title_stage){canvas.title_begin(_vm.cutin_kind()==2);_title_stage=1;return;}
        if(_title_stage==1)
        {canvas.title_restore(sequence,_title_frame);_title_change=sequence.frames[_title_frame].count;_title_stage=2;return;}
        if(_title_stage==2)
        {if(canvas.ready()){canvas.title_show();_title_stage=3;}_canvas_blank=false;return;}
        const auto& frame=sequence.frames[_title_frame];
        int count=bn::min(128,int(frame.count)-_title_change);
        canvas.title_changes(frame,_title_change,count);_title_change+=count;return;
    }
    if(_vm.state()==story_state::cutin)
    {
        if(!_prepared || !_movie.active())
        {
            if(!_movie.release())return;
            if(!_scene.active())
            {canvas.release();video_upload::reclaim();_scene.init();canvas.trial_screen();_canvas_blank=true;}
            _scene.request_background(_vm.trial_view(),story_scripts[_vm.script()].mode==2,_vm.trial_layout());
            _scene.visible(true);
            if(!_scene.release_actors() || !_scene.ready())return;
            // Retain the courtroom BG; ordinary actors relinquish OBJ memory first.
            if(!_canvas_blank)canvas.trial_screen();
            _canvas_blank=true;
            _movie.init(_vm.cutin_kind(),_vm.cutin_variant(),_resume_movie);
            _resume_movie=0;_prepared=true;_full=false;
        }
        _movie.visible(true);
        return;
    }
    if(!_movie.release())return;
    if(show_pictures())
    {
        if(!_scene.release() || !_reenact_stage.release())return;
        if(!_picture_canvas){canvas.release();canvas.trial_screen();_picture_canvas=true;return;}
        _images.set_visible(true);
        if(!_images.ready())return;
        if(!_prepared){if(!_dialogue.prepared())_dialogue.begin(_vm.text(),layout());_prepared=true;}
        if(_full && !_base_prepared)
        {
            canvas.trial_screen();
            if(_vm.state()==story_state::text && !_vm.simple_trial_text())
            {canvas.frame_corners(story_ui_tiles);dialogue_presenter::speaker(canvas,_vm.speaker(),false);}
            _base_prepared=true;return;
        }
        if(_vm.state()==story_state::stage){_full=false;return;}
        dialogue(canvas);if(!_dialogue.redrawing()){hud(canvas);_full=_base_prepared=false;}
        return;
    }
    if(_vm.reenact() && (_vm.state()==story_state::text || _vm.state()==story_state::pause || _vm.state()==story_state::reenact || _vm.state()==story_state::screen || _vm.state()==story_state::running))
    {
        if(!_scene.release())return;
        if(!_base_prepared && _full)
        {canvas.clear(true);_base_prepared=true;_canvas_blank=false;}
        if(_vm.reenact_scene()>=0)
        {
            _reenact_stage.request(_vm.reenact_scene(),_settled_reenact || _vm.state()!=story_state::reenact,_settled_reenact && _vm.reenact_closed());
            _reenact_stage.visible(true);
            if(_reenact_stage.scene()!=_vm.reenact_scene() || !_reenact_stage.ready())return;
        }
        if(_vm.state()==story_state::reenact){_prepared=true;_full=false;return;}
        if(!_prepared)
        {
            if(!_dialogue.prepared())_dialogue.begin(_vm.text(),layout());
            else _dialogue.repaint();
            if(_vm.state()==story_state::screen)_dialogue.finish();
            _prepared=true;_full=true;
        }
        dialogue(canvas);if(_dialogue.redrawing())return;_full=_base_prepared=false;return;
    }
    if(!_reenact_stage.release())return;
    if(_vm.state()==story_state::menu || _vm.state()==story_state::inventory || _vm.state()==story_state::failure || _vm.state()==story_state::end)
    {
        if(!_scene.release())return;
        if(_vm.state()==story_state::menu)
        {
            bool repaint=_full || !_prepared;
            choices(canvas);
            if(_prepared)hud(canvas,repaint || !_vm.objection_menu());
        }

        else if(_vm.bad_end())
        {canvas.bad_end();_prepared=true;_full=false;}
        else if(canvas.picture(trial_choice_pictures[0]))
        {canvas.text(24,64,_vm.text(),text_canvas::white);canvas.text(72,144,"A 返回剧本",text_canvas::white);_prepared=true;_full=false;}
        return;
    }
    bool fresh=!_scene.active();
    if(fresh)
    {canvas.release();video_upload::reclaim();unsigned begin=unsigned(bn::core::current_cpu_ticks());_scene.init();trial_runtime.scene_init=unsigned(bn::core::current_cpu_ticks())-begin;canvas.trial_screen();_canvas_blank=true;_full=true;}
    if(_vm.trial_camera()>=0)_scene.request_pan(_vm.trial_camera(),_vm.camera_frame(),_vm.witch_hiro(),_vm.trial_poses(),_vm.trial_actor());
    else _scene.request(_vm.trial_actor(),_vm.trial_view(),story_scripts[_vm.script()].mode==2,_vm.trial_layout(),_vm.trial_poses());
    _scene.visible(true);
    if(fresh)return;
    if(_pause_repaint){canvas.trial_screen();_canvas_blank=true;_pause_repaint=false;return;}
    if(_vm.state()==story_state::camera)
    {if(_full)canvas.trial_screen();_prepared=true;_full=false;return;}
    if(!_prepared)
    {
        if(!_dialogue.prepared())
        {
            if(_vm.restored_debate() && _return_page.end())_dialogue.restore(_return_page);
            else {_dialogue.begin(_vm.text(),layout());if(_vm.restored_debate())_dialogue.finish();}
        }
        else _dialogue.repaint();
        if(_vm.state()==story_state::screen)_dialogue.finish();
        _prepared=true;_full=true;
    }
    if(_full && !_dialogue.redrawing() && !_base_prepared)
    {
        unsigned begin=unsigned(bn::core::current_cpu_ticks());
        if(!_canvas_blank && !_keep_canvas)canvas.trial_screen();
        _canvas_blank=false;
        trial_runtime.clear_work=unsigned(bn::core::current_cpu_ticks())-begin;
        if((_vm.state()==story_state::text || (_vm.state()==story_state::screen && *_vm.text())) && !_vm.simple_trial_text())
        {
            canvas.frame_corners(story_ui_tiles);
            dialogue_presenter::speaker(canvas,_vm.speaker(),false);
        }
        _base_prepared=true;return;
    }
    unsigned begin=unsigned(bn::core::current_cpu_ticks());dialogue(canvas);trial_runtime.text_work=unsigned(bn::core::current_cpu_ticks())-begin;
    if(_dialogue.redrawing())return;
    begin=unsigned(bn::core::current_cpu_ticks());if(_hud || _full)hud(canvas);trial_runtime.hud_work=unsigned(bn::core::current_cpu_ticks())-begin;_full=_keep_canvas=_base_prepared=false;
}
int trial_player::commit(int bytes)
{
    if(_pictures)bytes=_images.commit(bytes);
    if(_scene.active())bytes=_scene.commit(bytes);
    if(_movie.active())bytes=_movie.commit(bytes);
    if(_reenact_stage.active())bytes=_reenact_stage.commit(bytes);
    return bytes;
}
}
