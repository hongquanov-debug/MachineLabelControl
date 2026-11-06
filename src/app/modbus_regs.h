/**
 * @file modbus_regs.h
 * @brief Register map shared by HMI (Modbus RTU slave, §7.1) and SCADA (Modbus TCP, §7.3).
 *
 * Holding registers (4xxxx, 0-based index):
 *   0  40001 command: 1 START, 2 STOP, 3 PAUSE, 4 RESET_ALARM, 5 START_HOMING,
 *                     (ext) 6 JOG_ENTER, 7 JOG_EXIT
 *   1  40002 recipe id (1..16)
 *   2  40003 conveyor speed, cans/min (60..156)
 *   3  40004 theta offset x100 (-1800..+1800 = -18.00..+18.00 deg)
 *   4  40005 press time p5_down_dwell_ms (20..100)
 *   5  40006 M4 overrun pulses (0..200)
 *   9  40010 (ext) jog axis 0..4 = M2..M6
 *  10  40011 (ext) jog direction -1/0/+1
 *  11  40012 (ext) manual outputs, MANUAL_JOG only: bit n = DO n (0..14)
 *         P1 P2 P3 P4 P5 P7 P8 M7 VAC1..VAC6 VAC_BLOW; read = actual DO 0..14.
 *         Refused (exception 04) outside MANUAL_JOG or when P5 down is asked while M3 turns.
 *  20..40 40021..40041 recipe editor (PC configuration over Ethernet),
 *         see lib/pmems_core/include/recipe_regs.h
 *
 * Input registers (3xxxx, 0-based index). 32-bit values: high word first.
 *   0  30001 FSM state      1  30002 alarm code     2  30003 speed cans/min
 *   3  30004..5 fed cans    5  30006..7 OK cans     7  30008..9 NG cans
 *   9  30010 M6 height 0.1 mm                     10  30011 air pressure 0.01 MPa
 *  11  30012..13 DI bitmask                       13  30014..15 DO bitmask
 *  (ext) 15 VFD out freq x100, 16 VFD current x10, 17 cans on belt,
 *        18..19 active alarm bitmask, 20 vision CRC errors, 21 vision late results,
 *        22 config flags: bit0 recipe change allowed, bit1 EEPROM OK, bit2 M6 moving to recipe height,
 *           bit3 SAFETY BENCH MODE (safety chain ignored, KM1 locked off), bit4 safety via LI extension
 */
#ifndef MODBUS_REGS_H
#define MODBUS_REGS_H

#include "modbus_pdu.h"

#define MB_HOLDING_COUNT 50U
#define MB_INPUT_COUNT   40U

/** Create the mutex guarding the shared recipe edit buffer (before HMI/net tasks start). */
void modbus_regs_init(void);
const MbMap_t *modbus_regs_map(void);

#endif
