#include "tccwin.h"
#include "stb_ds.h"
#include <stdio.h>
int main(void){
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);  int *a=NULL; for(int i=1;i<=4;i++) arrput(a,i*i);
  int n=(int)arrlen(a), last=a[n-1]; arrfree(a);
  printf("ds %d %d\n", n, last); return (n==4&&last==16)?0:1;
}