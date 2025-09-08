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
There should be a roo2obj executable file in the build directory if everything successfully compiled.
## Usage
From the build directory, run:
```
./roo2obj <.roo file path> <texture directory path> <output directory path>
```
The texture directory path should be a folder that contains all the tile BGF files, but upacked into pngs and json (use bgf2png for this). 

When finished, the program will output the OBJ and MTL files to the specified output directory.
## Texture Directory
The MTL file uses this directory to locate textures for mesh faces. A script is included to set it up automatically. From the scripts directory, run:
```
./make_tex_dir <resource directory path> <path to bgf2png executable>
```
When finished, the script should output a directory called "textures" in the same directory.
## Importing into Blender
There are some visual issues when directly importing the OBJ files in Blender. To fix this, open the Scripts tab and copy the code from ```scripts/blender_fixes.py```, and run it. The script adjusts materials with transparency so they render properly, switches texture sampling to nearest neighbor for a sharper pixelated look, and enables backface culling.