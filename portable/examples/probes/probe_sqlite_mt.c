/* probe_sqlite_mt.c —— SQLite 并发压测（ROADMAP B3）
 *
 * ============================================================
 * 为什么需要这个探针
 * ============================================================
 *
 * 现有 probe_sqlite.c 全是单线程的。而 SQLite 的线程模式声明是：
 *
 *   sqlite3_compileoption_get() 列表里有 THREADSAFE=1 与 MUTEX_W32
 *   -> SQLITE_THREADSAFE=1，即**序列化模式**（SQLite 的默认值），
 *      互斥锁启用，sqlite3_open() 默认也用序列化模式
 *
 * 但"声明是线程安全的"与"实际并发下不出错"是两件事。
 * 这个探针用真实线程压测，验证三件事：
 *
 *   1. 多线程**共享一个连接**（序列化模式的承诺）—— 计数不能丢
 *   2. 多线程**各用自己的连接**写同一个库 —— 不能损坏，冲突要报
 *      SQLITE_BUSY 而不是静默丢数据
 *   3. WAL 模式下读写并发 —— 读者不能看到半个事务，不能读到损坏页
 *
 * 「未验证边界」见文件末尾。
 *
 * 编译：scripts\tcc.cmd examples\probes\probe_sqlite_mt.c -With sqlite -Run
 */

#include "tccwin.h"
#include "sqlite3.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================
 * 测试框架
 * ============================================================ */
static int pass = 0, fail = 0;

static void ck(const char *what, int ok)
{
    if (ok) { ++pass; printf("  [OK]   %s\n", what); }
    else    { ++fail; printf("  [FAIL] %s\n", what); }
}

/* 生成一个临时库路径。
 *
 * ⚠️ 这里**必须实测哪个目录可写**，不能二选一地写死：
 *
 *   - 早期版本写死 GetTempPathA()。当时的理由是"自检脚本会 Push-Location
 *     到 $env:TEMP 下的随机子目录，依赖 cwd 是脆弱的"。
 *     那个理由本身没错，但后来在**受限（沙箱）环境**下实测发现
 *     %TEMP% 反而**不可写**，于是探针整体失败，而失败原因跟 SQLite
 *     毫无关系（现象是 SQLITE_CANTOPEN，容易误判成"SQLite 不能建库"）。
 *   - 反过来把 cwd 写死也不行，原因就是上面那条。
 *
 * 所以改成：**逐个候选目录实际建一个文件试试**，第一个成功的就用。
 * 顺序是 cwd -> %TEMP%。两者都不可写则返回 0（探针会显式报失败，
 * 而不是让 SQLite 抛一个误导性的 CANTOPEN）。
 *
 * 返回写入 buf 的长度；0 表示失败。 */
static int dir_is_writable(const char *dir)
{
    char probe[MAX_PATH + 32];
    FILE *f;
    if (!dir || !dir[0]) return 0;
    if ((int)strlen(dir) + 32 >= (int)sizeof probe) return 0;
    /* 名字带进程号，避免并发自检互相打架 */
    sprintf(probe, "%s\\tcwin_wtest_%lu.tmp", dir, (unsigned long)GetCurrentProcessId());
    f = fopen(probe, "wb");
    if (!f) return 0;
    fputc('x', f);
    fclose(f);
    remove(probe);
    return 1;
}

static int tmp_db_path(char *buf, int cap, const char *name)
{
    char dir[480];
    DWORD n;
    int len;

    /* 候选 1：当前目录 */
    if (dir_is_writable(".")) {
        if ((int)strlen(name) + 1 >= cap) return 0;
        strcpy(buf, name);
        return (int)strlen(buf);
    }

    /* 候选 2：%TEMP% */
    n = GetTempPathA((DWORD)sizeof dir, dir);
    if (n > 0 && n < sizeof dir && dir_is_writable(dir)) {
        len = (int)strlen(dir);
        if (len + (int)strlen(name) + 1 >= cap) return 0;
        strcpy(buf, dir);       /* 已确认容量 */
        strcat(buf, name);
        return (int)strlen(buf);
    }

    return 0;   /* 没有可写目录 —— 探针会显式报这个失败 */
}
static void ck_int(const char *what, int got, int want)
{
    if (got == want) { ++pass; printf("  [OK]   %s\n", what); }
    else { ++fail; printf("  [FAIL] %-52s 得到 %d，期望 %d\n", what, got, want); }
}

