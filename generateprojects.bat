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

set TMP_FOLDER=%cd%\tmp

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

call :DownloadPackage https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31_clang_cl.zip dxc

echo Copying DXC files to '%DXC_DEST_FOLDER%'
mkdir "%DXC_DEST_FOLDER%" 2>nul
xcopy "%TMP_FOLDER%\dxc\bin\x64\*" "%DXC_DEST_FOLDER%\" /E /I /Y

:AfterDownloadDXC

rem check & download Nvidia Aftermath

set "AFTERMATH_DEST_FOLDER=%cd%\extern\nvidia\aftermath"
if exist "%AFTERMATH_DEST_FOLDER%\GFSDK_Aftermath.h" (
    echo Nvidia Aftermath files found in %AFTERMATH_DEST_FOLDER%. Skipping Download.
	echo:
	goto :AfterDownloadAftermath
)

call :DownloadPackage https://developer.nvidia.com/downloads/assets/tools/secure/nsight-aftermath-sdk/2024_2_0/windows/NVIDIA_Nsight_Aftermath_SDK_2024.2.0.24200.zip  aftermath

echo Copying Nvidia Aftermath files to '%AFTERMATH_DEST_FOLDER%'
mkdir "%AFTERMATH_DEST_FOLDER%" 2>nul
xcopy "%TMP_FOLDER%\aftermath\include\*" "%AFTERMATH_DEST_FOLDER%\" /E /I /Y
xcopy "%TMP_FOLDER%\aftermath\lib\x64\*" "%AFTERMATH_DEST_FOLDER%\" /E /I /Y
xcopy "%AFTERMATH_DEST_FOLDER%\GFSDK_Aftermath_Lib.x64.dll" "%cd%\bin\" /E /I /Y

:AfterDownloadAftermath

goto :AfterDownloadPackages

:DownloadPackage
set URL="%~1"
set PACKAGE_NAME=%~2

echo Downloading '%PACKAGE_NAME%' to '%TMP_FOLDER%\%PACKAGE_NAME%'
mkdir "%TMP_FOLDER%\%PACKAGE_NAME%"
powershell -Command "Invoke-WebRequest -Uri '%URL%' -OutFile '%TMP_FOLDER%\%PACKAGE_NAME%\tmp.zip'"

echo Extracting '%PACKAGE_NAME%'
powershell -Command "Expand-Archive -Path '%TMP_FOLDER%\%PACKAGE_NAME%\tmp.zip' -DestinationPath '%TMP_FOLDER%\%PACKAGE_NAME%'"
goto :eof

:AfterDownloadPackages

if exist %TMP_FOLDER% (
	echo Deleting tmp folder
	rd /S /Q %TMP_FOLDER%"
)

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