/*
 * 最小化 FreeRTOS 内核（对应野火《FreeRTOS内核实现与应用开发实战指南》
 * 第11章“任务延时列表的实现”）。
 *
 * main.c 保持第9章的写法：任务创建后由 main 手动插入 pxReadyTasksLists[1]/[2]。
 * 因此这里把“就绪列表的数组下标”直接当作任务优先级：
 *   - 空闲任务在 pxReadyTasksLists[0]
 *   - 任务1 在 pxReadyTasksLists[1]
 *   - 任务2 在 pxReadyTasksLists[2]
 *
 * 第11章起，延时任务不再留在就绪列表里：
 *   vTaskDelay() 把任务从就绪列表移除并插入延时列表，
 *   SysTick 中断通过 xNextTaskUnblockTime 判断到期后，再把任务放回就绪列表。
 */

#include "FreeRTOS.h"
#include "task.h"
#include "list.h"

/* 任务就绪列表：下标即优先级 */
List_t pxReadyTasksLists[ configMAX_PRIORITIES ];

/* 当前正在运行（或即将运行）的任务控制块 */
TCB_t * volatile pxCurrentTCB = NULL;

/* 系统时基计数器，每个 SysTick 中断加 1 */
volatile TickType_t xTickCount = 0;

/* 就绪任务优先级位图：bit n 为 1 表示优先级 n 有就绪任务 */
static volatile UBaseType_t uxTopReadyPriority = tskIDLE_PRIORITY;

/* ---------------- 任务延时列表（第11章） ---------------- */
static List_t xDelayedTaskList1;
static List_t xDelayedTaskList2;
static List_t * volatile pxDelayedTaskList = NULL;
static List_t * volatile pxOverflowDelayedTaskList = NULL;

/* 下一个任务解锁时刻（xTickCount 达到该值时有任务延时到期） */
static volatile TickType_t xNextTaskUnblockTime = portMAX_DELAY;

/* 时基计数器溢出次数 */
static UBaseType_t xNumOfOverflows = 0;

/* 由用户在 main.c 中提供空闲任务的内存 */
void vApplicationGetIdleTaskMemory( TCB_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize );

/*-----------------------------------------------------------*/
/* 查找最高优先级就绪任务（参考野火第10章）                    */
/*-----------------------------------------------------------*/

#if ( configUSE_PORT_OPTIMISED_TASK_SELECTION == 1 )

/* 根据优先级把位图中对应的位置 1 */
#define taskRECORD_READY_PRIORITY( uxPriority ) \
    portRECORD_READY_PRIORITY( ( uxPriority ), uxTopReadyPriority )

/* 根据优先级把位图中对应的位清 0。
 * 第11章任务可能被移到延时列表，所以要确认该优先级下确实没有就绪任务了，
 * 才把位图中的对应位清零。 */
#define taskRESET_READY_PRIORITY( uxPriority )                                  \
    do {                                                                        \
        if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ ( uxPriority ) ] ) ) \
            == ( UBaseType_t ) 0 )                                              \
        {                                                                       \
            portRESET_READY_PRIORITY( ( uxPriority ), uxTopReadyPriority );     \
        }                                                                       \
    } while( 0 )

/* 找到最高优先级就绪任务并更新 pxCurrentTCB */
#define taskSELECT_HIGHEST_PRIORITY_TASK()                                      \
    do {                                                                        \
        UBaseType_t uxTopPriority;                                              \
        portGET_HIGHEST_PRIORITY( uxTopPriority, uxTopReadyPriority );          \
        listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB,                              \
                                     &( pxReadyTasksLists[ uxTopPriority ] ) ); \
    } while( 0 )

#else /* 通用方法（第11章起延时任务已从就绪列表移除，通用方法可以正常工作） */

#define taskRECORD_READY_PRIORITY( uxPriority )                                 \
    do {                                                                        \
        if( ( uxPriority ) > uxTopReadyPriority )                               \
        {                                                                       \
            uxTopReadyPriority = ( uxPriority );                                \
        }                                                                       \
    } while( 0 )

#define taskRESET_READY_PRIORITY( uxPriority )

