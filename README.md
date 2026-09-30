# Forza Ambazon

A C++17 driving demo built with native [raylib](https://www.raylib.com/) and [Bullet Physics](https://github.com/bulletphysics/bullet3). Four raycast wheels apply suspension and tire impulses to a single chassis rigid body. The demo includes WASD driving, handbrake drifting, rear tire skid marks, a chase camera, and suspension ridges.

## Build on Windows with MSYS2 UCRT64

Open the **MSYS2 UCRT64** terminal and install the dependencies:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-raylib \
  mingw-w64-ucrt-x86_64-bullet
```

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
| Escape | Exit |

Build speed before holding Space and A or D to start a slide. Use W to keep driving through it; release Space and straighten or countersteer to regain grip.

## Code and physics

- `include/vehicle.hpp`: vehicle input, wheel state, and physics interface.
- `src/vehicle.cpp`: Bullet world, chassis collisions, raycast suspension, tire grip, and handbrake behavior.
- `src/main.cpp`: rendering, keyboard input, fixed step loop, chase camera, and a bounded skid mark buffer.
- `tests/physics_tests.cpp`: headless checks for settling, driving, reverse, acceleration pitch, grip, drift recovery, and crossing ridges.

Physics runs at 120 Hz independently of rendering. The chassis weighs 1100 kg and has a lowered center of mass. Steering grip receives priority in the tire friction circle, and steering angle decreases as speed rises. The handbrake reduces rear grip and brakes those tires, allowing the rear to slide while the front wheels steer and drive. Suspension rays query only ground collision objects.

This is an arcade vehicle model. Wheel meshes are visual; Bullet handles the chassis rigid body and world collisions. Tuning constants are at the top of `src/vehicle.cpp`.

## Checks and debugging

```bash
ctest --preset ucrt64
gdb ./build/ucrt64/forzaambazon.exe
```

All source, build configuration, and checks use C++; no Go toolchain is needed.
