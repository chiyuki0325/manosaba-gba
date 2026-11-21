#include "text_canvas.h"
#include "text_breaks.h"
#include "trial_title_data.h"
#include "story_layout.h"
#include "story_format.h"
#include "choice_ui_data.h"
#include "nameplate_data.h"
#include "lzss.h"
#include "book_map_data.h"
#include "toast_data.h"
#include "execution_data.h"

#include "bn_bg_palette_item.h"
#include "bn_bg_palette_ptr.h"
#include "bn_bg_palettes.h"
#include "bn_bpp_mode.h"
#include "bn_color.h"
#include "bn_regular_bg_map_item.h"
#include "bn_regular_bg_map_ptr.h"
#include "bn_tile.h"
#include "bn_array.h"
#include "bn_memory.h"
#include "bn_core.h"
#include "bn_rect_window.h"
#include <cstring>

extern "C"
{
extern const uint32_t font_codes[];
extern const uint16_t font_rows[][12];
extern const uint8_t bad_end_rows[7][16];
extern const unsigned font_count;
}

namespace
{
BN_DATA_EWRAM_BSS bn::tile canvas_tiles[1200];
BN_DATA_EWRAM_BSS uint16_t label_inks[256];
int label_color=-1;
const uint8_t* label_ink_source=nullptr;

#ifdef __arm__
__attribute__((section(".ewram.ui_setup"),target("arm")))
#endif
void label_palette(const uint8_t* inks,int color)
{
    if(inks==label_ink_source && color==label_color)return;
    for(int i=0;i<256;++i)
    {
        int low=i&15,high=i>>4;
        label_inks[i]=uint16_t((low==3 ? color : inks[low])|((high==3 ? color : inks[high])<<8));
    }
    label_color=color;label_ink_source=inks;
}

constexpr auto make_map()
{
    bn::array<bn::regular_bg_map_cell, 32 * 32> cells{};
    for(int y = 0; y < 20; ++y)
    {
        for(int x = 0; x < 30; ++x)
        {
            cells[y * 32 + x] = uint16_t(y * 30 + x);
        }
    }
    return cells;
}

constexpr auto canvas_cells = make_map();
constexpr auto make_reader_map(bool full=false)
{
    bn::array<bn::regular_bg_map_cell,1024> cells{};
    for(int y=0;y<32;++y)for(int x=0;x<32;++x)cells[y*32+x]=uint16_t((y%(full ? 32 : 16))*28+x%28);
    return cells;
}
constexpr auto reader_cells=make_reader_map();
constexpr auto credits_cells=make_reader_map(true);
constexpr int execution_index(int logical)
{int x=logical%30,y=logical/30;return x>=8 && x<22 ? 1+y*14+x-8 : x>=27 && y>=17 ? 281+(y-17)*3+x-27 : 0;}
constexpr auto make_execution_map()
{
    bn::array<bn::regular_bg_map_cell,1024> cells{};
    for(int y=0;y<20;++y)for(int x=0;x<30;++x)cells[y*32+x]=uint16_t(execution_index(y*30+x));
    return cells;
}
constexpr auto execution_cells=make_execution_map();
// Once uploaded, this part of the paper bitmap is no longer edited in a reader.
// Reuse its CPU copy for the 224x128 text ring; no second image buffer.
bn::tile* reader_pixels(bool full){return canvas_tiles+(full ? 0 : 480);}

constexpr int toast_index(int logical)
{
    using namespace manosaba;
    int x=logical%30-toast_left/8,y=logical/30-toast_top/8;
    return x>=0 && x<toast_width/8 && y>=0 && y<toast_height/8 ? y*(toast_width/8)+x : -1;
}
constexpr int compact_index(int logical)
{
    int x=logical%30,y=logical/30;
    if(y>=10)return 1+32+20+(y-10)*30+x;
    if(x<4 && y<5)return 1+y*4+x;
    if(x>=26 && y<8)return 21+y*4+x-26;
    if(toast_index(logical)>=0)return y<5 ? 353+(y-3)*8+x-4 : 369+x;
    return 0;
}
constexpr auto compact_map()
{
    bn::array<bn::regular_bg_map_cell,1024> cells{};
    for(int y=0;y<20;++y) for(int x=0;x<30;++x)cells[y*32+x]=uint16_t(compact_index(y*30+x));
    return cells;
}
constexpr auto compact_cells=compact_map();
#ifdef __arm__
__attribute__((section(".iwram"),target("arm"),noinline))
#endif
void indexed_rectangle(uint32_t* pixels,int x,int y,int width,int height,unsigned color)
{
    int first=x/4,last=(x+width-1)/4;unsigned value=color*0x01010101u;
    for(int row=y;row<y+height;++row)for(int word=first;word<=last;++word)
    {
        int left=bn::max(0,x-word*4),right=bn::min(4,x+width-word*4);
        unsigned mask=(~0u<<(left*8))&(right==4 ? ~0u : (1u<<(right*8))-1);
        auto& target=pixels[(row/8*30+word/2)*16+(row%8)*2+word%2];target=(target&~mask)|(value&mask);
    }
}
void nibble_rectangle(uint32_t* pixels,int x,int y,int width,int height,unsigned color)
{
    unsigned value=color*0x11111111u;
    for(int row=y;row<y+height;++row)for(int tile=x/8;tile<=(x+width-1)/8;++tile)
    {
        int left=bn::max(0,x-tile*8),right=bn::min(8,x+width-tile*8);
        unsigned mask=(~0u<<(left*4))&(right==8 ? ~0u : (1u<<(right*4))-1);
        auto& word=pixels[(row/8*30+tile)*8+row%8];word=(word&~mask)|(value&mask);
    }
}
alignas(4) constexpr bn::color colors[16] = {
    bn::color(0, 0, 0), bn::color(31, 31, 30), bn::color(19, 18, 18),
    bn::color(31, 16, 21), bn::color(4, 3, 4), bn::color(27, 24, 17),
    bn::color(0, 0, 0), bn::color(1, 1, 1), bn::color(3, 3, 3),
    bn::color(6, 6, 6), bn::color(9, 9, 9), bn::color(13, 13, 13),
    bn::color(4, 2, 3), bn::color(8, 4, 5), bn::color(14, 7, 9), bn::color(22, 11, 14)
};

unsigned decode(const char*& text)
{
    unsigned code = static_cast<unsigned char>(*text++);
    if(code < 0x80)
    {
        return code;
    }
    unsigned remaining = code < 0xe0 ? 1 : code < 0xf0 ? 2 : 3;
    code &= remaining == 1 ? 0x1f : remaining == 2 ? 0x0f : 7;
    while(remaining--)
    {
        code = (code << 6) | (static_cast<unsigned char>(*text++) & 0x3f);
    }
    return code;
}
}

