#include "tccwin.h"
#include "cJSON.h"
#include <stdio.h>
int main(void){
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);  cJSON *j=cJSON_Parse("{\"a\":[1,2]}");
  int n = j ? cJSON_GetArraySize(cJSON_GetObjectItem(j,"a")) : -1;
  cJSON_Delete(j); printf("json %d\n", n); return n==2?0:1;
}