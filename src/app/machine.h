/**
 * @file machine.h
 * @brief System finite state machine (Spec §6.1) - runs every 10 ms.
 */
#ifndef MACHINE_H
#define MACHINE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MS_POWER_ON = 0,
    MS_ESTOP,
    MS_HOMING_REQUIRED,
    MS_HOMING_EXEC,
    MS_READY,
    MS_AUTO_RUN,
    MS_AUTO_PAUSE,
    MS_ALARM_STOP,
    MS_MANUAL_JOG,
} MachineState_t;

/** HMI register 30001 encoding (Spec §7.1, extended for states not listed there). */
typedef enum {
    HMI_ST_POWER_ON = 0,
    HMI_ST_ESTOP    = 1,
    HMI_ST_HOMING   = 2,
    HMI_ST_READY    = 3,
    HMI_ST_RUNNING  = 4,
    HMI_ST_ALARM    = 5,
    HMI_ST_PAUSED   = 6,   /* ext */
    HMI_ST_JOG      = 7,   /* ext */
    HMI_ST_HOME_REQ = 8,   /* ext */
} HmiState_t;

typedef enum {
    CMD_NONE         = 0,
    CMD_START        = 1,  /* also RESUME from pause */
    CMD_STOP         = 2,
    CMD_PAUSE        = 3,
    CMD_RESET_ALARM  = 4,
    CMD_START_HOMING = 5,
    CMD_JOG_ENTER    = 6,  /* ext */
    CMD_JOG_EXIT     = 7,  /* ext */
} MachineCmd_t;

void           machine_init(void);
void           machine_task(void *arg);
void           machine_post_cmd(MachineCmd_t cmd);
MachineState_t machine_state(void);
HmiState_t     machine_state_hmi(void);
bool           machine_recipe_change_allowed(void);
bool           machine_abort_requested(void);
void           machine_homing_done(bool ok);

#endif /* MACHINE_H */
