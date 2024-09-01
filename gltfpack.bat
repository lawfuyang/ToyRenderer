@SET INPUT="Sponza.gltf"
@SET OUTPUT="SponzaOptimized.glb"

gltfpack.exe -i %INPUT% -o %OUTPUT% -tu -noq -v

pause
