#include "net_link.h"

#include <string.h>

#include "FreeRTOS.h"
#include "app_config.h"
#include "bsp_w5500.h"
#include "modbus_regs.h"
#include "recipe.h"
#include "stm32f4xx.h"
#include "task.h"

static void make_mac(uint8_t mac[6])
{
    /* Locally administered MAC derived from the STM32 96-bit unique ID */
    uint32_t uid0 = *(volatile uint32_t *)UID_BASE;
    uint32_t uid1 = *(volatile uint32_t *)(UID_BASE + 4U);
    uint32_t uid2 = *(volatile uint32_t *)(UID_BASE + 8U);
    uint32_t h = uid0 ^ (uid1 * 31U) ^ (uid2 * 131U);
    mac[0] = 0x02;
    mac[1] = 0x50;   /* 'P' */
    mac[2] = (uint8_t)(h >> 24);
    mac[3] = (uint8_t)(h >> 16);
    mac[4] = (uint8_t)(h >> 8);
    mac[5] = (uint8_t)h;
}

void net_link_task(void *arg)
{
    (void)arg;
    static uint8_t rx[MB_TCP_MAX_ADU];
    static uint8_t tx[MB_TCP_MAX_ADU];

    W5500NetCfg_t cfg;
    SysConfig_t sc;
    recipe_get_syscfg(&sc);
    make_mac(cfg.mac);
    memcpy(cfg.ip, sc.ip, 4);
    memcpy(cfg.mask, sc.mask, 4);
    memcpy(cfg.gw, sc.gw, 4);

    while (!bsp_w5500_init(&cfg)) {
        vTaskDelay(pdMS_TO_TICKS(5000));   /* chip absent / not ready: retry */
    }

    for (;;) {
        for (uint8_t sn = 0; sn < NET_MODBUS_SOCKETS; sn++) {
            switch (bsp_w5500_sock_status(sn)) {
            case W5500_SOCK_CLOSED:
                (void)bsp_w5500_sock_listen(sn, NET_MODBUS_PORT);
                break;
            case W5500_SOCK_ESTABLISHED: {
                uint16_t n = bsp_w5500_sock_recv(sn, rx, sizeof(rx));
                if (n > 0U) {
                    size_t r = mb_tcp_process(modbus_regs_map(), rx, n, tx, sizeof(tx));
                    if (r > 0U) {
                        (void)bsp_w5500_sock_send(sn, tx, (uint16_t)r);
                    }
                }
                break;
            }
            case W5500_SOCK_CLOSE_WAIT:
                bsp_w5500_sock_disconnect(sn);
                break;
            case W5500_SOCK_LISTEN:
            case W5500_SOCK_INIT:
                break;
            default:
                /* transient states (SYNSENT, FIN_WAIT...) - close if stuck */
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
