@echo off
setlocal
set PORT=%1
if "%PORT%"=="" set PORT=9000
set FRAMES=%2
if "%FRAMES%"=="" set FRAMES=100
set ROOT=%~dp0..
set PATH=C:\msys64\ucrt64\bin;%PATH%
python "%ROOT%tools\run_local_smoke.py" --port %PORT% --frames %FRAMES%
