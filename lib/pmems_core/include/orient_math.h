/**
 * @file orient_math.h
 * @brief Can orientation compensation (station 2: P3/P4 + PU wheel M2).
 *
 *   theta_total = theta_vision + recipe.tab_to_label_offset + HMI offset (40004)
 *   normalised to (-180, +180]
 *   M2 angle    = theta_total * (D_can / D_PU) = theta_total * scale_x1000 / 1000
 *   M2 pulses   = M2 angle * 4.444 pulses/deg
 */
#ifndef ORIENT_MATH_H
#define ORIENT_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

float   orient_normalize_deg(float deg);
int32_t orient_m2_pulses(float theta_vision_deg, int16_t recipe_offset_x100,
                         int16_t hmi_offset_x100, uint16_t scale_x1000,
                         float m2_pulses_per_deg);

#ifdef __cplusplus
}
#endif

#endif
