#include "tccwin.h"
#include "tccmath.h"
#include "linmath.h"
#include <stdio.h>

int main(void){
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);  vec3 a = {3.0f, 4.0f, 0.0f};
  float alen = (float)vec3_len(a);

  mat4x4 m, inv, p;
  mat4x4_identity(m);
  mat4x4_translate_in_place(m, 1.0f, 2.0f, 3.0f);

  /* 注意：mat4x4_invert 返回 void，不是 int */
  mat4x4_invert(inv, m);
  mat4x4_mul(p, m, inv);

  float err = 0.0f;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      err += (float)fabs((double)(p[i][j] - (i == j ? 1.0f : 0.0f)));

  int ok = (alen > 4.99f && alen < 5.01f) && (err < 1e-3f);
  printf("linmath |a|=%.4f invErr=%.6f %s\n", (double)alen, (double)err, ok ? "ok" : "FAIL");
  return ok ? 0 : 1;
}