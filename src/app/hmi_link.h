/**
 * @file hmi_link.h
 * @brief HMI Kinco - Modbus RTU slave (ID 1, 19200 8N1) on RS485_1 / USART2 (Spec §7.1).
 */
#ifndef HMI_LINK_H
#define HMI_LINK_H

void hmi_link_init(void);
void hmi_link_task(void *arg);

#endif
