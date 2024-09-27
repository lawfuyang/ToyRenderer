@echo off
setlocal

rem Change to the directory containing the batch file
cd /d "%~dp0"

@SET SHADER_MAKE_EXE="%cd%/bin/ShaderMake.exe"
@SET DXC_PATH="%cd%/extern/dxc/dxc.exe"
@SET CONFIG_FILE="%cd%/source/shaders/shaderstocompile.txt"
@SET OUT="%cd%/bin/shaders/"
@SET INCLUDE_DIRS=--include="%cd%/" --include="%cd%/source/shaders/" --include="%cd%/extern/amd/FidelityFX/include/FidelityFX/gpu/"
@SET RELAXED_INCLUDES=--relaxedInclude=MathUtilities.h --relaxedInclude=vaShared.hlsl --relaxedInclude=XeGTAO.h
@SET COMPILER_OPTIONS="-HV 2021"
rem TODO: -Wconversion
rem TODO: -Wdouble-promotion -Whlsl-legacy-literal

%SHADER_MAKE_EXE% --platform="DXIL" --config=%CONFIG_FILE% --out=%OUT% --binaryBlob --compiler=%DXC_PATH% --shaderModel="6_5" --WX --embedPDB --matrixRowMajor %INCLUDE_DIRS% --outputExt=".bin" --continue --colorize %RELAXED_INCLUDES% --flatten --compilerOptions=%COMPILER_OPTIONS% -D FFX_GPU -D FFX_HLSL

if not "%1" == "NO_PAUSE" (
	pause
)
