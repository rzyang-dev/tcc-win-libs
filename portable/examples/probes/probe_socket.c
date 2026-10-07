/* socket 能力验证：真实 TCP + UDP 收发 + DNS + select + 错误路径
 *
 * 这是 P1 网络能力的核心验收测试 —— 不能只验证"能编译"。
 * 必须真实建立连接、收发数据、正确关闭。
 *
 * 包含顺序（重要）：
 *   tccwin.h   先 —— 引入 <windows.h>，类型体系就位
 *   winsock2.h 后 —— 只用已存在的类型，不重复定义
 *
 * 编译： scripts\tcc.cmd examples\probes\probe_socket.c -With socket -Run
 */
#include "tccwin.h"
#include <winsock2.h>       /* 官方头（原自写垫片已改为转发）*/
#include <ws2tcpip.h>      /* sockaddr_in6 / addrinfo / getaddrinfo */
#include <stdio.h>
#include <string.h>

static int pass = 0, fail = 0;
#define CK(d, c) do { if (c) { printf("  [OK]   %s\n", d); pass++; } \
                      else   { printf("  [FAIL] %s (WSA err=%d)\n", d, WSAGetLastError()); fail++; } } while (0)

/* ---------- 结构体布局自检（Windows x64 ABI 真实值，已与 MSDN 交叉核对）---------- */
static void check_layout(void) {
    printf("-- 结构体布局 --\n");
    CK("sizeof(sockaddr) == 16",          sizeof(struct sockaddr) == 16);
    CK("sizeof(sockaddr_in) == 16",       sizeof(struct sockaddr_in) == 16);
    CK("sizeof(sockaddr_in6) == 28",      sizeof(struct sockaddr_in6) == 28);
    CK("sizeof(sockaddr_storage) == 128", sizeof(struct sockaddr_storage) == 128);
    /* 尺寸按位宽分流 —— 实测（不加 -I，直接编）：
     *   32 位: WSADATA=400  fd_set=260
     *   64 位: WSADATA=408  fd_set=520
     * 两者都与 MSDN 一致。原来是硬编码 64 位值，导致 32 位下误报失败。
     * WSADATA: 4 个 WORD(8) + ptr + 257 + 129，按指针对齐
     * fd_set  : u_int(4) + SOCKET[64]，按指针对齐 */
    CK("sizeof(WSADATA) 与位宽相符",
       sizeof(WSADATA) == (sizeof(void *) == 8 ? 408 : 400));
    CK("sizeof(fd_set) 与位宽相符",
       sizeof(fd_set) == (sizeof(void *) == 8 ? 520 : 260));
    CK("sizeof(timeval) == 8",            sizeof(struct timeval) == 8);
    CK("offsetof(sin_port) == 2",
       (int)((char *)&((struct sockaddr_in *)0)->sin_port - (char *)0) == 2);
    CK("offsetof(sin_addr) == 4",
       (int)((char *)&((struct sockaddr_in *)0)->sin_addr - (char *)0) == 4);
    CK("offsetof(sin6_addr) == 8",
       (int)((char *)&((struct sockaddr_in6 *)0)->sin6_addr - (char *)0) == 8);
    printf("\n");
}

/* ---------- TCP 服务端线程 ---------- */
static SOCKET  g_listenSock = INVALID_SOCKET;
static USHORT  g_port = 0;
static int     g_srvAccepted = 0;
static const char *REPLY = "PONG-from-server";

static DWORD WINAPI server_thread(LPVOID arg) {
    (void)arg;
    struct sockaddr_in sa;
    int salen = sizeof sa;
    SOCKET c = accept(g_listenSock, (struct sockaddr *)&sa, &salen);
    if (c == INVALID_SOCKET) return 1;
    g_srvAccepted = 1;
    char buf[256];
    int n = recv(c, buf, sizeof buf - 1, 0);
    if (n > 0) {
        buf[n] = 0;
        printf("  [srv] 收到 %d 字节: '%s'\n", n, buf);
        send(c, REPLY, (int)strlen(REPLY), 0);
    }
    shutdown(c, SD_BOTH);
    closesocket(c);
    return 0;
}

