@SET INPUT="Sponza"
@SET INPUT_EXT="gltf"

gltfpack.exe -i %INPUT%.%INPUT_EXT% -o %INPUT%_Optimized.glb -tu -mi -noq -v

pause
