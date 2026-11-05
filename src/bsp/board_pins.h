/**
 * @file board_pins.h
 * @brief STM32F407VET6 pin assignment of the PMeMS mainboard.
 *
 * Pins marked [SPEC] are defined in docs/FIRMWARE_PROCESS_SPEC.md.
 * Pins marked [TODO(HW)] are NOT in the process spec - they are placeholders
 * chosen to avoid conflicts and MUST be checked against Hardware Spec V3.2
 * before flashing a real board.
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "stm32f4xx_hal.h"

/* ---------------- Conveyor encoder (TIM2 encoder mode) [SPEC] ---------- */
#define ENC_A_PORT        GPIOA
#define ENC_A_PIN         GPIO_PIN_0        /* TIM2_CH1, AF1 */
#define ENC_B_PORT        GPIOA
#define ENC_B_PIN         GPIO_PIN_1        /* TIM2_CH2, AF1 */

/* ---------------- Can sensor S2 (EXTI2) [SPEC: EXTI line 2] ------------ */
#define S2_PORT           GPIOE             /* [TODO(HW)] port of EXTI2 line */
#define S2_PIN            GPIO_PIN_2
#define S2_EXTI_IRQn      EXTI2_IRQn

/* ---------------- Label inspection sensor S5 [TODO(HW)] --------------- */
#define S5_PORT           GPIOE
#define S5_PIN            GPIO_PIN_1
#define S5_ACTIVE_LEVEL   GPIO_PIN_RESET    /* NPN -> low = label present */

/* ---------------- Camera trigger [SPEC] -------------------------------- */
#define CAM_TRIG_PORT     GPIOD
#define CAM_TRIG_PIN      GPIO_PIN_14

/* ---------------- RS485_1: HMI Kinco, USART2 --------------------------- */
#define RS485_1_TX_PORT   GPIOD             /* [TODO(HW)] PD5/PD6 remap (or PA2/PA3) */
#define RS485_1_TX_PIN    GPIO_PIN_5
#define RS485_1_RX_PORT   GPIOD
#define RS485_1_RX_PIN    GPIO_PIN_6
#define RS485_1_DIR_PORT  GPIOD             /* [TODO(HW)] */
#define RS485_1_DIR_PIN   GPIO_PIN_4

/* ---------------- RS485_2: VFD M1, USART3 ------------------------------ */
#define RS485_2_TX_PORT   GPIOD             /* [TODO(HW)] PD8/PD9 remap (or PB10/PB11) */
#define RS485_2_TX_PIN    GPIO_PIN_8
#define RS485_2_RX_PORT   GPIOD
#define RS485_2_RX_PIN    GPIO_PIN_9
#define RS485_2_DIR_PORT  GPIOD             /* [TODO(HW)] */
#define RS485_2_DIR_PIN   GPIO_PIN_7

/* ---------------- RS485_3: Vision PC, UART4 [SPEC] --------------------- */
#define RS485_3_TX_PORT   GPIOC
#define RS485_3_TX_PIN    GPIO_PIN_10
#define RS485_3_RX_PORT   GPIOC
#define RS485_3_RX_PIN    GPIO_PIN_11
#define RS485_3_DIR_PORT  GPIOD
#define RS485_3_DIR_PIN   GPIO_PIN_3

/* ---------------- EEPROM AT24C64 on I2C1 [SPEC] ------------------------ */
#define EEPROM_SCL_PORT   GPIOB
#define EEPROM_SCL_PIN    GPIO_PIN_8
#define EEPROM_SDA_PORT   GPIOB
#define EEPROM_SDA_PIN    GPIO_PIN_9

/* ---------------- W5500 on SPI2 [SPEC: SPI2] --------------------------- */
#define W5500_SCK_PORT    GPIOB
#define W5500_SCK_PIN     GPIO_PIN_13
#define W5500_MISO_PORT   GPIOB
#define W5500_MISO_PIN    GPIO_PIN_14
#define W5500_MOSI_PORT   GPIOB
#define W5500_MOSI_PIN    GPIO_PIN_15
#define W5500_CS_PORT     GPIOB             /* [TODO(HW)] */
#define W5500_CS_PIN      GPIO_PIN_12
#define W5500_RST_PORT    GPIOB             /* [TODO(HW)] */
#define W5500_RST_PIN     GPIO_PIN_1
#define W5500_INT_PORT    GPIOB             /* [TODO(HW)] */
#define W5500_INT_PIN     GPIO_PIN_0

