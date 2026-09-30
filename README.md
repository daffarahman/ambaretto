# Forza Ambazon

A C++17 coastal city driving demo built with native [raylib](https://www.raylib.com/) and [Jolt Physics](https://github.com/jrouwe/JoltPhysics). Four raycast wheels apply suspension and tire impulses to a single chassis rigid body. Explore a city street grid, hill districts, a coastal loop road, parks, and sandy beaches surrounded by animated ocean water. Includes WASD driving, handbrake drifting, rear tire skid marks, a chase camera, and a minimap.

## Build on Windows with MSYS2 UCRT64

Open the **MSYS2 UCRT64** terminal and install the dependencies:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-raylib
```

CMake downloads Jolt **5.4.0**, verifies the archive's SHA-256, and builds it as a static library with the game's compiler. The first configure needs internet access, and the first build compiles Jolt. No separate physics package is needed. Later builds reuse the downloaded source and compiled library in the build directory.

Configure, build, and run from the project directory:

```bash
cd /d/Daffa/Code/forzaambazon
cmake --preset ucrt64
cmake --build --preset ucrt64
./build/ucrt64/forzaambazon.exe
```

The preset creates a Debug build with symbols for GDB. For an optimized build:

```bash
cmake --preset ucrt64-release
cmake --build --preset ucrt64-release
./build/ucrt64-release/forzaambazon.exe
```

Run from UCRT64 so its shared libraries are on `PATH`. In PowerShell, prepend `C:\msys64\ucrt64\bin` to `PATH` before using the same CMake commands or running the executable:

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
cmake --preset ucrt64
cmake --build --preset ucrt64
.\build\ucrt64\forzaambazon.exe
```

## Controls

| Key | Action |
| --- | --- |
| W / S | Drive forward / reverse |
| A / D | Steer left / right |
| Space | Rear handbrake / drift |
| R | Reset the car and skid marks |
| F2 | Toggle the island aerial view |
| Escape | Exit |

Build speed before holding Space and A or D to start a slide. Use W to keep driving through it; release Space and straighten or countersteer to regain grip.

## Island map

The island spans roughly 640 by 560 meters, with climbing avenues, level building plots, tree-lined parks, and a continuous coastal road. Follow the main avenues outward to reach the coastal loop and beaches. Buildings and tree trunks have solid Jolt colliders. The terrain mesh is shared by rendering and physics, so the suspension follows the visible hills and beach slopes.

Water is animated scenery. Driving into the ocean automatically returns the car to its downtown spawn and clears skid marks. Press R to recover manually. The map is generated deterministically and needs no external assets.

## Code and physics

- `include/vehicle.hpp`: vehicle input, wheel state, and physics interface.
- `src/vehicle.cpp`: Jolt world, chassis collisions, raycast suspension, tire grip, and handbrake behavior.
- `src/main.cpp`: rendering, keyboard input, fixed step loop, chase camera, and a bounded skid mark buffer.
- `include/environment.hpp`, `src/environment.cpp`: shared terrain mesh, city layout, buildings, trees, spawn, and water recovery bounds.
- `src/environment_renderer.cpp`: terrain lighting, road markings, building details, vegetation, beach props, animated ocean, and minimap.
- `tests/physics_tests.cpp`: headless checks for driving behavior, terrain/collider alignment, climbing city streets, building collisions, and recovery. The original flat test track and suspension ridges remain as test fixtures.

Physics runs at 120 Hz independently of rendering. The chassis weighs 1100 kg and has a lowered center of mass. Steering grip receives priority in the tire friction circle, and steering angle decreases as speed rises. The handbrake reduces rear grip and brakes those tires, allowing the rear to slide while the front wheels steer and drive. Suspension rays query only ground collision objects.

This is an arcade vehicle model. Wheel meshes are visual; Jolt handles the chassis rigid body and world collisions, while the controller applies suspension and tire impulses at the ground contact points. An offset-center-of-mass shape lowers the chassis center of mass by 0.5 m. Continuous collision detection protects the chassis at speed. Tuning constants are at the top of `src/vehicle.cpp`.

## Checks and debugging

```bash
ctest --preset ucrt64
gdb ./build/ucrt64/forzaambazon.exe
```

All source, build configuration, and checks use C++; no Go toolchain is needed.

For a rendered map preview (saves the image and exits):

```bash
./build/ucrt64/forzaambazon.exe --overview --screenshot build/city-overview.png
```
