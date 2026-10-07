# build-lua.ps1 - 构建静态 Lua 解释器，或把 Lua 脚本打包成单文件 exe
#
# 三种产物：
#   1. 静态解释器   scripts\build-lua.ps1 -Interpreter
#        -> bin\slua.exe   单文件，零依赖（官方 lua55.exe 需要 lua55.dll）
#   2. 脚本打包器   scripts\build-lua.ps1 -Script app.lua
#        -> app.exe        把脚本嵌进 exe，双击即用
#   3. 字节码打包   scripts\build-lua.ps1 -Script app.lua -Bytecode
#        -> app.exe        预编译成字节码再嵌入，启动快 8~10 倍
#
# 为什么需要 -Bytecode：
#   Lua 源码解析约 6.7 us/行。实测进程启动时间：
#     10000 行脚本：源码嵌入 222 ms  ->  字节码嵌入 49 ms
#     5000  行脚本：源码嵌入 116 ms  ->  字节码嵌入 34 ms
#     52    行脚本：源码嵌入  21 ms  ->  字节码嵌入 20 ms（无差别）
#   经验阈值：脚本 < 500 行不必用字节码；> 1000 行应当用。
#
# 嵌入脚本后可用的全局表 C（需 -With 库）：
#   C.json_encode(t) / C.json_decode(s)      需 cjson
#   C.gzip_compress(s) / C.gzip_decompress(s) / C.crc32(s)   需 miniz

