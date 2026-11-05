/**
 * @file io_map.h
 * @brief Logical names of the 32 DI (Module_LI32) and 24 DO (Module_LO24), Spec §5.
 *        Value = byte * 8 + bit.
 */
#ifndef IO_MAP_H
#define IO_MAP_H

typedef enum {
    /* Byte 0 */
    DI_S7_LANE1 = 0, DI_S8_LANE2, DI_S9_LANE3, DI_S10_LANE4,
    DI_P1_EXT, DI_P1_RET, DI_P2_EXT, DI_P2_RET,
    /* Byte 1 */
    DI_P3_EXT = 8, DI_P3_RET, DI_P4_EXT, DI_P4_RET,
    DI_P5_UP, DI_P5_DOWN, DI_P8_EXT, DI_P8_RET,
    /* Byte 2 */
    DI_VAC_SW_1 = 16, DI_VAC_SW_2, DI_VAC_SW_3, DI_VAC_SW_4,
    DI_VAC_SW_5, DI_VAC_SW_6, DI_S0_QUEUE, DI_S1_RELEASE,
    /* Byte 3 */
    DI_ALM_M2 = 24, DI_ALM_M3, DI_ALM_M4, DI_ALM_M5,
    DI_ALM_M6A, DI_ALM_M6B, DI_S14_TAPE_END, DI_SPARE_32,
} DiBit_t;

typedef enum {
    /* Byte 0 */
    DO_CYL_P1 = 0, DO_CYL_P2, DO_CYL_P3, DO_CYL_P4,
    DO_CYL_P5, DO_VALVE_P7, DO_CYL_P8, DO_MTR_M7,
    /* Byte 1 */
    DO_VAC_HEAD_1 = 8, DO_VAC_HEAD_2, DO_VAC_HEAD_3, DO_VAC_HEAD_4,
    DO_VAC_HEAD_5, DO_VAC_HEAD_6, DO_VAC_BLOW, DO_SPARE_16,
    /* Byte 2 */
    DO_ENA_M2 = 16,   /* '1' = driver DISABLED (spec §5.2) */
    DO_ENA_M4,        /* '1' = driver DISABLED */
    DO_ENA_M5,        /* '1' = driver DISABLED */
    DO_ENA_M6,        /* '1' = driver DISABLED (M6a + M6b) */
    DO_SON_M3,        /* '1' = servo ON */
    DO_TOWER_RED, DO_TOWER_YEL, DO_TOWER_GRN,
} DoBit_t;

#define DI_BIT(x) (1UL << (x))
#define DO_BIT(x) (1UL << (x))

/**
 * DI polarity: bits set here are inverted after reading so that '1' always
 * means the "Logic 1" column of spec table §5.1.
 * TODO(HW): confirm opto-coupler polarity of Module_LI32.
 */
#define DI_INVERT_MASK 0x00000000UL

/** Safe output image: all valves off, stepper drivers disabled, servo off. */
#define DO_SAFE_IMAGE (DO_BIT(DO_ENA_M2) | DO_BIT(DO_ENA_M4) | DO_BIT(DO_ENA_M5) | DO_BIT(DO_ENA_M6))

#endif /* IO_MAP_H */
