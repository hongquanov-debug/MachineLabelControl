#include "orient_math.h"

#include <math.h>

float orient_normalize_deg(float deg)
{
    deg = fmodf(deg, 360.0f);
    if (deg > 180.0f) {
        deg -= 360.0f;
    } else if (deg <= -180.0f) {
        deg += 360.0f;
    }
    return deg;
}

int32_t orient_m2_pulses(float theta_vision_deg, int16_t recipe_offset_x100,
                         int16_t hmi_offset_x100, uint16_t scale_x1000,
                         float m2_pulses_per_deg)
{
    float total = theta_vision_deg + (float)recipe_offset_x100 / 100.0f + (float)hmi_offset_x100 / 100.0f;
    total = orient_normalize_deg(total);
    float m2_deg = total * (float)scale_x1000 / 1000.0f;
    return (int32_t)lroundf(m2_deg * m2_pulses_per_deg);
}
