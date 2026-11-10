/*
 * 最小化 FreeRTOS 内核（对应野火《FreeRTOS内核实现与应用开发实战指南》
 * 第10章“支持多优先级”）。
 *
 * main.c 保持第9章的写法：任务创建后由 main 手动插入 pxReadyTasksLists[1]/[2]。
 * 因此这里把“就绪列表的数组下标”直接当作任务优先级：
 *   - 空闲任务在 pxReadyTasksLists[0]
 *   - 任务1 在 pxReadyTasksLists[1]
 *   - 任务2 在 pxReadyTasksLists[2]
 * 调度时总是选择下标最大（优先级最高）的就绪任务。
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

/* 根据优先级把位图中对应的位清 0 */
#define taskRESET_READY_PRIORITY( uxPriority ) \
    portRESET_READY_PRIORITY( ( uxPriority ), uxTopReadyPriority )

/* 找到最高优先级就绪任务并更新 pxCurrentTCB */
#define taskSELECT_HIGHEST_PRIORITY_TASK()                                      \
    do {                                                                        \
        UBaseType_t uxTopPriority;                                              \
        portGET_HIGHEST_PRIORITY( uxTopPriority, uxTopReadyPriority );          \
        listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB,                              \
                                     &( pxReadyTasksLists[ uxTopPriority ] ) ); \
    } while( 0 )

#else /* 通用方法 */

/* 通用方法下，延时任务暂时还留在就绪列表里，所以不能只看“链表是否为空”，
 * 还要判断链表头任务的 xTicksToDelay 是否为 0（是否真的就绪）。 */
static UBaseType_t prvGetHighestReadyPriority( void )
{
    UBaseType_t uxPriority = uxTopReadyPriority;
    ListItem_t *pxItem;
    TCB_t *pxTCB;

    if( uxPriority >= ( UBaseType_t ) configMAX_PRIORITIES )
    {
        uxPriority = ( UBaseType_t ) configMAX_PRIORITIES - 1U;
    }

    for( ;; )
    {
        pxItem = listGET_HEAD_ENTRY( &( pxReadyTasksLists[ uxPriority ] ) );

        if( pxItem != ( ListItem_t * ) &( pxReadyTasksLists[ uxPriority ].xListEnd ) )
        {
            pxTCB = ( TCB_t * ) listGET_LIST_ITEM_OWNER( pxItem );

            /* 延时为 0 才是真正就绪的任务 */
            if( pxTCB->xTicksToDelay == 0 )
            {
                break;
            }
        }

        if( uxPriority == tskIDLE_PRIORITY )
        {
            /* 空闲任务永远就绪，正常不会走到这里 */
            break;
        }

        uxPriority--;
    }

    return uxPriority;
}

#define taskRECORD_READY_PRIORITY( uxPriority )                                 \
    do {                                                                        \
        if( ( uxPriority ) > uxTopReadyPriority )                               \
        {                                                                       \
            uxTopReadyPriority = ( uxPriority );                                \
        }                                                                       \
    } while( 0 )

#define taskRESET_READY_PRIORITY( uxPriority )                                  \
    do {                                                                        \
        uxTopReadyPriority = prvGetHighestReadyPriority();                      \
    } while( 0 )

#define taskSELECT_HIGHEST_PRIORITY_TASK()                                      \
    do {                                                                        \
        uxTopReadyPriority = prvGetHighestReadyPriority();                      \
        listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB,                              \
                                     &( pxReadyTasksLists[ uxTopReadyPriority ] ) ); \
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
}

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

    /* 选择最高优先级的就绪任务作为第一个运行的任务 */
    taskSELECT_HIGHEST_PRIORITY_TASK();

    /* 启动调度器，成功则不返回 */
    if( xPortStartScheduler() != pdFALSE )
    {
        /* 正常不会到这里 */
    }
}

/*-----------------------------------------------------------*/

void vTaskDelay( const TickType_t xTicksToDelay )
{
    TCB_t *pxTCB = pxCurrentTCB;

    /* 记录需要阻塞的 tick 数，下次 SysTick 开始逐个递减 */
    pxTCB->xTicksToDelay = xTicksToDelay;

    /* 任务进入延时，先把它的优先级从就绪位图中清掉 */
    taskRESET_READY_PRIORITY( pxTCB->uxPriority );

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

                /* 延时到期，把任务重新标记为就绪 */
                if( pxTCB->xTicksToDelay == 0 )
                {
                    taskRECORD_READY_PRIORITY( pxTCB->uxPriority );
                }
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
    /* 直接选择优先级最高的就绪任务，更新 pxCurrentTCB */
    taskSELECT_HIGHEST_PRIORITY_TASK();
}

/*-----------------------------------------------------------*/
