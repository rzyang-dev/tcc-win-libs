# tcc.ps1 - 便携开发环境的编译入口
#
# 目的：把 TCC 直接调用时的所有坑（include 顺序、缺导入库、不能加 -lm 等）
#       全部封装掉，使用者（包括 agent）只需要给源文件。
#
# 用法：
#   scripts\tcc.cmd hello.c                    编译成 hello.exe
#   scripts\tcc.cmd hello.c -o out.exe         指定输出
#   scripts\tcc.cmd hello.c -With cjson,tinydir
#   scripts\tcc.cmd hello.c -Run               编译后立即运行
#   scripts\tcc.cmd hello.c -32                编译 32 位
#   scripts\tcc.cmd -List                      列出可用库
#   scripts\tcc.cmd -ShowFlags app.c -With miniz
#
# 可用库：cjson stb_ds stb_sprintf jsmn utf8 tinydir xxhash sha256
#         log.c argparse miniz inih tinyexpr lua
#
# 实现说明：不使用 param() 绑定。PowerShell 会把 -o 解析成 -Output/-Obj 的
# 歧义缩写、把 -l 解析成 -LiteralPath，导致常规编译命令无法透传。
# 这里手工遍历 $args，未知参数原样交给 TCC。

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot      # portable/

$LIBDEF = [ordered]@{
    'cjson'       = @{ inc='lib\cjson';    src=@('lib\cjson\cJSON.c');                                   defs=@('CJSON_HIDE_SYMBOLS'); usage='JSON 解析/生成' }
    'stb_ds'      = @{ inc='include';      src=@();                                                      defs=@('STB_DS_IMPLEMENTATION'); usage='动态数组+哈希表' }
    'stb_sprintf' = @{ inc='include';      src=@();                                                      defs=@('STB_SPRINTF_IMPLEMENTATION'); usage='可靠 snprintf（支持千分位/%b）' }
    'jsmn'        = @{ inc='include';      src=@();                                                      defs=@();                     usage='极简 JSON 分词器' }
    'utf8'        = @{ inc='include';      src=@();                                                      defs=@();                     usage='Unicode 码点操作与校验' }
    'tinydir'     = @{ inc='include';      src=@();                                                      defs=@();                     usage='目录遍历（已补 Windows 文件大小）' }
    'xxhash'      = @{ inc='include';      src=@();                                                      defs=@('XXH_NO_XXH3','XXH_STATIC_LINKING_ONLY','XXH_IMPLEMENTATION'); usage='XXH32/XXH64；已关 XXH3（TCC 无 SSE2）' }
    'sha256'      = @{ inc='include';      src=@();                                                      defs=@('LONESHA256_IMPLEMENTATION'); usage='SHA-256（头文件名 lonesha256.h）' }
    'log.c'       = @{ inc='lib\log.c';    src=@('lib\log.c\log.c');                                     defs=@();                     usage='带级别/颜色的日志' }
    'argparse'    = @{ inc='lib\argparse'; src=@('lib\argparse\argparse.c','helper\msvcrt_compat.c'); defs=@();               usage='命令行解析（附带 strtof 垫片）' }
    'miniz'       = @{ inc='lib\miniz';    src=@('lib\miniz\miniz.c','lib\miniz\miniz_tdef.c','lib\miniz\miniz_tinfl.c','lib\miniz\miniz_zip.c'); defs=@(); usage='DEFLATE/ZIP/CRC32（需要全部 4 个 .c）' }
    'inih'        = @{ inc='lib\inih';     src=@('lib\inih\ini.c');                                      defs=@();                     usage='.ini 配置解析' }
    'tinyexpr'    = @{ inc='lib\tinyexpr'; src=@('lib\tinyexpr\tinyexpr.c');                             defs=@();                     usage='数学表达式求值' }
    'lua'         = @{ inc='include\lua';  src=@();                                                      defs=@();                     usage='Lua 5.5 头文件（嵌入用）' }
    'stb_image'   = @{ inc='include';      src=@();                                                      defs=@('STB_IMAGE_IMPLEMENTATION','STBI_NO_SIMD');       usage='图片解码 PNG/JPG/BMP/TGA/GIF/PSD/HDR；TCC 无 SIMD 已自动关' }
    'stb_image_write' = @{ inc='include';  src=@();                                                      defs=@('STB_IMAGE_WRITE_IMPLEMENTATION');                     usage='图片编码 PNG/BMP/TGA/JPG/HDR' }
    'stb_rect_pack'   = @{ inc='include';  src=@();                                                      defs=@('STB_RECT_PACK_IMPLEMENTATION');                     usage='矩形打包（图集/纹理）' }
    'stb_c_lexer'     = @{ inc='include';  src=@();                                                      defs=@('STB_C_LEXER_IMPLEMENTATION');                     usage='C 风格词法分析器' }
    'linmath'     = @{ inc='include';      src=@();                                                      defs=@();                     usage='向量/矩阵/四元数（配 tccmath.h）' }
    'toml'        = @{ inc='lib\toml';     src=@('lib\toml\toml.c','helper\msvcrt_compat.c');                                      defs=@();                     usage='TOML 配置解析（tomlc99）' }
    'utf8io'      = @{ inc='include';      src=@();                                                      defs=@();                     usage='UTF-8 输出常量（GBK 控制台适配，纯头文件）' }
    'readline'    = @{ inc='include;lib\readline'; src=@('lib\readline\tcwinreadline.c');                          defs=@();                     usage='终端行编辑器（自研，分两层可测）' }
    'sqlite'      = @{ inc='lib\sqlite';  src=@('lib\sqlite\sqlite3_tcc.c');                              defs=@();                     usage='SQLite 3.38.2 嵌入式数据库（公有领域，必须用包装文件）' }
    'regex'       = @{ inc='lib\regex';  src=@('lib\regex\re.c');                                      defs=@();                     usage='正则表达式（tiny-regex-c，公有领域）' }
    'nuklear'     = @{ inc='include';      src=@();                                                      defs=@(); libs=@('gdi32','user32'); usage='GUI 控件库（即时模式，含内置字体）' }
    'socket'      = @{ inc='include';      src=@();                                                      defs=@(); libs=@('ws2_32');   usage='TCP/UDP socket + DNS（官方 winsock2.h/ws2tcpip.h）' }
    'dirent'      = @{ inc='include';      src=@('helper\dirent_compat.c');                              defs=@();                     usage='POSIX 目录遍历（补 TCC 声明了但链不上的 opendir/readdir 等）' }
    'unistd'      = @{ inc='include';      src=@('helper\unistd_compat.c');                              defs=@();                     usage='补 useconds_t 让 <unistd.h> 能解析，并实现 usleep' }
    'time'        = @{ inc='include';      src=@('helper\time_compat.c');                                defs=@();                     usage='实现 gettimeofday / mingw_gettimeofday / _ftime64（TCC 声明了但链不上）' }
    'math'        = @{ inc='include';      src=@('helper\math_compat.c','helper\msvcrt_compat.c');      defs=@();                     usage='float 数学补全：modff 正确实现（宏会越界写）+ cbrt/cbrtf/strnlen 等' }
}

