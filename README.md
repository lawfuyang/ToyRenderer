This is a personal toy renderer meant as a playground for experimenting with ideas and rendering techniques.

## Features
- **Direct3D 12 Graphics API using [NVRHI](https://github.com/NVIDIA-RTX/NVRHI)**
- **Deferred Shading**
    - [Densely packed GBuffer](https://docs.google.com/presentation/d/1kaeg2qMi3_8nQqoR3Y2Ax9fJKUYLigPLPfdjfuEGowY/edit?slide=id.g27be1a2457b_0_128#slide=id.g27be1a2457b_0_128)
- **Instance Transforms & Scene TLAS updates on GPU**
- **Ray-traced Directional Light Shadows**
    - Denoised using [Nvidia NRD](https://github.com/NVIDIA-RTX/NRD)
- **[Hosek-Wilkie Sky Model](https://cgg.mff.cuni.cz/projects/SkylightModelling/)**
- **HDR Pipeline**
- **[Ground Truth Ambient Occlusion(XeGTAO)](https://github.com/GameTechDev/XeGTAO)**
- **[Physically-Based bloom](https://advances.realtimerendering.com/s2014/index.html#_NEXT_GENERATION_POST)**
- **[GPU Ray-Traced Global Illumination (WIP)](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)**
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
