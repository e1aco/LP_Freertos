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

/* 本章只创建这两个任务，TCB 定义在 main.c 中 */
extern TCB_t Task1TCB;
extern TCB_t Task2TCB;

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

void vTaskStartScheduler( void )
{
    /* 手动指定第一个运行的任务 */
    pxCurrentTCB = &Task1TCB;

    /* 启动调度器，成功则不返回 */
    if( xPortStartScheduler() != pdFALSE )
    {
        /* 正常不会到这里 */
    }
}

/*-----------------------------------------------------------*/

void vTaskSwitchContext( void )
{
    /* 本章两个任务轮流切换：当前是任务1就切到任务2，反之亦然 */
    if( pxCurrentTCB == &Task1TCB )
    {
        pxCurrentTCB = &Task2TCB;
    }
    else
    {
        pxCurrentTCB = &Task1TCB;
    }
}

/*-----------------------------------------------------------*/
