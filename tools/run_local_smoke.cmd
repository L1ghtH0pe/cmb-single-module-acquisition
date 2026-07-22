@echo off
setlocal
set PORT=%1
if "%PORT%"=="" set PORT=9000
set FRAMES=%2
if "%FRAMES%"=="" set FRAMES=100
set TIMEOUT=%3
if "%TIMEOUT%"=="" set TIMEOUT=30
set ROOT=%~dp0..
python "%ROOT%\tools\run_local_smoke.py" --port %PORT% --frames %FRAMES% --timeout %TIMEOUT% --prepend-path C:/msys64/ucrt64/bin
