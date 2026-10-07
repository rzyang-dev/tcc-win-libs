#include "tccwin.h"
#include "miniz.h"
#include <stdio.h>
#include <string.h>
int main(void){
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);  char src[512]; memset(src,'A',sizeof src);
  unsigned char out[1024]; mz_ulong olen=sizeof out;
  int st=mz_compress(out,&olen,(const unsigned char*)src,sizeof src);
  printf("compress %lu %d\n",(unsigned long)olen,st);
  return (st==MZ_OK && olen<sizeof src)?0:1;
}