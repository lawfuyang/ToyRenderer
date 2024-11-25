@echo off
setlocal enabledelayedexpansion

:: Change to the directory containing the batch file
cd /d "%~dp0"

:: Check if CMake is installed
where cmake >nul 2>&1

if %errorlevel% neq 0 (
    echo ERROR: CMake is not installed.
    exit /b 1
)

:: Check MSVC version
for /f "tokens=3" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64" /v Version 2^>nul') do set "MSVCVersion=%%A"
if NOT defined MSVCVersion (
    echo Visual Studio 2022 not installed???
	exit /b 1
)

:: Check against cached MSVC version in registry
set "REG_KEY=HKCU\Software\ToyRenderer"
set "REG_VALUE=Cached_MSVC_Version"
reg query "%REG_KEY%" /v "%REG_VALUE%" >nul 2>&1
if %errorlevel%==0 (

    for /f "tokens=3" %%B in ('reg query "%REG_KEY%" /v "%REG_VALUE%" ^| findstr "%REG_VALUE%"') do set CachedMSVCVersion=%%B
	REM echo CachedMSVCVersion: %CachedMSVCVersion%
	
	if NOT "%CachedMSVCVersion%"=="%MSVCVersion%" (
        echo MSVC change detected.
		del "%cd%\projects\ToyRenderer\CMakeCache.txt"
    )
) else (
	echo No cached MSVC version found in registry
	del "%cd%\projects\ToyRenderer\CMakeCache.txt"
)

:: add MSVC version to registry
reg add "%REG_KEY%" /v "%REG_VALUE%" /t REG_SZ /d "%MSVCVersion%" /f

set "TMP_FOLDER=%cd%\tmp"

:: DXC
set "DXC_DEST_FOLDER=%cd%\extern\dxc"
if not exist "%DXC_DEST_FOLDER%" (
	call :DownloadAndExtractPackage https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31_clang_cl.zip dxc
	xcopy "%TMP_FOLDER%\dxc\bin\x64\*" "%DXC_DEST_FOLDER%\" /E /I /Y
)

:: SDL3
set "SDL3_DEST_FOLDER=%cd%\extern\SDL"
if not exist "%SDL3_DEST_FOLDER%" (
	call :DownloadAndExtractPackage https://github.com/libsdl-org/SDL/releases/download/preview-3.1.6/SDL3-devel-3.1.6-VC.zip SDL
	xcopy "%TMP_FOLDER%\SDL\SDL3-3.1.6\include\*" "%SDL3_DEST_FOLDER%\" /E /I /Y
	xcopy "%TMP_FOLDER%\SDL\SDL3-3.1.6\lib\x64\*" "%cd%\bin\"
)

goto :AfterDownloadPackages

:DownloadAndExtractPackage
set URL="%~1"
set PACKAGE_NAME=%~2
set "PACKAGE_ARCHIVE=%TMP_FOLDER%\%PACKAGE_NAME%.zip"

if not exist %PACKAGE_ARCHIVE% (
	mkdir "%TMP_FOLDER%\%PACKAGE_NAME%"
	powershell -Command "Invoke-WebRequest -Uri '%URL%' -OutFile '%PACKAGE_ARCHIVE%'"
)

powershell -Command "Expand-Archive -Path '%PACKAGE_ARCHIVE%' -DestinationPath '%TMP_FOLDER%\%PACKAGE_NAME%'" -Force

goto :eof

:AfterDownloadPackages

:: generate projects
call :CreateProject "%cd%" "%cd%\projects\ToyRenderer"
goto :AfterGenerateProjects

:CreateProject
set SRC_PATH="%~1"
set BUILD_PATH="%~2"
cmake -S %SRC_PATH% -B %BUILD_PATH%
echo:
echo:
goto :eof

:AfterGenerateProjects

:: create shortcuts to VS solutions in root folder
call :CreateSlnShortcut "%cd%\projects\ToyRenderer\ToyRenderer.sln" "%cd%\ToyRenderer.sln.lnk"
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

echo:
pause