@echo off
rem P06 AnimEventClock - build one target from the command line (reproducible; no IDE needed).
rem Usage: build.bat [Target] [Configuration]
rem   Target:        AnimEventClockEditor (default) | AnimEventClock
rem   Configuration: Development (default) | DebugGame | Shipping   (the launcher engine cannot build Test)
rem Run it through the machine-wide queue (tools\ue_queue.py), never directly: one UE job at a time on this machine.
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0..\AnimEventClock\AnimEventClock.uproject"
set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=AnimEventClockEditor"
set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=Development"

call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" %TARGET% Win64 %CONFIG% -Project="%PROJECT%" -WaitMutex -NoHotReloadFromIDE
exit /b %ERRORLEVEL%
