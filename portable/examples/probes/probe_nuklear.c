/* Nuklear 功能性验证：确定性输入驱动 + 几何/状态/绘制输出断言
 *
 * 为什么这样测：GUI 最难验证的不是"能否编译"，而是
 *   1. 布局是否把控件放在正确位置
 *   2. 输入是否命中正确的控件
 *   3. 状态是否按预期变化
 *   4. 是否真的产生了可渲染的绘制数据
 * 这四件事都能用「注入输入 + 断言数值」验证，比截图比对更精确、
 * 完全确定性（无时序噪声），而且不会在用户桌面留下窗口。
 *
 * 字体用 Nuklear 内置的 ProggyClean（NK_INCLUDE_DEFAULT_FONT），
 * 无需外部 .ttf，符合单文件零依赖的目标。
 *
 * 编译： scripts\tcc.cmd examples\probes\probe_nuklear.c -With nuklear -Run
 */
#include "tccwin.h"
#include "nuklear_tcc_compat.h"
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_IMPLEMENTATION
#include "nuklear.h"
#include <stdio.h>
#include <string.h>

static int pass = 0, fail = 0;
#define CK(d, c) do { if (c) { printf("  [OK]   %s\n", d); pass++; } \
                      else   { printf("  [FAIL] %s\n", d); fail++; } } while (0)

#define WIN_W 400
#define WIN_H 300
#define BTN_H 30.0f

/* 顶点格式由使用者定义 —— Nuklear 不自带 nk_draw_vertex。
 * 照文档示例：position / uv / color */
struct probe_vertex {
    float   position[2];
    float   uv[2];
    nk_byte col[4];
};

static struct nk_context ctx;
static struct nk_rect   g_win;
static int g_c1, g_c2;
static nk_flags g_state;   /* 帧内捕获 last_widget_state 用 */

/* 一个完整帧的正确顺序（三条都由 nuklear 内部状态机强制）：
 *
 *   nk_input_begin() -> 报告输入 -> nk_input_end()
 *   -> nk_begin() -> 控件 -> nk_end() -> nk_clear()
 *
 * 坑 1：输入必须在 nk_input_begin/end **之间**报告。
 *   nk_input_begin 清零 buttons[].clicked（nuklear.h:18296），
 *   而 nk_input_button 在"状态未变化"时直接 return（18358）。
 *   在 begin/end 之外调用会让 clicked 永远为 0。
 *
 * 坑 2：每帧必须 nk_clear()，否则第二帧撞
 *   "Assertion failed: win->seq != ctx->seq"（nuklear.h:20968-20975）。
 *
 * 坑 3：**不要在同一帧里先调 nk_widget(&r, &ctx) 再用 nk_button_label**。
 *   nk_widget 会推进布局（nk_panel_alloc_space），多调一次会让按钮判断
 *   到下一个槽位，表现为"按钮永远点不动"。
 *   要取几何就在独立的发现帧里取（本测试正是这么做的）。
 *
 * 坑 4：默认按钮行为是**按下即触发**（NK_BUTTON_DEFAULT，nuklear.h:25010），
 *   不是抬起才触发。
 */
#define FRAME(px, py, down, body) do {                         \
    nk_input_begin(&ctx);                                      \
    nk_input_motion(&ctx, (int)(px), (int)(py));                \
    nk_input_button(&ctx, NK_BUTTON_LEFT,                      \
                    (int)(px), (int)(py), (down));              \
    nk_input_end(&ctx);                                        \
    nk_begin(&ctx, "probe", g_win, NK_WINDOW_NO_SCROLLBAR);     \
    nk_layout_row_dynamic(&ctx, BTN_H, 1);                      \
    body;                                                        \
    nk_end(&ctx);                                               \
    nk_clear(&ctx);                                             \
} while (0)

/* 用**函数**而不是宏。
 * 原因：TCC 无法解析"do-while 宏作为另一个 do-while 宏的参数"
 * （报 ';' expected (got "nk_end")，而同一段展开在 GCC 下合法）。
 * 改成函数后完全避开这个问题。 */
