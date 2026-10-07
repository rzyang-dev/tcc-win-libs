#include "tccwin.h"
#include "xxhash.h"
#include "lonesha256.h"
#include <stdio.h>
int main(void){
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);  const char *s="abc";
  unsigned h=XXH32(s,3,0);
  unsigned char d[32]; lonesha256(d,(const unsigned char*)s,3);
  printf("hash %08X %02x%02x\n", h, d[0], d[1]);
  return (d[0]==0xba && d[1]==0x78)?0:1;   /* SHA256("abc") 以 ba78 开头 */
}