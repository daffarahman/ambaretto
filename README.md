# Forza Ambazon

A C++17 coastal city driving and flying demo built with native [raylib](https://www.raylib.com/) and [Jolt Physics](https://github.com/jrouwe/JoltPhysics). Four raycast wheels apply suspension and tire impulses to a single chassis rigid body. Explore a city street grid, hill districts, a coastal loop road, parks, sandy beaches, and a connected airport surrounded by animated ocean water. Includes WASD driving, handbrake drifting, rear tire skid marks, a flyable propeller plane, third-person character control, a mouse camera, and a minimap.

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
| E | Enter the nearest car or plane / exit when stopped on the ground |
| WASD (on foot) | Move relative to the camera |
| Shift (on foot) | Sprint |
| Space (on foot) | Jump |
| Mouse | Orbit the third-person camera |
| Mouse wheel | Zoom the camera |
| R | Recover the car, or return the piloted plane to the airport |
| F2 | Toggle the island aerial view |
| F3 | Open / close live car tuning |
| Escape | Release the mouse and pause |
| Left click | Capture the mouse and resume |
| Window close button | Exit |

Build speed before holding Space and A or D to start a slide. Use W to keep driving through it; release Space and straighten or countersteer to regain grip.

Press E while stopped or moving slowly to leave the car. Walk back within a few meters and press E to enter. The game checks for a free exit beside either door and behind the car, so buildings cannot trap the character inside a wall. The car applies its parking brake while you explore on foot.

### Airport and flying

Drive **south along the main avenue** (toward increasing Z on the map) and continue across the coastal road to the airport apron. The airport has a level 336-meter runway (09/27), taxiway, aircraft parking, terminal, hangar, control tower, runway lights, and windsock. Its grass shoulders blend into the surrounding beach. The avenue, apron, taxiway, and runway are continuous paved surfaces using the existing asphalt texture.

The **yellow triangle** on the minimap marks the plane. It starts at the west end of the runway, facing east. Stop nearby, press E to leave the car, walk near the plane's cockpit, and press E to board. Entry selects the nearest available vehicle and checks distance and a clear path. Aircraft exit requires a slow speed and ground contact, with clear space beside the nose or wing tips.

| Key (in plane) | Action |
| --- | --- |
| Shift / Ctrl | Increase / decrease throttle; setting stays when released |
| W / S | Pitch nose down / up |
| A / D | Bank left / right |
| Left / Right arrows | Rudder and nose-wheel steering |
| Space | Landing gear wheel brakes |
| F | Toggle flaps for low-speed takeoff/landing |
| E | Exit after landing and stopping |
| R | Reset plane at the runway, retaining pilot mode |
| Mouse / wheel | Orbit / zoom the flight camera |

For takeoff, hold Shift until throttle reaches 100%, release the brake, accelerate straight down the runway, then gently pull S above roughly **100 km/h**. Release or tap S after lifting off to manage your climb. A/D banks the wings; the resulting tilted lift turns the flight path. Reduce throttle with Ctrl for descent, use F for flaps, and approach the runway at a shallow angle. Touch down on the wheels, reduce throttle to zero, and hold Space to stop before exiting. A stall warning means the wing's angle of attack is excessive: lower the nose and add power. A hard collision disables the engine and displays a recovery prompt.

The plane is an **850 kg Jolt rigid body** with collision shapes for the fuselage, wings, horizontal tail, and fin, plus three raycast spring/damper landing struts and tire friction. Aerodynamics use air-density and speed squared, angle-dependent lift, parasite/induced drag, reduced lift beyond stall, sideslip forces, propeller thrust, and damped elevator/aileron/rudder moments. The equations follow [NASA's lift equation](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/lift-equation/) and [drag equation](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/modern-drag-equation/), with coefficients tuned for this game. It is a simplified flight model. Gravity acts without power or airspeed, and the plane can glide with the engine off.

Flying continues beyond the island over the ocean. Entering the water recovers the piloted aircraft at the runway; R also clears damage, velocity, and throttle. The flight camera has a wider zoom range, and the HUD shows airspeed, altitude above ground, throttle, flaps, stall, and damage. Escape, focus loss, and aerial view pause flight. F3 car tuning is available after leaving the plane.

Start directly beside the airport or already seated in the plane:

```bash
./build/ucrt64/forzaambazon.exe --airport
./build/ucrt64/forzaambazon.exe --plane
./build/ucrt64/forzaambazon.exe --plane --screenshot build/plane-preview.png
```

The character uses Jolt's virtual capsule controller for slopes, steps, jumping, and collision with buildings, trees, and the vehicle. Its body turns toward movement and animates while walking or running. Mouse movement orbits the camera in either mode; the driving camera gradually follows the car again after you stop moving the mouse. Camera sweeps keep the view clear of walls and terrain. Focus loss and aerial view pause gameplay and release the mouse.

Press **F3** to tune the current car. The panel releases the mouse, frames the car for inspection, disables driving/character input, and applies the parking brake. Physics keeps running so suspension changes settle in front of you. Drag the sliders or scroll over a row; Up/Down selects a setting and Left/Right adjusts it. Hold Shift for finer edits. Right-drag outside the panel to orbit the car and scroll outside it to zoom. Panel clicks cannot recapture the mouse or move the camera. F3, Escape, or the Close button returns to the previous camera and mouse capture state. Losing window focus pauses the simulation and cancels slider dragging; click to recapture after closing the panel if focus was lost.

The Suspension tab edits tire radius, spring rest length, suspension travel, mount height, spring stiffness, damping, and the force cap per wheel. The Handling tab edits normal grip, rear handbrake grip, drive traction, steering angle, wheelbase, and track width. Wheelbase is the front-to-rear distance between wheel centers (default 2.50 m); track width is the left-to-right distance (default 1.64 m), shared by both axles. These sliders move the wheels, suspension mounts, and ground contacts together around the fixed car body. Steering's speed limit also uses the current wheelbase. Tire size updates both the GLB wheel meshes and ground contact calculations. Settings are bounded, and travel is constrained to keep the compressed suspension length positive. Live readings show each wheel's contact, compression, and spring force. **Defaults** restores the original setup; **Reset car** recovers the car and clears skid marks while retaining your tuning. R and water recovery also retain the setup. Edits last for the current game session.

## Island map

The island spans roughly 640 by 560 meters, with climbing avenues, level building plots, tree-lined parks, and a continuous coastal road. Follow the main avenues outward to reach the coastal loop and beaches. Buildings and tree trunks have solid Jolt colliders. The terrain mesh is shared by rendering and physics, so the suspension follows the visible hills and beach slopes.

Water is animated scenery. Driving into the ocean automatically returns the car to its downtown spawn and clears skid marks. Press R to recover manually. The map is generated deterministically.

Grass uses `assets/textures/grass.png`, beaches use `assets/textures/beach-sand.png`, and roads use `assets/textures/asphalt.png`. All three tile every four meters with mipmap filtering. Road markings are drawn above the asphalt. CMake copies `assets` beside each executable when building; the game loads that copy independently of the working directory.

## Code and physics

The car body uses `assets/models/trueno.glb`. The exported half-body is mirrored at load time, turned to face the driving direction, and scaled/positioned to match the existing 2.5-meter wheelbase. White paint, dark trim, and tinted glass fill the export's default white material slots; lamp colors come from its emissive materials. Each wheel uses `assets/models/trueno-wheel.glb`, recentered from its exported position and uniformly scaled to the physics tire radius of 0.34 meters. The detailed rims face outward on both sides; all four wheels follow the raycast suspension and rotate as the car moves, with steering on the front pair. Silver rims and dark rubber/tread fill the wheel export's default white slots. Both models are loaded once and copied beside the executable with the other assets. Missing or invalid models fall back to the respective procedural body or wheels.

- `include/vehicle.hpp`: vehicle input, wheel state, and physics interface.
- `src/vehicle.cpp`: Jolt world, chassis collisions, raycast suspension, tire grip, and handbrake behavior.
- `include/plane.hpp`, plane implementation in `src/vehicle.cpp`: aircraft rigid body, aerodynamics, flight controls, landing gear, damage, and reset.
- `include/airport.hpp`: shared runway/apron/access-road layout used by terrain, rendering, and aircraft spawn.
- `src/main.cpp`: rendering, keyboard input, fixed step loop, chase camera, and a bounded skid mark buffer.
- `src/car_renderer.cpp`: Trueno body/wheel loading, symmetry, wheel alignment, steering/spin animation, material lighting, and chassis orientation.
- `include/car_tuning.hpp`, `src/tuning_panel.cpp`: runtime tuning defaults/limits, sliders, precision input, and wheel telemetry.
- `include/player.hpp`, `src/player.cpp`: on-foot control and entering/exiting the vehicle; the Jolt capsule implementation lives alongside the physics world in `src/vehicle.cpp`.
- `src/third_person_camera.cpp`: mouse orbit, zoom, and camera-relative movement.
- `include/environment.hpp`, `src/environment.cpp`: shared terrain mesh, city layout, buildings, trees, spawn, and water recovery bounds.
- `src/environment_renderer.cpp`: terrain lighting, road markings, building details, vegetation, beach props, animated ocean, and minimap.
- `tests/physics_tests.cpp`: headless checks for driving behavior, terrain/collider alignment, climbing city streets, building collisions, and recovery. The original flat test track and suspension ridges remain as test fixtures.
- `tests/player_tests.cpp`: walking, sprinting, jumping, slopes, collisions, parking, entry/exit conditions, and mouse camera movement.
- `tests/tuning_tests.cpp`: live suspension changes, tire contact, validation, per-car isolation, setup retention, and slider input/focus behavior.
- `tests/plane_tests.cpp`: road access, runway collision alignment, parking, takeoff distance, lift/control response, stalls, gravity, gliding, landing/braking, taxi steering, aircraft collisions, entry/exit, and recovery.

Physics runs at 120 Hz independently of rendering. The chassis weighs 1100 kg and has a lowered center of mass. Steering grip receives priority in the tire friction circle, and steering angle decreases as speed rises. The handbrake reduces rear grip and brakes those tires, allowing the rear to slide while the front wheels steer and drive. Suspension rays query only ground collision objects.

This is an arcade vehicle model. Wheel meshes are visual; Jolt handles the chassis rigid body and world collisions, while the controller applies suspension and tire impulses at the ground contact points. An offset-center-of-mass shape lowers the chassis center of mass by 0.5 m. Continuous collision detection protects the chassis at speed. Runtime tuning defaults and slider limits are in `include/car_tuning.hpp`.

## Checks and debugging

```bash
ctest --preset ucrt64
gdb ./build/ucrt64/forzaambazon.exe
```

Open directly in tuning mode, or capture the panel:

```bash
./build/ucrt64/forzaambazon.exe --tuning
./build/ucrt64/forzaambazon.exe --tuning --screenshot build/tuning-preview.png
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
