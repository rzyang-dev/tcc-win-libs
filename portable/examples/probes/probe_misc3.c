/* probe_misc3.c —— 又一批此前无功能探针的库（真实调用 + 行为断言）
 *
 * 覆盖：
 *   inih         .ini 配置解析
 *   tinyexpr     数学表达式求值
 *   log.c        带级别/颜色的日志
 *   stb_rect_pack 矩形打包（图集/纹理）
 *
 * API 名全部**从各头文件读出**（本项目已多次因猜 API 而失败，见 P77）：
 *
 *   ini.h:82   INI_API int ini_parse(const char* filename, ini_handler, void*);
 *   ini.h:97   INI_API int ini_parse_string(const char* string, ini_handler, void*);
 *   ini.h:58   typedef int (*ini_handler)(void*, const char* section,
 *                                         const char* name, const char* value);
 *
 *   tinyexpr.h:70   double te_interp(const char *expression, int *error);
 *
 *   log.h:31   enum { LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL };
 *   log.h:42   void log_set_level(int level);
 *
 *   stb_rect_pack.h:90   STBRP_DEF int  stbrp_pack_rects(stbrp_context*, stbrp_rect*, int);
 *   stb_rect_pack.h:130  STBRP_DEF void stbrp_init_target(stbrp_context*, int w, int h,
 *                                                         stbrp_node*, int num_nodes);
 *
 * 用法：
 *   scripts\tcc.cmd examples\probes\probe_misc3.c -With inih,tinyexpr,log.c,stb_rect_pack -Run
 */

#include "tccwin.h"

#include <stdio.h>
#include <string.h>

#include "ini.h"
#include "tinyexpr.h"
#include "log.h"
#include "stb_rect_pack.h"

static int pass = 0, fail = 0;

static void ck(const char *what, int ok)
{
    if (ok) { ++pass; printf("  [OK]   %s\n", what); }
    else    { ++fail; printf("  [FAIL] %s\n", what); }
}

static void ck_int(const char *what, int got, int want)
{
    if (got == want) { ++pass; printf("  [OK]   %s\n", what); }
    else { ++fail; printf("  [FAIL] %-46s 得到 %d，期望 %d\n", what, got, want); }
}

/* ============================================================
 * 1. inih —— 解析一段 INI，检查回调收到的 section/name/value
 * ============================================================ */
typedef struct {
    int   count;
    int   got_name;
    int   got_port;
    int   got_debug;
    char  last_section[64];
    char  last_value[64];
} ini_capture;

static int on_ini(void *user, const char *section, const char *name, const char *value)
{
    ini_capture *c = (ini_capture *)user;
    c->count++;
    if (section) { strncpy(c->last_section, section, sizeof c->last_section - 1); }
    if (value)   { strncpy(c->last_value, value, sizeof c->last_value - 1); }

    if (section && strcmp(section, "server") == 0) {
        if (strcmp(name, "name") == 0) {
            c->got_name = (strcmp(value, "tcwin") == 0);
        } else if (strcmp(name, "port") == 0) {
            c->got_port = (strcmp(value, "8080") == 0);
        }
    }
    if (section && strcmp(section, "log") == 0) {
        if (strcmp(name, "debug") == 0) {
            c->got_debug = (strcmp(value, "true") == 0);
        }
    }
    return 1;   /* 非 0 = 继续解析 */
}

static void test_inih(void)
{
    static const char *ini =
        "[server]\n"
        "name = tcwin\n"
        "port = 8080\n"
        "\n"
        "[log]\n"
        "debug = true\n";
    ini_capture cap;
    int rc;

    printf("\n-- 1. inih: INI 解析 --\n");

    memset(&cap, 0, sizeof cap);
    rc = ini_parse_string(ini, on_ini, &cap);
    printf("       ini_parse_string 返回 %d，回调触发 %d 次\n", rc, cap.count);

    ck_int("解析成功（返回 0）", rc, 0);
    ck_int("回调收到 3 个键", cap.count, 3);
    ck("section=server / name=tcwin 解析正确", cap.got_name);
    ck("section=server / port=8080 解析正确", cap.got_port);
    ck("section=log / debug=true 解析正确", cap.got_debug);

    /* 畸形输入：inih 返回错误行号（正数），不是 0 */
    {
        static const char *bad = "[a]\nthis is not valid\n";
        ini_capture c2;
        int r2;
        memset(&c2, 0, sizeof c2);
        r2 = ini_parse_string(bad, on_ini, &c2);
        printf("       畸形 INI 返回 %d\n", r2);
        ck("畸形 INI 返回非 0（错误行号）", r2 != 0);
    }
}

