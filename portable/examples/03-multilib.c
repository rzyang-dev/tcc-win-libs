/* 03-multilib.c - 多库协作：一个文件指纹与清单工具
 *
 * 一次用上 6 个库：
 *   tinydir   遍历目录
 *   sha256    文件内容哈希
 *   xxhash    快速哈希 + 校验
 *   cjson     输出 JSON 清单
 *   inih      读取忽略规则
 *   stb_ds    动态数组收集结果
 *
 * 编译：
 *   scripts\tcc.cmd examples\03-multilib.c -With tinydir,sha256,xxhash,cjson,stb_ds -Run
 *   scripts\tcc.cmd examples\03-multilib.c -With tinydir,sha256,xxhash,cjson,stb_ds -- .
 */
/* 用 #ifndef 守卫：脚本的 -With 已经传了其中一部分宏，
 * 无条件 #define 会触发 "redefined" 警告。 */
#ifndef STB_DS_IMPLEMENTATION
#define STB_DS_IMPLEMENTATION
#endif
#ifndef LONESHA256_IMPLEMENTATION
#define LONESHA256_IMPLEMENTATION
#endif
#ifndef XXH_NO_XXH3
#define XXH_NO_XXH3          /* TCC 无 SSE2 内建，必须关掉 XXH3 */
#endif
#ifndef XXH_STATIC_LINKING_ONLY
#define XXH_STATIC_LINKING_ONLY
#endif
#ifndef XXH_IMPLEMENTATION
#define XXH_IMPLEMENTATION
#endif

#include "tccwin.h"
#include "stb_ds.h"
#include "tinydir.h"
#include "lonesha256.h"
#include "xxhash.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;          /* 相对路径 */
    unsigned long long size;
    char  sha256[65];    /* hex */
    unsigned xxh32;
} Entry;

static int read_all(const char *path, unsigned char **out, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return 0; }
    unsigned char *buf = (unsigned char *)malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    *out = buf;
    *len = got;
    return 1;
}

static void to_hex(const unsigned char *in, size_t n, char *out) {
    static const char *d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i*2]   = d[in[i] >> 4];
        out[i*2+1] = d[in[i] & 0xF];
    }
    out[n*2] = 0;
}

int main(int argc, char **argv) {
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);    const char *dir = (argc > 1) ? argv[1] : ".";
    printf("=== 文件指纹清单 ===\n目录: %s\n\n", dir);

    Entry *entries = NULL;
    tinydir_dir d;
    if (tinydir_open(&d, dir) == -1) {
        fprintf(stderr, "无法打开目录 %s\n", dir);
        return 1;
    }
    unsigned long long total = 0;
    while (d.has_next) {
        tinydir_file f;
        if (tinydir_readfile(&d, &f) == -1) break;
        if (!f.is_dir && f._size > 0) {
            unsigned char *data; size_t len;
            if (read_all(f.path, &data, &len)) {
                unsigned char dig[32];
                Entry e;
                memset(&e, 0, sizeof e);
                e.name = strdup(f.name);
                e.size = (unsigned long long)len;
                if (lonesha256(dig, data, len) == 0) to_hex(dig, 32, e.sha256);
                else strcpy(e.sha256, "(error)");
                e.xxh32 = (unsigned)XXH32(data, len, 0);
                arrput(entries, e);
                total += e.size;
                free(data);
            }
        }
        tinydir_next(&d);
    }
    tinydir_close(&d);

    /* 按名字排序输出 */
    size_t n = arrlen(entries);
    for (size_t i = 1; i < n; i++) {          /* 插入排序，量小够用 */
        Entry key = entries[i];
        size_t j = i;
        while (j > 0 && strcmp(entries[j-1].name, key.name) > 0) {
            entries[j] = entries[j-1]; j--;
        }
        entries[j] = key;
    }

    for (size_t i = 0; i < n; i++) {
        printf("%-24s %9llu  %08X  %.16s...\n",
               entries[i].name, entries[i].size,
               entries[i].xxh32, entries[i].sha256);
    }
    printf("\n共 %d 个文件, %llu 字节\n", (int)n, total);

    /* 生成 JSON 清单（cJSON） */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "directory", dir);
    cJSON_AddNumberToObject(root, "file_count", (double)n);
    cJSON_AddNumberToObject(root, "total_bytes", (double)total);
    cJSON *arr = cJSON_AddArrayToObject(root, "files");
    for (size_t i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", entries[i].name);
        cJSON_AddNumberToObject(o, "size", (double)entries[i].size);
        cJSON_AddStringToObject(o, "sha256", entries[i].sha256);
        cJSON_AddNumberToObject(o, "xxh32", (double)entries[i].xxh32);
        cJSON_AddItemToArray(arr, o);
    }
    char *json = cJSON_Print(root);
    printf("\n=== JSON 清单 ===\n%s\n", json);
    cJSON_free(json);
    cJSON_Delete(root);

    /* stb_ds 的动态数组要手动释放字符串 */
    for (size_t i = 0; i < n; i++) free(entries[i].name);
    arrfree(entries);
    return 0;
}
