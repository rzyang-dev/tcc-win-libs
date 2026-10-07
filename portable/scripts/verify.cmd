@echo off
setlocal
set "HERE=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%verify.ps1" %*
exit /b %ERRORLEVEL%
