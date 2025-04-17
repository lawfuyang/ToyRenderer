# Toy Renderer
This is a personal toy renderer meant as a playground for experimenting with ideas and rendering techniques.

## Features
- **Window handling & Mouse/KB I/O using [SDL3](https://github.com/libsdl-org/SDL)**
- **GLTF/GLB scene loading using [cgltf](https://github.com/jkuhlmann/cgltf)**
- **CPU/GPU Profiler using [microprofile](https://github.com/jonasmr/microprofile)**
- **Direct3D 12 Graphics API using fork of [NVRHI](https://github.com/NVIDIA-RTX/NVRHI)**
    - Fork changes:
        - Integration of [D3D12 Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator) into D3D12 codebase
        - **DispatchMeshIndirect** support
- **Offline Shader compilation & Hot Reloading using [ShaderMake](https://github.com/NVIDIA-RTX/ShaderMake)**
- **Multi-threaded Commandlist recording using [Taskflow](https://github.com/taskflow/taskflow)**
- **Render Graph**
    - Renderer scheduling
    - Resource dependency tracking & validation
    - Transient resource creation via Pooled Heaps
- **Mesh LODs generated with [meshoptimizer](https://github.com/zeux/meshoptimizer)**
- **GPU-Driven Rendering**
    - [2 Phase Occlusion Culling](https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf)
    - Meshlet rendering pipeline
    - Automatic mesh LOD selection based on quadric error metric
- **Deferred Shading**
    - Lambert Diffuse & Smith/Schlick Specular BRDF
    - [Densely packed GBuffer](https://docs.google.com/presentation/d/1kaeg2qMi3_8nQqoR3Y2Ax9fJKUYLigPLPfdjfuEGowY/edit?slide=id.g27be1a2457b_0_128#slide=id.g27be1a2457b_0_128)
- **Instance Transforms & Scene TLAS updates on GPU**
- **Ray-traced Directional Light Shadows**
    - Denoised using [Nvidia NRD](https://github.com/NVIDIA-RTX/NRD)
- **[Hosek-Wilkie Sky Model](https://cgg.mff.cuni.cz/projects/SkylightModelling/)**
- **HDR Pipeline**
    - Automatic camera exposure using luminance histogram
- **[Ground Truth Ambient Occlusion (XeGTAO)](https://github.com/GameTechDev/XeGTAO)**
- **[Physically-Based bloom](https://advances.realtimerendering.com/s2014/index.html#_NEXT_GENERATION_POST)**
- **Static Animations**
    - Linear Translation & Rotation only. No bones & skinning support yet.
- **GPU Ray-Traced Global Illumination with [Nvidia RTXGI-DDI](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)**

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
- Ctrl + Shift + 'comma' to dump performance capture for past 30 CPU/GPU frames

## Images
TBD...