/* ============================================================
 * 2. tinyexpr —— 真实求值
 * ============================================================ */
static void test_tinyexpr(void)
{
    int err = 0;
    double r;

    printf("\n-- 2. tinyexpr: 表达式求值 --\n");

    err = 0;
    r = te_interp("1+2", &err);
    ck_int("te_interp(\"1+2\", &err) 无错误", err, 0);
    ck("1+2 == 3", r > 2.99 && r < 3.01);
    printf("       1+2 = %g\n", r);

    err = 0;
    r = te_interp("2*3+4", &err);
    ck("2*3+4 == 10（优先级正确）", r > 9.99 && r < 10.01);

    err = 0;
    r = te_interp("(2+3)*4", &err);
    ck("(2+3)*4 == 20（括号正确）", r > 19.99 && r < 20.01);

    err = 0;
    r = te_interp("2^10", &err);
    ck("2^10 == 1024（幂运算）", r > 1023.9 && r < 1024.1);
    printf("       2^10 = %g\n", r);

    err = 0;
    r = te_interp("sqrt(16)", &err);
    ck("sqrt(16) == 4（函数调用）", r > 3.99 && r < 4.01);

    err = 0;
    r = te_interp("-5+3", &err);
    ck("-5+3 == -2（一元负号）", r > -2.01 && r < -1.99);

    /* 未定义变量必须报错，且 err 位置非 0 */
    err = 0;
    r = te_interp("xyz+1", &err);
    printf("       xyz+1 -> r=%g err=%d\n", r, err);
    ck("未定义变量设置 err 非 0", err != 0);

    /* 除零/非法表达式也要报错而不是静默给值 */
    err = 0;
    r = te_interp("1+", &err);
    printf("       1+ -> r=%g err=%d\n", r, err);
    ck("不完整表达式设置 err 非 0", err != 0);
}

/* ============================================================
 * 3. log.c —— 真实输出（重定向到文件后检查内容）
 * ============================================================ */
static void test_logc(void)
{
    const char *path = "probe_log_out.txt";
    FILE *f;
    char buf[512];
    size_t n;
    int  has_trace, has_error, has_fatal;

    printf("\n-- 3. log.c: 日志输出与级别过滤 --\n");

    /* 把 stderr 重定向到文件，因为 log.c 默认写 stderr。
     * 用 freopen 把 stderr 换掉，跑完再换回来。 */
    f = freopen(path, "w", stderr);
    if (!f) {
        ck("能重定向 stderr 到文件", 0);
        return;
    }

    log_set_level(LOG_TRACE);
    log_trace("TRACELINE %d", 1);
    log_error("ERRORLINE %d", 2);
    log_fatal("FATALLINE %d", 3);
    fflush(stderr);

    /* 收工：把 stderr 还原到控制台 */
    freopen("CON", "w", stderr);

    f = fopen(path, "rb");
    if (!f) { ck("能读回日志文件", 0); return; }
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    remove(path);

    has_trace = (strstr(buf, "TRACELINE") != 0);
    has_error = (strstr(buf, "ERRORLINE") != 0);
    has_fatal = (strstr(buf, "FATALLINE") != 0);

    printf("       日志文件 %d 字节\n", (int)n);
    ck("LOG_TRACE 级别下 log_trace 有输出", has_trace);
    ck("log_error 有输出", has_error);
    ck("log_fatal 有输出", has_fatal);
    ck("日志内容非空", n > 0);

    /* 级别过滤：设成 LOG_ERROR 后 log_trace 应当被抑制 */
    f = freopen(path, "w", stderr);
    if (f) {
        int has_trace2, has_error2;
        log_set_level(LOG_ERROR);
        log_trace("SHOULD_NOT_APPEAR %d", 9);
        log_error("SHOULD_APPEAR %d", 9);
        fflush(stderr);
        freopen("CON", "w", stderr);

        f = fopen(path, "rb");
        if (f) {
            n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = 0;
            fclose(f);
            remove(path);
            has_trace2 = (strstr(buf, "SHOULD_NOT_APPEAR") != 0);
            has_error2 = (strstr(buf, "SHOULD_APPEAR") != 0);
            ck("级别设为 LOG_ERROR 后 log_trace 被抑制", !has_trace2);
            ck("级别设为 LOG_ERROR 后 log_error 仍有输出", has_error2);
        }
    }
    log_set_level(LOG_TRACE);   /* 复位，免得影响后续输出 */
}

