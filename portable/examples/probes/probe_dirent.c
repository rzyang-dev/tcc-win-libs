/* probe_dirent.c —— 验证 TCC 的 <dirent.h> 现在真的能用
 *
 * ============================================================
 * 背景
 * ============================================================
 *
 * TCC 自带 <dirent.h>，**完整声明了 POSIX 目录接口**，但那些符号
 * msvcrt.dll 一个都没导出 —— 任何 `#include <dirent.h>` 的移植代码
 * 都在链接期报 `undefined symbol 'opendir'`。这是"头文件谎报能力"，
 * 比缺头文件更隐蔽，因为**编译能过**。
 *
 * `helper/dirent_compat.c` 用 msvcrt 真正有的 `_findfirst` 族把它补齐了。
 * 本探针验证补得对不对。
 *
 * 用法：scripts\tcc.cmd examples\probes\probe_dirent.c -With dirent -Run
 *
 * 「未验证边界」见文件末尾。
 */

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 建目录要用的 Win32 API。tccwin.h 会引入 windows.h。 */
#include "tccwin.h"

static int pass = 0, fail = 0;

static void ck(const char *what, int ok)
{
    if (ok) { ++pass; printf("  [OK]   %s\n", what); }
    else    { ++fail; printf("  [FAIL] %s\n", what); }
}

static void ck_int(const char *what, int got, int want)
{
    if (got == want) { ++pass; printf("  [OK]   %s\n", what); }
    else { ++fail; printf("  [FAIL] %-52s 得到 %d，期望 %d\n", what, got, want); }
}

/* 在目录里找一项，找到返回 1 */
static int dir_has(DIR *d, const char *want)
{
    struct dirent *e;
    rewinddir(d);
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, want) == 0) return 1;
    }
    return 0;
}

/* ============================================================
 * 1. 基本遍历
 * ============================================================ */
static void test_basic_walk(void)
{
    DIR *d;
    struct dirent *e;
    int n = 0, dots = 0, names_ok = 1;

    printf("\n-- 1. 基本遍历 --\n");

    d = opendir(".");
    ck("opendir(\".\") 返回非空", d != NULL);
    if (!d) return;

    while ((e = readdir(d)) != NULL) {
        if (e->d_name == NULL) { names_ok = 0; break; }
        /* d_namlen 必须等于 strlen(d_name) —— 实现里这一点要对上 */
        if ((int)e->d_namlen != (int)strlen(e->d_name)) names_ok = 0;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) dots++;
        n++;
        if (n > 500) break;              /* 防意外死循环 */
    }
    ck("遍历到至少 1 项", n >= 1);
    ck("d_namlen 与 strlen(d_name) 一致", names_ok);
    ck("包含 \".\" 与 \"..\" 两项", dots == 2);
    printf("       共读到 %d 项\n", n);
    ck_int("closedir 返回 0", closedir(d), 0);
}

/* ============================================================
 * 2. 遍历结束与关闭后的行为
 * ============================================================ */
static void test_end_and_close(void)
{
    DIR *d;
    struct dirent *e;
    int n = 0;

    printf("\n-- 2. 遍历结束 / 关闭后的行为 --\n");

    d = opendir(".");
    if (!d) { ck("opendir 成功", 0); return; }

    while (readdir(d) != NULL) { if (++n > 1000) break; }
    /* 到底之后必须持续返回 NULL，不能重新开始 */
    ck("到底后 readdir 返回 NULL", readdir(d) == NULL);
    ck("再调一次仍是 NULL（不会循环）", readdir(d) == NULL);

    /* rewinddir 之后必须能重新读到 */
    rewinddir(d);
    e = readdir(d);
    ck("rewinddir 后能重新读到（且不崩）", e != NULL || n == 0);

    ck_int("closedir 返回 0", closedir(d), 0);
}

/* ============================================================
 * 3. rewinddir / telldir / seekdir
 * ============================================================ */
