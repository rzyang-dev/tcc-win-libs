@echo off
REM build-lua.cmd - wrapper for build-lua.ps1 (bypasses execution policy)
REM   scripts\build-lua.cmd -Interpreter
REM   scripts\build-lua.cmd -Script app.lua -Bytecode -With cjson,miniz
setlocal
set "HERE=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%build-lua.ps1" %*
exit /b %ERRORLEVEL%