#define taskSELECT_HIGHEST_PRIORITY_TASK()                                      \
    do {                                                                        \
        UBaseType_t uxTopPriority = uxTopReadyPriority;                         \
        while( listLIST_IS_EMPTY( &( pxReadyTasksLists[ uxTopPriority ] ) ) )   \
        {                                                                       \
            --uxTopPriority;                                                    \
        }                                                                       \
        listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB,                              \
                                     &( pxReadyTasksLists[ uxTopPriority ] ) ); \
        uxTopReadyPriority = uxTopPriority;                                     \
    } while( 0 )

#endif /* configUSE_PORT_OPTIMISED_TASK_SELECTION */

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

    /* 初始化任务延时列表 */
    vListInitialise( &xDelayedTaskList1 );
    vListInitialise( &xDelayedTaskList2 );
    pxDelayedTaskList = &xDelayedTaskList1;
    pxOverflowDelayedTaskList = &xDelayedTaskList2;
}

/*-----------------------------------------------------------*/

/* 复位“下一个任务解锁时刻” */
static void prvResetNextTaskUnblockTime( void )
{
    TCB_t *pxTCB;

    if( listLIST_IS_EMPTY( pxDelayedTaskList ) != pdFALSE )
    {
        xNextTaskUnblockTime = portMAX_DELAY;
    }
    else
    {
        pxTCB = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( pxDelayedTaskList );
        xNextTaskUnblockTime = listGET_LIST_ITEM_VALUE( &( pxTCB->xStateListItem ) );
    }
}

/* 时基计数器溢出时切换延时列表 */
#define taskSWITCH_DELAYED_LISTS()                                              \
    do {                                                                        \
        List_t *pxTemp = pxDelayedTaskList;                                     \
        pxDelayedTaskList = pxOverflowDelayedTaskList;                          \
        pxOverflowDelayedTaskList = pxTemp;                                     \
        xNumOfOverflows++;                                                      \
        prvResetNextTaskUnblockTime();                                          \
    } while( 0 )

/* 将任务按优先级添加到就绪列表 */
#define prvAddTaskToReadyList( pxTCB )                                          \
    do {                                                                        \
        taskRECORD_READY_PRIORITY( ( pxTCB )->uxPriority );                     \
        vListInsertEnd( &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ),        \
                        &( ( pxTCB )->xStateListItem ) );                       \
    } while( 0 )

/*-----------------------------------------------------------*/

/* main.c 是手动把任务插入就绪列表的，这里根据任务所在的就绪列表
 * 回填 TCB 的 uxPriority，并建立优先级位图。 */
