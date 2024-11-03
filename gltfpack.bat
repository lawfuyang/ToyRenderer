@echo off
setlocal enabledelayedexpansion

set "filename="
set "extension="

rem Loop through .gltf and .glb files in the folder
for %%F in (*.gltf *.glb) do (
    set "filename=%%~nF"
    set "extension=%%~xF"
	
	echo Filename: !filename!
    echo Extension: !extension!
	
	gltfpack.exe -i !filename!!extension! -o !filename!_Optimized.glb -tu -cc -v
)

endlocal

pause