namespace manosaba
{

text_canvas::text_canvas(bool compact) : _graphical(compact)
{
    allocate();
    for(int index = 0; index < 600; ++index) dirty(index);
}
void text_canvas::allocate()
{
    auto bpp=_choice_ui ? bn::bpp_mode::BPP_8 : bn::bpp_mode::BPP_4;
    const auto* palette_colors=_choice_ui ? reinterpret_cast<const bn::color*>(choice_ui_palette) : colors;
    // Keep the compact UI inside its existing six 2KiB BG blocks (384 tiles).
    _tiles = bn::regular_bg_tiles_ptr::allocate(_choice_ui ? 1200 : _execution_ui ? 290 : _graphical ? 381 : 600,bpp,_trial_canvas || bool(_map_bg));
    _bg = bn::regular_bg_ptr::create(8,48,bn::regular_bg_map_ptr::create(
        bn::regular_bg_map_item(_execution_ui ? execution_cells[0] : _graphical ? compact_cells[0] : canvas_cells[0],bn::size(32,32)),*_tiles,
        bn::bg_palette_ptr::create(bn::bg_palette_item(bn::span<const bn::color>(palette_colors,_choice_ui ? _palette_colors : 16),bpp))));
    _bg->set_priority(0);
    for(int i=0;i<16;++i)_colors[i]=palette_colors[i].data();
    if(!_choice_ui)
    {
        auto palette=_bg->palette();
        for(int i=0;i<4;++i)
        {_colors[12+i]=nameplate_diamond_colors[i];palette.set_color(12+i,bn::color(_colors[12+i]));}
    }
    if(_graphical)
    {
        auto vram=_tiles->vram().value();
        for(auto& word:vram[0].data)word=0;
    }
}
void text_canvas::clear(bool compact)
{
    bool full=!_bg || _reader_full || _execution_ui || _save_tiles || _choice_ui || _graphical!=compact;
    _save_tiles=nullptr;_save_bytes=0;
    map_close();
    _picture=nullptr;_picture_bytes=38400;
    graphical(compact);
    _choice_ui=false;
    for(int i = 0; i < 16; ++i) _colors[i] = colors[i].data();
    for(int i=0;i<4;++i)_colors[12+i]=nameplate_diamond_colors[i];
    auto palette = _bg->palette();
    palette.set_colors(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(_colors),16));
    if(full)
    {
        bn::memory::set_words(0,600*8,canvas_tiles[0].data);
        std::memset(_dirty,1,sizeof(_dirty));_dirty_count=600;
        std::memset(_occupied,0,sizeof(_occupied));return;
    }
    for(int index = 0; index < 600; ++index)
    {
        if(_occupied[index])
        {
            std::memset(canvas_tiles[index].data,0,32);
            dirty(index);
            _occupied[index]=false;
        }
    }
}

void text_canvas::bad_end()
{
    clear();
    auto pixel=[this](int x,int y)
    {
        int tile=y/8*30+x/8;
        canvas_tiles[tile].data[y%8]|=1u<<((x%8)*4);dirty(tile);
    };
    // 56px Unifont text, 4px gap, 5px chevron; right/bottom margins are 16px.
    for(int ch=0;ch<7;++ch)for(int row=0;row<16;++row)for(int col=0;col<8;++col)
        if(bad_end_rows[ch][row]&(128u>>col))pixel(159+ch*8+col,128+row);
    for(int row=0;row<9;++row)pixel(219+(row<5 ? row : 8-row),132+row);
}

#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void text_canvas::glyph(int x,int y,const uint16_t* rows,int width,ink color,bool cast_shadow)
{
    int height=12+int(cast_shadow),draw_width=width+int(cast_shadow);
    unsigned keep=0xfffu<<(12-width);
    if(_choice_ui && !_reader_drawing)
    {
        static constexpr uint32_t bytes[16]={0,0x01000000,0x00010000,0x01010000,0x00000100,0x01000100,0x00010100,0x01010100,
            0x00000001,0x01000001,0x00010001,0x01010001,0x00000101,0x01000101,0x00010101,0x01010101};
        auto* pixels=reinterpret_cast<uint32_t*>(canvas_tiles);
        int first=x/4,last=bn::min((x+draw_width-1)/4,59),offset=x%4;
        for(int row=0;row<height && y+row<160;++row)
        {
            unsigned main=(row<12 ? rows[row]&keep : 0)<<(4-offset);
            unsigned shade=(cast_shadow && row ? rows[row-1]&keep : 0)<<(3-offset);int py=y+row;
            for(int word=first;word<=last;++word)
            {
                int shift=12-(word-first)*4;unsigned occupied=bytes[(main>>shift)&15],under=bytes[(shade>>shift)&15];
                if(!(occupied|under))continue;
                auto& target=pixels[(py/8*30+word/2)*16+py%8*2+word%2];
                unsigned top=occupied*255;
                target=(target&~((occupied|under)*255))|((under*unsigned(shadow))&~top)|(occupied*unsigned(color));
            }
        }
    }
    else
    {
    static constexpr uint32_t nibbles[256]={0u,268435456u,16777216u,285212672u,1048576u,269484032u,17825792u,286261248u,65536u,268500992u,16842752u,285278208u,1114112u,269549568u,17891328u,286326784u,4096u,268439552u,16781312u,285216768u,1052672u,269488128u,17829888u,286265344u,69632u,268505088u,16846848u,285282304u,1118208u,269553664u,17895424u,286330880u,256u,268435712u,16777472u,285212928u,1048832u,269484288u,17826048u,286261504u,65792u,268501248u,16843008u,285278464u,1114368u,269549824u,17891584u,286327040u,4352u,268439808u,16781568u,285217024u,1052928u,269488384u,17830144u,286265600u,69888u,268505344u,16847104u,285282560u,1118464u,269553920u,17895680u,286331136u,16u,268435472u,16777232u,285212688u,1048592u,269484048u,17825808u,286261264u,65552u,268501008u,16842768u,285278224u,1114128u,269549584u,17891344u,286326800u,4112u,268439568u,16781328u,285216784u,1052688u,269488144u,17829904u,286265360u,69648u,268505104u,16846864u,285282320u,1118224u,269553680u,17895440u,286330896u,272u,268435728u,16777488u,285212944u,1048848u,269484304u,17826064u,286261520u,65808u,268501264u,16843024u,285278480u,1114384u,269549840u,17891600u,286327056u,4368u,268439824u,16781584u,285217040u,1052944u,269488400u,17830160u,286265616u,69904u,268505360u,16847120u,285282576u,1118480u,269553936u,17895696u,286331152u,1u,268435457u,16777217u,285212673u,1048577u,269484033u,17825793u,286261249u,65537u,268500993u,16842753u,285278209u,1114113u,269549569u,17891329u,286326785u,4097u,268439553u,16781313u,285216769u,1052673u,269488129u,17829889u,286265345u,69633u,268505089u,16846849u,285282305u,1118209u,269553665u,17895425u,286330881u,257u,268435713u,16777473u,285212929u,1048833u,269484289u,17826049u,286261505u,65793u,268501249u,16843009u,285278465u,1114369u,269549825u,17891585u,286327041u,4353u,268439809u,16781569u,285217025u,1052929u,269488385u,17830145u,286265601u,69889u,268505345u,16847105u,285282561u,1118465u,269553921u,17895681u,286331137u,17u,268435473u,16777233u,285212689u,1048593u,269484049u,17825809u,286261265u,65553u,268501009u,16842769u,285278225u,1114129u,269549585u,17891345u,286326801u,4113u,268439569u,16781329u,285216785u,1052689u,269488145u,17829905u,286265361u,69649u,268505105u,16846865u,285282321u,1118225u,269553681u,17895441u,286330897u,273u,268435729u,16777489u,285212945u,1048849u,269484305u,17826065u,286261521u,65809u,268501265u,16843025u,285278481u,1114385u,269549841u,17891601u,286327057u,4369u,268439825u,16781585u,285217041u,1052945u,269488401u,17830161u,286265617u,69905u,268505361u,16847121u,285282577u,1118481u,269553937u,17895697u,286331153u};
        auto* destination=_reader_drawing ? reader_pixels(_reader_full) : canvas_tiles;
        int stride=_reader_drawing ? 28 : 30;
        int first=x/8,last=bn::min((x+draw_width-1)/8,stride-1),offset=x%8;
        for(int row=0;row<height && (_reader_drawing || y+row<160);++row)
        {
            unsigned main=(row<12 ? rows[row]&keep : 0)<<(12-offset);
            unsigned shade=(cast_shadow && row ? rows[row-1]&keep : 0)<<(11-offset);int py=_reader_drawing ? (y+row)&(_reader_full ? 255 : 127) : y+row;
            for(int tile=first;tile<=last;++tile)
            {
                int shift=16-(tile-first)*8;unsigned occupied=nibbles[(main>>shift)&255],under=nibbles[(shade>>shift)&255];
                if(!(occupied|under))continue;
                auto& target=destination[py/8*stride+tile].data[py%8];unsigned top=occupied*15;
                target=(target&~((occupied|under)*15))|((under*unsigned(shadow))&~top)|(occupied*unsigned(color));
            }
        }
    }
    if(_reader_drawing)return;
    for(int row=y/8;row<=bn::min(y+height-1,159)/8;++row)
        for(int tile=x/8;tile<=bn::min((x+draw_width-1)/8,29);++tile)dirty(row*30+tile);
}

