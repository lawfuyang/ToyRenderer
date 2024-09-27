@echo off
setlocal enabledelayedexpansion

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

rem download dxc

rem Check if dxc.exe already exists
set DXC_EXEC=%cd%\extern\dxc\dxc.exe
set DXC_DEST_FOLDER=%cd%\extern\dxc
if exist "%DXC_EXEC%" (

    echo DXC files found in %DXC_DEST_FOLDER%

    set "DESIRED_DXC_VERSION_STRING=dxcompiler.dll: 1.8 - 1.8.2407.7 (416fab6b5); dxil.dll: 1.8(101.8.2407.12)"
    
    :: Run dxc.exe and capture the version output
    for /f "delims=" %%i in ('"%DXC_EXEC%" --version') do set "currentVersionString=%%i"
    
    if "!currentVersionString!"=="!DESIRED_DXC_VERSION_STRING!" (
        echo Desired DXC version found. Skipping Download.
        echo:
        goto :AfterDownloadDXC
    ) else (
        echo Desired DXC version mismatch. Proceeding to download DXC and overrite existing files.
    )
)

set DXC_URL=https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31_clang_cl.zip
set DXC_ZIP_FILE=dxc.zip
set DXC_TMP_FOLDER=%TEMP%\dxc

echo Downloading DXC .zip file to '%DXC_TMP_FOLDER%'
mkdir "%DXC_TMP_FOLDER%"
powershell -Command "Invoke-WebRequest -Uri '%DXC_URL%' -OutFile '%DXC_TMP_FOLDER%\%DXC_ZIP_FILE%'"

echo Extracting DXC .zip files
powershell -Command "Expand-Archive -Path '%DXC_TMP_FOLDER%\%DXC_ZIP_FILE%' -DestinationPath '%DXC_TMP_FOLDER%'"

echo Copying DXC files to '%DXC_DEST_FOLDER%'
mkdir "%DXC_DEST_FOLDER%" 2>nul
xcopy "%DXC_TMP_FOLDER%\bin\x64\*" "%DXC_DEST_FOLDER%\" /E /I /Y

echo Deleting '%DXC_TMP_FOLDER%'
rd /S /Q "%DXC_TMP_FOLDER%"

:AfterDownloadDXC

rem create shortcuts to VS solutions in root folder
call :CreateSlnShortcut "%cd%\projects\ToyRenderer\ToyRenderer.sln" "%cd%\ToyRenderer.sln.lnk"
call :CreateSlnShortcut "%cd%\projects\ShaderMake\ShaderMake.sln" "%cd%\ShaderMake.sln.lnk"
goto :AfterCreateSlnShortcuts

:CreateSlnShortcut
set TargetPath="%~1"
set ShortcutPath="%~2"
echo Creating shortcut for %TargetPath%
echo Set objShell = WScript.CreateObject("WScript.Shell") >> CreateShortcut.vbs
echo Set objShortcut = objShell.CreateShortcut(%ShortcutPath%) >> CreateShortcut.vbs
echo objShortcut.TargetPath = %TargetPath% >> CreateShortcut.vbs
echo objShortcut.Save>> CreateShortcut.vbs
cscript //nologo CreateShortcut.vbs
del CreateShortcut.vbs
goto :eof

:AfterCreateSlnShortcuts

echo:
pause