#include "pulse_ramp.h"

#include <math.h>

float ramp_init(RampState_t *r, const PulseProfile_t *p, uint32_t total)
{
    float fs = (p->f_start > 1.0f) ? p->f_start : 1.0f;
    float fm = (p->f_max > fs) ? p->f_max : fs;
    r->v2_start   = fs * fs;
    r->v2_max     = fm * fm;
    r->two_a      = 2.0f * ((p->accel > 1.0f) ? p->accel : 1.0f);
    r->v2         = r->v2_start;
    r->total      = total;
    r->done       = 0;
    r->continuous = (total == 0U);
    r->stopping   = false;
    return fs;
}

float ramp_step(RampState_t *r)
{
    r->done++;
    if (!r->continuous && r->done >= r->total) {
        return 0.0f;
    }

    bool decel;
    if (r->stopping) {
        decel = true;
    } else if (r->continuous) {
        decel = false;
    } else {
        float decel_steps = (r->v2 - r->v2_start) / r->two_a;
        decel = (float)(r->total - r->done) <= decel_steps;
    }

    if (decel) {
        r->v2 -= r->two_a;
        if (r->v2 <= r->v2_start) {
            r->v2 = r->v2_start;
            if (r->stopping && r->continuous) {
                return 0.0f;
            }
        }
    } else if (r->v2 < r->v2_max) {
        r->v2 += r->two_a;
        if (r->v2 > r->v2_max) {
            r->v2 = r->v2_max;
        }
    }
    return sqrtf(r->v2);
}

void ramp_request_stop(RampState_t *r)
{
    if (r->continuous) {
        r->stopping = true;
    } else {
        /* shorten positioning move to the minimum braking distance */
        uint32_t brake = (uint32_t)((r->v2 - r->v2_start) / r->two_a) + 1U;
        if (r->done + brake < r->total) {
            r->total = r->done + brake;
        }
    }
}

void ramp_finish_after(RampState_t *r, uint32_t extra)
{
    r->total      = r->done + extra + 1U;
    r->continuous = false;
    r->stopping   = false;
}

void ramp_profile_from_times(uint32_t steps, float t_acc, float t_const, float f_start,
                             PulseProfile_t *out)
{
    /* trapezoid area: steps = (f_start + f_max) * t_acc + f_max * t_const */
    float denom = t_acc + t_const;
    if (denom <= 0.0f) {
        denom = 1e-3f;
    }
    float fmax = ((float)steps - f_start * t_acc) / denom;
    if (fmax < f_start) {
        fmax = f_start;
    }
    out->f_start = f_start;
    out->f_max   = fmax;
    out->accel   = (t_acc > 0.0f) ? (fmax - f_start) / t_acc : fmax * 1000.0f;
}