/* ============================================================
 * 场景 1：多线程共享一个连接（序列化模式的核心承诺）
 * ============================================================
 *
 * SQLite 在序列化模式下对连接加互斥锁，所以多个线程可以安全地
 * 用同一个 sqlite3* 做 sqlite3_exec。
 *
 * 测试方式：建一张计数表，N 个线程各自做 M 次 "UPDATE t SET n = n + 1"。
 * 若无互斥保护，这种 read-modify-write 会丢更新。
 *
 * 期望：最终 n == N * M，一次不丢。
 */
#define S1_THREADS  8
#define S1_PER      500

typedef struct {
    sqlite3 *db;
    int      done;          /* 完成计数，用于等待 */
    int      err;           /* 非零表示线程内出错 */
} s1_ctx;

static unsigned long __stdcall s1_worker(void *arg)
{
    s1_ctx *c = (s1_ctx *)arg;
    char sql[128];
    int i, rc;

    /* ⚠️ 这里**故意不用显式事务**。
     *
     * 实测踩到（见 PITFALLS P66）：在多线程共享一个连接时用
     * BEGIN IMMEDIATE / COMMIT 包住更新，会拿到
     *     SQLITE_ERROR (1): cannot start a transaction within a transaction
     *
     * 原因是 SQLite 的序列化模式保证的是**每一次 API 调用**是原子的，
     * 它**不保证跨多次调用的"事务"归某个线程独占**。线程 A 做完 BEGIN
     * 后让出锁，线程 B 也做 BEGIN，就在同一连接上开出了嵌套事务。
     *
     * 所以共享连接的正确用法是：**每个线程只发一条自身原子的话句**。
     * `UPDATE counter SET n = n + 1` 单条语句本身就是原子的，
     * 不需要外层事务。 */
    for (i = 0; i < S1_PER; i++) {
        rc = sqlite3_exec(c->db, "UPDATE counter SET n = n + 1", NULL, NULL, NULL);
        if (rc != SQLITE_OK) { c->err = rc; break; }
    }
    c->done = 1;
    return 0;
}

