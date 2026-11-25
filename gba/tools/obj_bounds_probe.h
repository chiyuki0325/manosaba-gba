/* Host diagnostic: compare OBJ sampling with/without its output bounding box.
 * The source texture, inverse affine registers, palette and priority are kept.
 * BG occlusion/blending is not simulated: the count is a clipping candidate,
 * separate from the native/unlimited-scanline framebuffer comparison. */
#ifndef MANOSABA_OBJ_BOUNDS_PROBE_H
#define MANOSABA_OBJ_BOUNDS_PROBE_H
/* A bottom-edge 128px OBJ box can also put ink at the top after Y wraps.
 * This is independent of source clipping and scanline exhaustion. */
static unsigned object_y_wrap_ink(void)
{
    static const int widths[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
    static const int heights[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};
    unsigned ink = 0;
    for (int i = 0; i < 128; ++i)
    {
        unsigned a = core->busRead16(core, 0x07000000 + i * 8), b = core->busRead16(core, 0x07000002 + i * 8),
                 c = core->busRead16(core, 0x07000004 + i * 8);
        if ((a & 768) != 768 || (a >> 14) == 3)
            continue;
        int w = widths[a >> 14][b >> 14], h = heights[a >> 14][b >> 14], top = a & 255;
        if (top >= 160 || top + 2 * h <= 256)
            continue;
        int left = b & 511;
        if (left >= 256)
            left -= 512;
        unsigned matrix = 0x07000000 + ((b >> 9) & 31) * 32;
        int pa = (int16_t)core->busRead16(core, matrix + 6), pb = (int16_t)core->busRead16(core, matrix + 14);
        int pc = (int16_t)core->busRead16(core, matrix + 22), pd = (int16_t)core->busRead16(core, matrix + 30);
        for (int y = 0; y < top + 2 * h - 256; ++y)
            for (int x = left < 0 ? 0 : left; x < left + 2 * w && x < 240; ++x)
            {
                int dx = x - left - w, dy = y + 256 - top - h;
                int u = ((pa * dx + pb * dy) >> 8) + w / 2, v = ((pc * dx + pd * dy) >> 8) + h / 2;
                if (u < 0 || v < 0 || u >= w || v >= h)
                    continue;
                unsigned tile = (c & 1023) + (v / 8) * (w / 8) + u / 8;
                unsigned p = core->busRead8(core, 0x06010000 + (tile & 1023) * 32 + (v % 8) * 4 + (u % 8) / 2);
                ink += !!((p >> ((u & 1) * 4)) & 15);
            }
    }
    return ink;
}
static unsigned object_bounds_clipping(unsigned* above_dialogue)
{
    static const int widths[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
    static const int heights[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};
    struct object
    {
        int x, y, w, h, bw, bh, pa, pb, pc, pd, affine, hflip, vflip, priority, tile, bank;
    } objects[128];
    unsigned char texels[32768];
    unsigned short palette[256];
    int count = 0;
    for (unsigned i = 0; i < 32768; ++i)
        texels[i] = core->busRead8(core, 0x06010000 + i);
    for (unsigned i = 0; i < 256; ++i)
        palette[i] = core->busRead16(core, 0x05000200 + i * 2);
    for (int i = 0; i < 128; ++i)
    {
        unsigned a = core->busRead16(core, 0x07000000 + i * 8), b = core->busRead16(core, 0x07000002 + i * 8),
                 c = core->busRead16(core, 0x07000004 + i * 8);
        if ((!(a & 256) && (a & 512)) || (a >> 14) == 3 || (a & 8192))
            continue;
        struct object* o = &objects[count++];
        o->x = b & 511;
        o->y = a & 255;
        if (o->x >= 256)
            o->x -= 512;
        if (o->y >= 160)
            o->y -= 256;
        o->w = widths[a >> 14][b >> 14];
        o->h = heights[a >> 14][b >> 14];
        o->bw = (a & 768) == 768 ? 2 * o->w : o->w;
        o->bh = (a & 768) == 768 ? 2 * o->h : o->h;
        o->affine = a & 256;
        o->hflip = b & 4096;
        o->vflip = b & 8192;
        o->priority = (c >> 10) & 3;
        o->tile = c & 1023;
        o->bank = c >> 12;
        unsigned matrix = 0x07000000 + ((b >> 9) & 31) * 32;
        o->pa = (int16_t)core->busRead16(core, matrix + 6);
        o->pb = (int16_t)core->busRead16(core, matrix + 14);
        o->pc = (int16_t)core->busRead16(core, matrix + 22);
        o->pd = (int16_t)core->busRead16(core, matrix + 30);
    }
    unsigned changed = 0;
    *above_dialogue = 0;
    for (int y = 0; y < 160; ++y)
        for (int x = 0; x < 240; ++x)
        {
            int normal = -1, unbounded = -1, normal_priority = 4, full_priority = 4;
            for (int i = 0; i < count; ++i)
            {
                const struct object* o = &objects[i];
                int u = x - o->x, v = y - o->y, inside = u >= 0 && v >= 0 && u < o->bw && v < o->bh;
                if (o->affine)
                {
                    int dx = u - o->bw / 2, dy = v - o->bh / 2;
                    u = ((o->pa * dx + o->pb * dy) >> 8) + o->w / 2;
                    v = ((o->pc * dx + o->pd * dy) >> 8) + o->h / 2;
                }
                else
                {
                    if (o->hflip)
                        u = o->w - 1 - u;
                    if (o->vflip)
                        v = o->h - 1 - v;
                }
                if (u < 0 || v < 0 || u >= o->w || v >= o->h)
                    continue;
                unsigned tile = o->tile + (v / 8) * (o->w / 8) + u / 8;
                unsigned packed = texels[((tile & 1023) * 32 + (v % 8) * 4 + (u % 8) / 2) & 32767],
                         ink = (packed >> ((u & 1) * 4)) & 15;
                if (!ink)
                    continue;
                int rgb = palette[o->bank * 16 + ink];
                if (o->priority < full_priority)
                {
                    unbounded = rgb;
                    full_priority = o->priority;
                }
                if (inside && o->priority < normal_priority)
                {
                    normal = rgb;
                    normal_priority = o->priority;
                }
            }
            if (normal != unbounded)
            {
                ++changed;
                *above_dialogue += y < 104;
            }
        }
    return changed;
}
#endif
