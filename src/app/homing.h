/**
 * @file homing.h
 * @brief Homing / recovery sequence (Spec §6.2):
 *   1. release P4, lift P5 (mandatory first)
 *   2. purge conveyor: M1 at 10 Hz for 5 s, clear tracking FIFO
 *   3. home M6 (top), M3 (+12.5 deg to station 0), M4 (label gap), M5 (lane 1), M2 (relative)
 *   4. move M6 to recipe height
 */
#ifndef HOMING_H
#define HOMING_H

#include <stdbool.h>

void homing_task(void *arg);
void homing_start(void);
bool homing_busy(void);

#endif
