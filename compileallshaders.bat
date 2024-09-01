@echo off
setlocal

rem Change to the directory containing the batch file
cd /d "%~dp0"

@SET SHADER_MAKE_EXE="%cd%/bin/ShaderMake.exe"
@SET CONFIG_FILE="%cd%/source/shaders/shaderstocompile.txt"
@SET OUT="%cd%/bin/shaders/"
@SET INCLUDE_DIRS=--include="%cd%/" --include="%cd%/source/shaders/" --include="%cd%/extern/amd/FidelityFX/include/FidelityFX/gpu/"
@SET RELAXED_INCLUDES=--relaxedInclude=MathUtilities.h --relaxedInclude=vaShared.hlsl --relaxedInclude=XeGTAO.h

set cache_file="%cd%\\projects\\CMakeCache.txt"
set DXC_PATH_VAR=DXC_PATH:FILEPATH

for /f "tokens=2 delims==" %%a in ('findstr "^%DXC_PATH_VAR%=" %cache_file%') do (
  set "DXC_PATH="%%a""
)

if not defined DXC_PATH (
    echo Cannot find DXC shader compiler file path from CMakeCache.txt. Did you run generateprojects.bat?
)

%SHADER_MAKE_EXE% --platform="DXIL" --config=%CONFIG_FILE% --out=%OUT% --binaryBlob --compiler=%DXC_PATH% --shaderModel="6_5" --WX --embedPDB --matrixRowMajor %INCLUDE_DIRS% --outputExt=".bin" --continue --colorize %RELAXED_INCLUDES% --flatten --hlsl2021 -D FFX_GPU -D FFX_HLSL

if not "%1" == "NO_PAUSE" (
	pause
)
