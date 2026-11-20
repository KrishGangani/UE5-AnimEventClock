@echo off
rem P06 AnimEventClock - run the automation tests headless in the editor and exit with their result.
rem Usage: run_tests.bat [Filter]   (default filter: AnimEventClock, i.e. every test of this project)
rem Exit code 0 = all passed; non-zero = a failure. Needs the Development editor target: build.bat
rem Log: ..\AnimEventClock\Saved\Logs\Tests.log. Run it through the machine-wide queue (tools\ue_queue.py), never directly.
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0..\AnimEventClock\AnimEventClock.uproject"
set "FILTER=%~1"
if "%FILTER%"=="" set "FILTER=AnimEventClock"
set "REPORT=%~dp0..\AnimEventClock\Saved\Automation\Reports"
set "LOG=%~dp0..\AnimEventClock\Saved\Logs\Tests.log"

"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -ExecCmds="Automation RunTests %FILTER%;Quit" -unattended -nullrhi -nosound -nosplash -nopause -traceautostart=0 -log -abslog="%LOG%" -ReportExportPath="%REPORT%"
exit /b %ERRORLEVEL%
