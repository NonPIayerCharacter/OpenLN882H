#include "proj_config.h"
#include "ln882h.h"
#include "hal/hal_cache.h"
#include "hal/hal_flash.h"
#include "mode_ctrl.h"
#include "utils/runtime/runtime.h"

static void set_interrupt_priority(void)
{
    __NVIC_SetPriorityGrouping(4);

    NVIC_SetPriority(SysTick_IRQn, 1);
    NVIC_SetPriority(UART0_IRQn, 4);
    NVIC_SetPriority(UART1_IRQn, 4);
}

int main(int argc, char* argv[])
{
    SetSysClock();
    set_interrupt_priority();
    __enable_irq();

    hal_flash_init();
    hal_flash_quad_mode_enable(1);
    flash_cache_disable();
    ln_runtime_measure_init();
    bootram_ctrl_init();
    bootram_ctrl_loop();

    while (1)
        ;
}

void MemManage_Handler(void)
{
    while (1)
        ;
}

void BusFault_Handler(void)
{
    while (1)
        ;
}

void UsageFault_Handler(void)
{
    while (1)
        ;
}

void* malloc(size_t size)
{
  extern unsigned int Image$$HEAP_SPACE0$$ZI$$Base;
  extern unsigned int Image$$HEAP_SPACE0$$ZI$$Limit;
	if(size > (uint32_t)(&Image$$HEAP_SPACE0$$ZI$$Limit) - (uint32_t)(&Image$$HEAP_SPACE0$$ZI$$Base)) return NULL;
	return &Image$$HEAP_SPACE0$$ZI$$Base;
}