static void test_position(void)
{
    DIR *d;
    struct dirent *e1, *e2;
    char first[260];
    long pos;

    printf("\n-- 3. 位置控制（telldir / seekdir / rewinddir）--\n");

    d = opendir(".");
    if (!d) { ck("opendir 成功", 0); return; }

    e1 = readdir(d);
    ck("读到第一项", e1 != NULL);
    if (!e1) { closedir(d); return; }
    strncpy(first, e1->d_name, sizeof first - 1);
    first[sizeof first - 1] = 0;

    pos = telldir(d);
    ck_int("读完第一项后 telldir == 1", (int)pos, 1);

    e2 = readdir(d);
    ck("读到第二项", e2 != NULL);

    /* seekdir(1) 后应回到第二项 */
    seekdir(d, 1);
    e2 = readdir(d);
    ck("seekdir(1) 后读到的是第二项（不是第一项）",
       e2 != NULL && strcmp(e2->d_name, first) != 0);

    /* seekdir(0) 后应回到第一项 */
    seekdir(d, 0);
    e2 = readdir(d);
    ck("seekdir(0) 后读到第一项",
       e2 != NULL && strcmp(e2->d_name, first) == 0);

    /* rewinddir 等价于 seekdir(0) */
    rewinddir(d);
    e2 = readdir(d);
    ck("rewinddir 后读到第一项",
       e2 != NULL && strcmp(e2->d_name, first) == 0);

    ck_int("telldir(0) 在 rewind 后 == 1（已读一项）", (int)telldir(d), 1);

    closedir(d);
}

/* ============================================================
 * 4. 子目录 + 与我们自己创建的已知内容对照
 * ============================================================
 *
 * 前面的用例都依赖"当前目录有什么"，不确定。这里自己建一个目录、
 * 放两个已知文件，然后确认遍历结果与预期一致。
 */
static void test_known_dir(void)
{
    const char *dir = "probe_dirent_tmp";
    char path[300];
    FILE *f;
    DIR *d;

    printf("\n-- 4. 已知内容的子目录 --\n");

    /* 清理可能的残留，然后建目录 */
    sprintf(path, "%s\\a.txt", dir);
    remove(path);
    sprintf(path, "%s\\b.txt", dir);
    remove(path);
    sprintf(path, "%s", dir);
    RemoveDirectoryA(path);

    ck("创建测试目录", CreateDirectoryA(dir, NULL) != 0);

    /* 放两个文件 */
    sprintf(path, "%s\\a.txt", dir);
    f = fopen(path, "wb"); if (f) { fputs("A", f); fclose(f); }
    sprintf(path, "%s\\b.txt", dir);
    f = fopen(path, "wb"); if (f) { fputs("B", f); fclose(f); }

    d = opendir(dir);
    ck("opendir(测试目录) 成功", d != NULL);
    if (d) {
        int n = 0;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) { (void)e; if (++n > 100) break; }
        /* 应看到 . / .. / a.txt / b.txt 共 4 项 */
        ck_int("目录内有 4 项（. .. a.txt b.txt）", n, 4);
        ck("能找到 a.txt", dir_has(d, "a.txt"));
        ck("能找到 b.txt", dir_has(d, "b.txt"));
        closedir(d);
    }

    /* 不存在的目录必须返回 NULL，不能崩 */
    ck("opendir(不存在的目录) 返回 NULL",
       opendir("probe_dirent_no_such_dir_xyz") == NULL);

    /* 清理 */
    sprintf(path, "%s\\a.txt", dir); remove(path);
    sprintf(path, "%s\\b.txt", dir); remove(path);
    sprintf(path, "%s", dir);        RemoveDirectoryA(path);
    ck("清理测试目录", GetFileAttributesA(dir) == INVALID_FILE_ATTRIBUTES);
}

/* ============================================================
 * 5. 与 tinydir 的结果交叉校验
 * ============================================================
 *
 * 本环境自带 tinydir（走 Win32 FindFirstFile，与 dirent_compat 走
 * msvcrt _findfirst 是两条不同路径）。两者对同一个目录的**文件项数**
 * 应当一致 —— 这是独立实现的交叉验证。
 */
static void test_cross_check_tinydir(void)
{
    DIR *d;
    struct dirent *e;
    int dirent_files = 0;

    printf("\n-- 5. 与独立实现交叉校验（项数一致性）--\n");

    d = opendir(".");
    if (!d) { ck("opendir 成功", 0); return; }
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) dirent_files++;
        if (dirent_files > 5000) break;
    }
    closedir(d);

    /* 用 Win32 自己的 FindFirstFile 数一遍（与 dirent_compat 不同 API） */
    {
        WIN32_FIND_DATAA fd;
        HANDLE h;
        int win32_files = 0;
        h = FindFirstFileA("*", &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) win32_files++;
                if (win32_files > 5000) break;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        printf("       dirent_compat 数到 %d 项，FindFirstFile 数到 %d 项\n",
               dirent_files, win32_files);
        ck_int("两条独立路径数出的项数一致", dirent_files, win32_files);
    }
}

int main(void)
{
    SetConsoleOutputCP(CP_UTF8);
    printf("=== probe_dirent: TCC 的 <dirent.h> 补齐验证 ===\n");

    test_basic_walk();
    test_end_and_close();
    test_position();
    test_known_dir();
    test_cross_check_tinydir();

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
