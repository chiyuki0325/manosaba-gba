#ifndef MANOSABA_SFX_UI_H
#define MANOSABA_SFX_UI_H
namespace manosaba
{
enum class ui_sound
{
    move,
    confirm,
    cancel,
    tab,
    open_book,
    close_book,
    save,
    load
};
void play_ui_sound(ui_sound sound);
} // namespace manosaba
#endif
