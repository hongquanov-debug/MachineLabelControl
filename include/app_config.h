/**
 * @file app_config.h
 * @brief Central process / kinematic constants (single source of truth = docs/FIRMWARE_PROCESS_SPEC.md).
 *
 * Every constant references the spec section it comes from. Values marked
 * "TODO(HW)" are not defined by the spec and must be confirmed on the machine.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#ifndef APP_FIRMWARE_REV
#define APP_FIRMWARE_REV "3.2.0"
#endif

/* ===================================================================== */
/* §1.1 Conveyor kinematics                                              */
/* ===================================================================== */
#define ENC_PULSES_PER_MM          10U       /* K_enc = 10 pulses/mm (0.1 mm/pulse) */
#define CAN_PITCH_MM               120U      /* P_can */
#define CAN_PITCH_PULSES           (CAN_PITCH_MM * ENC_PULSES_PER_MM)
#define CANS_PER_MIN_NOMINAL       120U
#define CANS_PER_MIN_MAX           156U
#define CANS_PER_MIN_MIN           60U       /* HMI 40003 range 60..156 */

/* ===================================================================== */
/* §1.2 Station coordinates (encoder pulses from S2)                     */
/* ===================================================================== */
#define ENC_POS_S2                 0
#define ENC_POS_CAMERA             500       /* +50 mm   +/-1.0 mm */
#define ENC_POS_ORIENT             3000      /* +300 mm  +/-1.5 mm */
#define ENC_POS_LABEL              6500      /* +650 mm  +/-0.5 mm */
#define ENC_POS_INSPECT            9500      /* +950 mm  +/-2.0 mm */
#define ENC_POS_CODE_CAM           10500     /* +1050 mm camera 2 DataMatrix (TODO(HW): measure) */
#define ENC_POS_REJECT             12500     /* +1250 mm +/-5.0 mm */
#define ENC_POS_END                20000     /* +2000 mm */

/* Station index (CanRecord_t.station_idx = next station to process) */
enum {
    STATION_IDX_CAMERA  = 1,
    STATION_IDX_ORIENT  = 2,
    STATION_IDX_LABEL   = 3,
    STATION_IDX_INSPECT = 4,
    STATION_IDX_CODE    = 5,   /* extension: camera 2 DataMatrix */
    STATION_IDX_REJECT  = 6,
    STATION_IDX_END     = 7,
};

#define TRACKING_TICK_HZ           5000U     /* TIM6 tracking scan: 0.2 ms -> 0.06 mm @ 312 mm/s */
#define S2_MIN_SPACING_PULSES      600U      /* TODO(HW): ignore S2 re-trigger closer than 60 mm */
#define S2_TRIGGER_FALLING_EDGE    1         /* TODO(HW): NPN sensor -> active low on MCU pin */
#define ORIENT_PRECLOSE_PULSES     300       /* TODO(HW): close P3 30 mm before can arrives */
#define CAM_TRIG_PULSE_US          10U       /* §1.2: 10 us strobe/trigger pulse */
#define P8_PUSH_TIME_MS            80U       /* TODO(HW): P8 extend time before retract */

/* ===================================================================== */
/* §2 Vision                                                              */
/* ===================================================================== */
#define VISION_BAUD                115200U
#define VISION_TIMEOUT_MS          80U
#define VISION_MAX_CONSEC_TIMEOUT  3U
#define VISION_SEND_TRIGGER_PACKET 1         /* §2.2.A: optional trigger request frame */

/* --- Camera 2: DataMatrix check after labeling (extension, see docs/VISION_PROTOCOL.md) ---
 * The PC decodes the code and validates its content; the MCU only times the
 * trigger, waits for the 0x21 result and rejects NG cans at P8.             */
#ifndef CODE_CHECK_ENABLE
#define CODE_CHECK_ENABLE          1         /* 0 = no camera 2 (S5 alone decides) */
#endif
#define CODE_TIMEOUT_MS            150U      /* camera 2 result window after CAM2_TRIG */
#define CODE_MAX_CONSEC_TIMEOUT    3U        /* -> ALM_09 */
#define CODE_MAX_CONSEC_NG         5U        /* -> ALM_21 (printer / label roll problem) */
#ifndef LABEL_S5_CHECK_ENABLE
#define LABEL_S5_CHECK_ENABLE      1         /* 0 = camera 2 replaces the S5 sensor */
#endif
#define RS485_DIR_SETUP_US         5U        /* §2.1: DIR before TX */
#define RS485_DIR_HOLD_US          15U       /* §2.1: DIR after TC */

#if CODE_CHECK_ENABLE
_Static_assert(ENC_POS_CODE_CAM > ENC_POS_INSPECT && ENC_POS_CODE_CAM < ENC_POS_REJECT,
               "camera 2 must sit between S5 and the reject cylinder P8");
#endif

