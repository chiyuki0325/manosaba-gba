#include "story_format.h"
namespace manosaba
{
const story_text_style* find_text_style(int script,unsigned pc,bool history)
{
    int low=0,high=story_text_style_count;
    while(low<high)
    {
        int mid=(low+high)/2;const auto& s=story_text_styles[mid];
        unsigned position=history ? s.pc : s.end;
        if(s.script<script || (s.script==script && position<pc))low=mid+1;
        else high=mid;
    }
    if(low==story_text_style_count)return nullptr;
    const auto& s=story_text_styles[low];
    return s.script==script && (history ? s.pc : s.end)==pc ? &s : nullptr;
}
}
