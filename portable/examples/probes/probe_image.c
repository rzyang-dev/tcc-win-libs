#include "tccwin.h"
#include "tccmath.h"
/* 实现宏由 scripts\tcc.cmd -With 提供；这里加守卫以便源码也能独立编译 */
#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#ifndef STBI_NO_SIMD
#define STBI_NO_SIMD          /* TCC 没有任何 SIMD intrinsic 头 */
#endif
#include "stb_image.h"
#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
#include "stb_image_write.h"
#include <stdio.h>

int main(void){
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);  unsigned char img[16*16*3];
  for(int i=0;i<16*16*3;i++) img[i]=(unsigned char)i;
  if(!stbi_write_png("v.png",16,16,3,img,16*3)) { printf("write failed\n"); return 1; }
  int w=0,h=0,c=0;
  unsigned char *b=stbi_load("v.png",&w,&h,&c,3);
  if(!b) { printf("load failed: %s\n", stbi_failure_reason()); return 1; }
  int ok = (w==16&&h==16);
  stbi_image_free(b); remove("v.png");
  printf("image %dx%d %s\n", w, h, ok?"ok":"FAIL");
  return ok?0:1;
}