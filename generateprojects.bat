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

set "TMP_FOLDER=%cd%\tmp"

set "MSVC_CACHE_FILE=%USERPROFILE%\ToyRenderer_MSVC_Version_Cache.txt"

:: Check MSVC version
for /f "tokens=3" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64" /v Version 2^>nul') do set "MSVCVersion=%%A"
if NOT defined MSVCVersion (
    echo Visual Studio 2022 not installed???
	exit /b 1
)
echo MSVC Version: !MSVCVersion!

:: Check against cached MSVC version in txt file
if exist "%MSVC_CACHE_FILE%" (
    for /f "delims=" %%A in (%MSVC_CACHE_FILE%) do set "CachedMSVCVersion=%%A"
    
    rem trim white space
    set "CachedMSVCVersion=!CachedMSVCVersion: =!"
    
    echo Cached MSVC Version: !CachedMSVCVersion!
	
	if NOT "!CachedMSVCVersion!"=="!MSVCVersion!" (
        echo MSVC change detected.
		del "%cd%\projects\ToyRenderer\CMakeCache.txt"
    )
) else (
	echo No cached MSVC version found
	del "%cd%\projects\ToyRenderer\CMakeCache.txt"
)

:: cache msvc version to txt file
echo !MSVCVersion! > %MSVC_CACHE_FILE%

:: DXC
set "DXC_DEST_FOLDER=%cd%\extern\dxc"
if not exist "%DXC_DEST_FOLDER%" (
	call :DownloadAndExtractPackage https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31_clang_cl.zip dxc
	xcopy "%TMP_FOLDER%\dxc\bin\x64\*" "%DXC_DEST_FOLDER%\" /E /I /Y
)

:: SDL3
set "SDL3_DEST_FOLDER=%cd%\extern\SDL"
if not exist "%SDL3_DEST_FOLDER%" (
	call :DownloadAndExtractPackage https://github.com/libsdl-org/SDL/releases/download/release-3.2.2/SDL3-devel-3.2.2-VC.zip SDL
	xcopy "%TMP_FOLDER%\SDL\SDL3-3.2.2\include\*" "%SDL3_DEST_FOLDER%\" /E /I /Y
	xcopy "%TMP_FOLDER%\SDL\SDL3-3.2.2\lib\x64\*" "%cd%\bin\"
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