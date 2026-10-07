#include "tccwin.h"
#include "toml.h"
#include <stdio.h>
int main(void){
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);  FILE *w=fopen("v.toml","wb"); fputs("a = 7\n",w); fclose(w);
  char e[128]; FILE *r=fopen("v.toml","r");
  toml_table_t *t=toml_parse_file(r,e,sizeof e); fclose(r);
  int v = t ? (int)toml_int_in(t,"a").u.i : -1;
  toml_free(t); remove("v.toml");
  printf("toml %d\n", v); return v==7?0:1;
}