# verify.ps1 - 环境自检
#
# 用法:  scripts\verify.cmd
#
# 编译并运行 examples\probes\ 下的全部探针，确认环境完好。
# 任何一项失败都说明拷贝过程损坏了文件，或环境不兼容。

$ErrorActionPreference = 'Continue'
$Root = Split-Path -Parent $PSScriptRoot
$pass = 0; $fail = 0
$failed = @()

function Report($name, $ok, $detail) {
    if ($ok) {
        Write-Host ("  [OK]   {0,-12} {1}" -f $name, $detail) -ForegroundColor Green
        $script:pass++
    } else {
        Write-Host ("  [FAIL] {0,-12} {1}" -f $name, $detail) -ForegroundColor Red
        $script:fail++
        $script:failed += $name
    }
}

Write-Host "=== portable 环境自检 ===" -ForegroundColor Cyan
Write-Host ("根目录: {0}" -f $Root)
Write-Host ""

Write-Host "-- 工具链文件 --"
$need = @(
    'toolchain\tcc\tcc.exe',
    'toolchain\tcc\i386-win32-tcc.exe',
    'toolchain\tcc\lib\libtcc1-64.a',
    'toolchain\lua-bin\Win64\luac55.exe',
    'toolchain\lua-bin\Win32\luac55.exe',
    'toolchain\lua-src\lua.c',
    'helper\luac2c.exe',
    'helper\txt2c.exe',
    'helper\msvcrt_compat.c',
    'scripts\tcc.ps1',
    'scripts\build-lua.ps1'
)
foreach ($n in $need) { Report $n (Test-Path (Join-Path $Root $n)) '' }

Write-Host ""
Write-Host "-- 头文件 --"
$hdrs = @(
    'include\tccwin.h','include\tccmath.h','include\stb_ds.h','include\stb_image.h',
    'include\stb_image_write.h','include\stb_rect_pack.h','include\stb_c_lexer.h',
    'include\stb_sprintf.h','include\tinydir.h','toolchain\tcc\include\winapi\winsock2.h','toolchain\tcc\include\winapi\ws2tcpip.h','include\nuklear.h','include\nuklear_tcc_compat.h','lib\sqlite\sqlite3.h','lib\regex\re.h','include\tcwinreadline.h','include\utf8io.h','lib\readline\tcwinreadline.c','include\xxhash.h','include\lonesha256.h',
    'include\linmath.h','include\utf8.h','include\jsmn.h',
    'lib\cjson\cJSON.h','lib\miniz\miniz.h','lib\toml\toml.h','lib\inih\ini.h',
    'lib\tinyexpr\tinyexpr.h','lib\log.c\log.h','lib\argparse\argparse.h'
)
foreach ($h in $hdrs) { Report $h (Test-Path (Join-Path $Root $h)) '' }

Write-Host ""
Write-Host "-- 导入库（现代 API 能否链接的关键）--"
$k32 = Join-Path $Root 'lib\tccdefs\kernel32.def'
if (Test-Path $k32) {
    $n = (Select-String -Path $k32 -Pattern '^\S' | Where-Object { $_.Line -notmatch '^(LIBRARY|EXPORTS|;)' }).Count
    Report 'kernel32.def' ($n -gt 1000) ("{0} 个导出（需 >1000）" -f $n)
} else { Report 'kernel32.def' $false '缺失' }
$nd = @(Get-ChildItem (Join-Path $Root 'lib\tccdefs') -Filter *.def -ErrorAction SilentlyContinue).Count
Report 'tccdefs 目录' ($nd -ge 20) ("{0} 个 .def" -f $nd)

