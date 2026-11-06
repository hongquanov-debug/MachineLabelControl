#include <stdbool.h>
#include <stdint.h>
#define S2_EXTI_IRQn 0
void HAL_NVIC_EnableIRQ(int);
void bsp_cam_trigger_pulse(void);
void bsp_cam2_trigger_pulse(void);
bool bsp_s5_label_present(void);
