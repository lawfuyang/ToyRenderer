@echo off
setlocal

:confirm
set /p userInput=Full clean Git repo!!! Are you sure you want to continue? (Type YES to proceed): 
if /I "%userInput%"=="YES" goto confirmed

goto end

:confirmed
echo You typed YES. Proceeding with the operation...
git reset --hard
git clean -ffdx
git submodule foreach --recursive git clean -ffdx

:end
endlocal
pause
