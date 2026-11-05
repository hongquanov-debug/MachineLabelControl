#include "alarm.h"

#include "crit.h"

static volatile uint32_t s_mask;   /* bit n = alarm code n active */

static const uint8_t k_severity[ALM_CODE_MAX + 1] = {
    [ALM_01_ESTOP_ACTIVATED]     = ALM_SEV_EMERGENCY,
    [ALM_02_SAFETY_DOOR_OPEN]    = ALM_SEV_EMERGENCY,
    [ALM_03_AIR_PRESSURE_LOW]    = ALM_SEV_STOP,
    [ALM_04_CABINET_OVERTEMP]    = ALM_SEV_WARNING,
    [ALM_05_VISION_TIMEOUT]      = ALM_SEV_STOP,
    [ALM_06_VACUUM_FEED_MISS]    = ALM_SEV_WARNING,
    [ALM_07_LABEL_TAPE_EMPTY]    = ALM_SEV_DRAIN_STOP,
    [ALM_08_CYL_P5_TIMEOUT]      = ALM_SEV_STOP,
    [ALM_10_DRIVER_M2_FAULT]     = ALM_SEV_STOP,
    [ALM_11_SERVO_M3_FAULT]      = ALM_SEV_STOP,
    [ALM_12_DRIVER_M4_FAULT]     = ALM_SEV_STOP,
    [ALM_13_DRIVER_M5_FAULT]     = ALM_SEV_STOP,
    [ALM_14_DRIVER_M6_FAULT]     = ALM_SEV_STOP,
    [ALM_15_CYLINDER_TIMEOUT]    = ALM_SEV_STOP,
    [ALM_16_HOMING_FAILED]       = ALM_SEV_STOP,
    [ALM_17_VFD_FAULT]           = ALM_SEV_STOP,
    [ALM_18_TRACKING_OVERFLOW]   = ALM_SEV_STOP,
    [ALM_19_VFD_COMM_LOST]       = ALM_SEV_STOP,
    [ALM_20_LI32_BUS_DISCONNECT] = ALM_SEV_STOP,
};

void alarm_raise(AlarmCode_t code)
{
    if (code == ALM_NONE || code > ALM_CODE_MAX) {
        return;
    }
    uint32_t s = crit_enter();
    s_mask |= (1UL << code);
    crit_exit(s);
}

void alarm_clear(AlarmCode_t code)
{
    if (code > ALM_CODE_MAX) {
        return;
    }
    uint32_t s = crit_enter();
    s_mask &= ~(1UL << code);
    crit_exit(s);
}

void alarm_clear_all(void)
{
    s_mask = 0;
}

bool alarm_is_active(AlarmCode_t code)
{
    return (code <= ALM_CODE_MAX) && ((s_mask >> code) & 1U);
}

uint32_t alarm_mask(void)
{
    return s_mask;
}

AlarmSeverity_t alarm_severity(AlarmCode_t code)
{
    return (code <= ALM_CODE_MAX) ? (AlarmSeverity_t)k_severity[code] : ALM_SEV_NONE;
}

AlarmSeverity_t alarm_highest_severity(void)
{
    return alarm_severity(alarm_current());
}

AlarmCode_t alarm_current(void)
{
    uint32_t m = s_mask;
    AlarmCode_t best = ALM_NONE;
    uint8_t best_sev = 0;
    for (uint32_t c = 1; c <= ALM_CODE_MAX; c++) {
        if ((m >> c) & 1U) {
            if (k_severity[c] > best_sev) {
                best_sev = k_severity[c];
                best = (AlarmCode_t)c;
            }
        }
    }
    return best;
}
