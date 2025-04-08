# Toy Renderer

This is a personal toy renderer meant as a playground for experimenting with ideas and rendering techniques.

## Features
- **TBD...**

## Building

- cmake -S . -B ./projects/ToyRenderer
- cmake --build ./projects/ToyRenderer -t ShaderMake
- cmake --build ./projects/ToyRenderer -t ToyRenderer

## Running

Use "**--scene**" commandline argument to load a scene. Example:

    --scene "C:\Workspace\Sponza.gltf"

- Only gltf & glb. The following extensions will assert:
    - EXT_mesh_gpu_instancing
    - KHR_texture_transform
    - KHR_texture_basisu
- No alpha blending, only opaque materials with Alpha Mask
- Only textures supported by [stb_image](https://github.com/nothings/stb/blob/master/stb_image.h), or DDS
- 