static void test_shared_connection(void)
{
    sqlite3 *db = NULL;
    s1_ctx ctx;
    HANDLE th[S1_THREADS];
    int i, rc, n = -1;

    printf("\n-- 场景 1：%d 线程共享一个连接（序列化模式）--\n", S1_THREADS);

    rc = sqlite3_open(":memory:", &db);
    ck("场景1 打开内存库", rc == SQLITE_OK && db != NULL);
    if (rc != SQLITE_OK) return;

    /* 内存库不支持多连接，正好用来测"共享同一连接"这一条。
     * busy_timeout 给足，避免内存库上不该有的锁冲突。 */
    sqlite3_busy_timeout(db, 20000);

    rc  = sqlite3_exec(db, "CREATE TABLE counter(n INTEGER)", NULL, NULL, NULL);
    rc |= sqlite3_exec(db, "INSERT INTO counter VALUES(0)", NULL, NULL, NULL);
    ck("场景1 建表并置初值 0", rc == SQLITE_OK);

    memset(&ctx, 0, sizeof ctx);
    ctx.db = db;

    for (i = 0; i < S1_THREADS; i++) {
        th[i] = CreateThread(NULL, 0, s1_worker, &ctx, 0, NULL);
    }
    for (i = 0; i < S1_THREADS; i++) {
        if (th[i]) { WaitForSingleObject(th[i], INFINITE); CloseHandle(th[i]); }
    }

    ck_int("场景1 无线程内错误", ctx.err, 0);

    {
        sqlite3_stmt *st = NULL;
        rc = sqlite3_prepare_v2(db, "SELECT n FROM counter", -1, &st, NULL);
        if (rc == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) {
            n = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
    }
    ck_int("场景1 计数不丢（8 线程 x 500 次 = 4000）", n, S1_THREADS * S1_PER);
    ck("场景1 完整性检查通过", sqlite3_exec(db, "PRAGMA integrity_check", NULL, NULL, NULL) == SQLITE_OK);

    sqlite3_close(db);
}

/* ============================================================
 * 场景 2：多线程各用自己的连接，写同一个**文件**库
 * ============================================================
 *
 * 这才是真实的并发写场景。SQLite 用文件锁串行化写事务，
 * 冲突时返回 SQLITE_BUSY（或设了 busy_timeout 后重试）。
 *
 * 期望：
 *   - 每个线程要么写成功，要么拿到 SQLITE_BUSY —— 不能静默丢
 *   - 最终行数 == 成功写入的次数（把两者的和做交叉校验）
 *   - 库没损坏
 */
#define S2_THREADS  4
#define S2_PER      200

typedef struct {
    const char *path;
    int  ok_count;      /* 成功写入次数 */
    int  busy_count;    /* SQLITE_BUSY 次数 */
    int  other_err;     /* 其它错误 */
    int  done;
} s2_ctx;

static unsigned long __stdcall s2_worker(void *arg)
{
    s2_ctx *c = (s2_ctx *)arg;
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;
    int i, rc;

    rc = sqlite3_open(c->path, &db);
    if (rc != SQLITE_OK) { c->other_err = rc; c->done = 1; return 0; }

    /* busy_timeout 让 SQLite 内部重试而不是立刻返回 BUSY。
     * 设 5 秒——足够，且若真的等不到会走 BUSY 分支，正好测到。 */
    sqlite3_busy_timeout(db, 5000);

    rc = sqlite3_prepare_v2(db, "INSERT INTO t(v) VALUES(?)", -1, &st, NULL);
    if (rc != SQLITE_OK) { c->other_err = rc; sqlite3_close(db); c->done = 1; return 0; }

    for (i = 0; i < S2_PER; i++) {
        sqlite3_bind_int(st, 1, i);
        rc = sqlite3_step(st);
        if (rc == SQLITE_DONE) {
            c->ok_count++;
        } else if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            c->busy_count++;      /* 明确报忙，不算丢数据 */
        } else {
            c->other_err = rc;
            break;
        }
        sqlite3_reset(st);
        sqlite3_clear_bindings(st);
    }

    sqlite3_finalize(st);
    sqlite3_close(db);
    c->done = 1;
    return 0;
}

static void test_separate_connections(void)
{
    s2_ctx ctx[S2_THREADS];
    HANDLE th[S2_THREADS];
    sqlite3 *db = NULL;
    char pathbuf[600];
    const char *path = pathbuf;
    int i, rc, total_ok = 0, total_busy = 0, total_err = 0;
    int rows = -1;

    printf("\n-- 场景 2：%d 线程各用独立连接写同一文件库 --\n", S2_THREADS);

    ck("场景2 能取到临时库路径", tmp_db_path(pathbuf, (int)sizeof pathbuf, "tcwin_probe_sqlite_mt.db") > 0);
    remove(path);

    rc = sqlite3_open(path, &db);
    ck("场景2 创建文件库", rc == SQLITE_OK && db != NULL);
    if (rc != SQLITE_OK) return;

    /* 用 WAL：写不阻塞读，且并发写表现更接近真实应用 */
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);
    rc = sqlite3_exec(db, "CREATE TABLE t(v INTEGER)", NULL, NULL, NULL);
    ck("场景2 建表", rc == SQLITE_OK);
    sqlite3_close(db);

    memset(ctx, 0, sizeof ctx);
    for (i = 0; i < S2_THREADS; i++) ctx[i].path = path;

    for (i = 0; i < S2_THREADS; i++) {
        th[i] = CreateThread(NULL, 0, s2_worker, &ctx[i], 0, NULL);
    }
    for (i = 0; i < S2_THREADS; i++) {
        if (th[i]) { WaitForSingleObject(th[i], INFINITE); CloseHandle(th[i]); }
    }

    for (i = 0; i < S2_THREADS; i++) {
        total_ok   += ctx[i].ok_count;
        total_busy += ctx[i].busy_count;
        total_err  += ctx[i].other_err;
    }
    printf("       成功 %d，BUSY %d，其它错误 %d（总尝试 %d）\n",
           total_ok, total_busy, total_err, S2_THREADS * S2_PER);

    ck_int("场景2 没有 BUSY/LOCKED 之外的错误", total_err, 0);
    ck("场景2 成功次数 + BUSY 次数 == 总尝试次数（没静默丢）",
       total_ok + total_busy == S2_THREADS * S2_PER);

    /* 交叉校验：库里的行数必须等于"成功写入次数" */
    rc = sqlite3_open(path, &db);
    if (rc == SQLITE_OK) {
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM t", -1, &st, NULL) == SQLITE_OK
            && sqlite3_step(st) == SQLITE_ROW) {
            rows = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
        sqlite3_close(db);
    }
    ck_int("场景2 库内行数 == 成功写入次数", rows, total_ok);

    /* 完整性检查：用独立连接跑，确认没损坏 */
    rc = sqlite3_open(path, &db);
    if (rc == SQLITE_OK) {
        sqlite3_stmt *st = NULL;
        const char *verdict = NULL;
        if (sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &st, NULL) == SQLITE_OK
            && sqlite3_step(st) == SQLITE_ROW) {
            verdict = (const char *)sqlite3_column_text(st, 0);
        }
        ck("场景2 完整性检查返回 ok", verdict != NULL && strcmp(verdict, "ok") == 0);
        sqlite3_finalize(st);
        sqlite3_close(db);
    } else {
        ck("场景2 完整性检查返回 ok", 0);
    }

    remove(path);

}