param(
    [string]$Script,
    [string]$Out,
    [switch]$Interpreter,
    [switch]$Bytecode,
    [switch]$32,
    [string[]]$With = @(),
    [switch]$AutoMath
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Tcc  = Join-Path $Root 'toolchain\tcc\tcc.exe'
$Luac = Join-Path $Root 'toolchain\lua-bin\Win64\luac55.exe'
$LuaSrc = Join-Path $Root 'toolchain\lua-src'
$Bin  = Join-Path $Root 'bin'
New-Item -ItemType Directory -Force -Path $Bin | Out-Null

if ($32) {
    $Tcc  = Join-Path $Root 'toolchain\tcc\i386-win32-tcc.exe'
    $Luac = Join-Path $Root 'toolchain\lua-bin\Win32\luac55.exe'
}

# Lua 核心源文件（排除三个有独立 main 的）
$core = Get-ChildItem "$LuaSrc\*.c" |
        Where-Object { $_.Name -notin @('lua.c','luac.c','wmain.c') } |
        ForEach-Object { $_.FullName }

$inc = @(
    '-I', (Join-Path $Root 'include'),
    '-I', (Join-Path $Root 'include\lua'),
    '-I', $LuaSrc,
    '-L', (Join-Path $Root 'lib\tccdefs'),
    '-D_WIN32_WINNT=0x0601'
)

if ($Interpreter) {
    $out = if ($Out) { $Out } else { Join-Path $Bin 'slua.exe' }
    $args = @($inc) + @('-o', $out, (Join-Path $LuaSrc 'lua.c')) + $core
    Write-Host "[build-lua] 静态解释器 -> $out"
    & $Tcc @args
    if ($LASTEXITCODE -ne 0) { throw "编译失败" }
    Write-Host "[build-lua] 完成：$((Get-Item $out).Length) 字节，零外部依赖" -ForegroundColor Green
    return
}

if (-not $Script) {
    Write-Host "用法:"
    Write-Host "  scripts\build-lua.ps1 -Interpreter              构建静态 lua.exe"
    Write-Host "  scripts\build-lua.ps1 -Script app.lua           脚本 -> 单文件 exe"
    Write-Host "  scripts\build-lua.ps1 -Script app.lua -Bytecode 预编译字节码（脚本大时用）"
    Write-Host "  scripts\build-lua.ps1 -Script app.lua -With cjson,miniz"
    exit 2
}
if (-not (Test-Path $Script)) { throw "找不到脚本 $Script" }

$base = [System.IO.Path]::GetFileNameWithoutExtension($Script)
$out  = if ($Out) { $Out } else { Join-Path $Bin "$base.exe" }
$tmp  = Join-Path $Root 'tmp' | Join-Path -ChildPath ("tcwin_luapack_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$embedSrc = Join-Path $tmp 'embed.c'
$embedArr = Join-Path $tmp 'payload.c'

# 1) 生成载荷：字节码或源码
if ($Bytecode) {
    $luacOut = Join-Path $tmp 'payload.luac'
    & $Luac -s -o $luacOut $Script
    if ($LASTEXITCODE -ne 0) { throw "luac 编译失败" }
    & (Join-Path $Root 'helper\luac2c.exe') $luacOut $embedArr tool_payload | Out-Null
    Write-Host "[build-lua] 字节码: $((Get-Item $luacOut).Length) 字节 (源 $((Get-Item $Script).Length) 字节)"
    $payloadDecl = "extern const unsigned char tool_payload[];`nextern const unsigned int tool_payload_len;"
    $payloadUse  = 'luaL_loadbuffer(L, (const char *)tool_payload, tool_payload_len, "=app")'
} else {
    & (Join-Path $Root 'helper\txt2c.exe') $Script $embedArr tool_payload | Out-Null
    Write-Host "[build-lua] 源码嵌入: $((Get-Item $Script).Length) 字节"
    $payloadDecl = "extern const char tool_payload[];`nextern const unsigned int tool_payload_len;"
    $payloadUse  = 'luaL_loadbuffer(L, tool_payload, tool_payload_len, "=app")'
}

# 2) 生成宿主 C 源码
$c = @"
/* 由 build-lua.ps1 生成 - 请勿手工编辑 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

$payloadDecl

/* --- 可选的 C 库绑定 --- */
#ifdef WITH_CJSON
#ifndef CJSON_HIDE_SYMBOLS
#define CJSON_HIDE_SYMBOLS       /* TCC 无法解析原型上的 __declspec */
#endif
#include "cJSON.h"
static void j2lua(lua_State *L, const cJSON *j);
static void j2lua(lua_State *L, const cJSON *j) {
    if (!j) { lua_pushnil(L); return; }
    switch (j->type & 0xFF) {
        case cJSON_NULL:   lua_pushnil(L); break;
        case cJSON_False:  lua_pushboolean(L, 0); break;
        case cJSON_True:   lua_pushboolean(L, 1); break;
        case cJSON_Number: lua_pushnumber(L, j->valuedouble); break;
        case cJSON_String: lua_pushstring(L, j->valuestring); break;
        case cJSON_Array: {
            lua_newtable(L); int i = 0; const cJSON *e;
            cJSON_ArrayForEach(e, j) { j2lua(L, e); lua_rawseti(L, -2, ++i); }
            break;
        }
        default: {
            lua_newtable(L); const cJSON *e;
            cJSON_ArrayForEach(e, j) { j2lua(L, e); lua_setfield(L, -2, e->string); }
            break;
        }
    }
}
static int l_json_decode(lua_State *L) {
    cJSON *j = cJSON_Parse(luaL_checkstring(L, 1));
    if (!j) { lua_pushnil(L); lua_pushstring(L, "json parse error"); return 2; }
    j2lua(L, j); cJSON_Delete(j); return 1;
}
static int l_json_encode(lua_State *L) {
    /* 简化：只处理标量表的编码，够配置/清单类用途 */
    luaL_checktype(L, 1, LUA_TTABLE);
    cJSON *j = cJSON_CreateObject();
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        const char *k = lua_tostring(L, -2);
        if (k) {
            switch (lua_type(L, -1)) {
                case LUA_TNUMBER:  cJSON_AddNumberToObject(j, k, lua_tonumber(L, -1)); break;
                case LUA_TBOOLEAN: cJSON_AddBoolToObject(j, k, lua_toboolean(L, -1)); break;
                case LUA_TSTRING:  cJSON_AddStringToObject(j, k, lua_tostring(L, -1)); break;
                default: break;
            }
        }
        lua_pop(L, 1);
    }
    char *s = cJSON_PrintUnformatted(j);
    lua_pushstring(L, s ? s : "{}");
    cJSON_free(s); cJSON_Delete(j);
    return 1;
}
#endif

#ifdef WITH_MINIZ
#include "miniz.h"
static int l_gzip_compress(lua_State *L) {
    size_t n; const char *s = luaL_checklstring(L, 1, &n);
    mz_ulong cap = mz_compressBound((mz_ulong)n);
    unsigned char *o = (unsigned char *)malloc(cap);
    mz_ulong ol = cap;
    int st = mz_compress(o, &ol, (const unsigned char *)s, (mz_ulong)n);
    if (st != MZ_OK) { free(o); return luaL_error(L, "compress failed: %d", st); }
    lua_pushlstring(L, (const char *)o, ol); free(o); return 1;
}
static int l_gzip_decompress(lua_State *L) {
    size_t n; const char *s = luaL_checklstring(L, 1, &n);
    /* 输出大小未知：流式 inflate 分块扩容。
     * 固定倍数缓冲是错的：压缩率可达 0.8%%，会严重低估。 */
    mz_stream st; memset(&st, 0, sizeof st);
    if (mz_inflateInit(&st) != MZ_OK) return luaL_error(L, "inflateInit failed");
    size_t cap = (n < 256 ? 4096 : n * 8), used = 0;
    unsigned char *buf = (unsigned char *)malloc(cap);
    st.next_in = (const unsigned char *)s; st.avail_in = (unsigned)n;
    int r;
    do {
        if (used == cap) { cap *= 2; buf = (unsigned char *)realloc(buf, cap); }
        st.next_out = buf + used; st.avail_out = (unsigned)(cap - used);
        r = mz_inflate(&st, MZ_NO_FLUSH);
        used = cap - st.avail_out;
        if (r != MZ_OK && r != MZ_STREAM_END && r != MZ_BUF_ERROR) {
            mz_inflateEnd(&st); free(buf);
            return luaL_error(L, "inflate failed: %d", r);
        }
    } while (r != MZ_STREAM_END);
    mz_inflateEnd(&st);
    lua_pushlstring(L, (const char *)buf, used); free(buf); return 1;
}
static int l_crc32(lua_State *L) {
    size_t n; const char *s = luaL_checklstring(L, 1, &n);
    lua_pushinteger(L, (lua_Integer)mz_crc32(MZ_CRC32_INIT, (const unsigned char *)s, (mz_ulong)n));
    return 1;
}
#endif

static const luaL_Reg ext_libs[] = {
#ifdef WITH_CJSON
    {"json_decode", l_json_decode},
    {"json_encode", l_json_encode},
#endif
#ifdef WITH_MINIZ
    {"gzip_compress", l_gzip_compress},
    {"gzip_decompress", l_gzip_decompress},
    {"crc32", l_crc32},
#endif
    {NULL, NULL}
};

int main(int argc, char **argv) {
    lua_State *L = luaL_newstate();
    if (!L) { fprintf(stderr, "lua_State 创建失败\n"); return 1; }
    luaL_openlibs(L);

    lua_newtable(L);
    for (const luaL_Reg *r = ext_libs; r->name; r++) {
        lua_pushcfunction(L, r->func);
        lua_setfield(L, -2, r->name);
    }
    lua_setglobal(L, "C");

    /* Lua 约定：arg[1] 是第一个参数，不是 exe 路径 */
    lua_newtable(L);
    for (int i = 1; i < argc; i++) {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i);
    }
    lua_setglobal(L, "arg");

    if ($payloadUse != LUA_OK) {
        fprintf(stderr, "脚本加载失败: %s\n", lua_tostring(L, -1));
        lua_close(L); return 1;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "运行错误: %s\n", lua_tostring(L, -1));
        lua_close(L); return 1;
    }
    lua_close(L);
    return 0;
}
"@
[System.IO.File]::WriteAllText($embedSrc, $c, (New-Object System.Text.UTF8Encoding($false)))

