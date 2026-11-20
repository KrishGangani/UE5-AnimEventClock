@echo off
rem P06 AnimEventClock - package a standalone Win64 build (BuildCookRun; the flags are the ones verified for UE5-UpdateBudget).
rem Usage: package.bat [Configuration]   Configuration: Development (default; all measurements) | Shipping (no CSV profiler, no net emulation; not used)
rem Note: the launcher (installed) engine cannot build the Test configuration or a Server target (Explanation/01b_Research_Findings.md).
rem Output: ..\Build\Windows\AnimEventClock.exe   (Build\ is git-ignored)
rem Run it through the machine-wide queue (tools\ue_queue.py), never directly.
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0..\AnimEventClock\AnimEventClock.uproject"
set "ARCHIVE=%~dp0..\Build"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Development"

call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJECT%" -noP4 -utf8output -unattended -platform=Win64 -clientconfig=%CONFIG% -build -cook -stage -pak -archive -archivedirectory="%ARCHIVE%"
exit /b %ERRORLEVEL%
