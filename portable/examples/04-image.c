/* 04-image.c - 图片读写 + 向量数学
 *
 * 编译：
 *   scripts\tcc.cmd examples\04-image.c -With stb_image,stb_image_write -Run
 */
#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#ifndef STBI_NO_SIMD
#define STBI_NO_SIMD          /* TCC 没有任何 SIMD intrinsic 头 */
#endif
#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif

#include "tccwin.h"
#include "tccmath.h"          /* linmath 用到 float 数学函数 */
#include "stb_image.h"
#include "stb_image_write.h"
#include "linmath.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);    const int W = 64, H = 48;

    /* --- 生成一张测试图：渐变 + 对角红线 --- */
    unsigned char *img = (unsigned char *)malloc(W * H * 3);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = (y * W + x) * 3;
            img[i+0] = (unsigned char)(x * 255 / W);
            img[i+1] = (unsigned char)(y * 255 / H);
            img[i+2] = 128;
        }

    /* --- 写出 PNG --- */
    if (!stbi_write_png("tcwin_out.png", W, H, 3, img, W * 3)) {
        printf("写 PNG 失败\n"); return 1;
    }
    printf("写出 tcwin_out.png\n");

    /* --- 读回来逐像素比对 --- */
    int w2, h2, comp;
    unsigned char *back = stbi_load("tcwin_out.png", &w2, &h2, &comp, 3);
    if (!back) { printf("读 PNG 失败: %s\n", stbi_failure_reason()); return 1; }
    printf("读回: %dx%d %d 通道\n", w2, h2, comp);

    int diff = 0;
    for (int i = 0; i < W * H * 3; i++) if (img[i] != back[i]) diff++;
    printf("像素差异字节数: %d %s\n", diff, diff == 0 ? "(完全一致)" : "(有损!)");
    stbi_image_free(back);

    /* --- 各格式往返 --- */
    /* 各写各的，避免用不同签名的函数指针放进同一张表（TCC 会警告） */
    printf("写 bmp  : %s\n", stbi_write_bmp("tcwin_out.bmp", W, H, 3, img) ? "成功" : "失败");
    printf("写 tga  : %s\n", stbi_write_tga("tcwin_out.tga", W, H, 3, img) ? "成功" : "失败");
    printf("写 jpg  : %s\n", stbi_write_jpg("tcwin_out.jpg", W, H, 3, img, 90) ? "成功" : "失败");

    /* --- linmath：向量与矩阵 --- */
    vec3 a = {3.0f, 4.0f, 0.0f};
    printf("\n|a| = %.4f (expect 5)\n", (double)vec3_len(a));

    vec3 n;
    vec3_norm(n, a);
    printf("归一化 = (%.4f, %.4f, %.4f), 长度 %.6f\n",
           (double)n[0], (double)n[1], (double)n[2], (double)vec3_len(n));

    vec3 b = {1.0f, 0.0f, 0.0f}, c = {0.0f, 1.0f, 0.0f}, d;
    vec3_mul_cross(d, b, c);
    printf("x 叉乘 y = (%.1f, %.1f, %.1f) (expect 0,0,1)\n",
           (double)d[0], (double)d[1], (double)d[2]);

    mat4x4 m, inv, prod;
    mat4x4_identity(m);
    mat4x4_translate_in_place(m, 1.0f, 2.0f, 3.0f);
    printf("平移矩阵 T(1,2,3) 第三列 = (%.1f, %.1f, %.1f)\n",
           (double)m[3][0], (double)m[3][1], (double)m[3][2]);

    if (mat4x4_invert(inv, m)) {
        mat4x4_mul(prod, m, inv);
        /* 乘积应为单位矩阵 */
        float err = 0.0f;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                err += (float)fabs((double)(prod[i][j] - (i == j ? 1.0f : 0.0f)));
        printf("M * M^-1 与单位阵总误差 = %.8f %s\n", (double)err,
               err < 1e-5f ? "(正确)" : "(异常)");
    } else {
        printf("矩阵不可逆（异常）\n");
    }

    free(img);
    printf("\n04-image: OK\n");
    return 0;
}