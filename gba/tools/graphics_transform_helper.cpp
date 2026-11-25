// Offline geometry uses the same lookup tables and reciprocal math as Butano.
#include <cstdio>
#include "bn_array.h"
#include "bn_affine_mat_attributes.h"
namespace bn
{
const auto sine_values = []
{
    array<int16_t, sin_lut_size> a{};
    for (int i = 0; i < sin_lut_size; ++i)
        a[i] = calculate_sin_lut_value(i * 65536 / (sin_lut_size - 1));
    return a;
}();
const array<int16_t, sin_lut_size>& sin_lut = sine_values;
const auto reciprocal_values = []
{
    array<uint16_t, reciprocal_16_lut_size> a{};
    for (int i = 1; i < reciprocal_16_lut_size; ++i)
        a[i] = calculate_reciprocal_lut_value<16>(i).data();
    return a;
}();
const array<uint16_t, reciprocal_16_lut_size>& reciprocal_16_lut = reciprocal_values;
} // namespace bn
int main()
{
    int scale, angle;
    while (std::scanf("%d %d", &scale, &angle) == 2)
    {
        auto a = bn::fixed::from_data(angle * 16), s = bn::fixed::from_data(scale * 16);
        bn::affine_mat_attributes matrix;
        matrix.set_scale(s);
        matrix.set_rotation_angle_safe(-a);
        std::printf("%d %d %d %d %d %d\n", bn::degrees_lut_cos_safe(a).data(), bn::degrees_lut_sin_safe(a).data(),
                    matrix.pa_register_value(), matrix.pb_register_value(), matrix.pc_register_value(),
                    matrix.pd_register_value());
    }
}
