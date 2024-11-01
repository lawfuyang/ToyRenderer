# Toy Renderer

This is a personal toy renderer meant as a playground for experimenting with ideas and rendering techniques.

## Building

Please don't. But if you wish to: 

- Run `generateprojects.bat` to generate VS project files & download required binaries.
- Open `ShaderMake.sln` and Compile. ShaderMake.exe is used to compile Shaders.
- Open `ToyRenderer.sln` and Compile/Run

## Running

Use "**--scene**" commandline arguments to load a scene

Example:

    --scene "C:\Workspace\Sponza.glb"

- Only gltf & glb supported.
- Only opaque material for now. Nothing fancy.
