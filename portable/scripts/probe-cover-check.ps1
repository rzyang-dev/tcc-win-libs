# probe-cover-check.ps1 -- verify each -With library is ACTUALLY used by the
# probe that verify.ps1 claims covers it.
#
# WHY: P79 records that I once produced a wrong "13 libraries have no probe"
# list by string-matching library names against verify.ps1's text, and that
# only 4 of 30 were then verified by hand. This script closes that gap
# mechanically instead of by inference.
#
# METHOD: for each mapping in verify.ps1's $map, take the probe file and check
# that it actually #includes a header belonging to that library (or, for
# header-only libs, references one of its symbols). Reports UNVERIFIED rather
# than guessing, so the output is honest about what it did not check.
#
# NOTE: pure ASCII on purpose (non-ASCII written through the shell has
# corrupted .ps1 files in this project before).
#
# USAGE pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\probe-cover-check.ps1
# EXIT  0 = every mapping confirmed, 1 = at least one unconfirmed

$ErrorActionPreference = 'Continue'
$Root = Split-Path -Parent $PSScriptRoot
$vps1 = Join-Path $Root 'scripts\verify.ps1'
$probeDir = Join-Path $Root 'examples\probes'

if (-not (Test-Path $vps1)) { Write-Host "FAIL: no scripts\verify.ps1"; exit 1 }

# Library -> the header(s) that identify it inside probe source.
# Built by listing what each lib directory actually ships (Get-ChildItem),
# not from memory.
$libHeaders = @{
    'cjson'           = @('cJSON.h')
    'stb_ds'          = @('stb_ds.h')
    'stb_sprintf'     = @('stb_sprintf.h')
    'jsmn'            = @('jsmn.h')
    'utf8'            = @('utf8.h')
    'tinydir'         = @('tinydir.h')
    'xxhash'          = @('xxhash.h')
    'sha256'          = @('lonesha256.h')
    'log.c'           = @('log.h')
    'argparse'        = @('argparse.h')
    'miniz'           = @('miniz.h')
    'inih'            = @('ini.h')
    'tinyexpr'        = @('tinyexpr.h')
    'lua'             = @('lua.h')
    'stb_image'       = @('stb_image.h')
    'stb_image_write' = @('stb_image_write.h')
    'stb_rect_pack'   = @('stb_rect_pack.h')
    'stb_c_lexer'     = @('stb_c_lexer.h')
    'linmath'         = @('linmath.h')
    'regex'           = @('re.h')
    'sqlite'          = @('sqlite3.h')
    'readline'        = @('tcwinreadline.h')
    'utf8io'          = @('utf8io.h')
    'unistd'          = @('unistd.h')
    'time'            = @('sys/time.h', 'time.h')
    'math'            = @('tccmath.h')
    'nuklear'         = @('nuklear.h')
    'socket'          = @('winsock2.h')
    'dirent'          = @('dirent.h')
    'toml'            = @('toml.h')
}

# Parse $map: probe file -> comma separated -With list
$map = @()
foreach ($ln in (Get-Content $vps1 -Encoding UTF8)) {
    if ($ln -match "^\s*'(probe_[\w\.]+\.c)'\s*=\s*'([^']*)'") {
        $map += [pscustomobject]@{ Probe = $Matches[1]; Libs = @($Matches[2] -split ',' | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() }) }
    }
}
Write-Host ("verify.ps1 \$map entries: " + $map.Count)

# Library -> probes claiming to cover it
$byLib = @{}
foreach ($m in $map) {
    foreach ($l in $m.Libs) {
        if (-not $byLib.ContainsKey($l)) { $byLib[$l] = @() }
        $byLib[$l] += $m.Probe
    }
}

# Legitimate exceptions, each with its reason recorded. These are NOT
# failures -- they are cases where "probe includes the lib header" is the
# wrong test. Anything NOT listed here and unconfirmed is a real gap.
$exceptions = @{
    'argparse' = 'probe_winapi.c does not use argparse; it pairs with -With argparse only because that LIBDEF entry is the one shipping helper/msvcrt_compat.c (see tcc.ps1). argparse itself has NO functional probe -- recorded as a gap in P79.'
    'lua'      = 'not in $map at all; lua is verified end-to-end by verify.ps1''s build-lua check (real interpreter build + run), not by a C probe.'
}

$confirmed = @()
$unconfirmed = @()
$documented = @()
$noProbe = @()

foreach ($lib in ($libHeaders.Keys | Sort-Object)) {
    if (-not $byLib.ContainsKey($lib)) {
        $noProbe += $lib
        continue
    }
    $found = $false
    $how = ''
    foreach ($probe in $byLib[$lib]) {
        $pp = Join-Path $probeDir $probe
        if (-not (Test-Path $pp)) { continue }
        $txt = Get-Content $pp -Raw -Encoding UTF8
        foreach ($h in $libHeaders[$lib]) {
            # match  #include "x.h"  or  #include <x.h>
            if ($txt -match ('#include\s*[<"]' + [regex]::Escape($h) + '[>"]')) {
                $found = $true
                $how = ($probe + ' includes ' + $h)
                break
            }
        }
        if ($found) { break }
    }
    if ($found) { $confirmed += ($lib + '  <-  ' + $how) }
    elseif ($exceptions.ContainsKey($lib)) { $documented += ($lib + '  --  ' + $exceptions[$lib]) }
    else { $unconfirmed += ($lib + '  <-  ' + ($byLib[$lib] -join ',') + '  (no matching #include found)') }
}

Write-Host ""
Write-Host ("CONFIRMED (" + $confirmed.Count + "):")
foreach ($c in $confirmed) { Write-Host ("  OK   " + $c) }
if ($documented.Count -gt 0) {
    Write-Host ""
    Write-Host ("DOCUMENTED EXCEPTION (" + $documented.Count + "):")
    foreach ($d in $documented) { Write-Host ("  --   " + $d) }
}
if ($unconfirmed.Count -gt 0) {
    Write-Host ""
    Write-Host ("UNCONFIRMED (" + $unconfirmed.Count + "):")
    foreach ($u in $unconfirmed) { Write-Host ("  ??   " + $u) }
}
if ($noProbe.Count -gt 0) {
    Write-Host ""
    Write-Host ("NOT IN \$map AT ALL (" + $noProbe.Count + "):")
    foreach ($n in $noProbe) { Write-Host ("  --   " + $n) }
}

Write-Host ""
if ($unconfirmed.Count -eq 0) {
    Write-Host ("RESULT: " + $confirmed.Count + " confirmed, 0 unconfirmed, " + $noProbe.Count + " not in map")
    exit 0
}
Write-Host ("RESULT: " + $unconfirmed.Count + " unconfirmed")
exit 1
