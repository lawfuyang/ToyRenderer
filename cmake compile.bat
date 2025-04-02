@echo off
setlocal

cmake --build ./projects/ToyRenderer -t ShaderMake --config Debug
cmake --build ./projects/ToyRenderer -t ToyRenderer --config Debug

pause