# 3) 组装编译命令
$defs = @('-D_WIN32_WINNT=0x0601')
$extraSrc = @()
# 支持 -With cjson,miniz 与 -With cjson -With miniz 两种写法
$withFlat = @()
foreach ($w in $With) { foreach ($part in ([string]$w -split ',')) { if ($part.Trim()) { $withFlat += $part.Trim() } } }

foreach ($w in $withFlat) {
    switch ($w.ToLower()) {
        'cjson' {
            $defs += '-DWITH_CJSON'; $defs += '-DCJSON_HIDE_SYMBOLS'
            $inc += @('-I', (Join-Path $Root 'lib\cjson'))
            $extraSrc += (Join-Path $Root 'lib\cjson\cJSON.c')
        }
        'miniz' {
            $defs += '-DWITH_MINIZ'
            $inc += @('-I', (Join-Path $Root 'lib\miniz'))
            $extraSrc += (Join-Path $Root 'lib\miniz\miniz.c')
            $extraSrc += (Join-Path $Root 'lib\miniz\miniz_tdef.c')
            $extraSrc += (Join-Path $Root 'lib\miniz\miniz_tinfl.c')
            $extraSrc += (Join-Path $Root 'lib\miniz\miniz_zip.c')
        }
        default { Write-Warning "打包器暂不支持库 '$w'（只有 cjson 和 miniz）" }
    }
}

$all = @($inc) + $defs + @('-o', $out, $embedSrc, $embedArr) + $extraSrc + $core
Write-Host "[build-lua] 编译 -> $out"
& $Tcc @all
if ($LASTEXITCODE -ne 0) { throw "编译失败" }
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
Write-Host "[build-lua] 完成：$((Get-Item $out).Length) 字节，单文件零依赖" -ForegroundColor Green