void text_canvas::dirty(int index)
{
    _occupied[index]=true;
    if(! _dirty[index])
    {
        _dirty[index] = true;
        ++_dirty_count;
    }
}

void text_canvas::rectangle(int x, int y, int width, int height, ink color)
{
    if(_choice_ui)
    {
        indexed_rectangle(reinterpret_cast<uint32_t*>(canvas_tiles),x,y,width,height,unsigned(color));
        for(int row=y/8;row<=(y+height-1)/8;++row)for(int tile=x/8;tile<=(x+width-1)/8;++tile)dirty(row*30+tile);
        return;
    }
    unsigned value=unsigned(color)*0x11111111u;
    if(!((x|y|width|height)&7))
    {
        for(int row=y/8;row<(y+height)/8;++row)
        {
            int first=row*30+x/8;
            bn::memory::set_words(value,width,canvas_tiles[first].data);
            for(int tile=first;tile<first+width/8;++tile)dirty(tile);
        }
        return;
    }
    nibble_rectangle(reinterpret_cast<uint32_t*>(canvas_tiles),x,y,width,height,unsigned(color));
    for(int row=y/8;row<=(y+height-1)/8;++row)
        for(int tile=x/8;tile<=(x+width-1)/8;++tile)dirty(row*30+tile);
}

const char* text_canvas::text(int x, int y, const char* value, ink color, int right, int bottom,bool cast_shadow)
{
    cursor position{x,y};
    return text(position,dialogue_layout{x,y,right,bottom},value,nullptr,color,cast_shadow);
}

