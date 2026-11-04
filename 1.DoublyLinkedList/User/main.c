#include "FreeRTOS.h"
#include "list.h"

struct xLIST      List_Test;

struct xLIST_ITEM List_Item1;
struct xLIST_ITEM List_Item2;
struct xLIST_ITEM List_Item3;

int main(void)
{
    vListInitialise( &List_Test ); // 创建根节点

    vListInitialiseItem( &List_Item1 ); // 创建节点1
    List_Item1.xItemValue = 1; // 给出节点顺序

    vListInitialiseItem( &List_Item2 );
    List_Item2.xItemValue = 2;

    vListInitialiseItem( &List_Item3 );
    List_Item3.xItemValue = 3;

    vListInsert( &List_Test, &List_Item2 ); // 插入节点
    vListInsert( &List_Test, &List_Item1 );
    vListInsert( &List_Test, &List_Item3 );

    for (;;)
    {
    }
}
