#include "sfx_ui.h"
#include "sfx_player.h"
namespace manosaba {
void play_ui_sound(ui_sound sound)
{
    static constexpr const char* aliases[]={"Sfx_System_Move_001","Sfx_System_Submit_001","Sfx_System_Cancel_001","Sfx_System_Tab_001a","Sfx_System_OpenBook_001","Sfx_System_CloseBook_001","Sfx_System_SaveData_001","Sfx_System_LoadData_001"};
    sfx_command command;command.cue=sfx_player::find(aliases[int(sound)]);sfx_player::execute(command);
}
}