#ifdef __arm__
__attribute__((section(".ewram.ui_setup"),target("arm")))
#endif
const char* text_canvas::text(cursor& position, const dialogue_layout& box, const char* value, const char* end, ink color,bool cast_shadow,bool original_breaks)
{
    int& x = position.x;
    int& y = position.y;
    while(*value && (! end || value < end))
    {
        const char* previous = value;
        unsigned code = decode(value);
        if(code=='\r' && !original_breaks){if(x>box.left && soft_break_space(previous))x+=dialogue_ascii_width;continue;}
        if(code == '\n' || code=='\r')
        {
            x = box.left;
            y += dialogue_line_height;
            continue;
        }
        bool control=code>=0xe000 && code<=0xe004;
        int width = control ? 13 : code < 128 ? dialogue_ascii_width : dialogue_wide_width;
        if(x + width > box.right)
        {
            x = box.left;
            y += dialogue_line_height;
        }
        if(y + 12 > box.bottom)
        {
            return previous;
        }
        if(code==' ' || code==0x3000){x+=width;continue;}
        unsigned lo = 0, hi = font_count;
        while(lo < hi)
        {
            unsigned mid = (lo + hi) / 2;
            if(font_codes[mid] < code)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        if(lo < font_count && font_codes[lo] == code)
        {
            glyph(x,y,font_rows[lo],control ? 12 : width,control && color!=muted ? white : color,cast_shadow);
        }
        x += width;
    }
    return value;
}

void text_canvas::release()
{
    map_close();
    _execution_ui=false;
    _bg.reset();_tiles.reset();_picture=nullptr;_save_tiles=nullptr;_save_bytes=0;_picture_bytes=38400;_trial_canvas=false;_decoration=nullptr;
}
#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void text_canvas::book_page(const uint8_t* tiles,const uint16_t* palette,int first,int count,bool reader)
{
    if(!first)
    {
        reader_close();
        if(!_choice_ui || !_bg || _palette_colors!=(reader ? 32 : 256))
        {release();bn::core::update();_palette_colors=reader ? 32 : 256;_choice_ui=true;_graphical=false;allocate();}
        _trial_canvas=false;_picture=nullptr;
        book_colors(palette);
    }
    auto* destination=reinterpret_cast<uint8_t*>(canvas_tiles);
    for(int tile=first;tile<first+count;++tile)
    {
        const auto* source=tiles+tile*32;auto* out=destination+tile*64;
        for(int i=0;i<32;++i){*out++=uint8_t(16+(source[i]&15));*out++=uint8_t(16+(source[i]>>4));}
        dirty(tile);
    }
    _picture_bytes=(first+count)*64;
}
void text_canvas::reader_picture(const ui_picture& image,const uint8_t* remap,int first,int count)
{
    if(!first)
    {
        release();bn::core::update();_palette_colors=240;_choice_ui=true;_graphical=false;allocate();
        auto palette=_bg->palette();
        palette.set_colors(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(image.palette),240));
        std::memcpy(_colors,image.palette,32);
    }
    auto* source=reinterpret_cast<const uint8_t*>(image.raw)+first*64;
    auto* destination=reinterpret_cast<uint8_t*>(canvas_tiles)+first*64;
    for(int i=0;i<count*64;++i){unsigned value=source[i];destination[i]=uint8_t(value<240 ? value : remap[value-240]);}
    for(int tile=first;tile<first+count;++tile)dirty(tile);
    _picture_bytes=(first+count)*64;
}
void text_canvas::reader_open(int top,int height,bool full,bool black)
{
    if(full)
    {
        release();bn::core::update();_choice_ui=_graphical=false;_dirty_count=0;std::memset(_dirty,0,sizeof(_dirty));_toast=0;
        static constexpr uint16_t palette[16]={32767,7|(2<<5)|(2<<10),14|(7<<5)|(7<<10),30|(10<<5)|(10<<10),0,0,28|(27<<5)|(24<<10)};
        std::memcpy(_colors,palette,sizeof(_colors));
        if(black){_colors[background]=0;_colors[white]=32767;_colors[shadow]=4|(4<<5)|(4<<10);}
        bn::bg_palettes::set_transparent_color(bn::color(_colors[background]));
    }
    _reader_full=full;
    _reader_top=top;
    _map_tiles=bn::regular_bg_tiles_ptr::allocate(full ? 896 : 448,bn::bpp_mode::BPP_4,true);
    auto palette=bn::bg_palette_ptr::create(bn::bg_palette_item(
        bn::span<const bn::color>(reinterpret_cast<const bn::color*>(_colors),16),bn::bpp_mode::BPP_4));
    _map_bg=bn::regular_bg_ptr::create(0,0,bn::regular_bg_map_ptr::create(
        bn::regular_bg_map_item(full ? credits_cells[0] : reader_cells[0],bn::size(32,32)),*_map_tiles,palette));
    _map_tile_vram=_map_tiles->vram()->data();
    _map_bg->set_priority(0);_map_bg->set_visible(false);if(_bg)_bg->set_priority(1);
    auto window=bn::rect_window::internal();
    window.set_boundaries(top-80,-108,top+height-80,108);
    bn::window::outside().set_show_bg(*_map_bg,false);
    bn::window::external().set_show_bg(*_map_bg,false);
    bn::window::sprites().set_show_bg(*_map_bg,false);
    _map_bg->set_visible_in_window(true,window);
    std::memset(reader_pixels(full),0,(full ? 896 : 448)*32);_reader_dirty=full ? ~0u : 0xffff;
    _reader_active=true;
}
__attribute__((noinline)) void text_canvas::reader_close()
{
    if(!_reader_active)return;
    if(_reader_full)bn::bg_palettes::set_transparent_color(bn::color(0,0,0));
    bn::rect_window::internal().set_visible(false);
    _map_bg.reset();_map_tiles.reset();_reader_active=_reader_full=false;
    _reader_dirty=0;
    if(_bg)_bg->set_priority(0);
}
void text_canvas::reader_line(int row,const char* value,ink color,bool centered,const story_text_style* style,int offset)
{
    int mask=_reader_full ? 255 : 127,y=row*14&mask;
    for(int i=0;i<14;++i)
    {
        int py=(y+i)&mask;
        for(int tile=0;tile<28;++tile)reader_pixels(_reader_full)[py/8*28+tile].data[py%8]=0;
        _reader_dirty|=1u<<(py/8);
    }
    const char* end=value;while(*end && *end!='\n')++end;
    int left=0;
    if(centered){const char* p=value;int width=0;while(p<end)width+=decode(p)<128 ? 6 : 12;left=(216-width)/2;}
    cursor position{left,y};_reader_drawing=true;
    styled_text(position,{0,y,216,y+13},value,end,color,style,offset,true);_reader_drawing=false;
}
void text_canvas::styled_text(cursor& position,const dialogue_layout& box,const char* value,const char* end,ink color,
                              const story_text_style* style,int offset,bool history,bool original_breaks)
{
    if(!style || color!=white){text(position,box,value,end,color,true,original_breaks);return;}
    while(value<end)
    {
        int length=end-value;auto segment_color=color;
        for(int i=0;i<style->count;++i)
        {
            const auto& span=story_color_spans[style->first+i];
            int first=history ? span.history_first : span.first,last=history ? span.history_last : span.last;
            if(offset>=first && offset<last){segment_color=text_canvas::ink(history ? 12+style->color : selection);length=bn::min(length,last-offset);}
            else if(offset<first)length=bn::min(length,first-offset);
        }
        text(position,box,value,value+length,segment_color,true,original_breaks);value+=length;offset+=length;
    }
}
void text_canvas::reader_position(int pixel){_map_bg->set_top_left_position(12,_reader_top-(pixel&(_reader_full ? 255 : 127)));}
void text_canvas::reader_color(int index,uint16_t color)
{
    if(_colors[index]==color)return;
    _colors[index]=color;
    auto palette=_map_bg->palette();palette.set_color(index,bn::color(color));
}
void text_canvas::reader_show()
{bn::rect_window::internal().set_visible(true);_map_bg->set_visible(true);}
void text_canvas::execution_screen()
{
    release();bn::core::update();_execution_ui=_graphical=true;_choice_ui=false;_palette_colors=16;
    allocate();auto palette=_bg->palette();palette.set_colors(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(execution_colors),16));
    std::memset(_dirty,0,sizeof(_dirty));_dirty_count=0;
    const auto* source=execution_base+8;
    auto copy=[&](int x,int y)
    {int tile=y*30+x;std::memcpy(canvas_tiles[tile].data,source,32);source+=8;dirty(tile);};
    for(int y=0;y<20;++y)for(int x=8;x<22;++x)copy(x,y);
    for(int y=17;y<20;++y)for(int x=27;x<30;++x)copy(x,y);
    _bg->set_visible(false);
}
void text_canvas::execution_fill(int pose,int from,int to)
{
    while(from!=to)
    {
        bool rising=from<to;int level=rising ? from++ : --from;
        auto patch=execution_patches[pose][level];
        for(int i=patch.first;i<patch.first+patch.count;++i)
        {
            const auto& change=execution_changes[i];
            canvas_tiles[change.word/8].data[change.word%8]=rising ? change.after : change.before;
            dirty(change.word/8);
        }
    }
}
void text_canvas::execution_button(int pose,int level)
{
    for(int y=0;y<7;++y)for(int x=0;x<7;++x)
    {int tile=(y+6)*30+x+11;std::memcpy(canvas_tiles[tile].data,execution_buttons[pose]+(y*7+x)*8,32);dirty(tile);}
    execution_fill(pose,0,level);
}
void text_canvas::execution_complete()
{
    for(int y=0;y<7;++y)for(int x=0;x<7;++x)
    {int tile=(y+6)*30+x+11;std::memcpy(canvas_tiles[tile].data,execution_check+(y*7+x)*8,32);dirty(tile);}
}
void text_canvas::book_colors(const uint16_t* palette)
{
    auto colors=_bg->palette();
    colors.set_colors(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(palette),_palette_colors));
    std::memcpy(_colors,palette,32);
}
void text_canvas::palette_color(int index,uint16_t color)
{
    if(index<16)_colors[index]=color;
    auto palette=_bg->palette();palette.set_color(index,bn::color(color));
}
void text_canvas::book_restore(const uint8_t* paper,int x,int y,int width,int height)
{
    auto* pixels=reinterpret_cast<uint8_t*>(canvas_tiles);
    for(int row=y;row<y+height;++row)for(int tile=x/8;tile<=(x+width-1)/8;++tile)
    {
        int index=row/8*30+tile,left=bn::max(x,tile*8)-tile*8,right=bn::min(x+width,tile*8+8)-tile*8;
        auto* out=pixels+index*64+(row%8)*8;
        const auto* source=paper+index*32+(row%8)*4;
        if(left&1){out[left]=uint8_t(16+(source[left/2]>>4));++left;}
        if(right&1){out[right-1]=uint8_t(16+(source[right/2]&15));--right;}
        auto* pairs=reinterpret_cast<uint16_t*>(out+left);
        for(int col=left/2;col<right/2;++col)
        {
            unsigned value=source[col];
            *pairs++=uint16_t(0x1010u+(value&15)+((value&240)<<4));
        }
        dirty(index);
    }
}
// Keep resource destruction out of the drawing routines and the VBlank callback.
__attribute__((noinline)) void text_canvas::map_close()
{
    reader_close();
    _map_bg.reset();_map_tiles.reset();_map_bytes=0;_map_cells_pending=false;
    _map_hold_ui=_map_atomic_ui=false;
}
uint8_t* text_canvas::scratch(){return reinterpret_cast<uint8_t*>(canvas_tiles+600);}
void text_canvas::map_tint(uint16_t color,int amount)
{if(_map_bg){auto palette=_map_bg->palette();palette.set_fade(bn::color(color),bn::fixed::from_data(amount*16));}}
void text_canvas::map_page(const uint16_t* palette,int first,int count,bool story)
{
    if(!first)
    {
        release();bn::core::update();_choice_ui=_trial_canvas=false;_graphical=story;
        _map_tiles=bn::regular_bg_tiles_ptr::allocate(map_tile_capacity,bn::bpp_mode::BPP_4);
        auto colors=bn::bg_palette_ptr::create(bn::bg_palette_item(
            bn::span<const bn::color>(reinterpret_cast<const bn::color*>(palette),16),bn::bpp_mode::BPP_4));
        _map_bg=bn::regular_bg_ptr::create(0,0,bn::regular_bg_map_ptr::allocate(bn::size(64,32),*_map_tiles,colors));
        _map_tile_vram=_map_tiles->vram()->data();
        auto map=_map_bg->map();_map_cell_vram=map.vram()->data();
        _map_bg->set_priority(1);_map_bg->set_visible(false);allocate();
        _picture=nullptr;_picture_bytes=38400;
        if(!story)
        {auto ui_colors=_bg->palette();ui_colors.set_colors(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(palette),16));std::memcpy(_colors,palette,32);}
    }
    for(int tile=first;tile<first+count;++tile)
    {
        for(auto& word:canvas_tiles[tile].data)word=0x88888888u;
        dirty(tile);
    }
}
void text_canvas::map_position(int left,int top)
{
    _map_left=left;_map_top=top;
    _map_bg->set_top_left_position(4-left,25-top);
}
int text_canvas::map_cell_offset() const {return _map_bg->palette().id()<<12;}
void text_canvas::map_upload(const uint8_t* pixels,int first,int count)
{_map_data=pixels;_map_first=first;_map_bytes=count*32;}
void text_canvas::map_cells(const uint16_t* cells)
{_map_cells_data=cells;_map_cells_pending=true;}
void text_canvas::map_capture(int row,int count)
{
    for(int y=25+row;y<25+row+count;++y)for(int tx=0;tx<30;++tx)
    {
        int first=bn::max(4,tx*8),last=bn::min(236,tx*8+8),index=y/8*30+tx;
        int sx=first-4+_map_left,sy=y-25+_map_top,tile_x=sx/8;
        int cell=tile_x/32*1024+(sy/8)*32+tile_x%32;
        unsigned source=_map_tile_vram[_map_cell_vram[cell]&1023].data[sy%8]>>((sx%8)*4);
        if(sx%8)
        {
            ++tile_x;cell=tile_x/32*1024+(sy/8)*32+tile_x%32;
            source|=_map_tile_vram[_map_cell_vram[cell]&1023].data[sy%8]<<((8-sx%8)*4);
        }
        auto& target=canvas_tiles[index].data[y%8];
        unsigned occupied=(((target&0x77777777u)+0x77777777u)|target)&0x88888888u;
        unsigned mask=~((occupied>>3)*15u);
        mask&=(~0u<<((first%8)*4))&(last-tx*8==8 ? ~0u : (1u<<((last-tx*8)*4))-1);
        target=(target&~mask)|((source<<((first%8)*4))&mask);dirty(index);
    }
}
void text_canvas::map_reveal(bool atomic)
{
    _map_bg->set_visible(true);_map_hold_ui=false;_map_atomic_ui=atomic;
}
#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void text_canvas::stamp(int x,int y,int width,int height,const uint8_t* source,int stride)
{
    if(!stride)stride=width;
    auto* pixels=reinterpret_cast<uint8_t*>(canvas_tiles);
    if(_choice_ui)
    {
        for(int row=0;row<height;++row)
        {
            int py=y+row;
            for(int col=0;col<width;)
            {
                int px=x+col,count=bn::min(8-px%8,width-col);
                auto* out=pixels+(py/8*30+px/8)*64+(py%8)*8+px%8;
                for(int i=0;i<count;++i){int value=*source++;if(value)out[i]=uint8_t(value);}
                col+=count;
            }
            source+=stride-width;
        }
    }
    else for(int row=0;row<height;++row)
    {
        int py=y+row;
        for(int col=0;col<width;)
        {
            int px=x+col,count=bn::min(8-px%8,width-col);
            auto& target=canvas_tiles[py/8*30+px/8].data[py%8];unsigned word=target;
            for(int i=0;i<count;++i)
            {
                int value=*source++,shift=(px%8+i)*4;
                if(value)word=(word&~(15u<<shift))|(unsigned(value)<<shift);
            }
            target=word;col+=count;
        }
        source+=stride-width;
    }
    for(int row=y/8;row<=(y+height-1)/8;++row)for(int col=x/8;col<=(x+width-1)/8;++col)dirty(row*30+col);
}
#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void text_canvas::initial(int x,int y,const uint16_t* rows,ink color)
{
    auto* pixels=reinterpret_cast<uint8_t*>(canvas_tiles);
    for(int pass=0;pass<2;++pass)for(int row=0;row<16;++row)for(int col=0;col<16;++col)
    {
        if(!(rows[row]&(1<<(15-col))))continue;
        int px=x+col+1-pass,py=y+row+1-pass,tile=py/8*30+px/8;
        if(_choice_ui)pixels[tile*64+(py%8)*8+px%8]=uint8_t(pass ? color : shadow);
        else
        {
            auto& word=canvas_tiles[tile].data[py%8];int shift=(px%8)*4;
            word=(word&~(15u<<shift))|(unsigned(pass ? color : shadow)<<shift);
        }
    }
    for(int row=y/8;row<=(y+16)/8;++row)for(int col=x/8;col<=(x+16)/8;++col)dirty(row*30+col);
}
void text_canvas::trial_screen()
{
    if(!_trial_canvas)
    {
        release();bn::core::update();_graphical=_choice_ui=false;_trial_canvas=true;allocate();
        for(int i=0;i<600;++i)dirty(i);
    }
    clear(false);
}
void text_canvas::title_begin(bool timeup)
{
    trial_screen();_bg->set_visible(false);
    auto palette=_bg->palette();
    palette.set_colors(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(timeup ? trial_timeup_palette : trial_title_palette),16));
}
void text_canvas::title_restore(const trial_title_sequence& sequence,int frame)
{
    // The 4bpp UI uses half of the existing 8bpp canvas allocation.
    auto* refs=reinterpret_cast<uint16_t*>(canvas_tiles+600);
    int checkpoint=frame/30;
    std::memcpy(refs,sequence.snapshots[checkpoint],1200);
    for(int f=checkpoint*30+1;f<=frame;++f)
    {
        const auto& delta=sequence.frames[f];
        for(int i=0;i<delta.count;++i)refs[delta.changes[i*2]]=delta.changes[i*2+1];
    }
    for(int tile=0;tile<600;++tile)
    {
        std::memcpy(canvas_tiles[tile].data,trial_title_bytes+refs[tile]*32,32);
        dirty(tile);_occupied[tile]=true;
    }
}
void text_canvas::title_changes(const trial_title_frame& frame,int first,int count)
{
    for(int i=first;i<first+count;++i)
    {
        int tile=frame.changes[i*2],ref=frame.changes[i*2+1];
        std::memcpy(canvas_tiles[tile].data,trial_title_bytes+ref*32,32);
        dirty(tile);_occupied[tile]=true;
    }
}
void text_canvas::frame_corners(const uint32_t* tiles)
{
    for(int y=10;y<20;++y)for(int x=0;x<30;++x)
        if(x<6 || x>=25)
        {
            int tile=y*30+x;std::memcpy(canvas_tiles[tile].data,tiles+tile*8,32);dirty(tile);
        }
}
bool text_canvas::picture(const ui_picture& image,bool reload)
{
    if(reload || _picture!=&image || !_choice_ui)
    {
        if(!_choice_ui || _palette_colors!=256)
        {
            release();bn::core::update();_palette_colors=256;_choice_ui=true;_graphical=false;allocate();
        }
        _picture=&image;_picture_bytes=0;
        auto palette=_bg->palette();palette.set_colors(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(image.palette),256));
        for(int i=0;i<16;++i)_colors[i]=image.palette[i];
    }
    return _picture_bytes==38400;
}
bool text_canvas::save_page(const uint32_t* tiles,const uint16_t* palette)
{
    if(_save_tiles!=tiles)
    {
        if(!_save_tiles)
        {
            if(_bg || _map_bg){release();return false;}
            _graphical=_choice_ui=false;_palette_colors=16;allocate();
        }
        _save_tiles=tiles;_save_bytes=0;_picture=nullptr;_picture_bytes=38400;
        auto colors=_bg->palette();colors.set_colors(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(palette),16));
        std::memcpy(_colors,palette,32);
    }
    return _save_bytes==19200;
}
void text_canvas::prepare()
{
    if(_save_tiles && _save_bytes<19200)
    {
        int amount=bn::min(4096,19200-_save_bytes);
        std::memcpy(reinterpret_cast<uint8_t*>(canvas_tiles)+_save_bytes,reinterpret_cast<const uint8_t*>(_save_tiles)+_save_bytes,amount);
        for(int tile=_save_bytes/32;tile<(_save_bytes+amount)/32;++tile)dirty(tile);
        _save_bytes+=amount;return;
    }
    if(!_picture || _picture_bytes==38400)return;
    int amount=bn::min(4096,38400-_picture_bytes);auto* target=reinterpret_cast<uint8_t*>(canvas_tiles)+_picture_bytes;
    if(_picture->raw)std::memcpy(target,reinterpret_cast<const uint8_t*>(_picture->raw)+_picture_bytes,amount);
    else
    {
        const auto& block=_picture->blocks[_picture_bytes/4096];
        if(block.codec)lzss_decode(block.data,target,amount);else std::memcpy(target,block.data,amount);
    }
    for(int tile=_picture_bytes/64;tile<(_picture_bytes+amount)/64;++tile)dirty(tile);
    _picture_bytes+=amount;
}
#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void text_canvas::restore(int x,int y,int width,int height)
{
    if(_save_tiles)
    {
        for(int ty=y/8;ty<(y+height)/8;++ty)for(int tx=x/8;tx<(x+width)/8;++tx)
        {
            int tile=ty*30+tx;auto* target=canvas_tiles[tile].data;const auto* source=_save_tiles+tile*8;
            bool changed=false;for(int row=0;row<8;++row)if(target[row]!=source[row]){target[row]=source[row];changed=true;}
            if(changed)dirty(tile);
        }
        return;
    }
    for(int ty=y/8;ty<(y+height)/8;++ty)for(int tx=x/8;tx<(x+width)/8;++tx)
    {
        int tile=ty*30+tx;auto* target=reinterpret_cast<uint32_t*>(canvas_tiles)+tile*16;const auto* source=_picture->raw+tile*16;
        bool changed=false;for(int row=0;row<16;++row)if(target[row]!=source[row]){target[row]=source[row];changed=true;}
        if(changed)dirty(tile);
    }
}
#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void text_canvas::blit(int x,int y,int width,int height,const uint32_t* source)
{
    for(int ty=0;ty<height/8;++ty)for(int tx=0;tx<width/8;++tx)
    {
        int tile=(y/8+ty)*30+x/8+tx;auto* target=reinterpret_cast<uint32_t*>(canvas_tiles)+tile*16;bool changed=false;
        for(int row=0;row<16;++row)
        {
            unsigned value=*source++,occupied=(((value&0x7f7f7f7fu)+0x7f7f7f7fu)|value)&0x80808080u,mask=(occupied>>7)*255;
            unsigned result=(target[row]&~mask)|value;if(target[row]!=result){target[row]=result;changed=true;}
        }
        if(changed)dirty(tile);
    }
}

