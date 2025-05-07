# Toy Renderer

## Building

- run `generateprojects.bat`
- run `cmake compile.bat`
- Open `ToyRenderer.sln` and Compile/Run

## Running

Use "**--scene**" commandline argument to load a scene. Example:

    --scene "C:\Workspace\Sponza.gltf"

- Only gltf & glb. The following extensions will assert:
    - EXT_mesh_gpu_instancing
    - KHR_texture_transform
    - KHR_texture_basisu
- No alpha blending, only opaque materials with Alpha Mask
- Only textures supported by [stb_image](https://github.com/nothings/stb/blob/master/stb_image.h), or DDS

## Images
### Meshlet Rendering Pipeline
|Lit|Instances|Meshlets|
|---|---|---|
|![image](https://github.com/user-attachments/assets/9120e30d-d74f-4b66-8593-d2ca497bbb2a)|![image](https://github.com/user-attachments/assets/d8cf01e1-058a-42cb-8f6c-e7e5b5e5d2c1)|![image](https://github.com/user-attachments/assets/728c77c6-0c94-40bb-ba35-8f793899d406)|

### Raytraced Dynamic Diffuse Global Illumination (RTX-DDGI)
|With|Without|
|---|---|
|![image](https://github.com/user-attachments/assets/f3d35e0e-8bd2-4ee9-842c-8e031567c6e0)|![image](https://github.com/user-attachments/assets/6d47b57d-6f82-446d-ad2c-3e383f9e6c88)|
|![image](https://github.com/user-attachments/assets/e29978c2-c0eb-4b7d-9c54-e053da41cb38)|![image](https://github.com/user-attachments/assets/15c3d15b-c251-421c-8d3d-f126ac918393)|
|![image](https://github.com/user-attachments/assets/32344b4b-4baf-4770-82b0-80041c4dc9a5)|![image](https://github.com/user-attachments/assets/e2cc7e9c-12e6-4fd0-b900-7cf9a8fe6333)|

|Cornell|Debug Probes|
|---|---|
|![image](https://github.com/user-attachments/assets/03aa9f4c-a00e-4b1e-9205-fc32956311d7)|![image](https://github.com/user-attachments/assets/585bc9f3-5a09-4865-9d98-eee7858afdfe)|

### Raytraced Shadows
|Raw|Cone Traced|Denoised|
|---|---|---|
|![image](https://github.com/user-attachments/assets/71e5fb32-a282-4b62-9f4b-2bb4d39d6bf8)|![image](https://github.com/user-attachments/assets/2bc7ceaa-de62-4ee4-9d17-e42846265098)|![image](https://github.com/user-attachments/assets/de992f1c-e413-408c-aab2-188dc238450d)|
|![image](https://github.com/user-attachments/assets/a4cca08b-c68c-44aa-b239-f69cc58e1530)|![image](https://github.com/user-attachments/assets/ae382317-ea28-4284-8eac-ea4586443f0a)|![image](https://github.com/user-attachments/assets/81001d97-28e7-451f-89c1-157f3d7f3bf9)|

### Ambient Occlusion (XeGTAO)
|With|Without|Debug|
|---|---|---|
|![image](https://github.com/user-attachments/assets/827be21d-e9ef-4b9c-b89b-24a3964f8cf5)|![image](https://github.com/user-attachments/assets/4e2da59d-2b97-40e6-83ee-353fd87c04f3)|![image](https://github.com/user-attachments/assets/140b25b5-06c1-4b6d-8467-665f3848e47f)|

## Notable Features
- **Window handling & Mouse/KB I/O using [SDL3](https://github.com/libsdl-org/SDL)**
- **GLTF/GLB scene loading using [cgltf](https://github.com/jkuhlmann/cgltf)**
- **CPU/GPU Profiler using [microprofile](https://github.com/jonasmr/microprofile)**
- **Direct3D 12 Graphics API using fork of [NVRHI](https://github.com/NVIDIA-RTX/NVRHI)**
    - Fork changes:
        - [D3D12 Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator)
        - [DispatchMesh Indirect](https://microsoft.github.io/DirectX-Specs/d3d/MeshShader.html#executeindirect)
        - [Pipeline Statistics](https://microsoft.github.io/DirectX-Specs/d3d/MeshShader.html#pipeline-statistics)
        - [HLSL Dynamic Resources](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_6_DynamicResources.html) (WIP)
- **Offline Shader compilation & Hot Reloading using [ShaderMake](https://github.com/NVIDIA-RTX/ShaderMake)**
- **Multi-threaded Commandlist recording using [Taskflow](https://github.com/taskflow/taskflow)** (WIP)
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
    - [Automatic Exposure Using a Luminance Histogram](https://bruop.github.io/exposure/)
- **[Ground Truth Ambient Occlusion (XeGTAO)](https://github.com/GameTechDev/XeGTAO)**
- **[Physically-Based bloom](https://advances.realtimerendering.com/s2014/index.html#_NEXT_GENERATION_POST)**
- **Static Animations**
    - Linear Translation & Rotation only. No bones & skinning support yet.
- **GPU Ray-Traced Global Illumination with [Nvidia RTXGI-DDI](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)**
- **Texture mip streaming with [Sampler Feedback](https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html)** (WIP)