/* ============================================================
 * 4. stb_rect_pack —— 打包正确性（边界 + 两两不重叠）
 * ============================================================ */
static void test_rect_pack(void)
{
    stbrp_context ctx;
    stbrp_node nodes[64];
    stbrp_rect rects[8];
    int i, j, overlap = 0, all_packed = 1, in_bounds = 1;

    printf("\n-- 4. stb_rect_pack: 矩形打包 --\n");

    for (i = 0; i < 8; i++) {
        rects[i].id = i;
        rects[i].w = 16;
        rects[i].h = 16;
        rects[i].was_packed = 0;
        rects[i].x = 0;
        rects[i].y = 0;
    }
    stbrp_init_target(&ctx, 64, 64, nodes, 64);
    {
        int ok = stbrp_pack_rects(&ctx, rects, 8);
        ck("8 个 16x16 放得进 64x64（返回非 0）", ok != 0);
    }

    for (i = 0; i < 8; i++) {
        if (!rects[i].was_packed) all_packed = 0;
        if (rects[i].x < 0 || rects[i].y < 0) in_bounds = 0;
        if (rects[i].x + rects[i].w > 64) in_bounds = 0;
        if (rects[i].y + rects[i].h > 64) in_bounds = 0;
    }
    ck("8 个矩形全部 was_packed", all_packed);
    ck("全部落在 64x64 边界内", in_bounds);

    /* 关键正确性：两两不重叠 */
    for (i = 0; i < 8; i++) {
        for (j = i + 1; j < 8; j++) {
            int ax1 = rects[i].x, ay1 = rects[i].y;
            int ax2 = ax1 + rects[i].w, ay2 = ay1 + rects[i].h;
            int bx1 = rects[j].x, by1 = rects[j].y;
            int bx2 = bx1 + rects[j].w, by2 = by1 + rects[j].h;
            int sep = (ax2 <= bx1) || (bx2 <= ax1) || (ay2 <= by1) || (by2 <= ay1);
            if (!sep) {
                overlap++;
                printf("       重叠: rect%d(%d,%d) 与 rect%d(%d,%d)\n",
                       i, ax1, ay1, j, bx1, by1);
            }
        }
    }
    ck_int("8 个矩形两两不重叠", overlap, 0);
    printf("       首个矩形落在 (%d,%d)\n", rects[0].x, rects[0].y);

    /* 放不下时必须诚实返回 0 */
    {
        stbrp_context c2;
        stbrp_node n2[8];
        stbrp_rect r2;
        int ok;
        r2.id = 0; r2.w = 200; r2.h = 200; r2.was_packed = 0; r2.x = 0; r2.y = 0;
        stbrp_init_target(&c2, 64, 64, n2, 8);
        ok = stbrp_pack_rects(&c2, &r2, 1);
        ck("放不下的矩形返回 0", ok == 0);
        ck("放不下的矩形 was_packed == 0", r2.was_packed == 0);
    }
}

int main(void)
{
    SetConsoleOutputCP(CP_UTF8);
    printf("=== probe_misc3: inih / tinyexpr / log.c / stb_rect_pack ===\n");

    test_inih();
    test_tinyexpr();
    test_logc();
    test_rect_pack();

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