Write-Host ""
Write-Host "-- 端到端编译运行（examples\probes）--"
$probeDir = Join-Path $Root 'examples\probes'
$map = @{
    'probe_core.c'     = ''
    'probe_cjson.c'    = 'cjson'
    'probe_stbds.c'    = 'stb_ds'
    'probe_hash.c'     = 'xxhash,sha256'
    'probe_miniz.c'    = 'miniz'
    'probe_tinydir.c'  = 'tinydir'
    'probe_toml.c'     = 'toml'
    'probe_image.c'    = 'stb_image,stb_image_write'
    'probe_linmath.c'  = 'linmath'
    'probe_socket.c'   = 'socket'
    'probe_nuklear.c'  = 'nuklear'
    'probe_regex.c'    = 'regex'
    'probe_sqlite.c'   = 'sqlite'
    'probe_stbtt.c'    = 'nuklear'
    'probe_readline.c' = 'readline'
    'probe_encoding.c' = 'utf8io'
    'probe_winapi.c'   = 'argparse'
    'probe_rl_utf8.c'  = ''
    'probe_sqlite_mt.c' = 'sqlite'
    'probe_nk_cjk.c'   = 'nuklear'
    'probe_dirent.c'   = 'dirent'
    'probe_unistd.c'   = 'unistd'
    'probe_timecompat.c' = 'time'
    'probe_mathgap.c'  = 'math'
    'probe_sqlite32.c' = 'sqlite'
    'probe_stdcall_voidptr.c' = ''
    'probe_misc.c'     = 'jsmn,sha256,stb_sprintf'
    'probe_misc3.c'    = 'inih,tinyexpr,log.c,stb_rect_pack'
    'probe_argparse.c' = 'argparse'
    'probe_utf8lib.c'  = 'utf8'
    'probe_clexer.c'   = 'stb_c_lexer'
}
$tmp = Join-Path $Root 'tmp' | Join-Path -ChildPath ("tcwin_verify_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$runner = Join-Path $Root 'scripts\tcc.ps1'

foreach ($name in ($map.Keys | Sort-Object)) {
    $srcf = Join-Path $probeDir $name
    if (-not (Test-Path $srcf)) { Report $name $false '探针文件缺失'; continue }
    $exe = Join-Path $tmp ($name -replace '\.c$','.exe')
    $a = @($srcf, '-o', $exe, '-Quiet')
    if ($map[$name]) { $a += @('-With', $map[$name]) }
    # TCC 把警告写到 stderr。PowerShell 在有 ErrorActionPreference='Stop'
    # 或遇到 NativeCommandError 时会中断 —— 这里显式降级为 Continue，
    # 否则一条 "redefined" 警告就会让整轮自检提前结束。
    $oldEAP = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $build = & $runner @a 2>&1
        $buildRc = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldEAP
    }
    if ($buildRc -ne 0) {
        $e = ($build | Select-String 'error:' | Select-Object -First 1)
        Report $name $false ("编译失败: {0}" -f $e)
        continue
    }
    # 编译通过但出现 redefined 警告 —— 这是真实的头文件冲突，必须暴露
    $redef = ($build | Select-String 'redefined' | Select-Object -First 1)
    if ($redef) {
        Report $name $false ("编译有重定义警告: {0}" -f ($redef -replace '^.*warning:\s*',''))
        continue
    }
    Push-Location $tmp
    $out = & $exe 2>&1
    $rc = $LASTEXITCODE
    Pop-Location
    # 只显示探针的结论行，避免几十行输出刷屏
    $summary = ($out | Select-String -Pattern '^===.*(通过|失败)' | Select-Object -Last 1)
    if (-not $summary) { $summary = ($out | Select-Object -Last 1) }
    Report $name ($rc -eq 0) ("$summary".Trim())
}
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ""
Write-Host "-- examples（含 readline 示例）--"
# 注意：上面的 $tmp 在探针循环末尾已被删除，examples 必须用自己的临时目录
$exTmp = Join-Path $Root 'tmp' | Join-Path -ChildPath ("tcwin_verify_ex_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $exTmp | Out-Null
$exDir = Join-Path $Root 'examples'
$exList = @(
    @{ f = '01-hello.c';    w = '' },
    @{ f = '02-winapi.c';   w = '' },
    @{ f = '03-multilib.c'; w = 'tinydir,sha256,xxhash,cjson,stb_ds' },
    @{ f = '04-image.c';    w = 'stb_image,stb_image_write' },
    @{ f = '05-repl.c';     w = 'readline,tinyexpr' }
)
foreach ($ex in $exList) {
    $src = Join-Path $exDir $ex.f
    $dst = Join-Path $exTmp ($ex.f -replace '\.c$', '.exe')
    $a = @($src, '-o', $dst, '-Quiet')
    if ($ex.w) { $a += @('-With', $ex.w) }
    $oldEAP = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $bo = & $runner @a 2>&1; $brc = $LASTEXITCODE } finally { $ErrorActionPreference = $oldEAP }
    $detail = ''
    if ($brc -ne 0) { $detail = (($bo | Select-String 'error:' | Select-Object -First 1) -replace '^.*error:\s*','') }
    Report ("examples\" + $ex.f) ($brc -eq 0) $detail
}

Write-Host ""
Write-Host "-- 32 位交叉编译（用 lib\tccdefs32）--"
# 导入库目录必须与目标位数匹配。lib\tccdefs 由 64 位 DLL 生成，
# 32 位链接它会报 undefined symbol '_except_handler3'。见 PITFALLS P60。
$tmp32 = Join-Path $Root 'tmp' | Join-Path -ChildPath ("tcwin_verify32_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tmp32 | Out-Null
$p32 = Join-Path $tmp32 'p32.c'
$src32 = @'
#include "tccwin.h"
#include <stdio.h>
int main(void) {
    MEMORYSTATUSEX m;
    unsigned long long t;
    CRITICAL_SECTION c;
    m.dwLength = sizeof m;
    if (!GlobalMemoryStatusEx(&m)) return 1;
    t = GetTickCount64();
    InitializeCriticalSectionEx(&c, 0, 0);
    EnterCriticalSection(&c); LeaveCriticalSection(&c); DeleteCriticalSection(&c);
    printf("%d %d %d", (int)sizeof(void*), (int)(t > 0), (int)(m.ullTotalPhys > 0));
    return 0;
}
'@
Set-Content -Path $p32 -Value $src32 -Encoding ASCII
$e32 = Join-Path $tmp32 'p32.exe'
$a32 = @($p32, '-o', $e32, '-Quiet', '-32')
$oldEAP32 = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try { $b32 = & $runner @a32 2>&1; $rc32 = $LASTEXITCODE } finally { $ErrorActionPreference = $oldEAP32 }
if ($rc32 -ne 0) {
    $d32 = (($b32 | Select-String 'error' | Select-Object -First 1) -replace '^.*error:\s*','')
    Report '32-bit' $false $d32
} else {
    $out32 = (& $e32 2>&1) -join ' '
    # 期望 4 1 1：指针 4 字节 = 真 32 位；GetTickCount64 与 GlobalMemoryStatusEx 可用
    Report '32-bit' ($out32 -eq '4 1 1') "sizeof(void*) + 现代 API: $out32 (期望 4 1 1)"
}
Remove-Item -Recurse -Force $tmp32 -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "-- 32 位跑代表性探针（用 -32 真实编译并运行）--"
# 这些探针曾经只在 64 位下跑过，于是"32 位下 22 个 float 函数不可用"
# 这个缺陷长期没被发现（见 PITFALLS P73）。这里挑覆盖面最广的几个，
# 在 32 位下也真实编译 + 运行，防止回归。
#
# 不跑全部探针是为了控制自检时长。选中的分别是：
#   probe_mathgap             float 数学的位宽差异（P73）+ modff 越界写（P74）
#   probe_winapi              Win32 API + 结构体尺寸
#   probe_socket              winsock 结构体尺寸的位宽差异
#   probe_nuklear             nk_size 等类型宽度（TCC 适配的核心）
#   probe_dirent/unistd/timecompat  新增的垫片
$probes32 = @(
    @{ f='probe_mathgap.c';    w='math' },
    @{ f='probe_winapi.c';     w='argparse' },
    @{ f='probe_socket.c';     w='socket' },
    @{ f='probe_nuklear.c';    w='nuklear' },
    @{ f='probe_dirent.c';     w='dirent' },
    @{ f='probe_unistd.c';     w='unistd' },
    @{ f='probe_timecompat.c'; w='time' }
)
$tmp32b = Join-Path $Root 'tmp' | Join-Path -ChildPath ("tcwin_verify32b_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tmp32b | Out-Null
foreach ($pb in $probes32) {
    $srcP = Join-Path $Root ("examples\probes\" + $pb.f)
    $exeP = Join-Path $tmp32b ($pb.f -replace '\.c$', '.exe')
    $aP = @($srcP, '-o', $exeP, '-Quiet', '-32', '-With', $pb.w)
    $oldP = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $bP = & $runner @aP 2>&1; $rcP = $LASTEXITCODE } finally { $ErrorActionPreference = $oldP }
    if ($rcP -ne 0) {
        $dP = (($bP | Select-String 'error' | Select-Object -First 1) -replace '^.*error:\s*','')
        Report ("32-bit " + $pb.f) $false $dP
        continue
    }
    Push-Location $tmp32b
    $outP = & $exeP 2>&1
    $rcRun = $LASTEXITCODE
    Pop-Location
    $sumP = ($outP | Select-String -Pattern '通过, ' | Select-Object -Last 1)
    Report ("32-bit " + $pb.f) ($rcRun -eq 0) $(if ($sumP) { $sumP.ToString().Trim() } else { '运行通过' })
}
Remove-Item -Recurse -Force $tmp32b -ErrorAction SilentlyContinue

Write-Host "-- Lua --"
# 输出到 bin\_verify_slua.exe：临时目录已在上面清理，不能再用
$binDir = Join-Path $Root 'bin'
New-Item -ItemType Directory -Force -Path $binDir | Out-Null
$vslua = Join-Path $binDir '_verify_slua.exe'
Remove-Item $vslua -ErrorAction SilentlyContinue
& (Join-Path $Root 'scripts\build-lua.ps1') -Interpreter -Out $vslua *>&1 | Out-Null
if (Test-Path $vslua) {
    $o = & $vslua -e "io.write(tostring(0xF0|0x0F)..' '.._VERSION)" 2>&1
    Report 'build-lua' ("$o" -match '255') "静态解释器: $o"
    Remove-Item $vslua -ErrorAction SilentlyContinue
} else {
    Report 'build-lua' $false '静态解释器构建失败'
}


Write-Host ""
Write-Host "-- -With registry consistency (scripts\check-with.ps1) --"
# verify.ps1 compiles each probe with -With <lib>, so a library that HAS a
# probe is already exercised. But 13 of the 30 registered libraries have no
# probe, so a typo in their inc=/src= path would go unnoticed. check-with.ps1
# covers all 30 uniformly. Kept as a separate ASCII-only script on purpose:
# writing non-ASCII into a .ps1 through the shell has corrupted scripts here.
$cwScript = Join-Path $Root 'scripts\check-with.ps1'
if (Test-Path $cwScript) {
    $oldCW = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $cwOut = & pwsh -NoProfile -ExecutionPolicy Bypass -File $cwScript 2>&1; $cwRc = $LASTEXITCODE } finally { $ErrorActionPreference = $oldCW }
    $cwDetail = (($cwOut | Select-String 'RESULT:' | Select-Object -Last 1) -replace '^\s*RESULT:\s*','')
    if (-not $cwDetail) { $cwDetail = 'no RESULT line' }
    Report '-With registry' ($cwRc -eq 0) $cwDetail
} else {
    Report '-With registry' $false 'scripts\check-with.ps1 missing'
}

Write-Host ""
Write-Host "-- probe/library coverage (scripts\probe-cover-check.ps1) --"
# Confirms each -With library is actually #included by the probe that
# verify.ps1 claims covers it. Closes the gap recorded in P79, where I had
# produced a wrong no-probe list by string-matching and only hand-verified 4.
$pcScript = Join-Path $Root 'scripts\probe-cover-check.ps1'
if (Test-Path $pcScript) {
    $oldPC = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $pcOut = & pwsh -NoProfile -ExecutionPolicy Bypass -File $pcScript 2>&1; $pcRc = $LASTEXITCODE } finally { $ErrorActionPreference = $oldPC }
    $pcDetail = (($pcOut | Select-String 'RESULT:' | Select-Object -Last 1) -replace '^\s*RESULT:\s*','')
    if (-not $pcDetail) { $pcDetail = 'no RESULT line' }
    Report 'probe coverage' ($pcRc -eq 0) $pcDetail
} else {
    Report 'probe coverage' $false 'scripts\probe-cover-check.ps1 missing'
}

Write-Host ""
if ($fail -eq 0) {
    Write-Host ("=== 结果: {0} 通过, 0 失败 — 环境完好 ===" -f $pass) -ForegroundColor Green
    exit 0
} else {
    Write-Host ("=== 结果: {0} 通过, {1} 失败 ===" -f $pass, $fail) -ForegroundColor Red
    Write-Host "失败项:" -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}
