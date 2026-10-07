# check-with.ps1 -- verify the -With library registry actually WORKS.
#
# WHY THIS EXISTS
#   verify.ps1 compiles each probe with -With <lib>, so a library that HAS a
#   probe is already exercised -- but probe_image.c defines its own
#   STB_IMAGE_IMPLEMENTATION, so it could NOT detect that `-With stb_image`
#   itself was broken. And 13 of the 30 registered libraries have no probe.
#
#   HISTORY (why v1 was too weak):
#     v1 compiled `int main(void){return 0;}` for each -With name. That proves
#     "registration exists + paths exist + it compiles" but NOT "the library is
#     usable". Four libraries (stb_c_lexer / stb_rect_pack / stb_image /
#     stb_image_write) were reported as "all 30 compile+link" while in fact
#     every symbol was undefined -- they were missing their
#     *_IMPLEMENTATION macro. See PITFALLS P78.
#
#   v2 (this file) additionally compiles a program that **calls at least one
#   real symbol** from each library whose symbol is known ($calls), forcing
#   real linking. Two non-obvious things learned while building this:
#
#     1. `-With` adds include PATHS and DEFINES -- it does NOT add the
#        `#include` itself. The generated source must include the library
#        header explicitly, hence $hdrs.
#     2. A library may need more than the -With flag supplies to compile at
#        all (nuklear needs NK_IMPLEMENTATION + nuklear_tcc_compat.h). Such
#        libraries are listed in $preHdr / $hdrs instead.
#     3. Some libraries legitimately CANNOT be strong-checked: -With lua",
    
#        build-lua.cmd, not from -With. So 'link a real symbol via -With lua'
#        is impossible by design, and lua stays WEAK on purpose.
#        (utf8io.h is all static __inline, but its inline functions are
#        callable, so it CAN be strong-checked.)
#        Report those as WEAK rather than
#        FAILED, so the script stays honest instead of raising a false alarm.
#
# NOTE: this file is deliberately pure ASCII. Writing non-ASCII into a .ps1
# through the shell has corrupted scripts in this project before; keep it ASCII.
#
# USAGE  pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\check-with.ps1
# EXIT   0 = all consistent, 1 = something is broken

$ErrorActionPreference = 'Continue'
$Root = Split-Path -Parent $PSScriptRoot
$tccPs1 = Join-Path $Root 'scripts\tcc.ps1'

if (-not (Test-Path $tccPs1)) {
    Write-Host "FAIL: cannot find scripts\tcc.ps1 under $Root"
    exit 1
}

$lines = Get-Content $tccPs1 -Encoding UTF8

# --- parse the LIBDEF table -------------------------------------------------
$names = @()
$entries = @()
foreach ($ln in $lines) {
    if ($ln -match "^\s*'([\w\.]+)'\s*=\s*@\{\s*inc='([^']*)'(.*)$") {
        $names += $Matches[1]
        $entries += [pscustomobject]@{ Name = $Matches[1]; Inc = $Matches[2]; Rest = $Matches[3] }
    }
}

Write-Host ("LIBDEF entries: " + $names.Count)
if ($names.Count -lt 25) {
    Write-Host ("FAIL: expected >= 25 entries, got " + $names.Count)
    exit 1
}

$fail = 0

# --- 1. inc= directories must exist ----------------------------------------
$missInc = @()
foreach ($e in $entries) {
    foreach ($p in ($e.Inc -split ';')) {
        if ($p -and -not (Test-Path (Join-Path $Root $p))) { $missInc += ($e.Name + ' -> ' + $p) }
    }
}
if ($missInc.Count -eq 0) {
    Write-Host "OK   inc= paths all exist"
} else {
    Write-Host ("FAIL inc= paths missing: " + ($missInc -join ', '))
    $fail++
}

# --- 2. src= files must exist ----------------------------------------------
$missSrc = @()
foreach ($e in $entries) {
    if ($e.Rest -match 'src=@\(([^)]*)\)') {
        foreach ($s in ($Matches[1] -split ',')) {
            $s = $s.Trim().Trim("'")
            if ($s -and -not (Test-Path (Join-Path $Root $s))) { $missSrc += ($e.Name + ' -> ' + $s) }
        }
    }
}
if ($missSrc.Count -eq 0) {
    Write-Host "OK   src= files all exist"
} else {
    Write-Host ("FAIL src= files missing: " + ($missSrc -join ', '))
    $fail++
}

