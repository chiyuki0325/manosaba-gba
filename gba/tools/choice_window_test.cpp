#include "choice_window.h"
#include <cassert>
#include <cstdio>
using namespace manosaba;
int main()
{
    const int bubbles[]={32,32,32,24};
    auto w=visible_choices(bubbles,4,0,0,16,120);
    assert(w.first==0 && w.count==2);
    w=visible_choices(bubbles,4,1,w.first,16,120);
    assert(w.first==1 && w.count==3); // The second option reveals the third and Return.
    w=visible_choices(bubbles,4,2,w.first,16,120);
    assert(w.first==1 && w.count==3);
    w=visible_choices(bubbles,4,3,w.first,16,120);
    assert(w.first==1 && w.count==3);
    w=visible_choices(bubbles,4,0,w.first,16,120);
    assert(w.first==0 && w.count==2);
    const int three[]={48,32,24};
    w=visible_choices(three,3,1,0,16,120);
    assert(w.first==1 && w.count==2);
    const int scrolls[]={40,40,40,40};
    w=visible_choices(scrolls,4,2,0,0,144);
    assert(w.first==1 && w.count==3);
    const int mixed[]={72,40,40,40};
    w=visible_choices(mixed,4,1,0,0,144);
    assert(w.first==1 && w.count==3);
    // Variable-height menus keep selection and the next option together whenever possible.
    const int edge_heights[][4]={{32,80,48,24},{80,32,48,24}};
    for(int count=3;count<=4;++count)for(const auto& heights:edge_heights)
    {
        int first=0;
        for(int step=0;step<count*2;++step)
        {
            int selected=step%count;w=visible_choices(heights,count,selected,first,16,120);first=w.first;
            assert(w.height<=120 && selected>=w.first && selected<w.first+w.count);
            if(selected+1<count && heights[selected]+16+heights[selected+1]<=120)
                assert(selected+1<w.first+w.count);
        }
    }
    std::puts("PASS: shared choice scrolling, next-row visibility, mixed heights and wraparound");
}
