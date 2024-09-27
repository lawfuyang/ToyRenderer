@echo off
setlocal enabledelayedexpansion

rem Change to the directory containing the batch file
cd /d "%~dp0"

rem Check if CMake is installed
where cmake >nul 2>&1

if %errorlevel% neq 0 (
    echo ERROR: CMake is not installed.
    exit /b 1
)

rem check & download DXC
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

rem check & download Nvidia Aftermath

set "AFTERMATH_DEST_FOLDER=%cd%\extern\nvidia\aftermath"
if exist "%AFTERMATH_DEST_FOLDER%\GFSDK_Aftermath.h" (
    echo Nvidia Aftermath files found in %AFTERMATH_DEST_FOLDER%. Skipping Download.
	echo:
	goto :AfterDownloadAftermath
)

set AFTERMATH_URL=https://developer.nvidia.com/downloads/assets/tools/secure/nsight-aftermath-sdk/2024_2_0/windows/NVIDIA_Nsight_Aftermath_SDK_2024.2.0.24200.zip
set AFTERMATH_ZIP_FILE=aftermath.zip
set AFTERMATH_TMP_FOLDER=%TEMP%\aftermath

echo Downloading Nvidia Aftermath .zip file to '%AFTERMATH_TMP_FOLDER%'
mkdir "%AFTERMATH_TMP_FOLDER%"
powershell -Command "Invoke-WebRequest -Uri '%AFTERMATH_URL%' -OutFile '%AFTERMATH_TMP_FOLDER%\%AFTERMATH_ZIP_FILE%'"

echo Extracting Nvidia Aftermath .zip files
powershell -Command "Expand-Archive -Path '%AFTERMATH_TMP_FOLDER%\%AFTERMATH_ZIP_FILE%' -DestinationPath '%AFTERMATH_TMP_FOLDER%'"

echo Copying Nvidia Aftermath files to '%AFTERMATH_DEST_FOLDER%'
mkdir "%AFTERMATH_DEST_FOLDER%" 2>nul
xcopy "%AFTERMATH_TMP_FOLDER%\include\*" "%AFTERMATH_DEST_FOLDER%\" /E /I /Y
xcopy "%AFTERMATH_TMP_FOLDER%\lib\x64\*" "%AFTERMATH_DEST_FOLDER%\" /E /I /Y
xcopy "%AFTERMATH_DEST_FOLDER%\GFSDK_Aftermath_Lib.x64.dll" "%cd%\bin\" /E /I /Y

echo Deleting '%AFTERMATH_TMP_FOLDER%'
rd /S /Q "%AFTERMATH_TMP_FOLDER%"

:AfterDownloadAftermath

rem generate projects
call :CreateProject "%cd%" "%cd%\projects\ToyRenderer"
call :CreateProject "%cd%\extern\ShaderMake" "%cd%\projects\ShaderMake"
goto :AfterGenerateProjects

:CreateProject
set SRC_PATH="%~1"
set BUILD_PATH="%~2"
echo Generating %SRC_PATH%...
cmake -S %SRC_PATH% -B %BUILD_PATH%
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