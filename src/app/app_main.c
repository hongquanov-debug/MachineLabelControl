/**
 * Task map (CMSIS-RTOS v2)
 * ------------------------------------------------------------------------------
 *  Task        Prio               Period / trigger      Role
 *  io          Realtime           1 ms                  LI32 scan, LO24 commit, safety chain
 *  label       High2              queue (per can)       station 3: M3 index, P5, vacuum
 *  orient      High1              queue (per can)       station 2: P3/P4/M2
 *  feed        High               belt pitch            P1/P2 escapement
 *  vision      AboveNormal        RS485_3 stream        Vision protocol
 *  machine     Normal1            10 ms                 FSM, lamps, recipe, watchdog
 *  homing      Normal             notification          homing / purge sequence
 *  hmi         Normal             RS485_1 idle          Modbus RTU slave
 *  vfd         BelowNormal        100 ms                Modbus RTU master (VFD M1)
 *  net         Low                5 ms                  Modbus TCP (W5500)
 *
 *  ISR (outside RTOS): pulse timers M2..M6 (prio 2)
 *  ISR (RTOS aware)  : EXTI2 S2 + TIM6 tracking (prio 5), UART/DMA (prio 6)
 */
#include "app_main.h"

#include "alarm.h"
#include "bsp_encoder.h"
#include "cmsis_os2.h"
#include "hmi_link.h"
#include "homing.h"
#include "io.h"
#include "machine.h"
#include "modbus_regs.h"
#include "motion.h"
#include "net_link.h"
#include "params.h"
#include "recipe.h"
#include "station_feed.h"
#include "station_label.h"
#include "station_orient.h"
#include "tracking.h"
#include "vfd.h"
#include "vision.h"

typedef struct {
    osThreadFunc_t fn;
    const char    *name;
    uint32_t       stack_bytes;
    osPriority_t   prio;
} TaskDef_t;

static const TaskDef_t k_tasks[] = {
    {io_task,             "io",      1024, osPriorityRealtime},
    {station_label_task,  "label",   1024, osPriorityHigh2},
    {station_orient_task, "orient",  1024, osPriorityHigh1},
    {station_feed_task,   "feed",     768, osPriorityHigh},
    {vision_task,         "vision",  1024, osPriorityAboveNormal},
    {machine_task,        "machine", 1280, osPriorityNormal1},
    {homing_task,         "homing",  1024, osPriorityNormal},
    {hmi_link_task,       "hmi",     1280, osPriorityNormal},
    {vfd_task,            "vfd",     1024, osPriorityBelowNormal},
    {net_link_task,       "net",     1536, osPriorityLow},
};

static void init_task(void *arg)
{
    (void)arg;

    /* 1. Outputs to a safe state as early as possible */
    io_init();

    /* 2. Persistent data */
    recipe_init();
    CanRecipe_t r;
    recipe_get_active(&r);
    params_init(r.belt_speed_cans_min);

    /* 3. Peripherals / modules */
    motion_init();
    bsp_encoder_init();
    machine_init();
    modbus_regs_init();
    vision_init();
    hmi_link_init();
    vfd_init();
    tracking_init();          /* starts TIM6 + EXTI2 */

    /* 4. Application tasks */
    for (unsigned i = 0; i < sizeof(k_tasks) / sizeof(k_tasks[0]); i++) {
        osThreadAttr_t attr = {0};
        attr.name       = k_tasks[i].name;
        attr.stack_size = k_tasks[i].stack_bytes;
        attr.priority   = k_tasks[i].prio;
        osThreadId_t id = osThreadNew(k_tasks[i].fn, NULL, &attr);
        configASSERT(id != NULL);
    }

    osThreadExit();
}

void app_create(void)
{
    osThreadAttr_t attr = {0};
    attr.name       = "init";
    attr.stack_size = 1536;
    attr.priority   = osPriorityRealtime7;
    (void)osThreadNew(init_task, NULL, &attr);
}
