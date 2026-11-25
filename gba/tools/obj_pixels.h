/* Test-only GBA OBJ coverage probe; UI backgrounds are deliberately excluded. */
#ifndef MANOSABA_TEST_OBJ_PIXELS_H
#define MANOSABA_TEST_OBJ_PIXELS_H
static int object_pixel(int x, int y, unsigned* color)
{
    static const int w[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
    static const int h[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};
    for (int i = 0; i < 128; ++i)
    {
        unsigned a = core->busRead16(core, 0x07000000 + i * 8), b = core->busRead16(core, 0x07000002 + i * 8),
                 c = core->busRead16(core, 0x07000004 + i * 8);
        if ((!(a & 0x100) && (a & 0x200)) || (a >> 14) == 3)
            continue;
        int width = w[a >> 14][b >> 14], height = h[a >> 14][b >> 14], left = b & 511, top = a & 255;
        if (left >= 256)
            left -= 512;
        if (top >= 160)
            top -= 256;
        int box_w = width, box_h = height;
        if ((a & 0x300) == 0x300)
        {
            box_w *= 2;
            box_h *= 2;
        }
        int u = x - left, v = y - top;
        if (u < 0 || v < 0 || u >= box_w || v >= box_h)
            continue;
        if (a & 0x100)
        {
            unsigned matrix = 0x07000000 + ((b >> 9) & 31) * 32;
            int pa = (int16_t)core->busRead16(core, matrix + 6), pb = (int16_t)core->busRead16(core, matrix + 14);
            int pc = (int16_t)core->busRead16(core, matrix + 22), pd = (int16_t)core->busRead16(core, matrix + 30);
            int dx = u - box_w / 2, dy = v - box_h / 2;
            u = ((pa * dx + pb * dy) >> 8) + width / 2;
            v = ((pc * dx + pd * dy) >> 8) + height / 2;
            if (u < 0 || v < 0 || u >= width || v >= height)
                continue;
        }
        else
        {
            if (b & 0x1000)
                u = width - 1 - u;
            if (b & 0x2000)
                v = height - 1 - v;
        }
        unsigned tile = (c & 1023) + (v / 8) * (width / 8) + u / 8;
        unsigned packed = core->busRead8(core, 0x06010000 + tile * 32 + (v % 8) * 4 + (u % 8) / 2);
        unsigned index = (packed >> ((u & 1) * 4)) & 15;
        if (index)
        {
            if (color)
                *color = core->busRead16(core, 0x05000200 + ((c >> 12) * 16 + index) * 2);
            return (a & 0x100) ? (int)((b >> 9) & 31) + 1 : 1;
        }
    }
    return 0;
}
static int object_ink(int x, int y)
{
    return object_pixel(x, y, NULL);
}
#endif
