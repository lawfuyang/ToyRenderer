@echo off
setlocal

rem Change to the directory containing the batch file
cd /d "%~dp0"

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
SET CONFIG_FILE="%cd%/source/shaders/shaderstocompile.txt"
SET OUT="%cd%/bin/shaders/"

SET RELAXED_INCLUDES=--relaxedInclude=MathUtilities.h
SET RELAXED_INCLUDES=%RELAXED_INCLUDES% --relaxedInclude=vaShared.hlsl
SET RELAXED_INCLUDES=%RELAXED_INCLUDES% --relaxedInclude=vaShared.hlsl --relaxedInclude=XeGTAO.h

SET INCLUDE_DIRS=--include="%cd%/"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/source/shaders/"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/extern/amd/FidelityFX/sdk/include/FidelityFX/gpu/"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/extern/nvidia/NRD/Shaders/Include"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/extern/nvidia/NRD/Shaders/Resources"
SET INCLUDE_DIRS=%INCLUDE_DIRS% --include="%cd%/projects/ToyRenderer/_deps/mathlib-src"

SET GLOBAL_DEFINES=-D FFX_GPU -D FFX_HLSL
SET GLOBAL_DEFINES=%GLOBAL_DEFINES% -D NRD_NORMAL_ENCODING=2
SET GLOBAL_DEFINES=%GLOBAL_DEFINES% -D NRD_ROUGHNESS_ENCODING=1

rem SET COMPILER_OPTIONS="-Wconversion -Wdouble-promotion -Whlsl-legacy-literal"
rem SET COMPILER_OPTIONS="-HV 202x"

%SHADER_MAKE_EXE% --platform="DXIL" --config=%CONFIG_FILE% --out=%OUT% --binaryBlob --compiler=%DXC_PATH% --shaderModel="6_6" --WX --embedPDB --matrixRowMajor %INCLUDE_DIRS% --outputExt=".bin" --continue --colorize %RELAXED_INCLUDES% --flatten --compilerOptions=%COMPILER_OPTIONS% %GLOBAL_DEFINES%

if not "%1" == "NO_PAUSE" (
	pause
)