# --- 3. a real symbol from each library must compile AND link ---------------
#
# Value = full body of main(). It must reference at least one symbol that lives
# in the library's implementation, so a missing *_IMPLEMENTATION macro shows up
# as an undefined symbol instead of passing silently.
$calls = @{
    'cjson'           = 'cJSON *j = cJSON_Parse("{}"); cJSON_Delete(j); return 0;'
    'stb_ds'          = 'int *a = 0; arrput(a, 1); arrfree(a); return 0;'
    'stb_sprintf'     = 'char b[16]; stbsp_sprintf(b, "%d", 1); return 0;'
    'jsmn'            = 'jsmn_parser p; jsmn_init(&p); return 0;'
    'utf8'            = 'return (int)utf8len("ab");'
    'tinydir'         = 'tinydir_dir d; (void)d; return 0;'
    'xxhash'          = 'return (int)XXH32("a", 1, 0);'
    'sha256'          = 'unsigned char o[32]; lonesha256(o, (const unsigned char*)"a", 1); return 0;'
    'argparse'        = 'struct argparse ap; int v=0; struct argparse_option o[2]; static const char *const u[2]; const char *av[3]; memset(o,0,sizeof o); o[0].type=ARGPARSE_OPT_BOOLEAN; o[0].short_name=0x76; o[0].long_name="verbose"; o[0].value=&v; o[0].help="v"; o[1].type=ARGPARSE_OPT_END; u[0]="p"; u[1]=0; av[0]="p"; av[1]="-v"; av[2]=0; argparse_init(&ap,o,u,0); argparse_parse(&ap,2,av); return (v!=0)?0:1;'
    'miniz'           = 'return (int)mz_crc32(0, (const unsigned char*)"a", 1);'
    'tinyexpr'        = 'return (int)te_interp("1+1", 0);'
    'stb_image'       = 'int w,h,n; unsigned char*p = stbi_load("x", &w, &h, &n, 0); if (p) stbi_image_free(p); return 0;'
    'stb_image_write' = 'unsigned char o[4] = {1,2,3,4}; return stbi_write_bmp("cw_t.bmp", 1, 1, 4, o) ? 0 : 1;'
    'stb_rect_pack'   = 'stbrp_context c; stbrp_node nd[4]; stbrp_rect r; r.id=0;r.w=4;r.h=4;r.was_packed=0;r.x=0;r.y=0; stbrp_init_target(&c,8,8,nd,4); stbrp_pack_rects(&c,&r,1); return 0;'
    'stb_c_lexer'     = 'stb_lexer l; char s[32]; stb_c_lexer_init(&l,"a","a"+1,s,32); return stb_c_lexer_get_token(&l) ? 0 : 1;'
    'linmath'         = 'vec3 a; vec3 b; vec3_add(a, b, b); return (int)a[0];'
    'regex'           = 'int ml = 0; return (re_match("a", "a", &ml) >= 0) ? 0 : 1;'
    'sqlite'          = 'sqlite3 *db = 0; sqlite3_open(":memory:", &db); sqlite3_close(db); return 0;'
    'unistd'          = 'usleep(0); return 0;'
    'time'            = 'struct timeval tv; gettimeofday(&tv, 0); return (tv.tv_sec > 0) ? 0 : 1;'
    'math'            = 'float ip; return (modff(1.5f, &ip) > 0.0f) ? 0 : 1;'
    'socket'          = 'WSADATA w; WSAStartup(MAKEWORD(2,2), &w); WSACleanup(); return 0;'
    'dirent'          = 'DIR *d = opendir("."); if (d) closedir(d); return 0;'
    'log.c'           = 'log_set_level(LOG_TRACE); log_trace("x"); return 0;'
    'nuklear'         = 'struct nk_context c; struct nk_rect b = nk_rect(0,0,10,10); nk_init_default(&c, 0); nk_begin(&c, "t", b, NK_WINDOW_BORDER); nk_end(&c); nk_free(&c); return 0;'
    'inih'            = 'return (ini_parse_string("[s]", 0, 0) >= 0) ? 0 : 0;'
    'toml'            = 'char e[64]; char cfg[] = "a = 1"; toml_table_t *t = toml_parse(cfg, e, sizeof e); if (t) toml_free(t); return 0;'
    'readline'        = 'tcwin_rl_t rl; tcwin_rl_init(&rl, "> "); return tcwin_rl_history_count(&rl);'
    'utf8io'          = 'char o[64]; return (u8_wide_to_cp(o, sizeof o, L"a", 65001) >= 0) ? 0 : 1;'
}

