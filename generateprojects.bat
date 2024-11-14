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

:: IMGUI
set "IMGUI_DEST_FOLDER=%cd%\extern\imgui"
if not exist "%IMGUI_DEST_FOLDER%" (
	call :DownloadAndExtractPackage https://github.com/ocornut/imgui/archive/refs/tags/v1.91.5.zip imgui
	
	:: .h/.cpp files in immediate dir
	for %%f in ("%TMP_FOLDER%\imgui\imgui-1.91.5\*.h" "%TMP_FOLDER%\imgui\imgui-1.91.5\*.cpp") do (
		xcopy "%%f" "%IMGUI_DEST_FOLDER%\"
	)

	xcopy "%TMP_FOLDER%\imgui\imgui-1.91.5\backends\imgui_impl_win32.h" "%IMGUI_DEST_FOLDER%\"
	xcopy "%TMP_FOLDER%\imgui\imgui-1.91.5\backends\imgui_impl_win32.cpp" "%IMGUI_DEST_FOLDER%\"
	xcopy "%TMP_FOLDER%\imgui\imgui-1.91.5\misc\cpp\imgui_stdlib.h" "%IMGUI_DEST_FOLDER%\"
	xcopy "%TMP_FOLDER%\imgui\imgui-1.91.5\misc\cpp\imgui_stdlib.cpp" "%IMGUI_DEST_FOLDER%\"
)

:: DXC
set "DXC_DEST_FOLDER=%cd%\extern\dxc"
if not exist "%DXC_DEST_FOLDER%" (
	call :DownloadAndExtractPackage https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31_clang_cl.zip dxc
	xcopy "%TMP_FOLDER%\dxc\bin\x64\*" "%DXC_DEST_FOLDER%\" /E /I /Y
)

:: Nvidia Aftermath
set "AFTERMATH_DEST_FOLDER=%cd%\extern\nvidia\aftermath"
if not exist "%AFTERMATH_DEST_FOLDER%" (
    echo %AFTERMATH_DEST_FOLDER% doesn't exist!
	call :DownloadAndExtractPackage https://developer.nvidia.com/downloads/assets/tools/secure/nsight-aftermath-sdk/2024_2_0/windows/NVIDIA_Nsight_Aftermath_SDK_2024.2.0.24200.zip aftermath

	xcopy "%TMP_FOLDER%\aftermath\include\*" "%AFTERMATH_DEST_FOLDER%\" /E /I /Y
	xcopy "%TMP_FOLDER%\aftermath\lib\x64\*" "%AFTERMATH_DEST_FOLDER%\" /E /I /Y
	xcopy "%AFTERMATH_DEST_FOLDER%\GFSDK_Aftermath_Lib.x64.dll" "%cd%\bin\" /E /I /Y
)

:: AMD FidelityFX SDK
set "FFX_DEST_FOLDER=%cd%\extern\amd\FidelityFX"
if not exist "%FFX_DEST_FOLDER%" (
	call :DownloadAndExtractPackage https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/archive/refs/tags/v1.1.2.zip ffx

	xcopy "%TMP_FOLDER%\ffx\FidelityFX-SDK-1.1.2\sdk\include\FidelityFX\gpu\*" "%FFX_DEST_FOLDER%\include\FidelityFX\gpu\" /E /I /Y
	xcopy "%TMP_FOLDER%\ffx\FidelityFX-SDK-1.1.2\sdk\src\backends\dx12\shaders\*" "%FFX_DEST_FOLDER%\src\backends\dx12\shaders\" /E /I /Y
)

:: Magic Enum
set "MAGIC_ENUM_DEST_FOLDER=%cd%\extern\magic_enum"
if not exist "%MAGIC_ENUM_DEST_FOLDER%" (
	call :DownloadAndExtractPackage https://github.com/Neargye/magic_enum/archive/refs/tags/v0.9.7.zip magic_enum

	xcopy "%TMP_FOLDER%\magic_enum\magic_enum-0.9.7\include\magic_enum\*" "%MAGIC_ENUM_DEST_FOLDER%\" /E /I /Y
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
call :CreateProject "%cd%\extern\ShaderMake" "%cd%\projects\ShaderMake"
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

echo:
pause