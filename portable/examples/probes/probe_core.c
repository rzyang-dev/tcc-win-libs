#include "tccwin.h"
#include "tccmath.h"
#include <stdio.h>
int main(void){
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);  volatile LONG c=0; InterlockedExchangeAdd(&c,3);
  printf("%llu %ld %.4f\n",(unsigned long long)GetTickCount64(),(long)c,(double)sqrtf(2.0f));
  return (c==3)?0:1;
}