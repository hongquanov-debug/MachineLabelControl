/**
 * @file params.h
 * @brief Runtime parameters written by HMI / SCADA (holding registers 40003, 40004, jog).
 */
#ifndef PARAMS_H
#define PARAMS_H

#include <stdint.h>

typedef struct {
    uint16_t speed_cans_min;      /* 40003: 60 .. 156 */
    int16_t  theta_offset_x100;   /* 40004: -1800 .. +1800 (= -18.00 .. +18.00 deg) */
    uint16_t jog_axis;            /* 40010 (ext): 0=M2 .. 4=M6 */
    int16_t  jog_dir;             /* 40011 (ext): -1 / 0 / +1 */
} RuntimeParams_t;

void params_init(uint16_t speed_cans_min);
const volatile RuntimeParams_t *params_get(void);
volatile RuntimeParams_t *params_mut(void);

#endif
