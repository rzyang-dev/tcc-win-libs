/* msvcrt_compat.h - 声明 msvcrt_compat.c 提供的补充函数
 *
 * TCC 的 string.h / stdlib.h 里没有 strnlen 的原型，
 * 不声明会得到 "implicit declaration" 警告（返回值按 int 处理，
 * 对 size_t 返回值是错误的）。用 -include 或直接包含本头文件。
 */
#ifndef MSVCRT_COMPAT_H
#define MSVCRT_COMPAT_H

#include <stdlib.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

float strtof(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
size_t strnlen(const char *s, size_t maxlen);

#ifdef __cplusplus
}
#endif
#endif /* MSVCRT_COMPAT_H */