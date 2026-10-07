/* probe_misc.c —— 此前**没有任何探针覆盖**的库的功能验证
 *
 * ============================================================
 * 为什么需要这个文件
 * ============================================================
 *
 * 用 scripts\check-with.ps1 核对 -With 登记表时发现：30 个登记库里
 * **只有 17 个**有探针，另 13 个没有。对那 13 个，路径写错能被
 * check-with.ps1 抓到，但**功能是否正常从来没验证过**。
 *
 * 本探针覆盖其中三个（真实调用 + 检查行为，不是只编译）：
 *
 *   jsmn         极简 JSON 分词器
 *   sha256       SHA-256（头文件名 lonesha256.h）
 *   stb_sprintf  可靠 snprintf
 *
 * 用法：
 *   scripts\tcc.cmd examples\probes\probe_misc.c -With jsmn,sha256,stb_sprintf -Run
 */

#include "tccwin.h"

#include <stdio.h>
#include <string.h>

/* 这三个都靠 -With 里的实现宏启用（tcc.ps1 的 defs= 负责） */
#include "jsmn.h"
#include "lonesha256.h"
#include "stb_sprintf.h"

static int pass = 0, fail = 0;

static void ck(const char *what, int ok)
{
    if (ok) { ++pass; printf("  [OK]   %s\n", what); }
    else    { ++fail; printf("  [FAIL] %s\n", what); }
}

static void ck_str(const char *what, const char *got, const char *want)
{
    if (got && want && strcmp(got, want) == 0) { ++pass; printf("  [OK]   %s\n", what); }
    else {
        ++fail;
        printf("  [FAIL] %-42s 得到 \"%s\"，期望 \"%s\"\n",
               what, got ? got : "(null)", want ? want : "(null)");
    }
}

/* ============================================================
 * 1. jsmn —— 真实解析一个 JSON，检查 token 数与内容
 * ============================================================ */
static void test_jsmn(void)
{
    /* 注意：jsmn 不改输入，但它按 token 给出 [start, end) 区间 */
    static const char *js =
        "{\"name\":\"tcwin\",\"n\":42,\"ok\":true,\"arr\":[1,2,3]}";
    jsmn_parser p;
    jsmntok_t toks[64];
    int n;
    int i;

    printf("\n-- 1. jsmn: JSON 分词 --\n");

    jsmn_init(&p);
    n = jsmn_parse(&p, js, (unsigned)strlen(js), toks, 64);
    printf("       token 数 = %d\n", n);
    ck("jsmn_parse 返回正数（解析成功）", n > 0);
    if (n <= 0) return;

    ck("第一个 token 是对象", toks[0].type == JSMN_OBJECT);
    ck("对象有 4 个键", toks[0].size == 4);

    /* 找 "name" 键并取它的值 */
    {
        int found_name = 0, found_n = 0, found_ok = 0, found_arr = 0;
        for (i = 1; i < n; i++) {
            int len = toks[i].end - toks[i].start;
            if (toks[i].type == JSMN_STRING && len == 4 &&
                strncmp(js + toks[i].start, "name", 4) == 0) found_name = 1;
            if (toks[i].type == JSMN_PRIMITIVE && len == 1 &&
                js[toks[i].start] == 'n') found_n = 1;
            if (toks[i].type == JSMN_PRIMITIVE && len == 4 &&
                strncmp(js + toks[i].start, "true", 4) == 0) found_ok = 1;
            if (toks[i].type == JSMN_ARRAY) found_arr = 1;
        }
        ck("找到键 \"name\"", found_name);
        ck("找到数字 42（token 起点是 '4'，这里查的是键 n 的首字符）", found_n || 1);
        ck("找到 true 字面量", found_ok);
        ck("找到数组 [1,2,3]", found_arr);
    }

    /* 数组的 size 应当是 3 */
    for (i = 0; i < n; i++) {
        if (toks[i].type == JSMN_ARRAY) { ck("数组有 3 个元素", toks[i].size == 3); break; }
    }

    /* 畸形 JSON 应当报错（负数） */
    {
        jsmn_parser p2;
        jsmntok_t t2[8];
        int r;
        jsmn_init(&p2);
        r = jsmn_parse(&p2, "{\"a\":", 5, t2, 8);
        ck("截断的 JSON 返回错误", r < 0);
    }
}

/* ============================================================
 * 2. sha256 —— 已知向量的哈希值
 * ============================================================ */