/* ===================================================================== */
/* §3 Labeling station (6 heads, M3 index 60 deg)                         */
/* ===================================================================== */
#define LABEL_HEADS                6U
#define LABEL_CYCLE_MS             400U      /* @150 cans/min */
#define M3_INDEX_ACC_MS            35U
#define M3_INDEX_CONST_MS          80U
#define M3_INDEX_DEC_MS            35U
#define M3_INDEX_TOTAL_MS          (M3_INDEX_ACC_MS + M3_INDEX_CONST_MS + M3_INDEX_DEC_MS) /* 150 */
#define LABEL_FEED_BLOW_DELAY_MS   30U       /* VAC on -> 30 ms -> P7 blow assist */
#define LABEL_FEED_BLOW_MS         30U
#define LABEL_VAC_DETECT_MS        50U       /* default, overridden by recipe.vacuum_detect_ms */
#define LABEL_T_P5_DOWN_MS         160U      /* cycle time when DO_CYL_P5 = 1 */
#define LABEL_T_PRESS_MS           210U      /* label touches can lid */
#define LABEL_P5_DWELL_MS          40U       /* default, recipe.p5_down_dwell_ms; t_up = 210 + dwell */
#define LABEL_BLOW_OFF_MS          30U       /* default, recipe.blow_off_time_ms; release = t_up - blow */
#define LABEL_T_P5_UP_DEADLINE_MS  380U      /* P5 must be up (DI_P5_UP) before this time */
#define CYL_P5_TIMEOUT_MS          150U      /* ALM_08 */
#define LABEL_MAX_LATENESS_MS      20U       /* can event older than this -> LABEL_MISS */

/* ===================================================================== */
/* §4 Motion kinematics (pulse/direction axes)                           */
/* ===================================================================== */
#define PULSE_TIMER_TICK_HZ        10500000U /* all pulse timers prescaled to 10.5 MHz */
#define PULSE_F_START_HZ           400.0f

#define M2_PULSES_PER_DEG          (1600.0f / 360.0f)            /* 4.444 */
#define M2_VMAX_HZ                 48000.0f                      /* 1800 rpm */
#define M2_ACCEL_HZ_S              (12000.0f / 60.0f * 1600.0f)  /* 12000 rpm/s */

#define M3_PULSES_PER_DEG          (10000.0f * 10.0f / 360.0f)   /* 277.778 */
/* NOTE: spec §4 lists 50 kHz max, but a 60 deg index (16 667 pulses) in 35/80/35 ms
 * needs ~145 kHz peak. Limit set to the required value - see docs/SPEC_NOTES.md. */
#define M3_VMAX_HZ                 150000.0f
#define M3_ACCEL_HZ_S              (15000.0f / 60.0f * 10000.0f)
#define M3_HOME_OFFSET_DEG         12.50f
#define M3_HOMING_HZ               (15.0f / 60.0f * 10000.0f)    /* 15 rpm motor */

#define M4_PULSES_PER_MM           33.953f
#define M4_VMAX_HZ                 (300.0f * M4_PULSES_PER_MM)
#define M4_ACCEL_HZ_S              (1500.0f * M4_PULSES_PER_MM)
#define M4_HOMING_HZ               (30.0f * M4_PULSES_PER_MM)

#define M5_PULSES_PER_MM           160.0f
#define M5_VMAX_HZ                 (150.0f * M5_PULSES_PER_MM)
#define M5_ACCEL_HZ_S              (1000.0f * M5_PULSES_PER_MM)
#define M5_HOMING_HZ               (20.0f * M5_PULSES_PER_MM)
#define M5_SOFT_MIN_MM             0.0f
#define M5_SOFT_MAX_MM             45.0f

#define M6_PULSES_PER_MM           320.0f
#define M6_VMAX_HZ                 (25.0f * M6_PULSES_PER_MM)
#define M6_ACCEL_HZ_S              (200.0f * M6_PULSES_PER_MM)
#define M6_HOMING_HZ               (5.0f * M6_PULSES_PER_MM)
#define M6_SOFT_MIN_MM             0.0f
#define M6_SOFT_MAX_MM             98.0f
#define M6_HOME_POS_MM             98.0f     /* home = highest position */
#define M6_REF_POS_330ML_X10       300U      /* TODO(HW): absolute M6 pos for 330 ml std (recipes are relative) */

#define HOMING_TIMEOUT_MS          30000U

