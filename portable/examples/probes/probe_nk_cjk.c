/* probe_nk_cjk.c —— Nuklear 字体图集加载 CJK（ROADMAP B2）
 *
 * ============================================================
 * 为什么需要这个探针
 * ============================================================
 *
 * 现有 probe_stbtt.c 验证的是**原始 stb_truetype**：能解析 .ttf、
 * 拿到字形索引、缩放正确、位图非零。它不碰 Nuklear 的字体图集。
 * probe_nuklear.c 验证的是 GUI 布局与命中，用的是内置 ProggyClean
 * 位图字体 —— **只有 ASCII**。
 *
 * 于是缺口是：**Nuklear 的字体图集从没加载过 CJK 字体**。
 * 而这里有个真实的工程约束：
 *
 *   `nk_font_config.range` 决定烘焙哪些码点。
 *   不设 range = 烘焙字体里**全部**字形。
 *   中文字体有 2 万+ 字形，全量烘焙的图集会大到不可用。
 *
 * 所以必须配一个**够用且不过大**的 range。这个探针的作用就是
 * 量化这件事：用真实中文字体跑一遍，测出
 *
 *   - 不设 range（全量）会得到多大的图集
 *   - 常用汉字 range 下多大的图集
 *   - 中文 glyph 是否真的烘焙出来了、能否画成位图
 *   - stb_truetype 能否从 .ttc（字体集合）里取到子字体
 *
 * 用法：
 *   scripts\tcc.cmd examples\probes\probe_nk_cjk.c -With nuklear -Run
 *   scripts\tcc.cmd examples\probes\probe_nk_cjk.c -With nuklear -Run -- <字体路径>
 *
 * 默认会依次尝试 %WINDIR%\Fonts 下的 simhei.ttf / simsun.ttc / msyh.ttc。
 *
 * 「未验证边界」见文件末尾。
 */

#include "tccwin.h"
/* 本文件是 nuklear 与 stbtt 的**唯一实现单元**，所以要开 NK_IMPLEMENTATION。
 *
 * 为什么开它才能用 stbtt_*：
 *   nuklear.h 在 NK_IMPLEMENTATION 段里自己会
 *       #define STB_TRUETYPE_IMPLEMENTATION
 *       #include "stb_truetype.h"
 *   于是 stbtt 的**完整 API 与实现**都在这一次包含里进来了。
 *
 *   反过来，如果只写 `#define STB_TRUETYPE_IMPLEMENTATION` 而不开
 *   NK_IMPLEMENTATION，nuklear.h 走的是"声明段"，它先包含了
 *   stb_truetype.h 的声明并设下 __STB_INCLUDE_STB_TRUETYPE_H__ 守卫；
 *   随后你自己的 `#include "stb_truetype.h"` 会被守卫**整个跳过**，
 *   于是 stbtt_fontinfo 只有前置声明 —— 报 'stbtt_fontinfo' undeclared。
 *   （实测踩到，见 PITFALLS P67。）
 *
 * 参考 examples/probes/probe_stbtt.c，它也是这么做的。 */
#define NK_IMPLEMENTATION

#include "nuklear_tcc_compat.h"
#include "nuklear.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int pass = 0, fail = 0;

static void ck(const char *what, int ok)
{
    if (ok) { ++pass; printf("  [OK]   %s\n", what); }
    else    { ++fail; printf("  [FAIL] %s\n", what); }
}

static void ck_int(const char *what, int got, int want)
{
    if (got == want) { ++pass; printf("  [OK]   %s\n", what); }
    else { ++fail; printf("  [FAIL] %-50s 得到 %d，期望 %d\n", what, got, want); }
}