$sources   = New-Object System.Collections.Generic.List[string]
$withList  = New-Object System.Collections.Generic.List[string]
$extra     = New-Object System.Collections.Generic.List[string]
$output    = $null
$doRun     = $false
$is32      = $false
$showFlags = $false
$listOnly  = $false

$i = 0
while ($i -lt $args.Count) {
    $a = [string]$args[$i]
    if     ($a -match '^(?i)-List$')      { $listOnly  = $true; $i++; continue }
    elseif ($a -match '^(?i)-ShowFlags$') { $showFlags = $true; $i++; continue }
    elseif ($a -match '^(?i)-Run$')       { $doRun     = $true; $i++; continue }
    elseif ($a -match '^(?i)-32$')        { $is32      = $true; $i++; continue }
    elseif ($a -match '^(?i)-Quiet$')     { $quiet     = $true; $i++; continue }
    elseif ($a -match '^(?i)-With$') {
        if ($i + 1 -lt $args.Count) {
            foreach ($n in ([string]$args[$i+1]) -split ',') { if ($n.Trim()) { $withList.Add($n.Trim()) } }
        }
        $i += 2; continue
    }
    elseif ($a -match '^(?i)-o$') {
        if ($i + 1 -lt $args.Count) { $output = [string]$args[$i+1] }
        $i += 2; continue
    }
    else {
        if ($a -match '\.(c|h|o)$') { $sources.Add($a) } else { $extra.Add($a) }
        $i++; continue
    }
}

if ($listOnly) {
    Write-Host "portable 可用库`n"
    Write-Host ("  {0,-14} {1}" -f 'NAME', 'DESCRIPTION')
    Write-Host "  " + ('-' * 76)
    foreach ($k in $LIBDEF.Keys) { Write-Host ("  {0,-14} {1}" -f $k, $LIBDEF[$k].usage) }
    Write-Host "`n示例： scripts\tcc.cmd app.c -With cjson,tinydir,miniz -Run"
    exit 0
}

$tccExe = Join-Path $Root 'toolchain\tcc\tcc.exe'
if ($is32) { $tccExe = Join-Path $Root 'toolchain\tcc\i386-win32-tcc.exe' }

$incDirs = New-Object System.Collections.Generic.List[string]
$incDirs.Add((Join-Path $Root 'include'))
$incDirs.Add((Join-Path $Root 'include\lua'))
# TCC 的 winapi 头都在 include\winapi\ 子目录里，而 windows.h 用
# #include <winnls.h> 这种**无前缀**写法引用它们，所以这个子目录
# 必须在 include 路径上。tcc.exe 不会自动加上它。
$incDirs.Add((Join-Path $Root 'toolchain\tcc\include\winapi'))