/* ============================================================
 * 场景 3：WAL 下读写并发 —— 读者不能看到半个事务
 * ============================================================
 *
 * WAL 的承诺：读者看到的是某个已提交版本的一致快照，不会读到
 * 写了一半的数据。
 *
 * 测试方式：一个写线程反复"插 N 行 + 提交"，若干读线程反复
 * "SELECT COUNT(*)"。行数可以跳到任意已提交值，但**必须始终是
 * 批次大小的整数倍** —— 否则说明读到了半个事务。
 */
#define S3_BATCH      10
#define S3_ROUNDS     60
#define S3_READERS    3
#define S3_READ_MAX   200000   /* 读者硬上限，防止写者异常时永久空转 */

typedef struct {
    const char *path;
    int  stop;
    int  bad_seen;      /* 非批次倍数的行数出现次数 */
    int  err;
    int  samples;
} s3_ctx;

static unsigned long __stdcall s3_writer(void *arg)
{
    s3_ctx *c = (s3_ctx *)arg;
    sqlite3 *db = NULL;
    int i, k;

    /* ⚠️ 每一条退出路径都必须置 stop=1。
     *
     * 实测踩到：原来的写法只在函数末尾置位，于是任何提前 break
     * 都会让读者线程卡在 while(!c->stop) 里**永久空转**，探针挂死。
     * 这是测试自身的 bug，不是 SQLite 的问题 —— 但现象很像死锁，
     * 见 PITFALLS P66。 */
    if (sqlite3_open(c->path, &db) != SQLITE_OK) {
        c->err = 1; c->stop = 1; return 0;
    }
    sqlite3_busy_timeout(db, 10000);

    for (i = 0; i < S3_ROUNDS; i++) {
        if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) {
            c->err = 2; goto out;
        }
        for (k = 0; k < S3_BATCH; k++) {
            if (sqlite3_exec(db, "INSERT INTO w(v) VALUES(1)", NULL, NULL, NULL) != SQLITE_OK) {
                c->err = 3; sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL); goto out;
            }
        }
        if (sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
            c->err = 4; goto out;
        }
    }

out:
    sqlite3_close(db);
    c->stop = 1;        /* 唯一的出口，保证一定置位 */
    return 0;
}

