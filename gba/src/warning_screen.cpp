#include "warning_screen.h"
#include "warning_screen_data.h"
#include "text_canvas.h"
#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_core.h"
namespace manosaba {
void warning_screen::draw(text_canvas& canvas)
{
    canvas.clear();canvas.visible(false);
    // RGB555 colours match the approved native-size mockup.
    constexpr uint16_t backdrop=2|(2<<5)|(3<<10);
    canvas.palette_color(text_canvas::background,backdrop);
    canvas.palette_color(text_canvas::shadow,backdrop);
    canvas.palette_color(text_canvas::white,27|(27<<5)|(26<<10));
    canvas.palette_color(text_canvas::warning,30|(9<<5)|(11<<10));
    canvas.palette_color(text_canvas::selection,11|(5<<5)|(6<<10));
    bn::bg_palettes::set_transparent_color(bn::color(backdrop));
    for(int i=0;i<7;++i)canvas.initial(92+i*8,4,warning_header[i],text_canvas::warning);
    canvas.rectangle(12,25,216,1,text_canvas::selection);
    canvas.present();
}
bool warning_screen::update(text_canvas& canvas)
{
    if(_line<8)
    {
        const auto& line=warning_lines[_line++];
        canvas.text(line.x,line.y,line.text,text_canvas::white,228,160,false);
        canvas.present();return false;
    }
    if(!canvas.ready())return false;
    canvas.visible(true);
    _remaining-=1+bn::core::last_missed_frames();
    if(_remaining>0)return false;
    bn::bg_palettes::set_transparent_color(bn::color(0,0,0));
    return true;
}
}
