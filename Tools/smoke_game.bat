@echo off
rem P06 AnimEventClock - smoke test: start the project, load the default map, run the deferred "quit", exit.
rem Usage: smoke_game.bat [packaged|editor|server] [extra args...]
rem   packaged (default)  ..\Build\Windows\AnimEventClock.exe without a GPU (-nullrhi). Run Tools\package.bat first.
rem   editor              the uncooked project in UnrealEditor-Cmd -game, without a GPU.
rem   server              the uncooked project in UnrealEditor-Cmd -server: a dedicated server. The launcher engine
rem                       cannot build a Server target, so this is how dedicated-server roles run (01b_Research_Findings.md).
rem Logs: ..\AnimEventClock\Saved\Logs\Smoke_<mode>.log (git-ignored). Extra args are appended to the command line.
rem Run it through the machine-wide queue (tools\ue_queue.py), never directly.
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "EDITOR_CMD=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
set "PROJECT=%~dp0..\AnimEventClock\AnimEventClock.uproject"
set "PACKAGED=%~dp0..\Build\Windows\AnimEventClock.exe"
set "LOGDIR=%~dp0..\AnimEventClock\Saved\Logs"
set "MODE=%~1"
if "%MODE%"=="" set "MODE=packaged"

set "EXTRA="
:collect
shift
if "%~1"=="" goto run
set EXTRA=%EXTRA% %1
goto collect

:run
if /I "%MODE%"=="packaged" goto packaged
if /I "%MODE%"=="editor" goto editor
if /I "%MODE%"=="server" goto server
echo smoke_game.bat: unknown mode "%MODE%" (use packaged, editor or server) 1>&2
exit /b 2

:packaged
if not exist "%PACKAGED%" (
	echo smoke_game.bat: no packaged build at "%PACKAGED%"; run Tools\package.bat first 1>&2
	exit /b 3
)
"%PACKAGED%" -nullrhi -nosound -unattended -nosplash -traceautostart=0 -ExecCmds="quit" -log -abslog="%LOGDIR%\Smoke_packaged.log" %EXTRA%
exit /b %ERRORLEVEL%

:editor
"%EDITOR_CMD%" "%PROJECT%" -game -nullrhi -nosound -unattended -nosplash -traceautostart=0 -ExecCmds="quit" -log -abslog="%LOGDIR%\Smoke_editor.log" %EXTRA%
exit /b %ERRORLEVEL%

:server
"%EDITOR_CMD%" "%PROJECT%" -server -nosound -unattended -nosplash -traceautostart=0 -ExecCmds="quit" -log -abslog="%LOGDIR%\Smoke_server.log" %EXTRA%
exit /b %ERRORLEVEL%
