/**
 * @file motion.h
 * @brief Axis layer on top of bsp_pulse: engineering units, kinematic limits,
 *        soft limits, driver enable and homing (Spec §4). Task context only.
 */
#ifndef MOTION_H
#define MOTION_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_pulse.h"

typedef struct {
    const char *name;
    const char *unit;       /* "deg" / "mm" */
    float ppu;              /* pulses per unit */
    float vmax_hz;
    float accel_hz_s;
    float home_hz;
    bool  home_forward;     /* search direction towards the home sensor */
    bool  has_home;
    bool  has_soft_limits;
    float soft_min, soft_max;
    float home_pos;         /* position (units) assigned at the home sensor */
} AxisParam_t;

const AxisParam_t *motion_param(Axis_t ax);

void  motion_init(void);
/** Enable / disable all drives: ENA bits (active low), M3 Servo-ON, M6 brake. */
void  motion_enable_all(bool on);
bool  motion_enabled(void);

bool  motion_move_abs(Axis_t ax, float pos_units, float speed_scale);
bool  motion_move_rel(Axis_t ax, float dist_units, float speed_scale);
bool  motion_move_pulses(Axis_t ax, int32_t pulses, const PulseProfile_t *prof);
bool  motion_jog(Axis_t ax, bool forward, float speed_scale);
void  motion_stop(Axis_t ax);
void  motion_abort_all(void);
bool  motion_busy(Axis_t ax);
bool  motion_wait(Axis_t ax, uint32_t timeout_ms);

float motion_position(Axis_t ax);
void  motion_set_position(Axis_t ax, float pos_units);

/** Search home sensor, decelerate, back off, re-approach slowly, set home_pos. */
bool  motion_home_axis(Axis_t ax, uint32_t timeout_ms);

#endif /* MOTION_H */
