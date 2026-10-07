/* SQLite 功能验证：真实建库 + 事务 + 查询
 *
 * 版本：SQLite 3.38.2 amalgamation（sqlite3.c + sqlite3.h + sqlite3ext.h）
 * 许可：公有领域（SQLite 是 public domain）
 *
 * 编译：scripts\tcc.cmd examples\probes\probe_sqlite.c -With sqlite -Run
 *
 * 注意：编译 sqlite3.c 必须用 lib\sqlite\sqlite3_tcc.c 包装文件，
 * 不能直接编译 sqlite3.c（原因见 PITFALLS P51）。-With sqlite 已处理。
 */
#include "tccwin.h"
#include <stdio.h>
#include <stdlib.h>   /* atoi / getenv */
#include <string.h>

#include "sqlite3.h"

static int pass = 0, fail = 0;
#define CK(d, c) do { if (c) { printf("  [OK]   %s\n", d); pass++; } \
                      else   { printf("  [FAIL] %s\n", d); fail++; } } while (0)

/* 回调上下文，用于验证查询结果 */
static int g_rows = 0;
static char g_name[64];
static int g_age = 0;

static int row_cb(void *unused, int ncol, char **val, char **col) {
    (void)unused; (void)col;
    if (ncol >= 2) {
        g_rows++;
        if (g_rows == 1) {
            strncpy(g_name, val[0] ? val[0] : "", sizeof g_name - 1);
            g_name[sizeof g_name - 1] = 0;
            g_age = val[1] ? atoi(val[1]) : -1;
        }
    }
    return 0;
}