$libSrc  = New-Object System.Collections.Generic.List[string]
$defines = New-Object System.Collections.Generic.List[string]
$shimIncludes = New-Object System.Collections.Generic.List[string]
$sysLibs      = New-Object System.Collections.Generic.List[string]

foreach ($key in $withList) {
    if (-not $LIBDEF.Contains($key)) { Write-Host "未知库 '$key'。用 -List 查看。" -ForegroundColor Red; exit 2 }
    $d = $LIBDEF[$key]
    $p = Join-Path $Root $d.inc
    if ((Test-Path $p) -and -not $incDirs.Contains($p)) { $incDirs.Add($p) }
    foreach ($s in $d.src) {
        $sp = Join-Path $Root $s
        if (Test-Path $sp) { $libSrc.Add($sp) } else { Write-Warning "库 '$key' 缺少 $s" }
    }
    foreach ($df in $d.defs) { if (-not $defines.Contains($df)) { $defines.Add($df) } }
    if ($d.libs) { foreach ($l in $d.libs) { if (-not $sysLibs.Contains($l)) { $sysLibs.Add($l) } } }
    # 带 msvcrt 垫片的库：自动 -include 垫片头，避免 implicit declaration
    foreach ($s in $d.src) {
        if ($s -like '*msvcrt_compat.c' -or $s -like '*unistd_compat.c') {
            if ($s -like '*unistd_compat.c') { $shimHdr = Join-Path $Root 'helper\unistd_compat.h' }
            else { $shimHdr = Join-Path $Root 'helper\msvcrt_compat.h' }
            if ((Test-Path $shimHdr) -and -not $shimIncludes.Contains($shimHdr)) { $shimIncludes.Add($shimHdr) }
        }
    }
}

$incDirs.Add((Join-Path $Root 'lib\tccdefs'))

# advapi32 是注册表 / 安全 token / 服务 API 所在 DLL。TCC 的 -l 是惰性的
# （只在实际引用了符号时才拉进来），所以默认带上它没有体积代价。
if (-not $sysLibs.Contains('advapi32')) { $sysLibs.Add('advapi32') }

$tccArgs = New-Object System.Collections.Generic.List[string]
foreach ($d in $incDirs) { $tccArgs.Add('-I'); $tccArgs.Add($d) }
foreach ($d in $defines) { $tccArgs.Add("-D$d") }
foreach ($h in $shimIncludes) { $tccArgs.Add('-include'); $tccArgs.Add($h) }
$tccArgs.Add('-D_WIN32_WINNT=0x0601')
foreach ($f in $extra)   { $tccArgs.Add($f) }
foreach ($s in $sources) { $tccArgs.Add($s) }
foreach ($s in $libSrc)  { $tccArgs.Add($s) }

if ($output) {
    $tccArgs.Add('-o'); $tccArgs.Add($output)
} elseif ($sources.Count -ge 1) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($sources[0])
    $tccArgs.Add('-o'); $tccArgs.Add((Join-Path (Get-Location) ($base + '.exe')))
}

# 导入库目录必须与目标位数匹配：lib\tccdefs 是从 64 位 DLL 生成的，
# 32 位链接它会导致 undefined symbol '_except_handler3'（见 docs/PITFALLS.md P60）
$defDir = if ($is32) { 'lib\tccdefs32' } else { 'lib\tccdefs' }
$tccArgs.Add('-L'); $tccArgs.Add((Join-Path $Root $defDir))
foreach ($l in $sysLibs) { $tccArgs.Add("-l$l") }

if ($showFlags) { Write-Host $tccExe; $tccArgs | ForEach-Object { Write-Host "  $_" }; exit 0 }

if ($sources.Count -eq 0) {
    Write-Host "用法: scripts\tcc.cmd <源文件.c> [-o 输出] [-With 库] [-Run] [-32]"
    exit 2
}

if (-not $quiet) { Write-Host "[tcc] $($tccArgs -join ' ')" -ForegroundColor DarkGray }
& $tccExe @tccArgs
$code = $LASTEXITCODE
if ($code -ne 0) { Write-Host "[tcc] 编译失败 (exit $code)" -ForegroundColor Red; exit $code }
if (-not $quiet) { Write-Host "[tcc] 编译成功" -ForegroundColor Green }

if ($doRun) {
    $exe = $output
    if (-not $exe) {
        $base = [System.IO.Path]::GetFileNameWithoutExtension($sources[0])
        $exe = Join-Path (Get-Location) ($base + '.exe')
    }
    # 相对路径要基于当前目录解析：PowerShell 的 CWD 与进程 CWD 可能不一致，
    # 直接 Test-Path 相对路径会误报"找不到"。
    if (-not [System.IO.Path]::IsPathRooted($exe)) {
        $exe = Join-Path (Get-Location) $exe
    }
    if (Test-Path -LiteralPath $exe) { & $exe; exit $LASTEXITCODE }
    Write-Host "[tcc] 找不到 $exe" -ForegroundColor Red; exit 1
}
exit 0
