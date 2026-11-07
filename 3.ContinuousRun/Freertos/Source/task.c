/*
 * 最小化 FreeRTOS 内核（对应野火《FreeRTOS内核实现与应用开发实战指南》
 * 第7章“任务的定义与任务切换”）。
 *
 * 本文件只实现本章需要的最小子集：
 *   - 就绪列表
 *   - 静态任务创建（把 TCB、栈、入口函数串起来）
 *   - 启动调度器
 *   - 任务切换（本章还不支持优先级，两个任务直接轮流切换）
 */

#include "FreeRTOS.h"
#include "task.h"
#include "list.h"

/* 任务就绪列表：下标即优先级，本章先不启用优先级 */
List_t pxReadyTasksLists[ configMAX_PRIORITIES ];

/* 当前正在运行（或即将运行）的任务控制块 */
TCB_t * volatile pxCurrentTCB = NULL;

/* 系统时基计数器，每个 SysTick 中断加 1 */
volatile TickType_t xTickCount = 0;

/* 本章只创建这两个任务，TCB 定义在 main.c 中 */
extern TCB_t Task1TCB;
extern TCB_t Task2TCB;

/* 空闲任务的 TCB 定义在 main.c 中 */
extern TCB_t IdleTaskTCB;

/* 由用户在 main.c 中提供空闲任务的内存 */
void vApplicationGetIdleTaskMemory( TCB_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize );

/*-----------------------------------------------------------*/

void prvInitialiseTaskLists( void )
{
    UBaseType_t uxPriority;

    for( uxPriority = ( UBaseType_t ) 0U;
         uxPriority < ( UBaseType_t ) configMAX_PRIORITIES;
         uxPriority++ )
    {
        vListInitialise( &( pxReadyTasksLists[ uxPriority ] ) );
    }
}

/*-----------------------------------------------------------*/

static void prvInitialiseNewTask( TaskFunction_t pxTaskCode,
                                  const char * const pcName,
                                  const uint32_t ulStackDepth,
                                  void * const pvParameters,
                                  TaskHandle_t * const pxCreatedTask,
                                  TCB_t * pxNewTCB )
{
    StackType_t * pxTopOfStack;
    UBaseType_t x;

    /* 获取栈顶地址（栈是向下增长的，数组最后一个元素为栈顶） */
    pxTopOfStack = pxNewTCB->pxStack + ( ulStackDepth - ( uint32_t ) 1 );

    /* 向下做 8 字节对齐（兼容浮点运算） */
    pxTopOfStack = ( StackType_t * )
                   ( ( ( uint32_t ) pxTopOfStack ) & ( ~( ( uint32_t ) 0x0007 ) ) );

    /* 保存任务名称 */
    for( x = ( UBaseType_t ) 0; x < ( UBaseType_t ) configMAX_TASK_NAME_LEN; x++ )
    {
        pxNewTCB->pcTaskName[ x ] = pcName[ x ];

        if( pcName[ x ] == 0x00 )
        {
            break;
        }
    }
    pxNewTCB->pcTaskName[ configMAX_TASK_NAME_LEN - 1 ] = '\0';

    /* 初始化 TCB 中的链表节点，并记录它的拥有者 */
    vListInitialiseItem( &( pxNewTCB->xStateListItem ) );
    listSET_LIST_ITEM_OWNER( &( pxNewTCB->xStateListItem ), pxNewTCB );

    /* 初始化任务栈：把任务第一次运行所需的环境参数压进栈里 */
    pxNewTCB->pxTopOfStack = pxPortInitialiseStack( pxTopOfStack,
                                                    pxTaskCode,
                                                    pvParameters );

    /* 返回任务句柄 */
    if( pxCreatedTask != NULL )
    {
        *pxCreatedTask = ( TaskHandle_t ) pxNewTCB;
    }
}

/*-----------------------------------------------------------*/

TaskHandle_t xTaskCreateStatic( TaskFunction_t pxTaskCode,
                                const char * const pcName,
                                const uint32_t ulStackDepth,
                                void * const pvParameters,
                                StackType_t * const puxStackBuffer,
                                TCB_t * const pxTaskBuffer )
{
    TCB_t * pxNewTCB;
    TaskHandle_t xReturn;

    if( ( pxTaskBuffer != NULL ) && ( puxStackBuffer != NULL ) )
    {
        pxNewTCB = ( TCB_t * ) pxTaskBuffer;
        pxNewTCB->pxStack = ( StackType_t * ) puxStackBuffer;

        prvInitialiseNewTask( pxTaskCode,
                              pcName,
                              ulStackDepth,
                              pvParameters,
                              &xReturn,
                              pxNewTCB );
    }
    else
    {
        xReturn = NULL;
    }

    return xReturn;
}

/*-----------------------------------------------------------*/

