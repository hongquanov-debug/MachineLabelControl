/**
 * @file pulse_ramp.h
 * @brief Per-pulse trapezoidal velocity ramp (constant acceleration).
 *
 * Kinematics: v(n+1)^2 = v(n)^2 +/- 2*a  (v in pulses/s, a in pulses/s^2, 1 step).
 * ramp_step() is called from the pulse timer update ISR after each emitted
 * pulse and returns the frequency of the next pulse (0 = motion finished).
 */
#ifndef PULSE_RAMP_H
#define PULSE_RAMP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float f_start;  /**< start/stop frequency [Hz] */
    float f_max;    /**< cruise frequency [Hz] */
    float accel;    /**< acceleration [Hz/s] = pulses/s^2 */
} PulseProfile_t;

typedef struct {
    float    v2;         /**< current v^2 */
    float    v2_start;
    float    v2_max;
    float    two_a;
    uint32_t total;      /**< total pulses (ignored when continuous) */
    uint32_t done;       /**< pulses emitted */
    bool     continuous; /**< jog / homing search: run until stop requested */
    bool     stopping;   /**< decelerate to f_start then finish */
} RampState_t;

/** total == 0 -> continuous mode. Returns first pulse frequency. */
float ramp_init(RampState_t *r, const PulseProfile_t *p, uint32_t total);

/** Called after each completed pulse. Returns next frequency or 0 when done. */
float ramp_step(RampState_t *r);

/** Request decelerated stop (continuous or positioning). */
void ramp_request_stop(RampState_t *r);

/** Convert running motion into a finite move ending @p extra pulses from now (e.g. sensor + overrun). */
void ramp_finish_after(RampState_t *r, uint32_t extra);

/**
 * Compute a profile that moves @p steps with symmetric accel/decel time t_acc
 * and cruise time t_const (seconds), e.g. M3 index 60deg: 35/80/35 ms (Spec §3.2).
 */
void ramp_profile_from_times(uint32_t steps, float t_acc, float t_const, float f_start,
                             PulseProfile_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PULSE_RAMP_H */
