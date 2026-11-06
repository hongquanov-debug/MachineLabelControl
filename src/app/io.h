/**
 * @file io.h
 * @brief Logical I/O layer: DI image (debounced scan of LI32), DO image (LO24),
 *        safety chain supervision. Runs the 1 ms I/O task.
 */
#ifndef IO_H
#define IO_H

#include <stdbool.h>
#include <stdint.h>

#include "io_map.h"

void io_init(void);
void io_task(void *arg);

bool     di(DiBit_t bit);
uint32_t di_image(void);

void     do_set(DoBit_t bit, bool on);   /* ISR safe; committed within 1 ms */
bool     do_get(DoBit_t bit);
uint32_t do_image(void);
void     do_commit_now(void);           /* immediate latch (timing-critical stations) */
void     io_outputs_safe(void);         /* all valves off, drivers disabled */

/** Wait until DI @p bit == @p level, polling every 1 ms. false on timeout. */
bool     io_wait_di(DiBit_t bit, bool level, uint32_t timeout_ms);

/** Safety chain snapshot (refreshed each 1 ms). */
typedef struct {
    bool estop_ok;
    bool door_closed;
    bool air_ok;
    bool reset_pressed;
    bool di_bus_ok;
} SafetyStatus_t;
SafetyStatus_t io_safety(void);

/** Set by the FSM: driver alarm inputs are evaluated only while drives are powered. */
void io_set_drives_powered(bool powered);

/** Raw safety extension byte from the last scan (PCB v1 commissioning, 30024 high byte). */
uint8_t io_safety_ext_raw(void);

/** Heartbeat counter incremented by the I/O task (watchdog supervision). */
uint32_t io_heartbeat(void);

#endif /* IO_H */