/* 空闲任务主体：没有其他任务可运行时执行 */
static volatile uint32_t ulIdleTaskCounter = 0;

static void prvIdleTask( void *pvParameters )
{
    ( void ) pvParameters;

    for( ;; )
    {
        ulIdleTaskCounter++;
    }
}

void vTaskStartScheduler( void )
{
    TCB_t *pxIdleTaskTCBBuffer = NULL;
    StackType_t *pxIdleTaskStackBuffer = NULL;
    uint32_t ulIdleTaskStackSize = 0;
    TaskHandle_t xIdleTaskHandle;

    /* 获取空闲任务的 TCB 与栈内存（由 main.c 提供） */
    vApplicationGetIdleTaskMemory( &pxIdleTaskTCBBuffer,
                                   &pxIdleTaskStackBuffer,
                                   &ulIdleTaskStackSize );

    /* 创建空闲任务，并挂到最低优先级就绪列表 [0] */
    xIdleTaskHandle = xTaskCreateStatic( (TaskFunction_t) prvIdleTask,
                                         (char *) "IDLE",
                                         (uint32_t) ulIdleTaskStackSize,
                                         (void *) NULL,
                                         (StackType_t *) pxIdleTaskStackBuffer,
                                         (TCB_t *) pxIdleTaskTCBBuffer );
    ( void ) xIdleTaskHandle;

    vListInsertEnd( &( pxReadyTasksLists[0] ),
                    &( ( (TCB_t *) pxIdleTaskTCBBuffer )->xStateListItem ) );

    /* 手动指定第一个运行的任务 */
    pxCurrentTCB = &Task1TCB;

    /* 启动调度器，成功则不返回 */
    if( xPortStartScheduler() != pdFALSE )
    {
        /* 正常不会到这里 */
    }
}

/*-----------------------------------------------------------*/

/*-----------------------------------------------------------*/

void vTaskDelay( const TickType_t xTicksToDelay )
{
    TCB_t *pxTCB = pxCurrentTCB;

    /* 记录需要阻塞的 tick 数，下次 SysTick 开始逐个递减 */
    pxTCB->xTicksToDelay = xTicksToDelay;

    /* 主动让出 CPU，在 PendSV 中挑选下一个可运行任务 */
    taskYIELD();
}

/*-----------------------------------------------------------*/

void xTaskIncrementTick( void )
{
    BaseType_t i;
    ListItem_t *pxItem;
    TCB_t *pxTCB;

    /* 更新系统时基 */
    xTickCount++;

    /* 扫描所有就绪列表中任务的延时计数，不为 0 则递减 */
    for( i = 0; i < ( BaseType_t ) configMAX_PRIORITIES; i++ )
    {
        pxItem = listGET_HEAD_ENTRY( &( pxReadyTasksLists[ i ] ) );

        while( pxItem != ( ListItem_t * ) &( pxReadyTasksLists[ i ].xListEnd ) )
        {
            pxTCB = ( TCB_t * ) listGET_LIST_ITEM_OWNER( pxItem );

            if( pxTCB->xTicksToDelay > 0 )
            {
                pxTCB->xTicksToDelay--;
            }

            pxItem = listGET_NEXT( pxItem );
        }
    }

    /* 请求一次任务切换，让延时到期的任务能立即运行 */
    portYIELD();
}

/*-----------------------------------------------------------*/

void vTaskSwitchContext( void )
{
    /* 与书上第 9 章一致：在任务1、任务2、空闲任务之间挑选可运行的任务 */
    if( pxCurrentTCB == &IdleTaskTCB )
    {
        /* 空闲任务正在运行：检查任务1/任务2 的延时是否结束 */
        if( Task1TCB.xTicksToDelay == 0 )
        {
            pxCurrentTCB = &Task1TCB;
        }
        else if( Task2TCB.xTicksToDelay == 0 )
        {
            pxCurrentTCB = &Task2TCB;
        }
        else
        {
            /* 两个任务都还在延时，继续运行空闲任务 */
            return;
        }
    }
    else
    {
        if( pxCurrentTCB == &Task1TCB )
        {
            if( Task2TCB.xTicksToDelay == 0 )
            {
                pxCurrentTCB = &Task2TCB;
            }
            else if( pxCurrentTCB->xTicksToDelay != 0 )
            {
                pxCurrentTCB = &IdleTaskTCB;
            }
            else
            {
                return;
            }
        }
        else if( pxCurrentTCB == &Task2TCB )
        {
            if( Task1TCB.xTicksToDelay == 0 )
            {
                pxCurrentTCB = &Task1TCB;
            }
            else if( pxCurrentTCB->xTicksToDelay != 0 )
            {
                pxCurrentTCB = &IdleTaskTCB;
            }
            else
            {
                return;
            }
        }
    }
}

/*-----------------------------------------------------------*/
