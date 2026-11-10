/*
 * 最小化 FreeRTOS 内核（对应野火《FreeRTOS内核实现与应用开发实战指南》
 * 第7章“任务的定义与任务切换”）。
 *
 * 说明：
 *   官方 V11 的 task.h 里 xTaskCreateStatic() 是带优先级参数的 7 参数版本，
 *   与本章手写的最小内核不兼容。为了不破坏官方源码树，这里提供同名 task.h，
 *   并把 ..\User 放在 Keil 包含路径的最前面，让它优先生效。
 */

#ifndef MINI_TASK_H
#define MINI_TASK_H

#include "FreeRTOS.h"
#include "list.h"

/* 任务控制块 */
typedef struct tskTaskControlBlock
{
    volatile StackType_t *pxTopOfStack;              /* 栈顶指针，必须是第一个成员 */
    ListItem_t            xStateListItem;            /* 任务节点（挂到就绪列表的“钩子”） */
    StackType_t          *pxStack;                   /* 任务栈起始地址 */
    char                  pcTaskName[ configMAX_TASK_NAME_LEN ]; /* 任务名称 */
    TickType_t            xTicksToDelay;             /* 阻塞延时的剩余 tick 数，0 表示未延时 */
    UBaseType_t           uxPriority;                /* 任务优先级，数值越大优先级越高 */
} tskTCB;

typedef tskTCB TCB_t;

/* 任务句柄 */
typedef TCB_t * TaskHandle_t;

/* 空闲任务优先级（最低） */
#define tskIDLE_PRIORITY    ( ( UBaseType_t ) 0U )

/* 就绪列表，定义在 task.c 中 */
extern List_t pxReadyTasksLists[ configMAX_PRIORITIES ];

/* 当前运行（或即将运行）的任务控制块，定义在 task.c 中 */
extern TCB_t * volatile pxCurrentTCB;

/* 任务相关接口 */
void prvInitialiseTaskLists( void );

TaskHandle_t xTaskCreateStatic( TaskFunction_t pxTaskCode,
                                const char * const pcName,
                                const uint32_t ulStackDepth,
                                void * const pvParameters,
                                StackType_t * const puxStackBuffer,
                                TCB_t * const pxTaskBuffer );

void vTaskStartScheduler( void );
void vTaskSwitchContext( void );

/* 阻塞延时（第 9 章） */
void vTaskDelay( const TickType_t xTicksToDelay );

/* SysTick 中断里更新系统时基并递减各任务的延时计数 */
void xTaskIncrementTick( void );

/* 手动触发一次任务切换（本章通过它轮流切换两个任务） */
#define taskYIELD()    portYIELD()

/* 移植层接口（port.c） */
StackType_t * pxPortInitialiseStack( StackType_t * pxTopOfStack,
                                     TaskFunction_t pxCode,
                                     void * pvParameters );

BaseType_t xPortStartScheduler( void );

#endif /* MINI_TASK_H */