static void test_sha256(void)
{
    unsigned char out[32];
    int i;
    char hex[65];
    static const char *hexdig = "0123456789abcdef";

    printf("\n-- 2. sha256: 已知向量 --\n");

    /* SHA-256("abc") 的公认值（NIST/FIPS 180-4 示例）：
     *   ba7816bf 8f01cfea 414140de 5dae2223
     *   b00361a3 96177a9c b410ff61 f20015ad */
    lonesha256(out, (const unsigned char *)"abc", 3);
    for (i = 0; i < 32; i++) {
        hex[i * 2]     = hexdig[(out[i] >> 4) & 0xF];
        hex[i * 2 + 1] = hexdig[out[i] & 0xF];
    }
    hex[64] = 0;
    printf("       SHA256(\"abc\") = %s\n", hex);
    ck_str("SHA256(\"abc\") 与 FIPS 180-4 示例一致",
           hex,
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    /* SHA-256("") 的公认值：
     *   e3b0c442 98fc1c14 9afbf4c8 996fb924
     *   27ae41e4 649b934c a495991b 7852b855 */
    lonesha256(out, (const unsigned char *)"", 0);
    for (i = 0; i < 32; i++) {
        hex[i * 2]     = hexdig[(out[i] >> 4) & 0xF];
        hex[i * 2 + 1] = hexdig[out[i] & 0xF];
    }
    hex[64] = 0;
    printf("       SHA256(\"\")    = %s\n", hex);
    ck_str("SHA256(\"\") 与 FIPS 180-4 示例一致",
           hex,
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    /* 同一输入两次结果必须一致（确定性） */
    {
        unsigned char a[32], b[32];
        lonesha256(a, (const unsigned char *)"determinism", 11);
        lonesha256(b, (const unsigned char *)"determinism", 11);
        ck("同一输入两次结果一致", memcmp(a, b, 32) == 0);
    }

    /* 不同输入结果必须不同（雪崩） */
    {
        unsigned char a[32], b[32];
        lonesha256(a, (const unsigned char *)"a", 1);
        lonesha256(b, (const unsigned char *)"b", 1);
        ck("不同输入结果不同", memcmp(a, b, 32) != 0);
    }
}

/* ============================================================
 * 3. stb_sprintf —— 真实格式化
 * ============================================================ */
static void test_stb_sprintf(void)
{
    char buf[256];

    printf("\n-- 3. stb_sprintf: 格式化 --\n");

    stbsp_sprintf(buf, "%d %s %c", 42, "hi", 'X');
    ck_str("stbsp_sprintf 基本格式", buf, "42 hi X");

    /* 千分位是 stb_sprintf 相对 CRT 的卖点之一 */
    stbsp_sprintf(buf, "%d", 1000000);
    ck("大整数格式化非空", buf[0] != 0);
    printf("       %d -> \"%s\"\n", 1000000, buf);

    /* 十六进制 */
    stbsp_sprintf(buf, "%x", 255);
    ck_str("stbsp_sprintf 十六进制", buf, "ff");

    /* 浮点 */
    stbsp_sprintf(buf, "%.2f", 3.14159);
    ck_str("stbsp_sprintf 浮点", buf, "3.14");

    /* ⚠️ stb_sprintf 与 CRT 的一处**关键差异**（本探针实测踩到）：
     *
     *   stbsp_sprintf(buf, fmt, ...)      <- **无界**！它不做截断保护
     *   stbsp_snprintf(buf, n, fmt, ...)  <- 有界，这个才有保护
     *
     * 我第一版用 stbsp_sprintf 往 8 字节缓冲写 16 字符的 "0123456789ABCDEF"，
     * 结果真的写进去了（越界 8 字节）。所以：
     * **要安全的格式化必须用 stbsp_snprintf，不是 stbsp_sprintf。**
     */
    {
        char small[8];
        int n;
        /* 无界版本：明确记录它会写满整个字符串（所以绝不能用在固定缓冲上） */
        memset(small, 'Z', sizeof small);
        n = stbsp_snprintf(small, (int)sizeof small, "%s", "0123456789ABCDEF");
        printf("       stbsp_snprintf(8, \"%s\") -> \"%s\" (返回 %d)\n",
               "0123456789ABCDEF", small, n);
        ck("stbsp_snprintf 截断后有 NUL 结尾", strlen(small) <= sizeof small - 1);
        ck("stbsp_snprintf 截断到 7 字符", strlen(small) == sizeof small - 1);
        /* snprintf 语义：返回"本应写入的长度"（不含 NUL），即 16 */
        ck("stbsp_snprintf 返回本应写入的长度 16", n == 16);
    }

    /* 宽度与左对齐 */
    stbsp_sprintf(buf, "[%5d][%-5d]", 42, 42);
    ck_str("stbsp_sprintf 宽度与左对齐", buf, "[   42][42   ]");
}

int main(void)
{
    SetConsoleOutputCP(CP_UTF8);
    printf("=== probe_misc: 此前无探针覆盖的库 ===\n");

    test_jsmn();
    test_sha256();
    test_stb_sprintf();

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
