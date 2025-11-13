@echo off
setlocal EnableDelayedExpansion

:: Change to the directory containing the batch file
cd /d "%~dp0"

:: this is some ghetto shit. sometimes ShaderMake.exe is output to its own folder
SET SHADER_MAKE_EXE=""
IF EXIST "%cd%/bin/ShaderMake.exe" (
    SET SHADER_MAKE_EXE="%cd%/bin/ShaderMake.exe"
) ELSE IF EXIST "%cd%/bin/Release/ShaderMake.exe" (
    SET SHADER_MAKE_EXE="%cd%/bin/Release/ShaderMake.exe"
) ELSE IF EXIST "%cd%/bin/Debug/ShaderMake.exe" (
    SET SHADER_MAKE_EXE="%cd%/bin/Debug/ShaderMake.exe"
) ELSE (
    echo ShaderMake.exe not found!
    pause
    exit
)

SET DXC_PATH="%cd%/extern/dxc/bin/x64/dxc.exe"
SET CONFIG_FILE="%cd%/shaderstocompile.txt"
SET OUT="%cd%/bin/shaders/"

SET RELAXED_INCLUDES=--relaxedInclude=MathUtilities.h
SET RELAXED_INCLUDES=%RELAXED_INCLUDES% --relaxedInclude=vaShared.hlsl
SET RELAXED_INCLUDES=%RELAXED_INCLUDES% --relaxedInclude=XeGTAO.h
SET RELAXED_INCLUDES=%RELAXED_INCLUDES% --relaxedInclude=../Types.h
SET RELAXED_INCLUDES=%RELAXED_INCLUDES% --relaxedInclude=rtxgi/Math.h
SET RELAXED_INCLUDES=%RELAXED_INCLUDES% --relaxedInclude=rtxgi/Types.h

SET INCLUDE_DIRS=--include="%cd%/"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/source/shaders/"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/extern/amd/FidelityFX/sdk/include/FidelityFX/gpu/"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/extern/nvidia/NRD/Shaders/Include"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/extern/nvidia/NRD/Shaders/Resources"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/build/_deps/mathlib-src"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/extern/nvidia/RTXGI-DDGI/rtxgi-sdk/include"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/extern/nvidia/RTXGI-DDGI/rtxgi-sdk/shaders/ddgi/include"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/extern/nvidia/MathLib/"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/extern/nvidia/RTXDI/include/"

:: SET COMPILER_OPTIONS="-Wconversion -Wdouble-promotion -Whlsl-legacy-literal"
:: SET COMPILER_OPTIONS="-HV 202x"

SET GLOBAL_DEFINES=-D FFX_GPU -D FFX_HLSL -D HLSL
SET GLOBAL_DEFINES=%GLOBAL_DEFINES% -D RTXGI_DDGI_USE_SHADER_CONFIG_FILE=1

:: global defines based on CMakeCache.txt
set "CACHE_FILE=%cd%\build\CMakeCache.txt"

call :SetGlobalDefineFromCMakeCache NRD_NORMAL_ENCODING
call :SetGlobalDefineFromCMakeCache NRD_ROUGHNESS_ENCODING
goto :AfterSetGlobalDefineFromCMakeCache

:SetGlobalDefineFromCMakeCache
set VARIABLE=%~1

set VALUE=-1
for /f "tokens=1,* delims==" %%a in ('findstr /r "%VARIABLE%" "%CACHE_FILE%"') do (
    set VALUE=%%b
)
if %VALUE% == -1 (
    echo Variable %VARIABLE% not found in CMakeCache.txt
) ELSE (
	echo %VARIABLE% : %VALUE%
	SET GLOBAL_DEFINES=%GLOBAL_DEFINES% -D %VARIABLE%=%VALUE%
)
goto :eof

:AfterSetGlobalDefineFromCMakeCache

%SHADER_MAKE_EXE% --platform="DXIL" --config=%CONFIG_FILE% --out=%OUT% --binaryBlob --compiler=%DXC_PATH% --shaderModel="6_8" --WX --embedPDB --matrixRowMajor %INCLUDE_DIRS% --outputExt=".bin" --continue --colorize %RELAXED_INCLUDES% --flatten --compilerOptions=%COMPILER_OPTIONS% %GLOBAL_DEFINES%

if not "%1" == "NO_PAUSE" (
	pause
)