void text_canvas::present()
{
    // Dirty tiles are uploaded by application during VBlank.
}

void text_canvas::toast(int id)
{
    if(_choice_ui)id=0;
    if(_bg && !_choice_ui && (id==3 || _toast==3))
    {auto palette=_bg->palette();palette.set_color(5,id==3 ? bn::color(31,4,6) : bn::color(_colors[5]));}
    if(id==_toast)return;
    _toast=uint8_t(id);
    for(int y=toast_top/8;y<(toast_top+toast_height)/8;++y)
        for(int x=toast_left/8;x<(toast_left+toast_width)/8;++x)dirty(y*30+x);
}

#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void text_canvas::decoration(const uint32_t* tiles)
{
    graphical(true);
    bool full=_decoration!=tiles;_decoration=tiles;
    for(int index = 0; index < 600; ++index)
    {
        if(!compact_index(index))continue;
        // Base ornaments persist; restore only tiles touched by text/nameplate.
        if(!full && !_occupied[index])continue;
        auto* target=canvas_tiles[index].data;
        const auto* source=tiles+index*8;
        bool changed=false;
        for(int row=0;row<8;++row)
        {
            if(target[row]!=source[row]){target[row]=source[row];changed=true;}
        }
        if(changed)dirty(index);
        _occupied[index]=false;
    }
}