static void prvInitialiseReadyPriorities( void )
{
    UBaseType_t uxPriority;
    ListItem_t *pxItem;

    uxTopReadyPriority = tskIDLE_PRIORITY;

    for( uxPriority = ( UBaseType_t ) 0U;
         uxPriority < ( UBaseType_t ) configMAX_PRIORITIES;
         uxPriority++ )
    {
        pxItem = listGET_HEAD_ENTRY( &( pxReadyTasksLists[ uxPriority ] ) );

        while( pxItem != ( ListItem_t * ) &( pxReadyTasksLists[ uxPriority ].xListEnd ) )
        {
            TCB_t *pxTCB = ( TCB_t * ) listGET_LIST_ITEM_OWNER( pxItem );

            pxTCB->uxPriority = uxPriority;
            taskRECORD_READY_PRIORITY( uxPriority );

            pxItem = listGET_NEXT( pxItem );
        }
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

    /* 优先级先在调度器启动时根据就绪列表回填 */
    pxNewTCB->uxPriority = tskIDLE_PRIORITY;

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

    vListInsertEnd( &( pxReadyTasksLists[ tskIDLE_PRIORITY ] ),
                    &( ( (TCB_t *) pxIdleTaskTCBBuffer )->xStateListItem ) );

    /* 根据 main.c 中手动插入的就绪列表，回填优先级并建立位图 */
    prvInitialiseReadyPriorities();

    /* 初始化系统时基和下一个任务解锁时刻 */
    xTickCount = ( TickType_t ) 0U;
    xNextTaskUnblockTime = portMAX_DELAY;

    /* 选择最高优先级的就绪任务作为第一个运行的任务 */
    taskSELECT_HIGHEST_PRIORITY_TASK();

    /* 启动调度器，成功则不返回 */
    if( xPortStartScheduler() != pdFALSE )
    {
        /* 正常不会到这里 */
    }
}

/*-----------------------------------------------------------*/

/* 将当前任务从就绪列表移到延时列表（第11章） */
static void prvAddCurrentTaskToDelayedList( TickType_t xTicksToWait )
{
    TickType_t xTimeToWake;
    const TickType_t xConstTickCount = xTickCount;

    /* 将任务从就绪列表移除 */
    ( void ) uxListRemove( &( pxCurrentTCB->xStateListItem ) );

    /* 如果该优先级下已经没有就绪任务，清掉优先级位图对应位 */
    taskRESET_READY_PRIORITY( pxCurrentTCB->uxPriority );

    /* 计算任务延时到期的时刻 */
    xTimeToWake = xConstTickCount + xTicksToWait;

    /* 用唤醒时刻作为延时列表节点的排序值 */
    listSET_LIST_ITEM_VALUE( &( pxCurrentTCB->xStateListItem ), xTimeToWake );

    if( xTimeToWake < xConstTickCount )
    {
        /* 时基计数器溢出，插入溢出延时列表 */
        vListInsert( pxOverflowDelayedTaskList, &( pxCurrentTCB->xStateListItem ) );
    }
    else
    {
        /* 没有溢出，插入正常延时列表 */
        vListInsert( pxDelayedTaskList, &( pxCurrentTCB->xStateListItem ) );

        /* 更新下一个任务解锁时刻 */
        if( xTimeToWake < xNextTaskUnblockTime )
        {
            xNextTaskUnblockTime = xTimeToWake;
        }
    }
}

/*-----------------------------------------------------------*/

void vTaskDelay( const TickType_t xTicksToDelay )
{
    /* 把当前任务挂到延时列表 */
    prvAddCurrentTaskToDelayedList( xTicksToDelay );

    /* 主动让出 CPU，在 PendSV 中挑选下一个可运行任务 */
    taskYIELD();
}

/*-----------------------------------------------------------*/

void xTaskIncrementTick( void )
{
    TCB_t *pxTCB;
    TickType_t xItemValue;
    const TickType_t xConstTickCount = xTickCount + 1;

    /* 更新系统时基 */
    xTickCount = xConstTickCount;

    /* 如果时基计数器溢出，则切换延时列表 */
    if( xConstTickCount == ( TickType_t ) 0U )
    {
        taskSWITCH_DELAYED_LISTS();
    }

    /* 最近的延时任务是否到期 */
    if( xConstTickCount >= xNextTaskUnblockTime )
    {
        for( ;; )
        {
            if( listLIST_IS_EMPTY( pxDelayedTaskList ) != pdFALSE )
            {
                /* 延时列表为空，设置最大解锁时刻并退出 */
                xNextTaskUnblockTime = portMAX_DELAY;
                break;
            }
            else
            {
                /* 取出延时列表头部的任务 */
                pxTCB = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( pxDelayedTaskList );
                xItemValue = listGET_LIST_ITEM_VALUE( &( pxTCB->xStateListItem ) );

                /* 还没到期，更新下一个解锁时刻并退出 */
                if( xConstTickCount < xItemValue )
                {
                    xNextTaskUnblockTime = xItemValue;
                    break;
                }

                /* 从延时列表移除，并重新加入就绪列表 */
                ( void ) uxListRemove( &( pxTCB->xStateListItem ) );
                prvAddTaskToReadyList( pxTCB );
            }
        }
    }

    /* 请求一次任务切换，让延时到期的任务能立即运行 */
    portYIELD();
}

/*-----------------------------------------------------------*/

void vTaskSwitchContext( void )
{
    /* 直接选择优先级最高的就绪任务，更新 pxCurrentTCB */
    taskSELECT_HIGHEST_PRIORITY_TASK();
}

/*-----------------------------------------------------------*/
