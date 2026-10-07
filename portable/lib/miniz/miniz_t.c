#include "miniz.h"
#include <stdio.h>
int main(void){unsigned char in[64];for(int i=0;i<64;i++)in[i]=(unsigned char)i;unsigned char out[256];mz_ulong cl=sizeof out;int s=mz_compress(out,&cl,in,64,MZ_DEFAULT_COMPRESSION);printf("miniz ok: 64 -> %lu (status %d)\n",(unsigned long)cl,s);return s==MZ_OK?0:1;}
