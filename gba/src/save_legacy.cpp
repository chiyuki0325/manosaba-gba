#include "save_legacy.h"
#include "save_archive.h"
#include "save_checkpoint.h"
#include "save_legacy_data.h"
#include "story_format.h"
#include "stage_player.h"
#include "music_player.h"
#include "sfx_player.h"
#include "build_config.h"
#if !MANOSABA_SAVE_SRAM
namespace manosaba {
namespace {
struct reader {
    save_archive& a;
    unsigned u(){unsigned value=0;a.field(value);return value;}
    int i(){int value=0;a.field(value);return value;}
    void fields(int count){while(count-->0)u();}
    unsigned text(int capacity){unsigned n=u();if(n>=unsigned(capacity))a.reject();else a.skip(int(n));return n;}
    unsigned links(){int n=i();if(n<0 || n>3){a.reject();return 0;}fields(n*4);return unsigned(n);}
};
struct court {
    int actor=0,view=0,picture=0,camera=0;unsigned duration=0;
    uint16_t poses[17]={};uint8_t layout=0;bool witch=false;
    void write(save_archive& a)
    {
        a.fixed(actor);a.fixed(view);a.fixed(picture);a.fixed(camera);a.fixed(duration);
        for(auto& p:poses)a.fixed(p);
        a.fixed(layout);a.fixed(witch);
    }
};
struct record {
    uint8_t flags=0;int script=0;unsigned source=0;
    uint16_t branches=0;uint8_t area=0;bool direct=false;uint32_t trial_low=0,trial_high=0;
    int hp=0,bad=0,chosen=0;uint32_t total=0;
    uint8_t book[book_entry_count]={},chapter=0;court current;
    int visual=-1;uint8_t printer_style=0,reenact=0;int16_t reenact_scene=-1,map=-1;
    uint16_t screen[4]={};bool fire=false,return_valid=false;int return_script=0;unsigned return_source=0;court interrupted;
    stage_entity entities[stage_entity_count];bool printer=true;
    music_checkpoint music;sfx_checkpoint sounds;
    void write(save_archive& a)
    {
        a.fixed(flags);a.fixed(script);a.fixed(source);a.fixed(branches);a.fixed(area);a.fixed(direct);a.fixed(trial_low);a.fixed(trial_high);
        a.fixed(hp);a.fixed(bad);a.fixed(chosen);a.fixed(total);a.bytes(book,sizeof(book));a.fixed(chapter);current.write(a);
        a.fixed(visual);a.fixed(printer_style);a.fixed(reenact);a.fixed(reenact_scene);a.fixed(map);
        for(auto& c:screen)a.fixed(c);
        a.fixed(fire);a.fixed(return_valid);a.fixed(return_script);a.fixed(return_source);interrupted.write(a);
        for(auto& e:entities)
        {
            for(auto& v:e.values)a.fixed(v);
            a.fixed(e.image);a.fixed(e.order);a.fixed(e.mask);
            uint8_t presentation=uint8_t(e.maskable)|(uint8_t(e.regular_cg)<<1);a.fixed(presentation);
        }
        a.fixed(printer);music.archive(a);sounds.archive(a);
    }
};
}
bool convert_legacy_save(uint8_t* source,int size,uint8_t* destination)
{
    save_archive input(source,size,true);reader r{input};record v;
    unsigned kind=r.u();if(kind>2)return false;
    v.script=r.i();unsigned pc=r.u(),state=r.u();r.u();unsigned text=r.text(story_text_capacity);r.text(96);r.text(96);
    if(v.script<0 || v.script>=story_script_count)return false;
    r.u();v.visual=r.i();r.i();v.branches=uint16_t(r.u());v.area=uint8_t(r.u());v.direct=r.u();bool map=r.u();
    int options=r.i();unsigned option_text=0;if(options<0 || options>story_max_options)return false;
    for(int n=0;n<options;++n){option_text+=r.text(story_option_capacity);if(!map)r.fields(2);r.u();}
    unsigned history=r.u();if(history>story_history_capacity)return false;input.skip(int(history*4));
    v.trial_low=r.u();v.trial_high=r.u();unsigned links=r.links();r.i();v.hp=r.i();v.bad=r.i();
    auto& c=v.current;c.actor=r.i();c.view=r.i();c.picture=r.i();c.camera=r.i();
    for(auto& p:c.poses)p=uint16_t(r.u());
    c.layout=uint8_t(r.u());c.duration=r.u();
    r.u();r.u();v.total=r.u();int candidates=r.i();if(candidates<0 || candidates>book_entry_count)return false;r.fields(candidates);
    v.chosen=r.i();int inventory=r.i();r.u();int cutin=r.i();r.i();input.bytes(v.book,sizeof(v.book));v.chapter=uint8_t(r.u());
    v.return_valid=r.u();r.u();c.witch=r.u();v.printer_style=uint8_t(r.u());bool reenact=r.u();r.u();v.reenact_scene=int16_t(r.i());r.u();int action=int(r.u());
    if(state==15)r.u();
    v.map=int16_t(r.i());r.u();for(auto& color:v.screen)color=uint16_t(r.u());r.fields(6);v.fire=r.u();r.fields(11);
    if(v.return_valid)
    {
        unsigned return_text=r.text(story_text_capacity),return_links=r.links(),end=r.u();
        v.return_script=r.i();r.i();r.u();r.u();auto& back=v.interrupted;
        back.actor=r.i();back.view=r.i();back.picture=r.i();back.camera=r.i();back.duration=r.u();
        for(auto& p:back.poses)p=uint16_t(r.u());
        back.layout=uint8_t(r.u());back.witch=r.u();r.fields(4);
        v.return_source=end-12-return_text-10*return_links;
    }
    auto stage=[&]()
    {
        r.i();r.i();r.u();
        for(auto& e:v.entities)
        {
            for(auto& value:e.values)value=r.i();
            e.image=int16_t(r.i());r.i();e.order=int16_t(r.i());e.mask=int16_t(r.i());
            unsigned presentation=r.u();e.maskable=presentation&1;e.regular_cg=presentation&2;
        }
        v.printer=r.u();
    };
    int picture_event=-1,active=-1;bool trial=story_scripts[v.script].mode!=0;
    v.entities[stage_camera].values[0]=v.entities[stage_camera].values[1]=0;
    if(kind==2)stage();
    if(!kind)
    {
        if(trial){r.i();picture_event=r.i();stage();r.fields(4+7+5);}
        else {stage();r.fields(7);}
        r.fields(18);input.skip(4);r.fields(2);active=r.i();r.fields(5);
    }
    v.music.legacy_archive(input);v.sounds.legacy_archive(input);
    if(!input.good() || input.size()!=size)return false;
    v.source=pc;
    switch(state)
    {
    case 1:v.source-=5+text;break;
    case 6:v.source-=12+text+10*links;break;
    case 2:
        if(map)
        {
            bool found=false;for(const auto& row:legacy_map_anchors)if(row.script==v.script && row.end==pc){v.source=row.start;found=true;break;}
            if(!found)return false;
        }
        else v.source-=5+unsigned(options)*8+option_text;
        break;
    case 7:v.source-=6+(inventory==2 ? unsigned(candidates)*2 : 0);break;
    case 3:case 4:v.source-=4+text;break;
    case 8:v.source-=5;break;
    case 12:if(cutin!=2)v.source-=4;else v.flags=save_cursor|save_failure;break;
    case 15:v.source-=4;break;
    case 16:v.source-=3;break;
    case 5:v.source-=5;if(trial ? picture_event<0 : active<0)v.flags|=save_stage_pending;break;
    default:v.flags=save_cursor;break;
    }
    if(v.source>=story_scripts[v.script].size)return false;
    bool closed=action==1;for(const auto& row:legacy_curtain_changes)if(row.script==v.script && row.end<=pc)closed=row.closed;
    v.reenact=reenact ? uint8_t(1|(uint8_t(closed && v.reenact_scene>=0)<<1)) : 0;
    save_archive out(destination,save_checkpoint_bytes,false);v.write(out);
    return out.good() && out.size()==save_checkpoint_bytes;
}
}
#endif
