@echo off
setlocal
cd /d "%~dp0.."
powershell -NoProfile -ExecutionPolicy Bypass -File ".\scripts\build.ps1" -Configuration Release
exit /b %errorlevel%
