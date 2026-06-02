#include "hal_delay.h"
#include "gd32l23x.h"
#include "FreeRTOS.h"
#include "task.h"

void hal_delay_ms(uint32_t ms)
{
    while (ms--) {
        SysTick->LOAD = SystemCoreClock / 1000 - 1;
        SysTick->VAL  = 0;
        SysTick->CTRL = SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_CLKSOURCE_Msk;
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
        SysTick->CTRL = 0;
    }
}

void hal_delay_task_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t hal_get_tick_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
