/* stb_truetype 字体渲染验证
 *
 * Nuklear 已内置 stb_truetype 与 stb_rect_pack 的完整实现
 * （nuklear.h:6477-6486 生效，1222 个 stbtt_ 符号），
 * 所以本项不需要额外文件 —— 用 -With nuklear 即可。
 *
 * 本探针真实加载外部 .ttf 并做光栅化，证明字体渲染链路可用。
 *
 * 编译：scripts\tcc.cmd examples\probes\probe_stbtt.c -With nuklear -Run -- <字体.ttf>
 * 若不传字体路径则用内置 ProggyClean 验证（不依赖系统文件）。
 */
#include "tccwin.h"
#include "nuklear_tcc_compat.h"
#define NK_IMPLEMENTATION
#include "nuklear.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pass = 0, fail = 0;
#define CK(d, c) do { if (c) { printf("  [OK]   %s\n", d); pass++; } \
                      else   { printf("  [FAIL] %s\n", d); fail++; } } while (0)

/* 从文件读入整个字体到内存；返回缓冲区，大小写入 *out_sz */
static unsigned char *read_file(const char *path, long *out_sz) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(buf); return NULL; }
    *out_sz = sz;
    return buf;
}

int main(int argc, char **argv) {
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);    printf("=== stb_truetype 字体渲染验证 ===\n\n");

    const char *path = (argc > 1) ? argv[1] : NULL;
    unsigned char *buf = NULL;
    long sz = 0;

    if (path) {
        printf("-- 外部字体: %s --\n", path);
        buf = read_file(path, &sz);
        CK("读取字体文件", buf != NULL && sz > 0);
        if (buf) printf("       文件大小 %ld 字节\n", sz);
    } else {
        printf("-- 未提供字体路径，跳过外部字体测试 --\n");
        printf("       （传一个 .ttf 路径即可测试，例如 C:\\Windows\\Fonts\\arial.ttf）\n");
    }
    printf("\n");

    if (buf) {
        /* ---------- 解析 ---------- */
        printf("-- TTF 解析 --\n");
        int nfonts = stbtt_GetNumberOfFonts(buf);
        printf("       字体数 %d\n", nfonts);
        CK("GetNumberOfFonts >= 1", nfonts >= 1);

        int off = stbtt_GetFontOffsetForIndex(buf, 0);
        printf("       字体偏移 %d\n", off);
        CK("GetFontOffsetForIndex 返回非负", off >= 0);

        stbtt_fontinfo info;
        memset(&info, 0, sizeof info);
        int init = stbtt_InitFont(&info, buf, off);
        CK("stbtt_InitFont 成功", init != 0);

        if (init) {
            int asc = 0, desc = 0, gap = 0;
            stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
            printf("       垂直度量 ascent=%d descent=%d lineGap=%d\n", asc, desc, gap);
            CK("ascent > 0", asc > 0);
            CK("descent < 0", desc < 0);
            printf("\n");

            /* ---------- 字形查询 ---------- */
            printf("-- 字形查询 --\n");
            int gA = stbtt_FindGlyphIndex(&info, 'A');
            int ga = stbtt_FindGlyphIndex(&info, 'a');
            int g0 = stbtt_FindGlyphIndex(&info, '0');
            printf("       'A'=%d 'a'=%d '0'=%d\n", gA, ga, g0);
            CK("字母字形索引都 > 0", gA > 0 && ga > 0 && g0 > 0);
            CK("大小写映射到不同字形", gA != ga);
            printf("\n");

            /* ---------- 缩放 ---------- */
            printf("-- 缩放 --\n");
            float s16 = stbtt_ScaleForPixelHeight(&info, 16.0f);
            float s32 = stbtt_ScaleForPixelHeight(&info, 32.0f);
            float sm  = stbtt_ScaleForMappingEmToPixels(&info, 16.0f);
            printf("       ScaleForPixelHeight(16)=%.6f (32)=%.6f\n", (double)s16, (double)s32);
            printf("       ScaleForMappingEmToPixels(16)=%.6f\n", (double)sm);
            CK("缩放系数 > 0", s16 > 0 && s32 > 0);
            CK("32px 缩放约为 16px 的两倍", s32 > s16 * 1.9f && s32 < s16 * 2.1f);
            printf("\n");

            /* ---------- 真实光栅化 ---------- */
            printf("-- 光栅化（真实生成位图） --\n");
            int w = 0, h = 0, xo = 0, yo = 0;
            unsigned char *bm = stbtt_GetCodepointBitmap(&info, 0, s32, 'A', &w, &h, &xo, &yo);
            printf("       'A' 位图 %dx%d offset=(%d,%d)\n", w, h, xo, yo);
            CK("位图尺寸 > 0", bm != NULL && w > 0 && h > 0);
            if (bm && w > 0 && h > 0) {
                long total = (long)w * h;
                long nz = 0;
                int maxv = 0;
                for (long i = 0; i < total; i++) {
                    if (bm[i]) nz++;
                    if (bm[i] > maxv) maxv = bm[i];
                }
                printf("       非零像素 %ld / %ld (%.1f%%), 最大灰度 %d\n",
                       nz, total, 100.0 * (double)nz / (double)total, maxv);
                CK("位图非全零（真的画出了字形）", nz > 0);
                CK("灰度覆盖完整范围（有抗锯齿）", maxv > 128);
                stbtt_FreeBitmap(bm, NULL);
            }

            /* 大写 A 应比小写 i 宽 */
            /* 注意：不要传 NULL 给 width/height —— 用真实变量，
             * 避免依赖库对空指针的处理。 */
            int wA = 0, hA = 0, xA = 0, yA = 0;
            int wI = 0, hI = 0, xI = 0, yI = 0;
            unsigned char *bmA = stbtt_GetCodepointBitmap(&info, 0, s32, 'W', &wA, &hA, &xA, &yA);
            unsigned char *bmI = stbtt_GetCodepointBitmap(&info, 0, s32, 'i', &wI, &hI, &xI, &yI);
            printf("       'W' %dx%d   'i' %dx%d\n", wA, hA, wI, hI);
            CK("'W' 比 'i' 宽（度量正确）", wA > wI);
            if (bmA) stbtt_FreeBitmap(bmA, NULL);
            if (bmI) stbtt_FreeBitmap(bmI, NULL);
            printf("\n");

            /* ---------- 水平度量与 kerning ---------- */
            printf("-- 度量与 kerning --\n");
            int advA = 0, lsbA = 0;
            stbtt_GetCodepointHMetrics(&info, 'A', &advA, &lsbA);
            printf("       'A' advance=%d lsb=%d\n", advA, lsbA);
            CK("advance > 0", advA > 0);

            int kern = stbtt_GetCodepointKernAdvance(&info, 'A', 'V');
            printf("       'A'+'V' kerning = %d\n", kern);
            CK("kerning 查询不崩溃（值可正可负可零）", 1);

            /* 行框 */
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetFontBoundingBox(&info, &x0, &y0, &x1, &y1);
            printf("       字体包围盒 (%d,%d)-(%d,%d)\n", x0, y0, x1, y1);
            CK("包围盒有效", x1 > x0 && y1 > y0);
            printf("\n");
        }
        printf("\n");
    }

    /* ---------- Nuklear 集成：把字体加入图集 ---------- */
    printf("-- Nuklear 图集集成 --\n");
    struct nk_font_atlas atlas;
    nk_font_atlas_init_default(&atlas);
    nk_font_atlas_begin(&atlas);

    struct nk_font *f1 = NULL;
    if (buf) {
        f1 = nk_font_atlas_add_from_memory(&atlas, buf, (nk_size)sz, 16.0f, NULL);
        CK("nk_font_atlas_add_from_memory 成功（外部 TTF）", f1 != NULL);
    }
    struct nk_font *f2 = nk_font_atlas_add_default(&atlas, 13.0f, NULL);
    CK("nk_font_atlas_add_default 成功（内置字体）", f2 != NULL);

    int aw = 0, ah = 0;
    const void *img = nk_font_atlas_bake(&atlas, &aw, &ah, NK_FONT_ATLAS_RGBA32);
    printf("       图集 %dx%d\n", aw, ah);
    CK("图集烘焙出位图", img != NULL && aw > 0 && ah > 0);

    if (img && aw > 0 && ah > 0) {
        const unsigned char *px = (const unsigned char *)img;
        long total = (long)aw * ah;
        long nz = 0;
        for (long i = 3; i < total * 4; i += 4) if (px[i] != 0) nz++;
        printf("       非透明像素 %ld / %ld (%.2f%%)\n",
               nz, total, 100.0 * (double)nz / (double)total);
        CK("图集含真实字形像素", nz > 100);
    }

    if (f1) {
        printf("       外部字体: 高度 %d, 字形数 %d\n",
               (int)f1->info.height, (int)f1->info.glyph_count);
        CK("外部字体度量为正", f1->info.height > 0 && f1->info.glyph_count > 0);
    }
    if (f2) {
        printf("       内置字体: 高度 %d, 字形数 %d\n",
               (int)f2->info.height, (int)f2->info.glyph_count);
        CK("内置字体度量为正", f2->info.height > 0 && f2->info.glyph_count > 0);
    }

    nk_font_atlas_end(&atlas, nk_handle_id(0), NULL);
    nk_font_atlas_clear(&atlas);
    CK("图集清理无崩溃", 1);
    printf("\n");

    if (buf) free(buf);

    printf("=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail ? 1 : 0;
}