#ifdef __arm__
__attribute__((section(".ewram.ui_setup"),target("arm")))
#endif
void text_canvas::flat_screen(const uint16_t* palette)
{
    if(!_choice_ui || !_bg || _palette_colors!=256)
    {
        release();bn::core::update();
        _palette_colors=256;_choice_ui=true;_graphical=false;allocate();
    }
    _picture=nullptr;_picture_bytes=38400;_decoration=nullptr;
    book_colors(palette);
    bn::memory::set_words(0,1200*8,canvas_tiles[0].data);
    std::memset(_occupied,0,sizeof(_occupied));
    for(int tile=0;tile<600;++tile)dirty(tile);
}

void text_canvas::pattern_restore(const ui_pattern& pattern,int x,int y,int width,int height)
{
    for(int ty=y/8;ty<=(y+height-1)/8;++ty)for(int tx=x/8;tx<=(x+width-1)/8;++tx)
    {
        int index=ty*30+tx,left=bn::max(x,tx*8)-tx*8,right=bn::min(x+width,tx*8+8)-tx*8;
        int top=bn::max(y,ty*8)-ty*8,bottom=bn::min(y+height,ty*8+8)-ty*8;
        const auto* source=pattern.tiles+pattern.cells[index]*8;auto* out=canvas_tiles[index].data;
        if(!left && right==8)
            for(int row=top;row<bottom;++row)out[row]=source[row];
        else
        {unsigned mask=(~0u<<(left*4))&(right==8 ? ~0u : (1u<<(right*4))-1);for(int row=top;row<bottom;++row)out[row]=(out[row]&~mask)|(source[row]&mask);}
        dirty(index);_occupied[index]=true;
    }
}
void text_canvas::pattern_screen(const ui_pattern& pattern)
{
    clear();_palette_colors=16;
    auto palette=_bg->palette();palette.set_colors(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(pattern.palette),16));
    std::memcpy(_colors,pattern.palette,32);
    bn::bg_palettes::set_transparent_color(bn::color(pattern.palette[0]));
}

