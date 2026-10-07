#include "tccwin.h"
#include "tinydir.h"
#include <stdio.h>
int main(void){
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);  tinydir_dir d; if(tinydir_open(&d,".")==-1) return 1;
  int n=0; while(d.has_next){ tinydir_file f; tinydir_readfile(&d,&f); if(!f.is_dir) n++; tinydir_next(&d); }
  tinydir_close(&d); printf("fs %d\n", n); return n>0?0:1;
}