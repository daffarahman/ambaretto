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
| E | Exit the car / enter when nearby |
| WASD (on foot) | Move relative to the camera |
| Shift (on foot) | Sprint |
| Space (on foot) | Jump |
| Mouse | Orbit the third-person camera |
| Mouse wheel | Zoom the camera |
| R | Reset the car and skid marks |
| F2 | Toggle the island aerial view |
| Escape | Release the mouse and pause |
| Left click | Capture the mouse and resume |
| Window close button | Exit |

Build speed before holding Space and A or D to start a slide. Use W to keep driving through it; release Space and straighten or countersteer to regain grip.

Press E while stopped or moving slowly to leave the car. Walk back within a few meters and press E to enter. The game checks for a free exit beside either door and behind the car, so buildings cannot trap the character inside a wall. The car applies its parking brake while you explore on foot.

The character uses Jolt's virtual capsule controller for slopes, steps, jumping, and collision with buildings, trees, and the vehicle. Its body turns toward movement and animates while walking or running. Mouse movement orbits the camera in either mode; the driving camera gradually follows the car again after you stop moving the mouse. Camera sweeps keep the view clear of walls and terrain. Focus loss and aerial view pause gameplay and release the mouse.

## Island map

The island spans roughly 640 by 560 meters, with climbing avenues, level building plots, tree-lined parks, and a continuous coastal road. Follow the main avenues outward to reach the coastal loop and beaches. Buildings and tree trunks have solid Jolt colliders. The terrain mesh is shared by rendering and physics, so the suspension follows the visible hills and beach slopes.

Water is animated scenery. Driving into the ocean automatically returns the car to its downtown spawn and clears skid marks. Press R to recover manually. The map is generated deterministically.

Grass uses `assets/textures/grass.png`, beaches use `assets/textures/beach-sand.png`, and roads use `assets/textures/asphalt.png`. All three tile every four meters with mipmap filtering. Road markings are drawn above the asphalt. CMake copies `assets` beside each executable when building; the game loads that copy independently of the working directory.

## Code and physics

The car body uses `assets/models/trueno.glb`. The exported half-body is mirrored at load time, turned to face the driving direction, and scaled/positioned to match the existing 2.5-meter wheelbase. White paint, dark trim, and tinted glass fill the export's default white material slots; lamp colors come from its emissive materials. Each wheel uses `assets/models/trueno-wheel.glb`, recentered from its exported position and uniformly scaled to the physics tire radius of 0.34 meters. The detailed rims face outward on both sides; all four wheels follow the raycast suspension and rotate as the car moves, with steering on the front pair. Silver rims and dark rubber/tread fill the wheel export's default white slots. Both models are loaded once and copied beside the executable with the other assets. Missing or invalid models fall back to the respective procedural body or wheels.

- `include/vehicle.hpp`: vehicle input, wheel state, and physics interface.
- `src/vehicle.cpp`: Jolt world, chassis collisions, raycast suspension, tire grip, and handbrake behavior.
- `src/main.cpp`: rendering, keyboard input, fixed step loop, chase camera, and a bounded skid mark buffer.
- `src/car_renderer.cpp`: Trueno body/wheel loading, symmetry, wheel alignment, steering/spin animation, material lighting, and chassis orientation.
- `include/player.hpp`, `src/player.cpp`: on-foot control and entering/exiting the vehicle; the Jolt capsule implementation lives alongside the physics world in `src/vehicle.cpp`.
- `src/third_person_camera.cpp`: mouse orbit, zoom, and camera-relative movement.
- `include/environment.hpp`, `src/environment.cpp`: shared terrain mesh, city layout, buildings, trees, spawn, and water recovery bounds.
- `src/environment_renderer.cpp`: terrain lighting, road markings, building details, vegetation, beach props, animated ocean, and minimap.
- `tests/physics_tests.cpp`: headless checks for driving behavior, terrain/collider alignment, climbing city streets, building collisions, and recovery. The original flat test track and suspension ridges remain as test fixtures.
- `tests/player_tests.cpp`: walking, sprinting, jumping, slopes, collisions, parking, entry/exit conditions, and mouse camera movement.

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

Start on foot, or capture a character preview:

```bash
./build/ucrt64/forzaambazon.exe --on-foot
./build/ucrt64/forzaambazon.exe --on-foot --screenshot build/character-preview.png
```