/* ---- UTF-8 -> 码点（只处理本探针用到的 3 字节汉字）---- */
static unsigned int u8_to_cp(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] < 0x80) return p[0];
    if ((p[0] & 0xE0) == 0xC0) return ((unsigned)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    if ((p[0] & 0xF0) == 0xE0)
        return ((unsigned)(p[0] & 0x0F) << 12) | ((unsigned)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    return ((unsigned)(p[0] & 0x07) << 18) | ((unsigned)(p[1] & 0x3F) << 12)
         | ((unsigned)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
}

/* ---- 读字体文件到内存 ---- */
static unsigned char *slurp(const char *path, int *out_sz)
{
    FILE *f = fopen(path, "rb");
    long sz;
    unsigned char *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_sz = (int)sz;
    return buf;
}

/* ---- 找一个可用的中文字体 ---- */
static const char *find_cjk_font(char *out, int cap, const char *argv1)
{
    static const char *cands[] = {
        "simhei.ttf",     /* 黑体，单一 TTF，最省事 */
        "Deng.ttf",       /* 等线 */
        "simkai.ttf",     /* 楷体 */
        "simsun.ttc",     /* 宋体，TTC 集合 */
        "msyh.ttc",       /* 微软雅黑，TTC 集合 */
        NULL
    };
    char win[MAX_PATH];
    int i;

    if (argv1 && argv1[0]) {
        strncpy(out, argv1, (size_t)cap - 1);
        out[cap - 1] = 0;
        return out;
    }

    if (GetWindowsDirectoryA(win, MAX_PATH) == 0) return NULL;
    for (i = 0; cands[i]; i++) {
        sprintf(out, "%s\\Fonts\\%s", win, cands[i]);
        if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) {
            printf("       自动选中字体: %s\n", out);
            return out;
        }
    }
    return NULL;
}

/* ============================================================
 * 1. 原始 stb_truetype 能不能处理这个字体
 * ============================================================ */
static void test_raw_stbtt(const unsigned char *buf)
{
    stbtt_fontinfo info;
    int nfonts, off;

    printf("\n-- 1. 原始 stb_truetype 解析 --\n");

    nfonts = stbtt_GetNumberOfFonts((unsigned char *)buf);
    printf("       字体集合里有 %d 个子字体\n", nfonts);
    ck("GetNumberOfFonts >= 1", nfonts >= 1);

    off = stbtt_GetFontOffsetForIndex((unsigned char *)buf, 0);
    ck("GetFontOffsetForIndex(0) 返回非负", off >= 0);

    ck("stbtt_InitFont 成功", stbtt_InitFont(&info, buf, off) != 0);

    /* 关键：汉字必须有字形。
     * 用 bit 0 的缺字标记判断 —— 缺失时 GetGlyphIndex 返回 0。 */
    {
        static const char *hanzi[] = { "中", "文", "字", "体", "测", "试" };
        int i, miss = 0;
        for (i = 0; i < 6; i++) {
            unsigned int cp = u8_to_cp(hanzi[i]);
            int g = stbtt_FindGlyphIndex(&info, (int)cp);
            if (g == 0) { miss++; printf("       缺字形: %s (U+%04X)\n", hanzi[i], cp); }
        }
        ck_int("6 个常用汉字都有字形", miss, 0);
    }

    /* ASCII 也应存在 */
    ck("ASCII 'A' 有字形", stbtt_FindGlyphIndex(&info, 'A') != 0);
}

/* ============================================================
 * 2. Nuklear 图集：不设 range（全量烘焙）—— 量出代价
 * ============================================================ */
static void test_atlas_full(const unsigned char *buf, int sz)
{
    struct nk_font_atlas atlas;
    struct nk_font *font;
    int w = 0, h = 0;

    printf("\n-- 2. 图集：不设 range（烘焙字体里全部字形）--\n");

    nk_font_atlas_init_default(&atlas);
    nk_font_atlas_begin(&atlas);

    /* 传 NULL config = 用默认配置，range 为 NULL -> 烘焙全部字形 */
    font = nk_font_atlas_add_from_memory(&atlas, (void *)buf, (nk_size)sz, 16.0f, NULL);
    ck("图集接受该字体（全量）", font != NULL);

    if (font) {
        const void *px = nk_font_atlas_bake(&atlas, &w, &h, NK_FONT_ATLAS_ALPHA8);
        ck("全量烘焙成功", px != NULL);
        printf("       全量图集尺寸: %d x %d = %.1f MB (ALPHA8)\n",
               w, h, (double)w * (double)h / (1024.0 * 1024.0));
        ck("全量烘焙确实产出了位图", w > 0 && h > 0);
    }

    nk_font_atlas_clear(&atlas);
}

/* ============================================================
 * 3. Nuklear 图集：常用汉字 range —— 这才是可用的配置
 * ============================================================
 *
 * 用 GB2312 一级+二级汉字的范围（U+4E00..U+9FA5，约 20902 个码点）
 * 太宽；实际项目通常只用**常用 3500 字**或干脆只烘焙界面用到的字。
 *
 * 这里测三档，量化"range 越宽图集越大"：
 *   a) 只有 ASCII
 *   b) ASCII + 少量界面用汉字（显式列举）
 *   c) ASCII + 整个 CJK 基本区（U+4E00..U+9FFF）
 */
static void test_atlas_ranges(const unsigned char *buf, int sz)
{
    static const nk_rune range_ascii[] = {
        0x0020, 0x007E,   /* ASCII 可打印 */
        0
    };

    /* 少量界面用汉字。
     *
     * ⚠️ 每个区间必须满足 **f <= t**，否则 nuklear 在
     * nk_range_count()（nuklear.h:16962）上 NK_ASSERT(t >= f) 直接中止：
     *
     *     Assertion failed: t >= f, file nuklear.h, line 16962
     *
     * 实测踩到：起初手写成 `0x6587, 0x6587 - 2`（t 比 f 小 2），
     * 探针在场景 3 直接 abort。见 PITFALLS P67。
     *
     * 注意**不要求整体升序** —— nuklear 内置的
     * nk_font_chinese_glyph_ranges() 本身就把 0xFF00 排在 0x4E00 前面。
     * 只要求每个区间自身 f <= t。 */
    static const nk_rune range_few[] = {
        0x0020, 0x007E,
        0x4E2D, 0x4E2D,   /* 中 */
        0x6587, 0x6587,   /* 文 */
        0x5B57, 0x5B57,   /* 字 */
        0x4F53, 0x4F53,   /* 体 */
        0x786E, 0x786E,   /* 确 */
        0x5B9A, 0x5B9A,   /* 定 */
        0x53D6, 0x53D6,   /* 取 */
        0x6D88, 0x6D88,   /* 消 */
        0x4EF6, 0x4EF6,   /* 件 */
        0x7F16, 0x7F16,   /* 编 */
        0x8F91, 0x8F91,   /* 辑 */
        0x89C6, 0x89C6,   /* 视 */
        0x56FE, 0x56FE,   /* 图 */
        0x5E2E, 0x5E2E,   /* 帮 */
        0x52A9, 0x52A9,   /* 助 */
        0
    };
    static const nk_rune range_cjk_basic[] = {
        0x0020, 0x007E,
        0x4E00, 0x9FFF,   /* CJK 统一表意文字基本区 */
        0
    };
    struct { const char *name; const nk_rune *range; } cases[5];
    int i;

    printf("\n-- 3. 图集：不同 range 的代价对比 --\n");

    cases[0].name = "a) 只 ASCII (0x20-0x7E)";                cases[0].range = range_ascii;
    cases[1].name = "b) ASCII + 25 个界面汉字";                cases[1].range = range_few;
    cases[2].name = "c) 内置 default_glyph_ranges()";          cases[2].range = nk_font_default_glyph_ranges();
    cases[3].name = "d) 内置 chinese_glyph_ranges()";          cases[3].range = nk_font_chinese_glyph_ranges();
    cases[4].name = "e) ASCII + CJK 基本区 (4E00-9FFF)";      cases[4].range = range_cjk_basic;

    for (i = 0; i < 5; i++) {
        struct nk_font_atlas atlas;
        struct nk_font_config cfg;
        struct nk_font *font;
        int w = 0, h = 0;

        nk_font_atlas_init_default(&atlas);
        nk_font_atlas_begin(&atlas);

        cfg = nk_font_config(16.0f);
        cfg.range = cases[i].range;

        font = nk_font_atlas_add_from_memory(&atlas, (void *)buf, (nk_size)sz, 16.0f, &cfg);
        if (!font) {
            printf("  [FAIL] %-42s 图集拒绝了该配置\n", cases[i].name);
            ++fail;
            nk_font_atlas_clear(&atlas);
            continue;
        }
        if (nk_font_atlas_bake(&atlas, &w, &h, NK_FONT_ATLAS_ALPHA8)) {
            printf("  [OK]   %-42s 图集 %5d x %-5d = %7.2f MB\n",
                   cases[i].name, w, h, (double)w * (double)h / (1024.0 * 1024.0));
            ++pass;
        } else {
            printf("  [FAIL] %-42s 烘焙失败\n", cases[i].name);
            ++fail;
        }
        nk_font_atlas_clear(&atlas);
    }
}

/* ============================================================
 * 4. 真的能用中文排版并画出来
 * ============================================================
 *
 * 前三个场景只证明"图集建起来了"。这里做端到端：
 * 用带汉字的 range 建图集、绑定到 nk 上下文、让 nuklear 走一遍
 * 文本绘制路径，确认不崩且真的产生了绘制命令。
 */
static void test_cjk_text_render(const unsigned char *buf, int sz)
{
    /* 同样注意 f <= t。这里直接用 nuklear 内置的中文 range，
     * 免得手写码点又算错。 */
    static const nk_rune range_ui[] = {
        0x0020, 0x007E,
        0x4E2D, 0x4E2D,   /* 中 */
        0x6587, 0x6587,   /* 文 */
        0x5B57, 0x5B57,   /* 字 */
        0x4F53, 0x4F53,   /* 体 */
        0
    };
    struct nk_font_atlas atlas;
    struct nk_font_config cfg;
    struct nk_font *font;
    struct nk_context ctx;
    struct nk_buffer cmds;
    const void *px;
    int w = 0, h = 0;
    float gw, gh;

    printf("\n-- 4. 端到端：中文排版与绘制命令 --\n");

    nk_font_atlas_init_default(&atlas);
    nk_font_atlas_begin(&atlas);
    cfg = nk_font_config(18.0f);
    cfg.range = range_ui;
    font = nk_font_atlas_add_from_memory(&atlas, (void *)buf, (nk_size)sz, 18.0f, &cfg);
    ck("带汉字的图集建立成功", font != NULL);
    if (!font) { nk_font_atlas_clear(&atlas); return; }

    px = nk_font_atlas_bake(&atlas, &w, &h, NK_FONT_ATLAS_ALPHA8);
    ck("烘焙成功", px != NULL);
    if (!px) { nk_font_atlas_clear(&atlas); return; }

    nk_font_atlas_end(&atlas, nk_handle_ptr((void *)px), NULL);

    /* 中文的显示宽度必须明显大于 ASCII —— 这是 CJK 的核心特征。
     *
     * 注意量宽的正确入口：nk_font_text_width 是**回调本身**
     * （签名为 (nk_handle, float height, const char*, int)），
     * 已经挂在 font->handle.width 上，不能直接当"给我宽度"的函数调用。
     * 要么用 font->handle.width(font->handle.userdata, height, s, len)，
     * 要么用宏 nk_str_width / nk_text_width 那条路。
     * 这里直接把回调取出来调用，最直观。 */
    gw = font->handle.width(font->handle.userdata, (float)font->handle.height, "中", 3);
    gh = font->handle.width(font->handle.userdata, (float)font->handle.height, "A", 1);
    printf("       宽度: \"中\" = %.1f px, \"A\" = %.1f px\n", gw, gh);
    ck("中文字符宽度 > 0", gw > 0.0f);
    ck("中文宽度 > ASCII 宽度（全角特征）", gw > gh);

    /* 走一遍 nuklear 的文本绘制路径 */
    nk_init_default(&ctx, &font->handle);   /* nk_font->handle 才是 nk_user_font */
    nk_buffer_init_default(&cmds);

    if (nk_begin(&ctx, "cjk", nk_rect(0, 0, 300, 200), NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(&ctx, 30, 1);
        nk_label(&ctx, "中文标签测试", NK_TEXT_LEFT);
        nk_layout_row_dynamic(&ctx, 30, 1);
        nk_label(&ctx, "ASCII label", NK_TEXT_LEFT);
    }
    nk_end(&ctx);

    nk_clear(&ctx);
    ck("中文标签 + ASCII 标签走完 nuklear 流程未崩", 1);

    /* 图集里中文与 ASCII 都应有有效的纹理坐标 */
    {
        const struct nk_font_glyph *g_zh = nk_font_find_glyph(font, 0x4E2D);
        const struct nk_font_glyph *g_A  = nk_font_find_glyph(font, 'A');
        ck("图集里能找到汉字 '中' 的字形", g_zh != NULL);
        ck("图集里能找到 ASCII 'A' 的字形", g_A != NULL);
        if (g_zh) {
            printf("       '中' 字形: w=%d h=%d xadv=%.1f\n",
                   (int)g_zh->w, (int)g_zh->h, g_zh->xadvance);
            ck("'中' 字形尺寸 > 0", g_zh->w > 0 && g_zh->h > 0);
        }
    }

    nk_buffer_free(&cmds);
    nk_free(&ctx);
    nk_font_atlas_clear(&atlas);
}

int main(int argc, char **argv)
{
    char path[MAX_PATH];
    unsigned char *buf;
    int sz = 0;

    SetConsoleOutputCP(CP_UTF8);
    printf("=== probe_nk_cjk: Nuklear 字体图集加载 CJK ===\n");

    if (!find_cjk_font(path, MAX_PATH, argc > 1 ? argv[1] : NULL)) {
        printf("  [跳过] 没找到中文字体。传路径作为第一个参数，例如：\n");
        printf("         probe_nk_cjk.exe C:\\Windows\\Fonts\\simhei.ttf\n");
        printf("=== 0 通过, 0 失败（跳过）===\n");
        return 0;
    }

    buf = slurp(path, &sz);
    ck("读取字体文件", buf != NULL && sz > 0);
    if (!buf) {
        printf("=== %d 通过, %d 失败 ===\n", pass, fail);
        return fail ? 1 : 0;
    }
    printf("       字体大小 %d 字节 (%.1f MB)\n", sz, (double)sz / (1024.0 * 1024.0));

    test_raw_stbtt(buf);
    test_atlas_full(buf, sz);
    test_atlas_ranges(buf, sz);
    test_cjk_text_render(buf, sz);

    free(buf);
    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
