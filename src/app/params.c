#include "params.h"

static volatile RuntimeParams_t s_params;

void params_init(uint16_t speed_cans_min)
{
    s_params.speed_cans_min    = speed_cans_min;
    s_params.theta_offset_x100 = 0;
    s_params.jog_axis          = 0;
    s_params.jog_dir           = 0;
}

const volatile RuntimeParams_t *params_get(void) { return &s_params; }
volatile RuntimeParams_t *params_mut(void)       { return &s_params; }
