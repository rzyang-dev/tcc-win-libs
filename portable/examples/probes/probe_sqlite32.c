/* probe_sqlite32.c —— 固化「TCC 32 位跑不了 SQLite」这个结论
 *
 * ============================================================
 * 这个探针为什么存在
 * ============================================================
 *
 * SQLite 在 TCC 下有个**位宽相关的硬缺陷**：64 位完全正常，
 * 32 位在 `sqlite3_initialize()` 里访问违例（0xC0000005）。
 *
 * 这个结论的证据是**编译器交叉验证**（验收标准第 3 条）：
 *
 *   | 编译器          | 位宽 | 结果                    |
 *   |-----------------|------|-------------------------|
 *   | TCC 0.9.27      |  64  | 正常，全套 CRUD 通过     |
 *   | TCC 0.9.27      |  32  | **访问违例 0xC0000005** |
 *   | MinGW gcc 6.3.0 |  32  | 正常，全套 CRUD 通过     |
 *
 * **同一份 sqlite3.c 源码，gcc 32 位正常而 TCC 32 位崩** ——
 * 所以是 TCC 的 32 位缺陷，不是 SQLite 的问题。
 *
 * 已排除的原因（都实测过，见 PITFALLS P75）：long long 运算、
 * 线程原语/TLS、malloc 对齐、栈被写坏。
 * **未定位到具体代码生成点。**
 *
 * 本探针的作用：把这个结论变成**每次自检都会跑的断言**，
 * 防止将来有人"修好了"却没发现，或者又踩进去。
 *
 * 用法：scripts\tcc.cmd examples\probes\probe_sqlite32.c -With sqlite -Run
 */

#include <stddef.h>
#include <stdint.h>

#include "tccwin.h"
#include "sqlite3.h"

#include <stdio.h>

static int pass = 0, fail = 0;

static void ck(const char *what, int ok)
{
    if (ok) { ++pass; printf("  [OK]   %s\n", what); }
    else    { ++fail; printf("  [FAIL] %s\n", what); }
}

int main(void)
{
    int bits = (int)(sizeof(void *) * 8);
    sqlite3 *db = 0;
    int rc;

    SetConsoleOutputCP(CP_UTF8);
    printf("=== probe_sqlite32: SQLite 的位宽缺陷固化 ===\n");
    printf("    指针 %d 字节 -> %d 位构建\n", (int)sizeof(void *), bits);

    printf("\n-- 1. 版本与线程模式（两位宽都该正常）--\n");
    printf("       libversion=%s  threadsafe=%d\n",
           sqlite3_libversion(), sqlite3_threadsafe());
    ck("sqlite3_libversion 返回非空", sqlite3_libversion() != NULL);
    ck("版本字符串以 3. 开头", sqlite3_libversion()[0] == '3');

    printf("\n-- 2. 核心断言：initialize 不能崩 --\n");
    /* 32 位下这一行会崩，所以本探针**只在 64 位下注册进自检**。
     * 32 位能在 `-With sqlite` 下编到这里，说明有人定义了
     * SQLITE_TCC_ALLOW_BROKEN_32BIT 绕过守卫 —— 那时这里就会崩，
     * 这正是"逃生舱不是修复"的证明。 */
    rc = sqlite3_initialize();
    ck("sqlite3_initialize() 返回（没崩）", 1);
    ck("sqlite3_initialize() 返回 SQLITE_OK", rc == SQLITE_OK);
    printf("       rc=%d\n", rc);

    printf("\n-- 3. 内存库的完整 CRUD（真实功能验证，不只是编译）--\n");
    rc = sqlite3_open(":memory:", &db);
    ck("sqlite3_open(:memory:) 成功", rc == SQLITE_OK && db != NULL);
    if (rc != SQLITE_OK) {
        printf("       errmsg=%s\n", db ? sqlite3_errmsg(db) : "(null)");
        goto out;
    }

    ck("CREATE TABLE",
       sqlite3_exec(db, "CREATE TABLE t(a INTEGER, b TEXT)", 0, 0, 0) == SQLITE_OK);

    {
        sqlite3_stmt *st = 0;
        int i;
        /* 用绑定参数插入，顺带验证 bind 路径 */
        ck("PREPARE INSERT",
           sqlite3_prepare_v2(db, "INSERT INTO t VALUES(?,?)", -1, &st, 0) == SQLITE_OK);
        for (i = 0; i < 10; i++) {
            sqlite3_bind_int(st, 1, i * 100);
            sqlite3_bind_text(st, 2, (i % 2) ? "odd" : "even", -1, SQLITE_STATIC);
            if (sqlite3_step(st) != SQLITE_DONE) { ck("INSERT step", 0); break; }
            sqlite3_reset(st);
        }
        sqlite3_finalize(st);
        ck("插入 10 行", i == 10);
    }

    {
        sqlite3_stmt *st = 0;
        int n = 0;
        ck("PREPARE SELECT",
           sqlite3_prepare_v2(db, "SELECT a,b FROM t ORDER BY a", -1, &st, 0) == SQLITE_OK);
        while (sqlite3_step(st) == SQLITE_ROW) n++;
        sqlite3_finalize(st);
        ck("读回 10 行", n == 10);
    }

    {
        /* 聚合 + 文本比较，验证 64 位整数路径（SQLite 的 i64 运算） */
        sqlite3_stmt *st = 0;
        rc = sqlite3_prepare_v2(db, "SELECT sum(a) FROM t", -1, &st, 0);
        ck("PREPARE sum", rc == SQLITE_OK);
        if (rc == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) {
            /* 0+100+...+900 = 4500 */
            ck("sum(a) == 4500", sqlite3_column_int64(st, 0) == 4500);
        }
        sqlite3_finalize(st);
    }

    {
        sqlite3_stmt *st = 0;
        rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM t WHERE b='odd'", -1, &st, 0);
        ck("PREPARE count", rc == SQLITE_OK);
        if (rc == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) {
            ck("count(odd) == 5", sqlite3_column_int(st, 0) == 5);
        }
        sqlite3_finalize(st);
    }

out:
    if (db) sqlite3_close(db);

    printf("\n-- 4. 位宽结论 --\n");
    if (bits == 64) {
        ck("64 位：SQLite 完全可用（本探针即为证据）", fail == 0);
    } else {
        /* 走到这里说明有人开了逃生舱 */
        printf("       **这是 32 位构建。TCC 32 位跑 SQLite 会崩（见 PITFALLS P75）。**\n");
        ck("32 位：本探针本不该被运行到（应被编译期守卫拦住）", 0);
    }

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
