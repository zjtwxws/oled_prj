/**
 * @file    app_watchdog.c
 * @brief   任务看门狗监视器实现（任务报到 + 硬件看门狗喂狗）
 */

#include "FreeRTOS.h"
#include "task.h"

#include "iwdg.h"
#include "debug_console.h"
#include "cli_cmds.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define WDT_MONITOR_MAX_TASKS 16U /* 支持监控的最大任务数 */

/**
 * @brief  每个任务的监控槽
 */
typedef struct
{
    const char *name;           /* 任务名（用于复位后日志定位） */
    uint32_t    timeout_ms;     /* 该任务允许的最大无响应时间 */
    uint32_t    last_feed_ms;   /* 上次报到的系统时间戳 */
    uint8_t     registered;     /* 是否已注册 */
    uint8_t     fed_this_cycle; /* 本轮是否已报到 */
} TaskWdtSlot_t;

static TaskWdtSlot_t s_slots[WDT_MONITOR_MAX_TASKS];
static uint8_t s_slot_count = 0U;

/**
 * @brief  注册任务到看门狗监视器
 * @param  name       任务名（指针必须保持有效）
 * @param  timeout_ms 允许的最大无响应时间（毫秒）
 * @param  enable_flag 是否启用看门狗监控
 * @return 成功返回槽位 ID；槽位已满返回 -1
 * @note   注册应在调度器启动前完成，或加临界区保护
 * @date   2026-09-01
 */
int wdt_monitor_register(const char *name, uint32_t timeout_ms, uint8_t enable_flag)
{
	if (0 == enable_flag)
	{
		return 0; /* 不启用看门狗监控，直接返回成功 */
	}

    if (s_slot_count >= WDT_MONITOR_MAX_TASKS)
    {
        return -1; /* 槽位已满 */
    }

    int id = (int)s_slot_count;
	
    s_slots[id].name = name;
    s_slots[id].timeout_ms = timeout_ms;
    s_slots[id].last_feed_ms = 0U;
    s_slots[id].registered = 1U;
    s_slots[id].fed_this_cycle = 0U;

    s_slot_count++;
	
    return id;
}

/**
 * @brief  任务报到：更新时间戳，标记本轮已报到
 * @param  slot_id 注册时返回的槽位 ID
 * @note   xTaskGetTickCount() 本身是原子读，不需要额外临界区
 * @date   2026-09-01
 */
 void wdt_monitor_feed(int slot_id)
{
    if (slot_id < 0 || slot_id >= (int)s_slot_count)
    {
        return;
    }

    s_slots[slot_id].last_feed_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    s_slots[slot_id].fed_this_cycle = 1U;
}

/**
 * @brief  监视器检查：所有任务都报到了才喂硬件看门狗
 * @note   应在看门狗任务主循环中周期调用
 * @date   2026-09-01
 */
static void wdt_monitor_check(void)
{
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint8_t  all_healthy = 1U;
    uint8_t  i;

    for (i = 0U; i < s_slot_count; i++)
    {
        TaskWdtSlot_t *slot = &s_slots[i];
        uint32_t elapsed = 0;

        if (slot->registered == 0U)
        {
            continue;
        }

        elapsed = now_ms - slot->last_feed_ms;

        if (elapsed > slot->timeout_ms)
        {
            /* 超时！记录故障任务名，然后不喂狗，等待硬件复位 */
            /* 这里可以写 NVRAM / Flash Log，复位后用于根因分析 */
            DEBUG_PRINTF("WDT timeout: task [%s] silent for %u ms (> %u ms)",
                         slot->name, (unsigned int)elapsed, (unsigned int)slot->timeout_ms);
			
            all_healthy = 0U;
			
            break; /* 一票否决 */
        }

        /* 重置本轮标志，准备下一轮检查 */
        slot->fed_this_cycle = 0U;
    }

    if (all_healthy != 0U)
    {
        /* 全票通过，才喂硬件看门狗 */
        iwdg_drv_feed();
    }
    /* 否则：静默等待硬件看门狗超时，触发复位 */
}

/**
 * @brief  CLI 命令：显示任务看门狗监控状态
 * @param  argc 参数个数
 * @param  argv 参数数组
 * @return 0=成功, 其他=失败
 * @date   2026-09-01
 */
int cli_cmd_wdt_monitor(uint8_t argc, char **argv)
{
	(void)argc;
	(void)argv;

	DEBUG_PRINTF("WDT Monitor Status: %u slots", (unsigned int)s_slot_count);
	DEBUG_PRINTF("  Slot  Name          Registered  Timeout (ms)  Last Feed (ms)  elapsed (ms)  Fed This Cycle");
	for (uint8_t i = 0U; i < s_slot_count; i++)
	{
		TaskWdtSlot_t *slot = &s_slots[i];
		uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
		uint32_t elapsed = now_ms - slot->last_feed_ms;
		DEBUG_PRINTF("  %2u    %-12s  %u           %10u  %14u  %7u  %14u",
					 (unsigned int)i,
					 slot->name,
					 (unsigned int)slot->registered,
					 (unsigned int)slot->timeout_ms,
					 (unsigned int)slot->last_feed_ms,
					 (unsigned int)elapsed,
					 (unsigned int)slot->fed_this_cycle);

	}

	return 0;
}

/* 看门狗任务：高于被监控任务的优先级，定期轮询 */
void watchdog_task(void *argument)
{
	(void)argument;
	DEBUG_PRINTF("watchdog_monitor_task start");

	cli_cmd_register("wdt_monitor", cli_cmd_wdt_monitor, "显示任务看门狗监控状态");

	const TickType_t xPeriod = pdMS_TO_TICKS(1000); /* 每1秒检查一次 */
	TickType_t xLastWakeTime = xTaskGetTickCount();

	for (;;) 
	{
		wdt_monitor_check();

		/* 用 vTaskDelayUntil 保证固定周期，避免累积误差 */
		xTaskDelayUntil(&xLastWakeTime, xPeriod);
	}
}