int main(void) {
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);    printf("=== SQLite 功能验证 ===\n\n");

    /* ---------- 版本 ---------- */
    printf("-- 版本与编译期信息 --\n");
    printf("       SQLite %s\n", sqlite3_libversion());
    printf("       源码 ID 前 20 字符: %.20s\n", sqlite3_sourceid());
    printf("       线程模式: %d (0=single 1=multi 2=serialized)\n", sqlite3_threadsafe());
    CK("sqlite3_libversion 返回非空", sqlite3_libversion() != NULL);
    CK("版本号与被编译的头一致",
       strcmp(sqlite3_libversion(), SQLITE_VERSION) == 0);
    printf("\n");

    /* ---------- 打开数据库（用临时文件，不用 :memory: 之外的东西） ----------
     *
     * ⚠️ 优先用**当前目录**，不直接用 %TEMP%：
     * 在受限（沙箱）环境里 %TEMP% 可能**不可写**，会让探针整体失败，
     * 而失败原因跟 SQLite 毫无关系。自检脚本会把工作目录设成一个
     * 可写的临时目录，所以当前目录是更可靠的落点。
     * %TEMP% 只作为当前目录不可用时的退路。 */
    printf("-- 打开数据库 --\n");
    char dbpath[512];
    const char *base = ".";
    const char *tmp = getenv("TEMP");
    if (tmp) base = tmp;
    /* 先用当前目录试；写不进去再退到 %TEMP% */
    snprintf(dbpath, sizeof dbpath, "tcwin_sqlite_probe.db");
    remove(dbpath);
    {
        sqlite3 *probe = NULL;
        int prc = sqlite3_open(dbpath, &probe);
        if (prc == SQLITE_OK && probe) {
            sqlite3_close(probe);
            remove(dbpath);
        } else {
            if (probe) sqlite3_close(probe);
            snprintf(dbpath, sizeof dbpath, "%s\\tcwin_sqlite_probe.db", base);
            remove(dbpath);
        }
    }

    sqlite3 *db = NULL;
    int rc = sqlite3_open(dbpath, &db);
    printf("       open(%s) -> %d\n", dbpath, rc);
    CK("sqlite3_open 成功", rc == SQLITE_OK && db != NULL);
    if (!db) { printf("无法打开数据库，终止\n"); return 1; }

    /* ---------- 建表 ---------- */
    printf("\n-- 建表与插入 --\n");
    char *err = NULL;
    rc = sqlite3_exec(db,
        "CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT, age INTEGER);",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) printf("       exec 错误: %s\n", err ? err : "(null)");
    CK("CREATE TABLE 成功", rc == SQLITE_OK);
    if (err) { sqlite3_free(err); err = NULL; }

    rc = sqlite3_exec(db,
        "INSERT INTO people(name,age) VALUES('Alice',30);"
        "INSERT INTO people(name,age) VALUES('Bob',25);"
        "INSERT INTO people(name,age) VALUES('Carol',41);",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) printf("       insert 错误: %s\n", err ? err : "(null)");
    CK("批量 INSERT 成功", rc == SQLITE_OK);
    if (err) { sqlite3_free(err); err = NULL; }

    /* 行数 */
    sqlite3_int64 nrows = 0;
    rc = sqlite3_exec(db, "SELECT COUNT(*) FROM people;", row_cb, NULL, &err);
    CK("SELECT COUNT(*) 执行成功", rc == SQLITE_OK);
    if (err) { sqlite3_free(err); err = NULL; }
    printf("       （COUNT 用回调统计，见下方 prepared statement）\n");

    /* ---------- 预处理语句查询 ---------- */
    printf("\n-- 预处理语句（prepared statement）--\n");
    sqlite3_stmt *st = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM people;", -1, &st, NULL);
    CK("sqlite3_prepare_v2 成功", rc == SQLITE_OK && st != NULL);
    if (st) {
        rc = sqlite3_step(st);
        if (rc == SQLITE_ROW) {
            nrows = sqlite3_column_int64(st, 0);
            printf("       行数 = %d\n", (int)nrows);
            CK("插入 3 行，COUNT = 3", nrows == 3);
        } else {
            CK("SELECT 返回行", 0);
        }
        sqlite3_finalize(st);
        st = NULL;
    }

    /* 带绑定参数的查询 */
    rc = sqlite3_prepare_v2(db, "SELECT name, age FROM people WHERE age > ?1 ORDER BY age;",
                            -1, &st, NULL);
    CK("带参数的 prepare 成功", rc == SQLITE_OK);
    if (st) {
        sqlite3_bind_int(st, 1, 28);
        g_rows = 0;
        int got = 0;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            got++;
            printf("       row %d: name=%s age=%d\n", got,
                   (const char *)sqlite3_column_text(st, 0),
                   sqlite3_column_int(st, 1));
        }
        CK("参数绑定查询返回 2 行（age>28）", got == 2);
        CK("结果集迭代正常结束（SQLITE_DONE）", rc == SQLITE_DONE);
        sqlite3_finalize(st);
        st = NULL;
    }

    /* ---------- 回调式查询 + 类型检查 ---------- */
    printf("\n-- 回调式查询与类型 --\n");
    g_rows = 0; g_name[0] = 0; g_age = 0;
    rc = sqlite3_exec(db, "SELECT name, age FROM people ORDER BY age;",
                      row_cb, NULL, &err);
    CK("sqlite3_exec 回调模式成功", rc == SQLITE_OK);
    printf("       回调触发 %d 次，第一行 name=%s age=%d\n", g_rows, g_name, g_age);
    CK("回调触发行数与表一致", g_rows == 3);
    CK("回调取到正确的最小年龄行（Bob,25）",
       strcmp(g_name, "Bob") == 0 && g_age == 25);
    if (err) { sqlite3_free(err); err = NULL; }
    printf("\n");

    /* ---------- 事务 ---------- */
    printf("-- 事务 --\n");
    rc = sqlite3_exec(db, "BEGIN;", NULL, NULL, &err);
    CK("BEGIN 成功", rc == SQLITE_OK);
    sqlite3_exec(db, "INSERT INTO people(name,age) VALUES('Dave',50);", NULL, NULL, NULL);
    rc = sqlite3_exec(db, "ROLLBACK;", NULL, NULL, &err);
    CK("ROLLBACK 成功", rc == SQLITE_OK);
    if (err) { sqlite3_free(err); err = NULL; }

    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM people;", -1, &st, NULL);
    if (st) {
        sqlite3_step(st);
        int after = sqlite3_column_int(st, 0);
        printf("       回滚后行数 = %d（期望仍为 3）\n", after);
        CK("回滚确实撤销了插入", after == 3);
        sqlite3_finalize(st);
        st = NULL;
    }

    rc = sqlite3_exec(db, "BEGIN; INSERT INTO people(name,age) VALUES('Eve',22); COMMIT;",
                      NULL, NULL, &err);
    CK("COMMIT 成功", rc == SQLITE_OK);
    if (err) { sqlite3_free(err); err = NULL; }
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM people;", -1, &st, NULL);
    if (st) {
        sqlite3_step(st);
        int after = sqlite3_column_int(st, 0);
        printf("       提交后行数 = %d（期望 4）\n", after);
        CK("提交确实生效", after == 4);
        sqlite3_finalize(st);
        st = NULL;
    }
    printf("\n");

    /* ---------- 错误路径 ---------- */
    printf("-- 错误处理 --\n");
    rc = sqlite3_exec(db, "SELECT * FROM no_such_table;", NULL, NULL, &err);
    printf("       查询不存在的表 -> %d, 错误信息: %.50s\n", rc, err ? err : "(null)");
    CK("非法 SQL 返回错误码", rc != SQLITE_OK);
    CK("错误信息非空", err != NULL && strlen(err) > 0);
    if (err) { sqlite3_free(err); err = NULL; }

    rc = sqlite3_prepare_v2(db, "THIS IS NOT SQL;", -1, &st, NULL);
    CK("prepare 非法 SQL 失败", rc != SQLITE_OK);
    if (st) { sqlite3_finalize(st); st = NULL; }

    /* 约束冲突 */
    sqlite3_exec(db, "CREATE TABLE uniq(v TEXT UNIQUE);", NULL, NULL, NULL);
    sqlite3_exec(db, "INSERT INTO uniq VALUES('x');", NULL, NULL, NULL);
    rc = sqlite3_exec(db, "INSERT INTO uniq VALUES('x');", NULL, NULL, &err);
    printf("       违反 UNIQUE 约束 -> %d\n", rc);
    CK("UNIQUE 约束生效", rc == SQLITE_CONSTRAINT);
    if (err) { sqlite3_free(err); err = NULL; }
    printf("\n");

    /* ---------- 持久化：重开文件确认数据落盘 ---------- */
    printf("-- 持久化 --\n");
    CK("sqlite3_close 成功", sqlite3_close(db) == SQLITE_OK);

    sqlite3 *db2 = NULL;
    rc = sqlite3_open(dbpath, &db2);
    CK("重新打开数据库成功", rc == SQLITE_OK && db2 != NULL);
    if (db2) {
        sqlite3_prepare_v2(db2, "SELECT COUNT(*) FROM people;", -1, &st, NULL);
        if (st) {
            sqlite3_step(st);
            int n = sqlite3_column_int(st, 0);
            printf("       重开后行数 = %d（期望 4）\n", n);
            CK("数据已持久化到磁盘", n == 4);
            sqlite3_finalize(st);
        }
        sqlite3_close(db2);
    }
    printf("\n");

    /* ---------- 清理 ---------- */
    remove(dbpath);
    printf("已删除测试数据库\n");

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail ? 1 : 0;
}
