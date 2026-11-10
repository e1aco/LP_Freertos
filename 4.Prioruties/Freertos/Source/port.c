/*
 * 最小化 FreeRTOS 移植层（Cortex-M3 / ARMClang AC6）。
 *
 * 对应野火《FreeRTOS内核实现与应用开发实战指南》第7章：
 *   - pxPortInitialiseStack()：伪造任务第一次运行时的栈帧
 *   - prvStartFirstTask()    ：设置 MSP、开中断，然后用 SVC 启动第一个任务
 *   - SVC_Handler()          ：从第一个任务的栈中恢复上下文
 *   - PendSV_Handler()       ：保存上文、调用 vTaskSwitchContext()、恢复下文
 *
 * 注意：书上的 __asm void 是 AC5 语法，这里改成 AC6 支持的
 *       __attribute__((naked)) + __asm volatile，逻辑与书上一致。
 */

#include "FreeRTOS.h"
#include "task.h"

/* SCB->SHPR3：用于设置 SysTick 和 PendSV 的异常优先级 */
#define portNVIC_SYSPRI2_REG     ( *( ( volatile uint32_t * ) 0xe000ed20 ) )
#define portNVIC_PENDSV_PRI      ( ( ( uint32_t ) configKERNEL_INTERRUPT_PRIORITY ) << 16UL )
#define portNVIC_SYSTICK_PRI     ( ( ( uint32_t ) configKERNEL_INTERRUPT_PRIORITY ) << 24UL )

/* 任务第一次运行时 xPSR 的 bit24 必须为 1，表示 Thumb 状态 */
#define portINITIAL_XPSR         ( 0x01000000 )
#define portSTART_ADDRESS_MASK   ( ( StackType_t ) 0xfffffffeUL )

/* SysTick 相关寄存器（第 9 章：产生系统时基中断） */
#define portNVIC_SYSTICK_CTRL_REG             ( *( ( volatile uint32_t * ) 0xe000e010 ) )
#define portNVIC_SYSTICK_LOAD_REG             ( *( ( volatile uint32_t * ) 0xe000e014 ) )
#define portNVIC_SYSTICK_CURRENT_VALUE_REG    ( *( ( volatile uint32_t * ) 0xe000e018 ) )

#define portNVIC_SYSTICK_CLK_BIT              ( 1UL << 2UL )
#define portNVIC_SYSTICK_INT_BIT              ( 1UL << 1UL )
#define portNVIC_SYSTICK_ENABLE_BIT           ( 1UL << 0UL )

/*-----------------------------------------------------------*/

/* 任务函数不允许返回，返回了就停在这里 */
static void prvTaskExitError( void )
{
    for( ;; )
    {
    }
}

/*-----------------------------------------------------------*/

StackType_t * pxPortInitialiseStack( StackType_t * pxTopOfStack,
                                     TaskFunction_t pxCode,
                                     void * pvParameters )
{
    /* 异常发生时，硬件自动入栈的内容：xPSR、PC、LR、R12、R3~R0 */
    pxTopOfStack--;
    *pxTopOfStack = portINITIAL_XPSR;                                     /* xPSR */
    pxTopOfStack--;
    *pxTopOfStack = ( ( StackType_t ) pxCode ) & portSTART_ADDRESS_MASK;  /* PC，任务入口 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) prvTaskExitError;                     /* LR，任务返回地址 */
    pxTopOfStack -= 5;                                                    /* R12、R3、R2、R1，默认 0 */
    *pxTopOfStack = ( StackType_t ) pvParameters;                         /* R0，任务形参 */

    /* 异常发生时，需要手动入栈的内容：R4~R11，默认 0 */
    pxTopOfStack -= 8;

    return pxTopOfStack;
}

/*-----------------------------------------------------------*/

static void prvStartFirstTask( void ) __attribute__( ( naked ) );

static void prvStartFirstTask( void )
{
    __asm volatile
    (
        "   ldr r0, =0xE000ED08   \n" /* SCB->VTOR */
        "   ldr r0, [r0]          \n" /* 向量表起始地址 */
        "   ldr r0, [r0]          \n" /* 向量表第一个字：初始 MSP */
        "   msr msp, r0           \n" /* 复位主栈指针 */
        "   cpsie i               \n" /* 开中断 */
        "   cpsie f               \n"
        "   dsb                   \n"
        "   isb                   \n"
        "   svc 0                 \n" /* 产生 SVC，去启动第一个任务 */
        "   nop                   \n"
        "   .ltorg                \n"
    );
}

/*-----------------------------------------------------------*/