/* ===================================================================== */
/* §6 Safety input source (J_SAFETY)                                      */
/* ===================================================================== */
/*
 * PCB v1 has a defect on J_SAFETY: E-Stop / door / air / reset cannot be wired
 * to the MCU pins. Workaround: one extra 74HCT165 ("LI extension", 8 inputs)
 * chained at the END of the LI32 chain (its QH -> SER of the 4th LI32 chip,
 * its own SER tied to GND). The scan then reads 6 bytes:
 *     byte 0..3 = LI32, byte 4 = safety extension, byte 5 = check byte (0x00).
 *
 * Fail-safe rules (see docs/SPEC_NOTES.md A20):
 *  - wire so that a HEALTHY signal reads 1 (E-Stop released = 1, door closed = 1,
 *    air OK = 1) and keep SAFETY_EXT_INVERT_MASK = 0. A data line stuck low then
 *    reads "unsafe", and a disconnected cable reads 0xFF on the check byte
 *    -> bus fault -> all safety signals forced "unsafe".
 *  - the MCU only MONITORS: the E-Stop must also cut KM1 in hardware
 *    (safety relay or NC contact in series with the KM1 coil).
 */
#define SAFETY_SRC_GPIO          0   /* PCB v2: J_SAFETY wired to MCU pins (board_pins.h) */
#define SAFETY_SRC_LI_EXT        1   /* PCB v1: 5th 74HCT165 at the end of the LI32 chain */
#ifndef SAFETY_SOURCE
#define SAFETY_SOURCE            SAFETY_SRC_LI_EXT
#endif
#define DIO_DI_EXT_BYTES         ((SAFETY_SOURCE == SAFETY_SRC_LI_EXT) ? 1U : 0U)
#define SAFETY_EXT_BIT_ESTOP     0   /* 1 = E-Stop released (NC chain closed) */
#define SAFETY_EXT_BIT_DOOR      1   /* 1 = guard door closed */
#define SAFETY_EXT_BIT_AIR       2   /* 1 = air pressure >= 0.5 MPa */
#define SAFETY_EXT_BIT_RESET     3   /* 1 = SAFETY_RESET button pressed */
#define SAFETY_EXT_INVERT_MASK   0x00U /* TODO(HW): keep 0 (see fail-safe rules) */

/*
 * BENCH MODE - for testing the board on a desk WITHOUT field wiring only.
 * Safety inputs are treated as healthy, but KM1 is LOCKED OFF in bsp_km1_set(),
 * so the motor / drive power is never switched on. Reported in input register
 * 30023 bit3. Never set it here: build the dedicated PlatformIO environment
 * "controller1_bench" (platformio.ini) instead.
 */
#ifndef SAFETY_BENCH_MODE
#define SAFETY_BENCH_MODE        0
#endif

/* ===================================================================== */
/* §6 Safety / FSM                                                        */
/* ===================================================================== */
#define PURGE_VFD_HZ_X100          1000U     /* 10 Hz */
#define PURGE_TIME_MS              5000U
#define S0_EMPTY_PAUSE_MS          2000U     /* TODO(HW): S0 empty this long -> auto pause */
#define CYL_GENERIC_TIMEOUT_MS     300U      /* TODO(HW): P1..P4, P8 reed switch timeout */
#define DI_BUS_FAULT_SCANS         3U        /* consecutive 0xFF on byte 5 -> ALM_20 */
#define CABINET_OVERTEMP_C         55

/* ===================================================================== */
/* §7 Modbus                                                              */
/* ===================================================================== */
#define HMI_BAUD                   19200U
#define HMI_SLAVE_ID               1U
#define VFD_BAUD                   9600U
#define VFD_SLAVE_ID               2U
#define VFD_REG_CMD                0x2000U
#define VFD_REG_FREQ               0x2001U
#define VFD_REG_FAULT              0x2100U
#define VFD_REG_OUT_FREQ           0x2102U
#define VFD_REG_CURRENT            0x2104U
#define VFD_CMD_RUN_FWD            0x0001U
#define VFD_CMD_COAST_STOP         0x0005U
#define VFD_POLL_MS                100U
#define VFD_RESP_TIMEOUT_MS        100U
/* TODO(HW): conveyor speed per VFD Hz (mm/s per Hz). 6.24 -> 50 Hz @ 312 mm/s */
#define BELT_MM_S_PER_HZ           6.24f

#define NET_IP_DEFAULT             {192, 168, 1, 200}
#define NET_MASK_DEFAULT           {255, 255, 255, 0}
#define NET_GW_DEFAULT             {192, 168, 1, 1}
#define NET_MODBUS_PORT            502U
#define NET_MODBUS_SOCKETS         4U

/* ===================================================================== */
/* §8 EEPROM                                                              */
/* ===================================================================== */
#define EEPROM_I2C_ADDR            0xA0U
#define EEPROM_PAGE_SIZE           32U
#define EEPROM_SIZE                8192U
#define EEPROM_ADDR_SYSCFG         0x0000U
#define EEPROM_ADDR_RECIPE_BASE    0x0040U
#define RECIPE_COUNT               16U
#define RECIPE_SIZE                64U

/* ===================================================================== */
/* Misc                                                                   */
/* ===================================================================== */
#define APP_ENABLE_WATCHDOG        1
#define WATCHDOG_TIMEOUT_MS        500U

#endif /* APP_CONFIG_H */