/* ---------- UDP 服务端线程 ---------- */
static SOCKET g_udpSock = INVALID_SOCKET;
static DWORD WINAPI udp_server(LPVOID arg) {
    (void)arg;
    struct sockaddr_in from;
    int flen = sizeof from;
    char buf[256];
    int n = recvfrom(g_udpSock, buf, sizeof buf - 1, 0,
                     (struct sockaddr *)&from, &flen);
    if (n > 0) {
        buf[n] = 0;
        printf("  [udp-srv] 收到 %d 字节: '%s'\n", n, buf);
        sendto(g_udpSock, "UDP-ACK", 7, 0, (struct sockaddr *)&from, flen);
    }
    return 0;
}

int main(void) {
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);    printf("=== socket 能力验证 ===\n\n");
    check_layout();

    /* ---------- WSAStartup ---------- */
    printf("-- WSAStartup --\n");
    WSADATA wsa;
    memset(&wsa, 0, sizeof wsa);
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    CK("WSAStartup(2.2) 成功", rc == 0);
    printf("       版本 %d.%d, 描述: %.40s\n",
           wsa.wVersion & 0xFF, (wsa.wVersion >> 8) & 0xFF, wsa.szDescription);
    CK("协商到版本 2.2",
       (wsa.wVersion & 0xFF) == 2 && ((wsa.wVersion >> 8) & 0xFF) == 2);
    printf("\n");

    /* ---------- TCP 回环 ---------- */
    printf("-- TCP 回环（真实建立连接、收发数据）--\n");
    g_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CK("socket(AF_INET, SOCK_STREAM)", g_listenSock != INVALID_SOCKET);

    int yes = 1;
    setsockopt(g_listenSock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* 127.0.0.1，避开防火墙 */
    addr.sin_port = 0;                                /* 让系统分配端口 */
    CK("bind(127.0.0.1:0)",
       bind(g_listenSock, (struct sockaddr *)&addr, sizeof addr) == 0);

    int alen = sizeof addr;
    CK("getsockname 取回实际端口",
       getsockname(g_listenSock, (struct sockaddr *)&addr, &alen) == 0);
    g_port = ntohs(addr.sin_port);
    printf("       监听端口 %u\n", (unsigned)g_port);
    CK("分配的端口非 0", g_port != 0);

    CK("listen(backlog=4)", listen(g_listenSock, 4) == 0);

    HANDLE srv = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);

    SOCKET cli = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CK("客户端 socket", cli != INVALID_SOCKET);

    struct sockaddr_in caddr;
    memset(&caddr, 0, sizeof caddr);
    caddr.sin_family = AF_INET;
    caddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    caddr.sin_port = htons(g_port);
    CK("connect 到服务端",
       connect(cli, (struct sockaddr *)&caddr, sizeof caddr) == 0);

    const char *msg = "PING-from-client";
    int sent = send(cli, msg, (int)strlen(msg), 0);
    CK("send 发送全部字节", sent == (int)strlen(msg));

    char rbuf[256];
    memset(rbuf, 0, sizeof rbuf);
    int got = recv(cli, rbuf, sizeof rbuf - 1, 0);
    CK("recv 收到服务端回复", got > 0);
    printf("  [cli] 收到 %d 字节: '%s'\n", got, rbuf);
    CK("回复内容逐字节正确", strcmp(rbuf, REPLY) == 0);

    shutdown(cli, SD_BOTH);
    closesocket(cli);
    WaitForSingleObject(srv, 5000);
    CloseHandle(srv);
    CK("服务端确实接受过连接", g_srvAccepted == 1);
    closesocket(g_listenSock);
    printf("\n");

    /* ---------- 错误路径 ---------- */
    printf("-- 错误路径 --\n");
    SOCKET bad = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in badaddr;
    memset(&badaddr, 0, sizeof badaddr);
    badaddr.sin_family = AF_INET;
    badaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    badaddr.sin_port = htons(1);        /* 端口 1 通常无监听 */
    int brc = connect(bad, (struct sockaddr *)&badaddr, sizeof badaddr);
    int berr = WSAGetLastError();
    printf("       连接 127.0.0.1:1 返回 %d, WSA 错误 %d\n", brc, berr);
    CK("连接无监听端口失败", brc == SOCKET_ERROR);
    CK("错误码是预期的拒绝/超时类",
       berr == WSAECONNREFUSED || berr == WSAETIMEDOUT || berr == WSAEHOSTUNREACH);
    closesocket(bad);
    printf("\n");

    /* ---------- UDP ---------- */
    printf("-- UDP 回环 --\n");
    g_udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    CK("UDP socket", g_udpSock != INVALID_SOCKET);

    struct sockaddr_in uaddr;
    memset(&uaddr, 0, sizeof uaddr);
    uaddr.sin_family = AF_INET;
    uaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    uaddr.sin_port = 0;
    CK("UDP bind", bind(g_udpSock, (struct sockaddr *)&uaddr, sizeof uaddr) == 0);
    int ulen = sizeof uaddr;
    getsockname(g_udpSock, (struct sockaddr *)&uaddr, &ulen);
    USHORT uport = ntohs(uaddr.sin_port);
    printf("       UDP 端口 %u\n", (unsigned)uport);

    HANDLE usrv = CreateThread(NULL, 0, udp_server, NULL, 0, NULL);

    SOCKET ucli = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in to;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    to.sin_port = htons(uport);
    CK("sendto 发送 9 字节",
       sendto(ucli, "UDP-HELLO", 9, 0, (struct sockaddr *)&to, sizeof to) == 9);

    char ubuf[256];
    memset(ubuf, 0, sizeof ubuf);
    struct sockaddr_in from;
    int flen2 = sizeof from;
    int un = recvfrom(ucli, ubuf, sizeof ubuf - 1, 0, (struct sockaddr *)&from, &flen2);
    CK("recvfrom 收到回复", un > 0);
    printf("  [ucli] 收到: '%s' 来自端口 %u\n", ubuf, (unsigned)ntohs(from.sin_port));
    CK("UDP 回复内容正确", strcmp(ubuf, "UDP-ACK") == 0);

    closesocket(ucli);
    WaitForSingleObject(usrv, 5000);
    CloseHandle(usrv);
    closesocket(g_udpSock);
    printf("\n");

    /* ---------- select ---------- */
    printf("-- select --\n");
    SOCKET s1 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(s1, &rf);
    CK("FD_ISSET 在 FD_SET 后为真", FD_ISSET(s1, &rf));
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 1000;      /* 1 ms，避免久等 */
    int sel = select(0, &rf, NULL, NULL, &tv);
    printf("       select 返回 %d（0 = 超时，无数据，符合预期）\n", sel);
    CK("select 无数据时超时返回 0", sel == 0);
    FD_CLR(s1, &rf);
    CK("FD_CLR 后 FD_ISSET 为假", !FD_ISSET(s1, &rf));
    closesocket(s1);
    printf("\n");

    /* ---------- DNS ---------- */
    printf("-- getaddrinfo --\n");
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int gai = getaddrinfo("localhost", "80", &hints, &res);
    CK("getaddrinfo(\"localhost\", \"80\")", gai == 0 && res != NULL);
    if (res) {
        char ip[64];
        inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, ip, sizeof ip);
        printf("       localhost -> %s\n", ip);
        CK("解析出非空地址", strlen(ip) > 0);
        freeaddrinfo(res);
    }
    printf("\n");

    /* ---------- 地址转换 ---------- */
    printf("-- 地址转换 --\n");
    struct in_addr ia;
    CK("inet_pton 解析 1.2.3.4", inet_pton(AF_INET, "1.2.3.4", &ia) == 1);
    char back[64];
    CK("inet_ntop 转回字符串",
       inet_ntop(AF_INET, &ia, back, sizeof back) != NULL &&
       strcmp(back, "1.2.3.4") == 0);
    printf("       往返: 1.2.3.4 -> 0x%08lX -> %s\n", (unsigned long)ia.s_addr, back);
    printf("\n");

    /* ---------- WSACleanup ---------- */
    printf("-- WSACleanup --\n");
    CK("WSACleanup 成功", WSACleanup() == 0);

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail ? 1 : 0;
}