static unsigned long __stdcall s3_reader(void *arg)
{
    s3_ctx *c = (s3_ctx *)arg;
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;

    if (sqlite3_open(c->path, &db) != SQLITE_OK) { c->err = 5; return 0; }
    sqlite3_busy_timeout(db, 10000);

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM w", -1, &st, NULL) != SQLITE_OK) {
        c->err = 6; sqlite3_close(db); return 0;
    }

    /* 两个退出条件：写者置位，或到达硬上限。
     * 硬上限是必需的 —— 否则写者若异常退出而 stop 没置位，
     * 这里会永久空转（实测踩过）。 */
    while (!c->stop && c->samples < S3_READ_MAX) {
        int n;
        if (sqlite3_step(st) == SQLITE_ROW) {
            n = sqlite3_column_int(st, 0);
            c->samples++;
            /* 行数必须是批次大小的整数倍 —— 否则读到了半个事务 */
            if (n % S3_BATCH != 0) c->bad_seen++;
        } else {
            c->err = 7;
            break;
        }
        sqlite3_reset(st);
    }

    sqlite3_finalize(st);
    sqlite3_close(db);
    return 0;
}

static void test_wal_read_write(void)
{
    s3_ctx wctx, rctx[S3_READERS];
    HANDLE wr, rd[S3_READERS];
    sqlite3 *db = NULL;
    char pathbuf[600];
    const char *path = pathbuf;
    int i, total_samples = 0, total_bad = 0, total_err = 0;
    char jmode[32] = { 0 };

    printf("\n-- 场景 3：WAL 下 %d 读者 + 1 写者并发 --\n", S3_READERS);

    ck("场景3 能取到临时库路径", tmp_db_path(pathbuf, (int)sizeof pathbuf, "tcwin_probe_sqlite_mt2.db") > 0);
    remove(path);

    if (sqlite3_open(path, &db) != SQLITE_OK) { ck("场景3 创建库", 0); return; }

    {
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, "PRAGMA journal_mode=WAL", -1, &st, NULL) == SQLITE_OK
            && sqlite3_step(st) == SQLITE_ROW) {
            const char *m = (const char *)sqlite3_column_text(st, 0);
            if (m) { strncpy(jmode, m, sizeof jmode - 1); }
        }
        sqlite3_finalize(st);
    }
    ck("场景3 journal_mode 确实是 wal", strcmp(jmode, "wal") == 0);

    sqlite3_exec(db, "CREATE TABLE w(v INTEGER)", NULL, NULL, NULL);
    sqlite3_close(db);

    memset(&wctx, 0, sizeof wctx); wctx.path = path;
    for (i = 0; i < S3_READERS; i++) { memset(&rctx[i], 0, sizeof rctx[i]); rctx[i].path = path; }

    for (i = 0; i < S3_READERS; i++) {
        rd[i] = CreateThread(NULL, 0, s3_reader, &rctx[i], 0, NULL);
    }
    Sleep(20);      /* 让读者先起来 */
    wr = CreateThread(NULL, 0, s3_writer, &wctx, 0, NULL);

    if (wr) { WaitForSingleObject(wr, INFINITE); CloseHandle(wr); }
    for (i = 0; i < S3_READERS; i++) {
        if (rd[i]) { WaitForSingleObject(rd[i], INFINITE); CloseHandle(rd[i]); }
    }

    for (i = 0; i < S3_READERS; i++) {
        total_samples += rctx[i].samples;
        total_bad     += rctx[i].bad_seen;
        total_err     += rctx[i].err;
    }
    total_err += wctx.err;

    printf("       读者取样 %d 次\n", total_samples);
    ck_int("场景3 读写线程都无错误", total_err, 0);
    ck_int("场景3 从未读到半个事务（行数始终是批次倍数）", total_bad, 0);
    ck("场景3 读者确实取到了样本（测试有效）", total_samples > 0);

    /* 最终行数应等于写者完成的批次数 * 批次大小 */
    if (sqlite3_open(path, &db) == SQLITE_OK) {
        sqlite3_stmt *st = NULL;
        int rows = -1;
        if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM w", -1, &st, NULL) == SQLITE_OK
            && sqlite3_step(st) == SQLITE_ROW) {
            rows = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
        ck_int("场景3 最终行数是批次倍数", (rows >= 0 && rows % S3_BATCH == 0) ? 1 : 0, 1);
        printf("       最终行数 %d\n", rows);
        sqlite3_close(db);
    }

    remove(path);
}

/* ============================================================
 * 场景 4：线程模式声明是否与实际一致
 * ============================================================ */
