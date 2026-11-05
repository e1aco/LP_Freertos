#include "FreeRTOS.h"
#include "task.h"

volatile portCHAR flag1; // 变量（volatile：防止编译器优化掉赋值）
volatile portCHAR flag2;
extern List_t pxReadyTasksLists[configMAX_PRIORITIES]; // 任务就绪列表

TaskHandle_t Task1_Handle; // 任务1的TCB指针
#define TASK1_STACK_SIZE	128
StackType_t Task1Stack[TASK1_STACK_SIZE];
TCB_t Task1TCB; // 任务1的TCB

TaskHandle_t Task2_Handle; // 任务1的TCB指针
#define TASK2_STACK_SIZE	128
StackType_t Task2Stack[TASK2_STACK_SIZE];
TCB_t Task2TCB; // 任务2的TCB

void delay (uint32_t count);
void Task1_Entry( void *p_arg ); // 任务函数
void Task2_Entry( void *p_arg );

int main(void)
{
    prvInitialiseTaskLists(); // 就绪列表初始化
    /* 创建任务 */
    Task1_Handle =
    xTaskCreateStatic( (TaskFunction_t)Task1_Entry,  /* 任务入口 */
                       (char *)"Task1",              /* 任务名称，字符串形式 */
                       (uint32_t)TASK1_STACK_SIZE ,  /* 任务栈大小，单位为字 */
                       (void *) NULL,                /* 任务形参 */
                       (StackType_t *)Task1Stack,    /* 任务栈起始地址 */
                       (TCB_t *)&Task1TCB );         /* 任务控制块 */
    /* 将任务添加到就绪列表1 */
    vListInsertEnd( &( pxReadyTasksLists[1] ), &( ((TCB_t *)(&Task1TCB))->xStateListItem ) );
    Task2_Handle =
    xTaskCreateStatic( (TaskFunction_t)Task2_Entry,  /* 任务入口 */
                       (char *)"Task2",              /* 任务名称，字符串形式 */
                       (uint32_t)TASK2_STACK_SIZE ,  /* 任务栈大小，单位为字 */
                       (void *) NULL,                /* 任务形参 */
                       (StackType_t *)Task2Stack,    /* 任务栈起始地址 */
                       (TCB_t *)&Task2TCB );         /* 任务控制块 */
     /* 将任务添加到就绪列表2 */
     vListInsertEnd( &( pxReadyTasksLists[2] ), &( ((TCB_t *)(&Task2TCB))->xStateListItem ) );

     /* 启动调度器，开始多任务调度，启动成功则不返回 */
     vTaskStartScheduler();

     for (;;)
     {
         /* 系统启动成功则不会到达这里 */
     }
 }

 /* 软件延时 */
 void delay (volatile uint32_t count)
 {
     for (; count!=0; count--);
 }
 /* 任务1 */
 void Task1_Entry( void *p_arg )
 {
     for ( ;; )
     {
         flag1 = 1;
         delay( 100 );
         flag1 = 0;
         delay( 100 );

     /* 任务切换，这里是手动切换 */
         taskYIELD();
     }
 }

 /* 任务2 */
 void Task2_Entry( void *p_arg )
 {
     for ( ;; )
     {
         flag2 = 1;
         delay( 100 );
         flag2 = 0;
         delay( 100 );

     /* 任务切换，这里是手动切换 */
         taskYIELD();
     }
 }