# Libraries whose minimal working recipe needs more than the -With flag
# supplies. Reported as WEAK, not FAILED.
$knownHeavy = @()

# library name -> header to #include
# (-With supplies include PATHS, not the #include itself)
# library name -> source text to place BEFORE the library header
# (compat headers / feature macros that -With does not supply)
$preHdr = @{
    'nuklear'         = '#include "nuklear_tcc_compat.h"' + "`n" + '#define NK_IMPLEMENTATION'
}

$hdrs = @{
    'cjson'           = 'cJSON.h'
    'stb_ds'          = 'stb_ds.h'
    'stb_sprintf'     = 'stb_sprintf.h'
    'jsmn'            = 'jsmn.h'
    'utf8'            = 'utf8.h'
    'tinydir'         = 'tinydir.h'
    'xxhash'          = 'xxhash.h'
    'sha256'          = 'lonesha256.h'
    'argparse'        = 'argparse.h'
    'miniz'           = 'miniz.h'
    'tinyexpr'        = 'tinyexpr.h'
    'stb_image'       = 'stb_image.h'
    'stb_image_write' = 'stb_image_write.h'
    'stb_rect_pack'   = 'stb_rect_pack.h'
    'stb_c_lexer'     = 'stb_c_lexer.h'
    'linmath'         = 'linmath.h'
    'regex'           = 're.h'
    'sqlite'          = 'sqlite3.h'
    'unistd'          = 'unistd.h'
    'math'            = 'tccmath.h'
    'socket'          = 'winsock2.h'
    'dirent'          = 'dirent.h'
    'log.c'           = 'log.h'
    'inih'            = 'ini.h'
    'toml'            = 'toml.h'
    'readline'        = 'tcwinreadline.h'
    'utf8io'          = 'utf8io.h'
    'nuklear'         = 'nuklear.h'
}

# --- library -> functional probe, parsed from verify.ps1's $map ------------
# Built by parsing rather than hardcoding, so it cannot drift out of sync.
# Used only to make the WEAK report honest: "no call expression in THIS script"
# is not the same as "not covered" -- verify.ps1 may run a functional probe.
$probeMap = @{}
$vps1 = Join-Path $Root 'scripts\verify.ps1'
if (Test-Path $vps1) {
    foreach ($vl in (Get-Content $vps1 -Encoding UTF8)) {
        if ($vl -match "^\s*'(probe_[\w\.]+\.c)'\s*=\s*'([^']*)'") {
            $probe = $Matches[1]
            foreach ($lib in ($Matches[2] -split ',')) {
                $lib = $lib.Trim()
                if ($lib) {
                    if ($probeMap.ContainsKey($lib)) { $probeMap[$lib] = $probeMap[$lib] + ',' + $probe }
                    else { $probeMap[$lib] = $probe }
                }
            }
        }
    }
}

