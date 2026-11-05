/**
 * @file vfd.h
 * @brief Conveyor VFD M1 over Modbus RTU master on RS485_2 (Spec §7.2).
 */
#ifndef VFD_H
#define VFD_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     online;
    bool     running_cmd;
    uint16_t freq_set_x100;
    uint16_t out_freq_x100;   /* 0x2102 */
    uint16_t current_x10;     /* 0x2104 */
    uint16_t fault_code;      /* 0x2100 */
    uint32_t comm_errors;
} VfdStatus_t;

void vfd_init(void);
void vfd_task(void *arg);
void vfd_run(uint16_t freq_x100);
void vfd_stop(void);
VfdStatus_t vfd_status(void);

/** Conveyor frequency for a production speed (cans/min). */
uint16_t vfd_freq_for_speed(uint16_t cans_per_min);

#endif