static void two_buttons(void) {
    if (nk_button_label(&ctx, "BTN1")) g_c1++;
    if (nk_button_label(&ctx, "BTN2")) g_c2++;
}

/* 只渲染 BTN1 并捕获它的状态。
 *
 * 为什么要单独一个函数：
 *   1. last_widget_state 是**单个变量**，每个控件都会重置它 ——
 *      两个按钮都渲染时 BTN2 会覆盖 BTN1 的状态（踩过一次）。
 *   2. 它每帧重置，必须在帧内（nk_end 之前）读。
 *   3. 让 FRAME 的 body 保持单条语句 —— TCC 无法解析宏参数里
 *      以分号分隔的多条语句。 */
static void btn1_state(void) {
    if (nk_button_label(&ctx, "BTN1")) g_c1++;
    g_state = ctx.last_widget_state;
}

/* 只渲染 BTN2 并捕获状态（理由同 btn1_state）。 */
static void btn2_state(void) {
    if (nk_button_label(&ctx, "BTN2")) g_c2++;
    g_state = ctx.last_widget_state;
}

int main(void) {
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);    printf("=== Nuklear 功能性验证 ===\n\n");

    /* ---------- 类型宽度（TCC 适配的核心） ---------- */
    printf("-- 类型宽度 --\n");
    /* 这三个类型必须与指针同宽 —— 这正是 nuklear_tcc_compat.h 修正
     * NK_SIZE_TYPE 的目的。原来硬编码 == 8，只在 64 位成立；
     * 32 位下 nk_size 应当是 4 字节，断言会误报失败。 */
    CK("nk_size 与指针同宽",   sizeof(nk_size) == sizeof(void *));
    CK("nk_ptr  与指针同宽",   sizeof(nk_ptr) == sizeof(void *));
    CK("nk_handle 与指针同宽", sizeof(nk_handle) == sizeof(void *));
    printf("\n");

    /* ---------- 字体烘焙 ---------- */
    printf("-- 字体烘焙（内置 ProggyClean） --\n");
    struct nk_font_atlas atlas;
    nk_font_atlas_init_default(&atlas);
    nk_font_atlas_begin(&atlas);

    /* 顺序：init -> begin -> add_default -> bake -> end。
     * 先 bake 会得到空图集，随后度量为 0，渲染时断言
     * "you forgot to add a font"（nuklear.h:20930）。 */
    struct nk_font *font = nk_font_atlas_add_default(&atlas, 13.0f, NULL);
    CK("nk_font_atlas_add_default 成功（必须在 bake 之前）", font != NULL);

    int fw = 0, fh = 0;
    const void *img = nk_font_atlas_bake(&atlas, &fw, &fh, NK_FONT_ATLAS_RGBA32);
    CK("nk_font_atlas_bake 返回非空位图", img != NULL);
    printf("       字体图集 %dx%d 像素\n", fw, fh);
    CK("图集尺寸合理", fw > 0 && fh > 0 && fw < 4096 && fh < 4096);

    if (img && fw > 0 && fh > 0) {
        const unsigned char *px = (const unsigned char *)img;
        long total = (long)fw * fh;
        long nonzero = 0;
        for (long i = 3; i < total * 4; i += 4) if (px[i] != 0) nonzero++;
        printf("       非透明像素 %ld / %ld (%.2f%%)\n",
               nonzero, total, 100.0 * (double)nonzero / (double)total);
        CK("图集中存在真实字形像素（非全零）", nonzero > 100);
    }

    if (font) {
        printf("       字形高度 %d, 上升 %d, 下降 %d, 字形数 %d\n",
               (int)font->info.height, (int)font->info.ascent,
               (int)font->info.descent, (int)font->info.glyph_count);
        CK("字体度量合理",
           font->info.height > 0 && font->info.ascent > 0 && font->info.glyph_count > 0);
    }

    nk_font_atlas_end(&atlas, nk_handle_id(0), NULL);
    CK("nk_init_default 成功", nk_init_default(&ctx, font ? &font->handle : NULL) != 0);
    g_win = nk_rect(0, 0, WIN_W, WIN_H);
    printf("\n");

    /* ---------- 发现帧：取几何（此帧不用按钮返回值） ----------
     * nk_widget 会推进布局，所以只在发现帧里用它。 */
    printf("-- 布局几何（发现帧） --\n");
    struct nk_rect b1 = {0}, b2 = {0};
    nk_input_begin(&ctx); nk_input_end(&ctx);
    nk_begin(&ctx, "probe", g_win, NK_WINDOW_NO_SCROLLBAR);
    nk_layout_row_dynamic(&ctx, BTN_H, 1);
    nk_widget(&b1, &ctx);
    nk_widget(&b2, &ctx);
    nk_end(&ctx);
    nk_clear(&ctx);

    printf("       按钮 0: (%.1f,%.1f) %.1fx%.1f\n",
           (double)b1.x, (double)b1.y, (double)b1.w, (double)b1.h);
    printf("       按钮 1: (%.1f,%.1f) %.1fx%.1f\n",
           (double)b2.x, (double)b2.y, (double)b2.w, (double)b2.h);
    CK("按钮宽度小于窗口宽度（有内边距）", (int)b1.w < WIN_W && (int)b1.w > WIN_W - 40);
    CK("按钮高度等于请求值 30", (int)b1.h == (int)BTN_H);
    CK("按钮在窗口内", b1.x >= 0 && b1.y >= 0);
    CK("第二个按钮在第一个下方", b2.y > b1.y);
    CK("两个按钮不重叠", b2.y >= b1.y + b1.h - 0.5f);
    CK("按钮水平位置一致（单列布局）", (int)b1.x == (int)b2.x);

    float c1x = b1.x + b1.w * 0.5f, c1y = b1.y + b1.h * 0.5f;
    float c2x = b2.x + b2.w * 0.5f, c2y = b2.y + b2.h * 0.5f;
    printf("       按钮中心: (%.1f,%.1f) 与 (%.1f,%.1f)\n",
           (double)c1x, (double)c1y, (double)c2x, (double)c2y);
    printf("\n");

    /* ---------- 悬停命中 ----------
     * 用 nk_input_is_mouse_hovering_rect 直接验证命中判定，
     * 这比读 last_widget_state 状态位更贴近实际语义，也更好定位问题：
     * last_widget_state 是**单个变量**，每个控件都会重置它，
     * 渲染多个控件时后面的会覆盖前面的（这里为它踩过坑）。
     *
     * 同时验证按钮返回值的形状（未按下时必须为 0）。 */
    printf("-- 输入：悬停命中 --\n");
    g_c1 = g_c2 = 0;

    nk_input_begin(&ctx);
    nk_input_motion(&ctx, (int)c1x, (int)c1y);
    nk_input_end(&ctx);
    CK("鼠标在按钮 0 中心时命中按钮 0",
       nk_input_is_mouse_hovering_rect(&ctx.input, b1) != 0);
    CK("鼠标在按钮 0 中心时不命中按钮 1",
       nk_input_is_mouse_hovering_rect(&ctx.input, b2) == 0);

    nk_input_begin(&ctx);
    nk_input_motion(&ctx, (int)c2x, (int)c2y);
    nk_input_end(&ctx);
    CK("移到按钮 1 中心后命中按钮 1",
       nk_input_is_mouse_hovering_rect(&ctx.input, b2) != 0);
    CK("此时不命中按钮 0",
       nk_input_is_mouse_hovering_rect(&ctx.input, b1) == 0);

    /* 移到按钮之外 */
    nk_input_begin(&ctx);
    nk_input_motion(&ctx, 5, (int)(b2.y + b2.h + 60.0f));
    nk_input_end(&ctx);
    CK("移到按钮外的空白区不命中任何按钮",
       nk_input_is_mouse_hovering_rect(&ctx.input, b1) == 0 &&
       nk_input_is_mouse_hovering_rect(&ctx.input, b2) == 0);

    /* 悬停但未按下时按钮必须返回 0；按下才返回 1 */
    g_state = 0;
    FRAME(c1x, c1y, 0, btn1_state());
    printf("       悬停未按下: BTN1 返回计数=%d state=%d\n", g_c1, (int)g_state);
    CK("悬停但未按下，按钮不触发", g_c1 == 0);
    CK("悬停时状态含 HOVER",
       (g_state & NK_WIDGET_STATE_HOVER) != 0);

    g_state = 0;
    FRAME(c1x, c1y, 1, btn1_state());
    printf("       悬停并按下: BTN1 返回计数=%d state=%d\n", g_c1, (int)g_state);
    CK("悬停并按下，按钮触发", g_c1 == 1);
    CK("按下时状态含 ACTIVED",
       (g_state & NK_WIDGET_STATE_ACTIVED) != 0);
    FRAME(c1x, c1y, 0, two_buttons());
    printf("\n");
    /* ---------- 完整点击序列 ---------- */
    printf("-- 输入：完整点击序列 --\n");
    g_c1 = g_c2 = 0;

    FRAME(c1x, c1y, 1, btn1_state());
    printf("       按下 BTN1: BTN1=%d BTN2=%d\n", g_c1, g_c2);
    CK("按下即触发 BTN1（NK_BUTTON_DEFAULT 行为）", g_c1 == 1);
    CK("按下 BTN1 不误触 BTN2", g_c2 == 0);

    FRAME(c1x, c1y, 0, btn1_state());
    printf("       抬起 BTN1: BTN1=%d BTN2=%d\n", g_c1, g_c2);
    CK("抬起不重复计数", g_c1 == 1 && g_c2 == 0);

    FRAME(c1x, c1y, 1, btn1_state());
    FRAME(c1x, c1y, 0, btn1_state());
    printf("       再次点击 BTN1: BTN1=%d BTN2=%d\n", g_c1, g_c2);
    CK("可以重复点击（累计 2）", g_c1 == 2 && g_c2 == 0);

    FRAME(c2x, c2y, 1, two_buttons());
    FRAME(c2x, c2y, 0, btn1_state());
    printf("       点击 BTN2: BTN1=%d BTN2=%d\n", g_c1, g_c2);
    CK("点 BTN2 只触发 BTN2", g_c1 == 2 && g_c2 == 1);

    FRAME(b2.x + b2.w * 0.5f, b2.y + b2.h + 40.0f, 1, two_buttons());
    FRAME(b2.x + b2.w * 0.5f, b2.y + b2.h + 40.0f, 0, two_buttons());
    printf("       点空白区: BTN1=%d BTN2=%d\n", g_c1, g_c2);
    CK("点击空白区不触发任何按钮", g_c1 == 2 && g_c2 == 1);
    printf("\n");

    /* ---------- 多列布局 ---------- */
    printf("-- 布局：多列 row --\n");
    struct nk_rect ra = {0}, rb = {0}, rc = {0};
    float widths[3] = { 0.5f, 0.25f, 0.25f };
    nk_input_begin(&ctx); nk_input_end(&ctx);
    nk_begin(&ctx, "probe", g_win, NK_WINDOW_NO_SCROLLBAR);
    nk_layout_row(&ctx, NK_DYNAMIC, 40, 3, widths);
    nk_widget(&ra, &ctx);
    nk_widget(&rb, &ctx);
    nk_widget(&rc, &ctx);
    nk_end(&ctx);
    nk_clear(&ctx);

    printf("       A=%.0f B=%.0f C=%.0f (窗口 %d)\n",
           (double)ra.w, (double)rb.w, (double)rc.w, WIN_W);
    CK("A 约占一半宽度", ra.w > 170 && ra.w < 230);
    CK("B 与 C 等宽", (int)rb.w == (int)rc.w);
    CK("三列从左到右递增", ra.x < rb.x && rb.x < rc.x);
    CK("三列宽度之和接近窗口宽度", ra.w + rb.w + rc.w > WIN_W - 60);
    printf("\n");

    /* ---------- 绘制输出 ---------- */
    printf("-- 绘制输出 --\n");
    struct nk_buffer cmds, vtx, idx;
    nk_buffer_init_default(&cmds);
    nk_buffer_init_default(&vtx);
    nk_buffer_init_default(&idx);

    nk_input_begin(&ctx); nk_input_end(&ctx);
    nk_begin(&ctx, "probe", g_win, NK_WINDOW_NO_SCROLLBAR);
    nk_layout_row_dynamic(&ctx, BTN_H, 1);
    nk_button_label(&ctx, "BTN1");
    nk_button_label(&ctx, "BTN2");
    nk_end(&ctx);

    int n_rect = 0, n_text = 0, n_total = 0;
    const struct nk_command *cmd;
    nk_foreach(cmd, &ctx) {
        n_total++;
        switch (cmd->type) {
            case NK_COMMAND_RECT:
            case NK_COMMAND_RECT_FILLED:
            case NK_COMMAND_RECT_MULTI_COLOR:
                n_rect++; break;
            case NK_COMMAND_TEXT:
                n_text++; break;
            default: break;
        }
    }
    printf("       命令总数 %d（矩形 %d，文本 %d）\n", n_total, n_rect, n_text);
    CK("产生了绘制命令", n_total > 0);
    CK("产生了矩形绘制命令（按钮背景/边框）", n_rect > 0);
    CK("产生了文本绘制命令（按钮标签）", n_text >= 2);

    struct nk_convert_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.shape_AA = NK_ANTI_ALIASING_ON;
    cfg.line_AA  = NK_ANTI_ALIASING_ON;
    cfg.circle_segment_count = 22;
    cfg.arc_segment_count    = 22;
    cfg.curve_segment_count  = 22;
    cfg.global_alpha = 1.0f;
    static const struct nk_draw_vertex_layout_element vlayout[] = {
        { NK_VERTEX_POSITION, NK_FORMAT_FLOAT,    NK_OFFSETOF(struct probe_vertex, position) },
        { NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,    NK_OFFSETOF(struct probe_vertex, uv) },
        { NK_VERTEX_COLOR,    NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct probe_vertex, col) },
        { NK_VERTEX_LAYOUT_END }
    };
    cfg.vertex_layout    = vlayout;
    cfg.vertex_size      = sizeof(struct probe_vertex);
    cfg.vertex_alignment = NK_ALIGNOF(struct probe_vertex);
    cfg.tex_null.texture = nk_handle_id(0);   /* 字段名是 tex_null，不是 null */

    int conv = nk_convert(&ctx, &cmds, &vtx, &idx, &cfg);
    /* 注意：NK_CONVERT_SUCCESS == 0（nuklear.h:1203），
     * 所以"成功"是 == 0 而不是 != 0。这里踩过一次。 */
    printf("       nk_convert 返回 %d (NK_CONVERT_SUCCESS=%d)\n",
           conv, (int)NK_CONVERT_SUCCESS);
    CK("nk_convert 成功", conv == NK_CONVERT_SUCCESS);
    if (conv == NK_CONVERT_SUCCESS) {
        nk_size vb = vtx.needed, ib = idx.needed;
        printf("       顶点缓冲 %d 字节, 索引缓冲 %d 字节\n", (int)vb, (int)ib);
        CK("顶点缓冲非空", vb > 0);
        CK("索引缓冲非空", ib > 0);
        CK("索引数能被 3 整除（三角形）", (ib % 3) == 0);
        CK("顶点缓冲大小是单顶点整数倍", vb % sizeof(struct probe_vertex) == 0);
    }

    nk_buffer_free(&cmds);
    nk_buffer_free(&vtx);
    nk_buffer_free(&idx);
    nk_clear(&ctx);
    printf("\n");

    /* ---------- 清理 ---------- */
    nk_free(&ctx);
    nk_font_atlas_clear(&atlas);
    CK("nk_free / nk_font_atlas_clear 无崩溃", 1);

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail ? 1 : 0;
}
