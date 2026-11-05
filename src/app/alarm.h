/**
 * @file alarm.h
 * @brief Alarm matrix (Spec §6.3). Codes 15..19 are implementation extensions.
 */
#ifndef ALARM_H
#define ALARM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ALM_NONE                 = 0,
    ALM_01_ESTOP_ACTIVATED   = 1,
    ALM_02_SAFETY_DOOR_OPEN  = 2,
    ALM_03_AIR_PRESSURE_LOW  = 3,
    ALM_04_CABINET_OVERTEMP  = 4,
    ALM_05_VISION_TIMEOUT    = 5,
    ALM_06_VACUUM_FEED_MISS  = 6,
    ALM_07_LABEL_TAPE_EMPTY  = 7,
    ALM_08_CYL_P5_TIMEOUT    = 8,
    ALM_10_DRIVER_M2_FAULT   = 10,
    ALM_11_SERVO_M3_FAULT    = 11,
    ALM_12_DRIVER_M4_FAULT   = 12,
    ALM_13_DRIVER_M5_FAULT   = 13,
    ALM_14_DRIVER_M6_FAULT   = 14,
    /* --- extensions (not in spec REV 3.2) --- */
    ALM_15_CYLINDER_TIMEOUT  = 15,  /* P1/P2/P3/P4/P8 reed switch timeout */
    ALM_16_HOMING_FAILED     = 16,
    ALM_17_VFD_FAULT         = 17,
    ALM_18_TRACKING_OVERFLOW = 18,  /* FIFO full / can jam */
    ALM_19_VFD_COMM_LOST     = 19,
    /* --- */
    ALM_20_LI32_BUS_DISCONNECT = 20,
    ALM_CODE_MAX             = 25
} AlarmCode_t;

typedef enum {
    ALM_SEV_NONE = 0,
    ALM_SEV_WARNING,     /* display only */
    ALM_SEV_DRAIN_STOP,  /* stop feeding, finish cans on belt, then stop (ALM_07) */
    ALM_SEV_STOP,        /* controlled stop -> STATE_ALARM_STOP */
    ALM_SEV_EMERGENCY    /* -> STATE_ESTOP, KM1 off */
} AlarmSeverity_t;

void            alarm_raise(AlarmCode_t code);   /* ISR safe */
void            alarm_clear(AlarmCode_t code);   /* ISR safe */
void            alarm_clear_all(void);
bool            alarm_is_active(AlarmCode_t code);
uint32_t        alarm_mask(void);
AlarmSeverity_t alarm_severity(AlarmCode_t code);
AlarmSeverity_t alarm_highest_severity(void);
/** Code to display on HMI (30002): highest severity, then lowest code. */
AlarmCode_t     alarm_current(void);

#endif /* ALARM_H */
