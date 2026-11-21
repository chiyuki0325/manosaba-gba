#ifndef CHOICE_WINDOW_H
#define CHOICE_WINDOW_H
namespace manosaba {
struct choice_window {int first,count,height;};

// Keep the selected row and its successor visible whenever they fit together.
inline choice_window visible_choices(const int* heights,int count,int selected,int first,int gap,int limit)
{
    if(selected<first)first=selected;
    int target=selected+1<count ? selected+1 : selected;
    int height=0;
    for(int i=first;i<=target;++i)height+=heights[i]+(i>first ? gap : 0);
    while(height>limit && first<selected){height-=heights[first]+gap;++first;}
    choice_window result{first,0,0};
    for(int i=first;i<count;++i)
    {
        int next=result.height+heights[i]+(i>first ? gap : 0);
        if(next>limit)break;
        result.height=next;++result.count;
    }
    return result;
}
}
#endif
