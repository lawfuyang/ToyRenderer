@echo off

rem Get start time
set start_time=%time%

rem Change to the directory containing the batch file
cd /d "%~dp0"

@SET PATH_TO_SRC="%cd%"
@SET PATH_TO_BUILD="%cd%\projects"

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

%CMAKE_PATH% -S %PATH_TO_SRC% -B %PATH_TO_BUILD% --fresh

rem create shortcut to ToyRenderer.sln in root folder
set TargetPath="%cd%\projects\ToyRenderer.sln"
set ShortcutPath="%cd%\ToyRenderer.sln.lnk"
echo Set objShell = WScript.CreateObject("WScript.Shell") >> CreateShortcut.vbs
echo Set objShortcut = objShell.CreateShortcut(%ShortcutPath%) >> CreateShortcut.vbs
echo objShortcut.TargetPath = %TargetPath% >> CreateShortcut.vbs
echo objShortcut.Save>> CreateShortcut.vbs
cscript //nologo CreateShortcut.vbs
del CreateShortcut.vbs

rem Get end time
set end_time=%time%

rem Calculate elapsed time
set /a hours=(%end_time:~0,2% - %start_time:~0,2% + 24) %% 24
set /a minutes=(%end_time:~3,2% - %start_time:~3,2% + 60) %% 60
set /a seconds=(%end_time:~6,2% - %start_time:~6,2% + 60) %% 60
set /a milliseconds=(%end_time:~9,2% - %start_time:~9,2% + 100) %% 1000

echo Elapsed time: %seconds%.%milliseconds% seconds

pause