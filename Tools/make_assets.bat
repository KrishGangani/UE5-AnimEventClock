@echo off
rem P06 AnimEventClock - generate the demo montages (/Game/AEC/Montages) with the editor commandlet AECMakeDemoAssets.
rem Usage: make_assets.bat     Needs the Development editor target (build.bat). Overwrites the montages.
rem Log: ..\AnimEventClock\Saved\Logs\MakeAssets.log. Exit code 0 = every montage created and saved.
rem Run it through the machine-wide queue (tools\ue_queue.py), never directly.
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0..\AnimEventClock\AnimEventClock.uproject"
set "LOG=%~dp0..\AnimEventClock\Saved\Logs\MakeAssets.log"

"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -run=AECMakeDemoAssets -unattended -nullrhi -nosound -nosplash -traceautostart=0 -log -abslog="%LOG%"
exit /b %ERRORLEVEL%
