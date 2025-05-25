@echo off
setlocal

cmake --build ./build -t ShaderMake --config Debug
cmake --build ./build -t ToyRenderer --config Debug

pause