$tmp = Join-Path $Root 'tmp' | Join-Path -ChildPath ("tcwin_checkwith_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$runner = Join-Path $Root 'scripts\tcc.ps1'

# headers some call expressions need beyond the library's own inc=
$prelude = @(
    '#include <stdio.h>',
    '#include <string.h>',
    '#include <stdlib.h>',
    '#include <winsock2.h>',
    '#include <ws2tcpip.h>',
    '#include <dirent.h>',
    ''
) -join "`n"

function Invoke-Case([string]$libName, [string]$body, [string]$tag) {
    $src = Join-Path $tmp ($tag + '.c')
    $exe = Join-Path $tmp ($tag + '.exe')
    # $hdrPre goes BEFORE the library header (compat headers, feature macros),
    # $hdr is the library header itself.
    $hdrPre = ''
    if ($preHdr.ContainsKey($libName)) { $hdrPre = $preHdr[$libName] + "`n" }
    $hdr = ''
    if ($hdrs.ContainsKey($libName)) { $hdr = '#include "' + $hdrs[$libName] + '"' + "`n" }
    $code = $prelude + $hdrPre + $hdr + "int main(void){ " + $body + " }`n"
    Set-Content -Path $src -Value $code -Encoding ASCII
    Remove-Item $exe -Force -ErrorAction SilentlyContinue
    $a = @($src, '-o', $exe, '-Quiet', '-With', $libName)
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $out = & $runner @a 2>&1; $rc = $LASTEXITCODE } finally { $ErrorActionPreference = $old }
    return [pscustomobject]@{ Ok = ($rc -eq 0); Out = ($out -join ' ') }
}

$badStrong = @()
$weak = @()
$strongCount = 0
foreach ($n in $names) {
    if ($calls.ContainsKey($n) -and -not ($knownHeavy -contains $n)) {
        $strongCount++
        $r = Invoke-Case $n $calls[$n] ('s_' + ($n -replace '[^\w]', '_'))
        if (-not $r.Ok) {
            $badStrong += $n
            $m = ($r.Out -replace '^.*error:\s*', '')
            Write-Host ("FAIL [" + $n + "] real-symbol link failed: " + $m.Substring(0, [Math]::Min(70, $m.Length)))
        }
    } else {
        $weak += $n
    }
}
if ($badStrong.Count -eq 0) {
    Write-Host ("OK   " + $strongCount + " libraries link a REAL symbol")
} else {
    Write-Host ("FAIL real-symbol failures: " + ($badStrong -join ', '))
    $fail++
}
if ($weak.Count -gt 0) {
    # "no call expression" means THIS SCRIPT has no real-symbol case for it.
    # It does NOT mean the library is unverified -- verify.ps1 runs functional
    # probes. Cross-reference $probeMap so the report is not misleading.
    $coveredByProbe = @()
    $trulyWeak = @()
    foreach ($n in $weak) {
        if ($probeMap.ContainsKey($n)) { $coveredByProbe += ($n + ' -> ' + $probeMap[$n]) }
        else { $trulyWeak += $n }
    }
    if ($coveredByProbe.Count -gt 0) {
        Write-Host ("WEAK no call expr here, but verify.ps1 covers it: " + ($coveredByProbe -join '; '))
    }
    if ($trulyWeak.Count -gt 0) {
        Write-Host ("WEAK no call expr AND no functional probe in verify.ps1: " + ($trulyWeak -join ', '))
    }
}

# weak fallback for EVERY library (catches broken paths / bad names)
$wSrc = Join-Path $tmp 'w.c'
Set-Content -Path $wSrc -Value 'int main(void){return 0;}' -Encoding ASCII
$wExe = Join-Path $tmp 'w.exe'
$badWeak = @()
foreach ($n in $names) {
    Remove-Item $wExe -Force -ErrorAction SilentlyContinue
    $a = @($wSrc, '-o', $wExe, '-Quiet', '-With', $n)
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $runner @a 2>&1 | Out-Null; $rc = $LASTEXITCODE } finally { $ErrorActionPreference = $old }
    if ($rc -ne 0) { $badWeak += $n }
}
if ($badWeak.Count -eq 0) {
    Write-Host ("OK   all " + $names.Count + " -With names compile (empty main)")
} else {
    Write-Host ("FAIL empty-main compile failures: " + ($badWeak -join ', '))
    $fail++
}

Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
Remove-Item (Join-Path $Root 'cw_t.bmp') -Force -ErrorAction SilentlyContinue

if ($fail -eq 0) { Write-Host "RESULT: consistent"; exit 0 }
Write-Host ("RESULT: " + $fail + " problem group(s)")
exit 1
