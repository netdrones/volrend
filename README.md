# PlenOctree Volume Rendering

This is a real-time volume renderer using OpenGL.
Here, we use a octre (or more generally N^3 tree) to model volumetric emission and absorption at RGB frequencies

Each voxel cell contains `(k, sigma)`,
where `k` are SH, SG, or ASG coefficients to model view-dependency, and
alpha composition is applied via the rule `1 - exp(-length * sigma)`.
The length within each grid cell is exactly computed.

## Build
Build using CMake as usual:

### Unix-like Systems
```sh
mkdir build && cd build
cmake ..
make -j12
```

- If you do not have CUDA-capable GPU, pass `-DVOLREND_USE_CUDA=OFF` after `cmake ..` to use fragment shader backend, which is also used for the web demo.
  It is slower and does not support mesh-insertion and dependent features such as lumisphere probe.

A real-time rendererer `volrend` and a headless version `volrend_headless` are built. The latter requires CUDA.

On Ubuntu, to get the dependencies, try
`sudo apt-get install libgl1-mesa-dev libxi-dev libxinerama-dev libxcursor-dev libxrandr-dev libgl1-mesa-dev libglu1-mesa-dev`

### Windows 10
Install Visual Studio 2019. Then
```sh
mkdir build && cd build
cmake .. -G"Visual Studio 16 2019"
cmake --build . --config Release
```
- If you do not have CUDA-capable GPU, pass `-DVOLREND_USE_CUDA=OFF` after `cmake ..` to use fragment shader backend, which is also used for the web demo.
  It is slower and does not support mesh-insertion and dependent features such as lumisphere probe.

A real-time rendererer `volrend` and a headless version `volrend_headless` are built. The latter requires CUDA.

### Dependencies
- C++17
- OpenGL
    - any dependencies of GLFW
- libpng-dev (only for writing image in headless mode and saving screenshot)

#### Optional
- CUDA Toolkit, I use 11.0
    - Pass `-DVOLREND_USE_CUDA=OFF` to disable it.

## Run
```sh
./volrend <name>.npz
```
See `--help` for flags.
For LLFF scenes, we also expect
```sh
<name>_poses_bounds.npy
```
In the same directory. This may be copied directly from the scene's `poses_bounds.npy` in the LLFF dataset.

### Keyboard + Mouse Controls
- Left mouse btn + drag: rotate about camera position
- Right mouse btn + drag: rotate about origin point (can be moved)
- Shift + Left mouse btn + drag: pan camera
- Middle mouse btn + drag: pan camera AND move origin point simultaneously
- WASDQE: move; Shift + WASDQE to move faster
- 123456: preset `world_up` directions, sweep through these keys if scene is using different coordinate system.
- 0: reset the focal length to default, if you messed with it

Lumisphere probe:
- IJKLUO: move the lumisphere probe; Hold shift to move faster

## Building the Website

The backend of the web demo is built from the shader version of the C++ source using emscripten.
Install emscripten per instructions here:
https://emscripten.org/docs/getting_started/downloads.html

Then use
```sh
mkdir embuild && cd embuild
emcmake cmake ..
make -j12
```

The full website should be written to `embuild/build`.
Some CMake scripts even write the html/css/js files.
To launch it locally for previewing, you can use the make target:
```sh
make serve
```
Which should launch a server at http://0.0.0.0:8000/
