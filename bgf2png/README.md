# bgf2png
This program converts a BGF file into a set of PNGs. The program also outputs a JSON file with metadata such as shrink_factor, sprite and group counts, and the dimentions, offset, and hotspots of each image.
## Installation
You’ll need to build from source. First, install CMake and its dependencies. Then, in this directory, run:
```
mkdir build
cd build
cmake ..
cmake --build .
```
There should be a bgf2png executable file in the build directory if everything successfully compiled.
## Usage
From the build directory, run:
```
./bgf2png <path to bgf file>
```
When finished, the program will output the PNG and JSON files in the same directory.