/* 配置 SysTick，按 configTICK_RATE_HZ 产生系统时基中断 */
static void vPortSetupTimerInterrupt( void )
{
    /* 先停止 SysTick */
    portNVIC_SYSTICK_CTRL_REG = 0UL;
    portNVIC_SYSTICK_CURRENT_VALUE_REG = 0UL;

    /* 重装载值 = 内核时钟 / 系统节拍频率 - 1 */
    portNVIC_SYSTICK_LOAD_REG = ( configCPU_CLOCK_HZ / configTICK_RATE_HZ ) - 1UL;

    /* 使用内核时钟、使能中断、启动计数 */
    portNVIC_SYSTICK_CTRL_REG = ( portNVIC_SYSTICK_CLK_BIT |
                                  portNVIC_SYSTICK_INT_BIT |
                                  portNVIC_SYSTICK_ENABLE_BIT );
}

/*-----------------------------------------------------------*/

/* SysTick 中断服务函数：更新时基并触发任务切换 */
void xPortSysTickHandler( void )
{
    portDISABLE_INTERRUPTS();
    xTaskIncrementTick();
    portENABLE_INTERRUPTS();
}

/* 向量表中 SysTick_Handler 由这里提供（覆盖启动文件的弱定义） */
void SysTick_Handler( void )
{
    xPortSysTickHandler();
}

/*-----------------------------------------------------------*/

BaseType_t xPortStartScheduler( void )
{
    /* 把 PendSV 和 SysTick 的优先级设为最低 */
    portNVIC_SYSPRI2_REG |= portNVIC_PENDSV_PRI;
    portNVIC_SYSPRI2_REG |= portNVIC_SYSTICK_PRI;

    /* 初始化 SysTick */
    vPortSetupTimerInterrupt();

    /* 启动第一个任务，不再返回 */
    prvStartFirstTask();

    return 0;
}

/*-----------------------------------------------------------*/

void SVC_Handler( void ) __attribute__( ( naked ) );

void SVC_Handler( void )
{
    __asm volatile
    (
        "   ldr r3, pxCurrentTCBConst2  \n"
        "   ldr r1, [r3]                \n" /* r1 = pxCurrentTCB */
        "   ldr r0, [r1]                \n" /* r0 = pxTopOfStack */
        "   ldmia r0!, {r4-r11}         \n" /* 手动恢复 R4~R11 */
        "   msr psp, r0                 \n" /* 剩下的交给硬件出栈 */
        "   isb                         \n"
        "   mov r0, #0                  \n"
        "   msr basepri, r0             \n" /* 开中断 */
        "   orr r14, #0xd               \n" /* 返回后使用 PSP、线程模式、Thumb */
        "   bx r14                      \n"
        "                               \n"
        "   .align 4                    \n"
        "pxCurrentTCBConst2: .word pxCurrentTCB \n"
    );
}

/*-----------------------------------------------------------*/

void PendSV_Handler( void ) __attribute__( ( naked ) );

void PendSV_Handler( void )
{
    __asm volatile
    (
        "   mrs r0, psp                 \n" /* r0 = 当前任务的 PSP */
        "   isb                         \n"
        "                               \n"
        "   ldr r3, pxCurrentTCBConst    \n"
        "   ldr r2, [r3]                \n" /* r2 = pxCurrentTCB */
        "                               \n"
        "   stmdb r0!, {r4-r11}         \n" /* 手动保存 R4~R11 */
        "   str r0, [r2]                \n" /* 更新 pxTopOfStack（保存上文完成） */
        "                               \n"
        "   stmdb sp!, {r3, r14}        \n" /* 保护 r3、r14 */
        "   mov r0, %0                  \n"
        "   msr basepri, r0             \n" /* 关中断，进入临界段 */
        "   dsb                         \n"
        "   isb                         \n"
        "   bl vTaskSwitchContext       \n" /* 选择下一个任务 */
        "   mov r0, #0                  \n"
        "   msr basepri, r0             \n" /* 开中断 */
        "   ldmia sp!, {r3, r14}        \n"
        "                               \n"
        "   ldr r1, [r3]                \n" /* r1 = pxCurrentTCB（已被更新） */
        "   ldr r0, [r1]                \n" /* r0 = 新任务的 pxTopOfStack */
        "   ldmia r0!, {r4-r11}         \n" /* 恢复 R4~R11 */
        "   msr psp, r0                 \n"
        "   isb                         \n"
        "   bx r14                      \n"
        "                               \n"
        "   .align 4                    \n"
        "pxCurrentTCBConst: .word pxCurrentTCB \n"
        : : "i" ( configMAX_SYSCALL_INTERRUPT_PRIORITY ) : "memory"
    );
}

/*-----------------------------------------------------------*/
