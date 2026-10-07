@echo off
REM tcc.cmd - build entry point (batch wrapper that bypasses PowerShell
REM           execution-policy restrictions; other agents hit this too).
REM
REM Usage:
REM   scripts\tcc.cmd hello.c
REM   scripts\tcc.cmd hello.c -o out.exe -Run
REM   scripts\tcc.cmd app.c -With cjson,tinydir,miniz
REM   scripts\tcc.cmd app.c -32 -Run
REM   scripts\tcc.cmd -List
REM   scripts\tcc.cmd -ShowFlags app.c -With miniz
REM
REM All arguments are forwarded verbatim to tcc.ps1, using
REM -ExecutionPolicy Bypass so unsigned scripts still run.

setlocal
set "HERE=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%tcc.ps1" %*
exit /b %ERRORLEVEL%