void text_canvas::choice_screen()
{
    _picture=nullptr;_picture_bytes=38400;_trial_canvas=false;
    if(! _choice_ui || _palette_colors!=256)
    {
        release();
        bn::core::update();
        _palette_colors=256;_choice_ui=true;_graphical=false;allocate();
        for(int index=0;index<600;++index)dirty(index);
    }
    auto palette=_bg->palette();
    palette.set_colors(bn::span<const bn::color>(reinterpret_cast<const bn::color*>(choice_ui_palette),256));
    for(int i=0;i<16;++i)_colors[i]=choice_ui_palette[i];
    bn::memory::copy(choice_ui_background[0],600*16,reinterpret_cast<uint32_t*>(canvas_tiles)[0]);
    for(int tile=0;tile<600;++tile)dirty(tile);
}

#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void text_canvas::choice_button(int y,const char* label,bool highlighted,bool bad,int height)
{
    const auto* artwork=choice_ui_buttons[int(bad)*2+int(highlighted)];
    for(int ty=0;ty<height/8;++ty)for(int tx=0;tx<choice_button_width/8;++tx)
    {
        int source_row=ty<2 ? ty : ty>=height/8-2 ? 5-(height/8-ty) : 2;
        const auto* source=artwork+(source_row*(choice_button_width/8)+tx)*16;
        int index=(y/8+ty)*30+choice_button_left/8+tx;
        bool changed=false;
        auto* target=reinterpret_cast<uint32_t*>(canvas_tiles)+index*16;
        for(int row=0;row<16;++row)
        {
            unsigned value=*source++;
            unsigned occupied=(((value&0x7f7f7f7fu)+0x7f7f7f7fu)|value)&0x80808080u;
            unsigned mask=(occupied>>7)*255;
            unsigned result=(target[row]&~mask)|value;
            if(target[row]!=result){target[row]=result;changed=true;}
        }
        if(changed)dirty(index);
    }
    int lines=1;for(const char* p=label;*p;++p)lines+=*p=='\n';
    int left=bad ? choice_text_bad_left : choice_text_left;
    int top=y+height/2-lines*8;
    for(const char* begin=label;*begin;top+=16)
    {
        const char* end=begin;int width=0;
        while(*end && *end!='\n')width+=decode(end)<128 ? dialogue_ascii_width : dialogue_wide_width;
        cursor position{(left+choice_text_right-width)/2,top};
        text(position,dialogue_layout{left,top,choice_text_right,top+13},begin,end,white);
        begin=*end ? end+1 : end;
    }
}

