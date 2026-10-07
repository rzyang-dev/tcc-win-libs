/* 01-hello.c - 最小示例：验证工具链可用
 *
 * 编译： scripts\tcc.cmd examples\01-hello.c -Run
 */
#include <stdio.h>

int main(void) {
    printf("portable dev env OK\n");
    printf("  __TINYC__      = %d\n", __TINYC__);
    printf("  sizeof(void*)  = %d\n", (int)sizeof(void *));
    return 0;
}
