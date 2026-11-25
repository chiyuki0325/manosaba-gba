#include "video_upload.h"
#include "graphics_profile.h"
#include "bn_regular_bg_map_item.h"
#include "bn_affine_bg_map_item.h"
#include "bn_regular_bg_tiles_ptr.h"
#include "bn_affine_bg_tiles_ptr.h"
#include "bn_bg_palette_ptr.h"
#include "bn_vector.h"
#include "bn_memory.h"
#include "bn_hw_dma.h"
// Reclaim allocator records only. Hardware still shows the previous frame.
namespace bn::bg_blocks_manager
{
void update();
}
namespace bn::sprite_tiles_manager
{
void update();
}
namespace bn::hdma_manager
{
bool commit_entries(bool use_dma);
}
namespace manosaba::video_upload
{
bool finishing = false;
BN_DATA_EWRAM_BSS bn::tile scenery[1024];
alignas(4) BN_DATA_EWRAM_BSS uint8_t book_pixels[216 * 120];
bool book_cache_dirty = false;
namespace
{
struct copy_job
{
    const uint16_t* source;
    uint16_t* target;
    uint16_t bytes, offset;
};
struct protected_range
{
    uintptr_t first, last;
};
BN_DATA_EWRAM_BSS bn::vector<copy_job, 128> jobs;
BN_DATA_EWRAM_BSS bn::vector<protected_range, 32> protected_ranges;
BN_DATA_EWRAM_BSS bn::tile overlay_pixels[36 + 9]; // Toast and fast-forward icon.
int toast_count = 0;
bool recording = false, preloaded = false;
void write(const copy_job& job, int first, int bytes)
{
    auto* target = job.target + first / 2;
    const auto* source = job.source + first / 2;
    if (job.offset)
        for (int i = 0; i < bytes / 2; ++i)
            target[i] = uint16_t(source[i] + job.offset);
    else if ((reinterpret_cast<uintptr_t>(source) | reinterpret_cast<uintptr_t>(target) | unsigned(bytes)) & 3)
        bn::hw::dma::copy_half_words(source, bytes / 2, target);
    else
        bn::hw::dma::copy_words(source, bytes / 4, target);
}
void split_copy(const copy_job& job, bool protected_part)
{
    uintptr_t first = reinterpret_cast<uintptr_t>(job.target), last = first + job.bytes;
    for (const auto& range : protected_ranges)
    {
        uintptr_t left = bn::max(first, range.first), right = bn::min(last, range.last);
        if (left >= right)
            continue;
        if (protected_part)
            write(job, int(left - reinterpret_cast<uintptr_t>(job.target)), int(right - left));
        else if (first < left)
            write(job, int(first - reinterpret_cast<uintptr_t>(job.target)), int(left - first));
        first = right;
        if (first == last)
            return;
    }
    if (!protected_part && first < last)
        write(job, int(first - reinterpret_cast<uintptr_t>(job.target)), int(last - first));
}
} // namespace
bool pending()
{
    return !jobs.empty();
}
void start_frame()
{
    protected_ranges.clear();
    recording = true;
    preloaded = false;
    toast_count = 0;
    protect(reinterpret_cast<const void*>(0x06010000), 32768);
}
void protect(const void* address, int bytes)
{
    protected_ranges.push_back(
        {reinterpret_cast<uintptr_t>(address), reinterpret_cast<uintptr_t>(address) + unsigned(bytes)});
}
void protect(const bn::regular_bg_ptr& bg)
{
    if (!bg.visible())
        return;
    const auto& tiles = bg.tiles();
    protect(reinterpret_cast<const void*>(0x06000000 + tiles.id() * 2048), tiles.tiles_count() * 32);
    auto owner = bg.map();
    protect(reinterpret_cast<const void*>(0x06000000 + owner.id() * 2048),
            owner.dimensions().width() * owner.dimensions().height() * 2);
}
void protect(const bn::affine_bg_ptr& bg)
{
    if (!bg.visible())
        return;
    const auto& tiles = bg.tiles();
    protect(reinterpret_cast<const void*>(0x06000000 + tiles.id() * 2048), tiles.tiles_count() * 32);
    auto owner = bg.map();
    protect(reinterpret_cast<const void*>(0x06000000 + owner.id() * 2048),
            owner.dimensions().width() * owner.dimensions().height());
}
const bn::tile& stable_tile(const bn::tile& tile)
{
    auto& result = overlay_pixels[toast_count++];
    result = tile;
    return result;
}
void prepare()
{
    unsigned bytes = 0;
    for (const auto& job : jobs)
        bytes += job.bytes;
    if (bytes <= 4096)
        return;
    // Sort/merge the old display's ranges once per large resource change.
    for (int i = 1; i < protected_ranges.size(); ++i)
        for (int j = i; j && protected_ranges[j].first < protected_ranges[j - 1].first; --j)
            bn::swap(protected_ranges[j], protected_ranges[j - 1]);
    for (int i = 1; i < protected_ranges.size();)
    {
        auto& previous = protected_ranges[i - 1];
        const auto& next = protected_ranges[i];
        if (next.first <= previous.last)
        {
            previous.last = bn::max(previous.last, next.last);
            protected_ranges.erase(protected_ranges.begin() + i);
        }
        else
            ++i;
    }
    begin();
    for (const auto& job : jobs)
        split_copy(job, false);
    end();
    preloaded = true;
}
void reclaim()
{
    bn::bg_blocks_manager::update();
    bn::sprite_tiles_manager::update();
}
bn::regular_bg_map_ptr map(const bn::regular_bg_map_item& item, const bn::regular_bg_tiles_ptr& tiles,
                           const bn::bg_palette_ptr& palette)
{
    auto result = bn::regular_bg_map_ptr::allocate(item.dimensions(), tiles, palette);
    graphics_performance.upload_pending = 1;
    jobs.push_back({item.cells_ptr(), result.vram()->data(),
                    uint16_t(item.dimensions().width() * item.dimensions().height() * 2),
                    uint16_t(result.tiles_offset() + (result.palette_banks_offset() << 12))});
    return result;
}
bn::affine_bg_map_ptr map(const bn::affine_bg_map_item& item, const bn::affine_bg_tiles_ptr& tiles,
                          const bn::bg_palette_ptr& palette)
{
    auto result = bn::affine_bg_map_ptr::allocate(item.dimensions(), tiles, palette);
    graphics_performance.upload_pending = 1;
    jobs.push_back({reinterpret_cast<const uint16_t*>(item.cells_ptr()),
                    reinterpret_cast<uint16_t*>(result.vram()->data()),
                    uint16_t(item.dimensions().width() * item.dimensions().height()), 0});
    return result;
}
void copy(const void* source, void* target, int bytes)
{
    graphics_performance.upload_pending = 1;
    jobs.push_back({static_cast<const uint16_t*>(source), static_cast<uint16_t*>(target), uint16_t(bytes), 0});
}
void begin()
{
    bn::hw::dma::stop_hdma(3);
}
void end()
{
    bn::hdma_manager::commit_entries(false);
}
void transfer(const void* source, void* target, int bytes)
{
    if (recording)
        copy(source, target, bytes);
    else
        write({static_cast<const uint16_t*>(source), static_cast<uint16_t*>(target), uint16_t(bytes), 0}, 0, bytes);
}
void commit()
{
    recording = false;
    for (const auto& job : jobs)
    {
        if (preloaded)
            split_copy(job, true);
        else
            write(job, 0, job.bytes);
    }
    jobs.clear();
    preloaded = false;
    graphics_performance.upload_pending = 0;
}
} // namespace manosaba::video_upload
