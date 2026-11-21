#include "home_screen.h"
#include "build_config.h"
#include "home_art.h"
#include "home_runtime.h"
#include "text_canvas.h"
#include "save_system.h"
#include "music_player.h"
#include "sfx_ui.h"
#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_keypad.h"
#include "bn_format.h"
#include "bn_core.h"
extern "C" {BN_DATA_EWRAM_BSS home_runtime_profile home_runtime={};}
namespace manosaba {
void home_screen::open(int page,bool hiro,int selected)
{
 _page=page;_selected=selected;_paint=0;_dirty=_page ? 31 : 0;_shown=false;_advance.reset();
 if(!page){_hiro=hiro;_music=false;_fade=32;bn::bg_palettes::set_fade(hiro ? bn::color(31,31,31) : bn::color(0,0,0),1);}
 else {_fade=0;bn::bg_palettes::set_fade_intensity(0);}
}
void home_screen::title()
{
 _page=_selected=0;_paint=0;_dirty=0;_shown=false;_advance.reset();
}
void home_screen::repaint()
{
 _paint=0;_dirty=_page ? 31 : 0;_shown=false;_advance.reset();
}
home_action home_screen::update(text_canvas& canvas,save_system& saves)
{
 bool ready=canvas.ready() && _paint==2 && !_dirty;
 if(!_page && !_music && saves.settings_ready())
 {music_command c;c.path="Bgm_36_1";c.loop=true;c.parameters=music_has_path|music_has_loop;music_player::execute(c);_music=true;}
 if(ready && !_shown){canvas.visible(true);_shown=true;}
 if(!_page && ready && _music && _fade){--_fade;bn::bg_palettes::set_fade_intensity(bn::fixed(_fade)/32);}
 ready=ready && !_fade && saves.settings_ready();
 home_runtime.page=unsigned(_page);home_runtime.selected=unsigned(_selected);home_runtime.ready=ready;home_runtime.hiro=_hiro;home_runtime.fade=unsigned(_fade);home_runtime.speed=unsigned(saves.dialogue_speed());home_runtime.music=unsigned(saves.music_level());home_runtime.sound=unsigned(saves.sound_level());home_runtime.hints=saves.branch_hints();home_runtime.breaks=saves.original_line_breaks();
 bool action=_advance.update(bn::keypad::a_held(),ready);
 if(!ready)return home_action::none;
 if(!_page)return action ? home_action::start : home_action::none;
 if(bn::keypad::b_pressed())return home_action::back;
 int previous=_selected;
 constexpr int count=5;
 if(bn::keypad::up_pressed())_selected=(_selected+count-1)%count;
 if(bn::keypad::down_pressed())_selected=(_selected+1)%count;
 if(previous!=_selected){_dirty|=(1<<previous)|(1<<_selected);play_ui_sound(ui_sound::move);}
 if(_page==2 && _selected<5-MANOSABA_DEBUG_MENU && (bn::keypad::left_pressed() || bn::keypad::right_pressed()))
 {
  int setting=_selected==3 ? 4 : _selected==4 ? 3 : _selected;
  int value=setting==0 ? saves.dialogue_speed() : setting==1 ? saves.music_level() : setting==2 ? saves.sound_level() : setting==3 ? int(saves.branch_hints()) : int(saves.original_line_breaks());
  value=setting>=3 ? !value : bn::clamp(value+(bn::keypad::right_pressed()?1:-1),0,setting==0 ?2:4);
  if(setting==4 && !value)return home_action::disable_breaks;
  saves.setting(setting,value);_dirty|=1<<_selected;play_ui_sound(ui_sound::move);
 }
 if(action)
 {
  play_ui_sound(ui_sound::confirm);
  if(_page==2)
  {
#if MANOSABA_DEBUG_MENU
   if(_selected==4)return home_action::debug;
#endif
   if(_selected==3){if(saves.original_line_breaks())return home_action::disable_breaks;saves.setting(4,1);_dirty|=1<<3;}
   return home_action::none;
  }
  constexpr home_action entries[]={home_action::new_game,home_action::load,home_action::gallery,home_action::music,home_action::settings};return entries[_selected];
 }
 return home_action::none;
}
void home_screen::draw(text_canvas& canvas,const save_system& saves)
{
 if(_paint==0)
 {
  bool loaded=canvas.picture(_page ? home_menus[_page-1] : home_titles[_hiro]);canvas.visible(false);
  if(loaded){_paint=_page ?1:2;if(_page==2)canvas.text(108,5,"选项");}return;
 }
 if(!_page)return;
 for(int row=0;row<5;++row)if(_dirty&(1<<row))
 {
  const auto& image=home_rows[_page-1][row][row==_selected];canvas.stamp(image.x,image.y,image.width,image.height,image.pixels);
  if(_page==1)
  {const char* names[]={"新游戏","载入","画廊","音乐馆","选项"};int w=row==1 || row==2 || row==4 ?24:36;canvas.text((240-w)/2,11+31*row,names[row]);}
  else
  {
   const char* names[]={"对话速度","音乐音量","音效音量","原作文字拆行",MANOSABA_DEBUG_MENU ? "debug入口" : "重要分支选项提示"};int y=29+25*row;
   if(row==4 && MANOSABA_DEBUG_MENU)canvas.text(93,y,names[row]);
   else
   {
    canvas.text(39,y,names[row]);
    if(!row){const char* speeds[]={"标准","快","即时"};auto label=bn::format<32>("< {} >",speeds[saves.dialogue_speed()]);canvas.text(saves.dialogue_speed()==1?167:155,y,label.c_str());}
    else if(row<3){auto label=bn::format<24>("<{}%>",(row==1?saves.music_level():saves.sound_level())*25);canvas.text(161,y,label.c_str());}
    else canvas.text(161,y,(row==3 ? saves.original_line_breaks() : saves.branch_hints())?"< 开 >":"< 关 >");
   }
  }
  _dirty&=~(1<<row);if(!_dirty)_paint=2;break;
 }
}
}
