@echo off

rem Get start time
set start_time=%time%

rem Change to the directory containing the batch file
cd /d "%~dp0"

rem Search for cmake.exe in the system's PATH
for /f "delims=" %%a in ('where cmake.exe 2^>nul') do (
    set "CMAKE_PATH="%%a""
    goto found
)

rem If cmake.exe wasn't found, display an error message and exit
echo ERROR: cannot find CMake.exe. Did you install it?
exit /b 1

:found
echo Found cmake.exe at %CMAKE_PATH%
echo:
echo:

rem generate projects
call :CreateProject "%cd%" "%cd%\projects\ToyRenderer"
call :CreateProject "%cd%\extern\ShaderMake" "%cd%\projects\ShaderMake"
goto :AfterGenerateProjects

:CreateProject
set SRC_PATH="%~1"
set BUILD_PATH="%~2"
echo Generating %SRC_PATH%...
%CMAKE_PATH% -S %SRC_PATH% -B %BUILD_PATH%
echo:
echo:
goto :eof

:AfterGenerateProjects

rem create shortcuts to VS solutions in root folder
call :CreateSlnShortcut "%cd%\projects\ToyRenderer\ToyRenderer.sln" "%cd%\ToyRenderer.sln.lnk"
call :CreateSlnShortcut "%cd%\projects\ShaderMake\ShaderMake.sln" "%cd%\ShaderMake.sln.lnk"
goto :AfterCreateSlnShortcuts

:CreateSlnShortcut
set TargetPath="%~1"
set ShortcutPath="%~2"
echo Set objShell = WScript.CreateObject("WScript.Shell") >> CreateShortcut.vbs
echo Set objShortcut = objShell.CreateShortcut(%ShortcutPath%) >> CreateShortcut.vbs
echo objShortcut.TargetPath = %TargetPath% >> CreateShortcut.vbs
echo objShortcut.Save>> CreateShortcut.vbs
cscript //nologo CreateShortcut.vbs
del CreateShortcut.vbs
goto :eof

:AfterCreateSlnShortcuts

rem Get end time
set end_time=%time%

rem Calculate elapsed time
set /a seconds=(%end_time:~6,2% - %start_time:~6,2% + 60) %% 60
set /a milliseconds=(%end_time:~9,2% - %start_time:~9,2% + 100) %% 1000

echo Elapsed time: %seconds%.%milliseconds% seconds

pause