void text_canvas::graphical(bool enabled)
{
    if(enabled==_graphical && ! _choice_ui && !_execution_ui && _bg)return;
    _decoration=nullptr;
    _picture=nullptr;_picture_bytes=38400;
    _graphical=enabled;_choice_ui=_execution_ui=false;_trial_canvas=false;
    _bg.reset();_tiles.reset();
    bn::core::update(); // Reclaim the previous fixed canvas tile allocation.
    allocate();
    for(int index=0;index<600;++index)dirty(index);
}
#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
int text_canvas::commit(int bytes)
{
    if(_reader_active)
    {
        int budget=bytes,used=0;
        int rows=_reader_full ? 32 : 16;
        for(int row=0;row<rows && budget>=28*32;)
        {
            if(!(_reader_dirty&(1u<<row))){++row;continue;}
            int count=1;
            while(row+count<rows && (_reader_dirty&(1u<<(row+count))) && budget>=(count+1)*28*32)++count;
            bn::memory::copy(reader_pixels(_reader_full)[row*28],count*28,_map_tile_vram[row*28]);
            _reader_dirty&=~(((1u<<count)-1)<<row);used+=count*28*32;budget-=count*28*32;row+=count;
        }
        bytes-=used;
    }
    if(_map_bytes)
    {
        int count=bn::min(bytes,_map_bytes)/32;
        bn::memory::copy(*reinterpret_cast<const uint32_t*>(_map_data),count*8,_map_tile_vram[_map_first].data[0]);
        _map_data+=count*32;_map_first+=count;_map_bytes-=count*32;bytes-=count*32;
    }
    if(_map_cells_pending && !_map_bytes && bytes>=4096)
    {
        bn::memory::copy(*_map_cells_data,2048,*_map_cell_vram);_map_cells_pending=false;bytes-=4096;
    }
    if(_map_hold_ui)return bytes;
    if(_map_atomic_ui){bytes=19200;_map_atomic_ui=false;}
    if(! _tiles || !_dirty_count)return bytes;
    auto vram = _tiles->vram().value();
    int units=_choice_ui ? 2 : 1,tile_bytes=units*32;
    for(int index=0;index<600 && bytes>=tile_bytes;)
    {
        if(!_dirty[index]){++index;continue;}
        int physical=_execution_ui ? execution_index(index) : _graphical ? compact_index(index) : index*units;
        if(_graphical && !physical){_dirty[index++]=false;--_dirty_count;continue;}
        int toast=_toast ? toast_index(index) : -1;
        if(toast>=0)
        {
            // Composite during upload: the underlying CPU canvas stays intact.
            bn::tile tile;
            const auto* source=toast_tiles[_toast-1]+toast*8;
            for(int row=0;row<8;++row)
            {
                unsigned value=source[row],occupied=(((value&0x77777777u)+0x77777777u)|value)&0x88888888u;
                tile.data[row]=(canvas_tiles[index].data[row]&~((occupied>>3)*15))|value;
            }
            bn::memory::copy(tile,1,vram[physical]);
            _dirty[index++]=false;--_dirty_count;bytes-=32;continue;
        }
        int count=1,limit=bn::min(bytes/tile_bytes,600-index);
        while(count<limit && _dirty[index+count] &&
              (!_toast || toast_index(index+count)<0) &&
              (!_graphical || (_execution_ui ? execution_index(index+count) : compact_index(index+count))==physical+count))++count;
        bn::memory::copy(canvas_tiles[index*units],count*units,vram[physical]);
        for(int i=0;i<count;++i)_dirty[index+i]=false;
        _dirty_count-=count;bytes-=count*tile_bytes;index+=count;
    }
    return bytes;
}

#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void text_canvas::label_word(int x,int y,uint32_t value,const uint8_t*)
{
    if(!value)return;
    int index=(y/8)*30+x/8;
    auto* target=reinterpret_cast<uint32_t*>(canvas_tiles)+index*16+(y%8)*2;
    unsigned pixels[2]={unsigned(label_inks[value&255])|(unsigned(label_inks[(value>>8)&255])<<16),
        unsigned(label_inks[(value>>16)&255])|(unsigned(label_inks[value>>24])<<16)};
    for(int half=0;half<2;++half)
    {
        unsigned v=pixels[half],occupied=(((v&0x7f7f7f7fu)+0x7f7f7f7fu)|v)&0x80808080u;
        target[half]=(target[half]&~((occupied>>7)*255))|v;
    }
    dirty(index);
}
#ifdef __arm__
__attribute__((section(".ewram.ui_setup"),target("arm")))
#endif
void text_canvas::label_base(int x,int y,int width,const uint8_t* inks)
{
    for(int row=0;row<25;++row)for(int col=0;col<width;col+=8)
    {
        int index=((row+3)/8*12+(col+6)/8)*8+(row+3)%8;
        unsigned value=(nameplate_diamond[index]>>24)|(nameplate_diamond[index+8]<<8);
        label_word(x+col,y+row,value,inks);
    }
}
#ifdef __arm__
__attribute__((section(".iwram"),target("arm")))
#endif
void text_canvas::label_text(int x,int y,const label_art& art,const uint8_t* inks)
{
    const uint32_t* source=art.pixels;
    for(int i=0;i<art.count;++i)
    {
        int position=art.positions[i],col=position&15,row=position>>4;
        if(_choice_ui)
        {
            for(int line=0;line<8;++line)label_word(x+col*8,y+row*8+line,*source++,inks);
        }
        else
        {
            int index=(y/8+row)*30+x/8+col;
            for(int line=0;line<8;++line)
            {
                unsigned value=*source++,occupied=(((value&0x77777777u)+0x77777777u)|value)&0x88888888u;
                auto& target=canvas_tiles[index].data[line];target=(target&~((occupied>>3)*15))|value;
            }
            dirty(index);
        }
    }
}
#ifdef __arm__
__attribute__((section(".ewram.ui_setup"),target("arm")))
#endif
void text_canvas::category_label(int x,int y,const label_art& art,const uint8_t* inks,int color)
{
    label_palette(inks,color);
    label_base(x,y,40,inks);label_text(x,y,art,inks);
}
void text_canvas::clock(int seconds,const label_art& frame,const uint8_t* inks)
{
    if(_choice_ui && label_ink_source!=inks)label_palette(inks,0);
    label_text(176,0,frame,inks);
    int minutes=seconds/60,part=seconds%60;
    char value[]="00:00";
    value[0]=char('0'+minutes/10);value[1]=char('0'+minutes%10);
    value[3]=char('0'+part/10);value[4]=char('0'+part%10);
    text(200,6,value,white,232,20);
}
#ifdef __arm__
__attribute__((section(".ewram.ui_setup"),target("arm")))
#endif
void text_canvas::name_label(const label_art* art, uint16_t color)
{
    _colors[3] = color;
    auto palette=_bg->palette();palette.set_color(3,bn::color(color));
    // Shared neutral diamond; the representative color belongs to the initial only.
    for(int y=0;y<4;++y)
    {
        int index=(11+y)*30+1;
        bn::memory::copy(nameplate_diamond[y*96],96,canvas_tiles[index].data[0]);
        for(int x=0;x<12;++x)dirty(index+x);
    }
    label_text(8,88,*art,nullptr);
}

}
