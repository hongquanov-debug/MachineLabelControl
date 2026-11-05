/**
 * @file bsp_w5500.h
 * @brief Minimal WIZnet W5500 driver on SPI2 (register access + TCP server sockets).
 *        Used by the Modbus TCP server (Spec §7.3). Task context only.
 */
#ifndef BSP_W5500_H
#define BSP_W5500_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gw[4];
} W5500NetCfg_t;

/* Socket status register values (Sn_SR) */
#define W5500_SOCK_CLOSED      0x00U
#define W5500_SOCK_INIT        0x13U
#define W5500_SOCK_LISTEN      0x14U
#define W5500_SOCK_ESTABLISHED 0x17U
#define W5500_SOCK_CLOSE_WAIT  0x1CU

/** Reset chip, check VERSIONR (0x04) and apply network config. */
bool bsp_w5500_init(const W5500NetCfg_t *cfg);
bool bsp_w5500_link_up(void);

uint8_t  bsp_w5500_sock_status(uint8_t sn);
bool     bsp_w5500_sock_listen(uint8_t sn, uint16_t port);
void     bsp_w5500_sock_disconnect(uint8_t sn);
void     bsp_w5500_sock_close(uint8_t sn);
uint16_t bsp_w5500_sock_rx_size(uint8_t sn);
uint16_t bsp_w5500_sock_recv(uint8_t sn, uint8_t *buf, uint16_t max);
bool     bsp_w5500_sock_send(uint8_t sn, const uint8_t *buf, uint16_t len);

#endif /* BSP_W5500_H */