static void test_thread_mode_report(void)
{
    printf("\n-- 场景 4：线程模式声明 --\n");

    ck_int("sqlite3_threadsafe() == 1（THREADSAFE=1，即序列化模式）",
           sqlite3_threadsafe(), 1);
    ck_int("编译选项里明确列出 THREADSAFE=1（不是默认值）",
           sqlite3_compileoption_used("THREADSAFE=1"), 1);
    ck_int("编译选项里列出 MUTEX_W32（Windows 互斥实现）",
           sqlite3_compileoption_used("MUTEX_W32"), 1);
    ck_int("没有关掉互斥（THREADSAFE=0 不存在）",
           sqlite3_compileoption_used("THREADSAFE=0"), 0);

    /* 上面的场景 1/2/3 就是"声明与实际是否一致"的证据 */
    printf("       场景 1/2/3 的通过即为「声明与实际一致」的证据\n");
}

/* ============================================================
 * 场景 5：把「嵌套事务」这个坑固化成断言
 * ============================================================
 *
 * 场景 1 的价值不只是"计数不丢"，还在于它**排除了一个错误用法**。
 * 最初场景 1 写成了每个线程 BEGIN IMMEDIATE / UPDATE / COMMIT，
 * 结果 8 个线程里只有 3 个跑完，其余立刻拿到：
 *
 *     SQLITE_ERROR (1): cannot start a transaction within a transaction
 *
 * 原因是 SQLite 的**序列化模式保护的是每一次 API 调用**，
 * 而不是"跨多次调用的事务归某线程独占"。线程 A 做完 BEGIN 让出锁，
 * 线程 B 也做 BEGIN，就在同一连接上开出了嵌套事务。
 *
 * 这里把这个行为测出来并断言，避免以后有人又踩：
 *   - 共享连接 + 单条原子语句         -> 安全
 *   - 共享连接 + 显式事务（无外部锁） -> 会报 SQLITE_ERROR
 * ------------------------------------------------------------ */
static void test_nested_transaction_hazard(void)
{
    sqlite3 *db = NULL;
    int rc, rc2;

    printf("\n-- 场景 5：共享连接上显式事务的陷阱 --\n");

    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        ck("场景5 打开内存库", 0);
        return;
    }
    sqlite3_exec(db, "CREATE TABLE t(v INTEGER)", NULL, NULL, NULL);

    /* 第一次 BEGIN 应该成功 */
    rc = sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL);
    ck_int("场景5 第一次 BEGIN IMMEDIATE 成功", rc, SQLITE_OK);

    /* 同一连接上第二次 BEGIN 必然失败 —— 这就是多线程共享连接时
     * 会撞到的东西（另一个线程"插进来"再 BEGIN） */
    rc2 = sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL);
    ck("场景5 同一连接上第二次 BEGIN 报错（另一个线程会撞到这里）",
       rc2 != SQLITE_OK);

    ck_int("场景5 错误码是 SQLITE_ERROR(1)", rc2, SQLITE_ERROR);
    ck("场景5 错误消息含 within a transaction",
       sqlite3_errmsg(db) != NULL &&
       strstr(sqlite3_errmsg(db), "within a transaction") != NULL);

    /* 收尾：回滚掉那个还开着的事务，再确认能正常继续用 */
    sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    rc = sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL);
    ck_int("场景5 回滚后 BEGIN 又能成功", rc, SQLITE_OK);
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);

    /* 对照：单条语句不需要 BEGIN，自身原子，永远不会有这个坑 */
    rc = sqlite3_exec(db, "INSERT INTO t VALUES(1)", NULL, NULL, NULL);
    ck_int("场景5 对照：单条 INSERT（无显式事务）成功", rc, SQLITE_OK);

    sqlite3_close(db);
}

int main(void)
{
    SetConsoleOutputCP(CP_UTF8);

    printf("=== probe_sqlite_mt: SQLite 并发压测 ===\n");
    printf("SQLite %s\n", sqlite3_libversion());

    test_thread_mode_report();
    test_shared_connection();
    test_separate_connections();
    test_wal_read_write();
    test_nested_transaction_hazard();

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
