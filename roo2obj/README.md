# roo2obj
This program converts a ROO file into a 3D OBJ mesh, outputting a .obj file with vertex data and a .mtl file with material data.
## Installation
You’ll need to build from source. First, install CMake and its dependencies. Then, in this directory, run:
```
mkdir build
cd build
cmake ..
cmake --build .
```
There should be a `roo2obj` executable file in the build directory if everything successfully compiled.
## Usage
From the build directory, run:
```
./roo2obj <.roo file path> <texture directory path> <output directory path>
```
The texture directory path should be a folder that contains all the "grdXXXXX.bgf" BGF files, but upacked into pngs and json (use bgf2png for this). 

When finished, the program will output the OBJ and MTL files to the specified output directory.
## Texture Directory
The MTL file uses this directory to locate textures for mesh faces. A script is included to set it up automatically. From the scripts directory, run:
```
./make_tex_dir <resource directory path> <path to bgf2png executable>
```
When finished, the script should output a directory called "textures" in the same directory.
## Importing into Blender
The exported OBJ has some quirks: the units are very large, the forward axis is Y, the up axis is Z, and the orientation is mirrored compared to how the ROO file appears in-game. To get it looking correct in Blender:
- Drag and drop the OBJ into Blender.
- In the import menu, set the scale to around **0.001**.
- Set **Y** as the forward axis and **Z** as the up axis, then press *Import*.
- In the object properties, set **Scale X** to negative (this flips the model).
- Switch to the *Scripting* tab, create a new script, and paste in the contents of `scripts/blender_fixes.py`. Run the script.

The python script adjusts materials with transparency so they render properly, switches texture sampling to nearest neighbor for a sharper pixelated look, and enables backface culling.
## Notes on Textures
If textures fail to render, it’s usually because the paths in the generated MTL file don’t match the actual texture directory. The MTL file expects texture paths to be relative to its location, so Blender may fail to find them if the directory structure isn’t aligned.

As a quick workaround, run `roo2obj` with an **absolute path** to the texture directory as the second argument. This ensures the MTL points directly to the correct files and makes Blender load the textures reliably. This workaround is only meant for viewing or exporting in Blender and shouldn’t be used for distribution. A better long-term solution is to export to a modern format like glTF (which supports embedded textures), or to ensure the MTL file uses correct relative paths.
## TODO
- Update the "Importing into Blender" section to remove the transformations. Only keep the step to run the pyton script.
- Fix normals
- Fix sloped floors / ceiling texture coordinates.