/* ---------------- Module_LI32: 4x 74HCT165 (bit-bang) [TODO(HW)] ------ */
#define DI_LOAD_PORT      GPIOD             /* SH/LD (active low) */
#define DI_LOAD_PIN       GPIO_PIN_10
#define DI_CLK_PORT       GPIOD
#define DI_CLK_PIN        GPIO_PIN_11
#define DI_DATA_PORT      GPIOD             /* QH of first chip */
#define DI_DATA_PIN       GPIO_PIN_13

/* ---------------- Module_LO24: 3x 74HCT595 + ULN2803A [TODO(HW)] ------ */
#define DO_SER_PORT       GPIOE
#define DO_SER_PIN        GPIO_PIN_7
#define DO_SRCLK_PORT     GPIOE
#define DO_SRCLK_PIN      GPIO_PIN_8
#define DO_RCLK_PORT      GPIOE
#define DO_RCLK_PIN       GPIO_PIN_10
#define DO_OE_PORT        GPIOE             /* /OE active low, held high until first commit.
                                               HW must pull /OE up: MCU pins float during reset */
#define DO_OE_PIN         GPIO_PIN_11

/* ---------------- Pulse outputs (PUL) [SPEC] --------------------------- */
#define M2_PUL_PORT       GPIOE             /* TIM1_CH1  AF1 */
#define M2_PUL_PIN        GPIO_PIN_9
#define M3_PUL_PORT       GPIOC             /* TIM8_CH1  AF3 */
#define M3_PUL_PIN        GPIO_PIN_6
#define M4_PUL_PORT       GPIOA             /* TIM3_CH1  AF2 */
#define M4_PUL_PIN        GPIO_PIN_6
#define M5_PUL_PORT       GPIOD             /* TIM4_CH1  AF2 */
#define M5_PUL_PIN        GPIO_PIN_12
#define M6_PUL_PORT       GPIOE             /* TIM9_CH1  AF3 (M6a + M6b in parallel) */
#define M6_PUL_PIN        GPIO_PIN_5

/* ---------------- Direction outputs (DIR) [TODO(HW)] ------------------- */
#define M2_DIR_PORT       GPIOE
#define M2_DIR_PIN        GPIO_PIN_12
#define M3_DIR_PORT       GPIOE
#define M3_DIR_PIN        GPIO_PIN_13
#define M4_DIR_PORT       GPIOE
#define M4_DIR_PIN        GPIO_PIN_14
#define M5_DIR_PORT       GPIOE
#define M5_DIR_PIN        GPIO_PIN_15
#define M6_DIR_PORT       GPIOE
#define M6_DIR_PIN        GPIO_PIN_6

/* ---------------- Home sensors [SPEC] ---------------------------------- */
#define HOME_M3_PORT      GPIOC
#define HOME_M3_PIN       GPIO_PIN_9
#define M3_Z_PORT         GPIOC             /* [TODO(HW)] servo Z phase */
#define M3_Z_PIN          GPIO_PIN_7
#define HOME_M4_PORT      GPIOE             /* label gap sensor SR21-IR */
#define HOME_M4_PIN       GPIO_PIN_3
#define HOME_M5_PORT      GPIOE
#define HOME_M5_PIN       GPIO_PIN_4
#define HOME_M6A_PORT     GPIOC
#define HOME_M6A_PIN      GPIO_PIN_13
#define HOME_M6B_PORT     GPIOC
#define HOME_M6B_PIN      GPIO_PIN_8
#define HOME_ACTIVE_LEVEL GPIO_PIN_RESET    /* [TODO(HW)] NPN sensors */

/* ---------------- Safety chain & misc [TODO(HW)] ----------------------- */
#define SAFE_ESTOP_PORT   GPIOC             /* 1 = E-Stop released (NC contact) */
#define SAFE_ESTOP_PIN    GPIO_PIN_0
#define SAFE_DOOR_PORT    GPIOC             /* 1 = door closed */
#define SAFE_DOOR_PIN     GPIO_PIN_1
#define SAFE_AIR_PORT     GPIOC             /* 1 = air >= 0.5 MPa (ISE30A switch) */
#define SAFE_AIR_PIN      GPIO_PIN_2
#define SAFE_RESET_PORT   GPIOC             /* SAFETY_RESET push-button, 1 = pressed */
#define SAFE_RESET_PIN    GPIO_PIN_3
#define KM1_PORT          GPIOC             /* main contactor KM1, 1 = energised */
#define KM1_PIN           GPIO_PIN_4
#define M6_BRAKE_PORT     GPIOC             /* 1 = brake released (24V coil) */
#define M6_BRAKE_PIN      GPIO_PIN_5
#define BUZZER_PORT       GPIOB
#define BUZZER_PIN        GPIO_PIN_7
#define LED_RUN_PORT      GPIOB             /* on-board heartbeat LED */
#define LED_RUN_PIN       GPIO_PIN_6

#endif /* BOARD_PINS_H */
