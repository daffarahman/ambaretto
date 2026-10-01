# Forza Ambazon

A C++17 driving and flying demo built with native [raylib](https://www.raylib.com/) and [Jolt Physics](https://github.com/jrouwe/JoltPhysics). Explore a compact, densely developed Miami-inspired region: downtown towers, apartments and shops, residential and industrial neighborhoods, Miami Beach hotels, cafes and clubs, PortMiami, two airports, and five Florida Keys with houses, roadside trees and marinas. Includes drifting, stealable NPC traffic, a flyable plane, on-foot exploration, a mouse camera, and a minimap.

## Build on Windows with MSYS2 UCRT64

Open the **MSYS2 UCRT64** terminal and install the dependencies:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-raylib \
  mingw-w64-ucrt-x86_64-glfw
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

The DOS-style menubar groups commands under **File** (resume, pause, quit), **Edit** (recover vehicle, restore car tuning), **Settings** (aerial map, car tuning, controller mapping), and **Help** (controls, about). Press **F10** to release the mouse and open it; navigate with arrows and Enter, or click menus while paused. Escape closes menus. Opening a dropdown or dialog pauses gameplay, including flight and live tuning. Resume through File, a gameplay click, or the mapped Pause/resume button (Start by default).

Open **Settings > Controller mapping** to select an action and **Add binding**, then press a keyboard key or gamepad button, or move a stick/trigger. Added bindings coexist with defaults; select a binding and **Remove binding** to replace a default. Up/Down selects actions, Left/Right selects bindings; scroll either list for more entries. Escape cancels capture; F10 is reserved for the menubar. **Defaults** restores all keyboard/gamepad bindings. **Deadzone** cycles 5–50% in 5% steps to tune stick drift. Changes save immediately; **Save & close** or Escape also retries any pending save. Save failures keep the editor open, display an error, and leave the previous file intact.

Mappings load from **`controller-mappings.ini` beside the executable**, independent of the working directory. The game creates the defaults on its first interactive launch. INI keeps the configuration readable and uses C++17 streams without a serialization dependency; it stores raylib key/button/axis codes, with `axis=code,direction` and directions of `-1` or `1`. Triggers use positive direction and normalize raylib's -1–1 range to 0–1. Each action accepts up to 64 bindings. Empty sections disable an action, omitted sections retain defaults, and invalid files preserve the current/default mappings with an error notice. F10 and Escape remain available for menu navigation even after remapping.

**Generic USB joysticks are supported without an Xbox/SDL gamepad mapping.** The game reads their raw buttons, axes, and D-pad hats through the same GLFW library used by raylib's desktop backend. The mapping editor shows connected controller names; choose **Add binding** and press a button or move a stick/D-pad to assign it. Generic layouts vary, so they use custom bindings instead of guessing which physical button is A/B/X/Y. These bindings display as **USB button**, **USB axis**, or **USB D-pad**, and save as zero-based `joystick_button=code`, `joystick_axis=code,direction`, and `joystick_hat=hat*4+direction` (up/right/down/left = 0/1/2/3). Raw axis 4/5 are ordinary axes, not assumed to be triggers. Up to 16 connected devices, 64 buttons, 16 axes, and four hats per raw device are supported. Existing standardized gamepad bindings and files continue to work. A new capture ignores buttons already held when Add binding was selected; release and press them again.

```ini
version=1
deadzone=0.2
[forward]
key=87
key=265
axis=1,-1
```

Gamepad defaults: left stick drives/walks and pitches/banks the plane; right stick orbits the camera. Y/Triangle enters/exits, B/Circle brakes, A/Cross jumps, and L-stick click sprints. RT/LT changes plane throttle, LB/RB controls the rudder, and X/Square toggles flaps. D-pad up recovers, right opens tuning, down zooms in, and left zooms out. Back/Select toggles the map; Start pauses/resumes. Keyboard defaults remain below.

| Key | Action |
| --- | --- |
| W / S | Drive forward / reverse; opposite direction brakes first |
| A / D | Steer left / right |
| Space | Rear handbrake / drift |
| E | Enter the nearest car or plane / steal a nearby stopped traffic car / exit when stopped on the ground |
| WASD (on foot) | Move relative to the camera |
| Shift (on foot) | Sprint |
| Space (on foot) | Jump |
| Mouse | Orbit the third-person camera |
| Mouse wheel | Zoom the camera |
| R | Recover the car, or return the piloted plane to the airport |
| F2 | Toggle the Miami and Keys aerial view |
| F3 | Open / close live car tuning |
| F10 | Open / close the menubar |
| Escape | Release the mouse and pause |
| Left click | Capture the mouse and resume |
| Window close button | Exit |

Player-controlled cars coast when you release W/S or the left stick. Hold the opposite direction to brake to a stop; after a 0.25-second pause, the car drives in that direction. Releasing the input cancels the change. This works in both directions and in stolen cars. NPC braking is unchanged.

The player's car engine loops `assets/sounds/car-engine.wav`. Pitch and volume smoothly follow speed and throttle in forward and reverse; the loop idles when stopped and mutes while paused, on foot, or flying. Raylib handles streaming and looping. Run `powershell -NoProfile -ExecutionPolicy Bypass -File tests/engine_audio_smoke.ps1` to check audio initialization and WAV streaming on a machine with audio output.

Build speed before holding Space and A or D to start a slide. Use W to keep driving through it; release Space and straighten or countersteer to regain grip.

Press E while stopped or moving slowly to leave the car. Walk back within a few meters and press E to enter. The game checks for a free exit beside either door and behind the car, so buildings cannot trap the character inside a wall. The car applies its parking brake while you explore on foot.

### NPC traffic and stealing cars

354 NPC cars travel through downtown, Miami Beach, the airport neighborhoods, both bay causeways, the Keys villages, and the full Overseas Highway. Local traffic follows the same street corners used to build the map. Up to 48 nearby NPC cars use full Jolt physics; distant traffic advances along its lanes and becomes physical when you approach. Stolen cars remain physical and retain your ownership between districts. Cars keep dark tinted windows without visible NPC occupants. Traffic slows for corners, queues behind vehicles, and stops when you approach on foot. Green minimap dots mark NPC cars; blue dots mark cars available to reenter after taking them.

Leave your car with E, walk beside a stopped traffic car, and press E when **Press E to steal traffic car** appears. Entry requires a clear path, a distance within 3.3 meters, and speed below 2.5 m/s. Stealing stops the car's AI control and gives you immediate WASD control. The camera, speed display, drifting, F3 tuning, and R/water recovery all follow the car you take. Exit and reenter it as usual, or steal another car. Abandoned cars stay parked, and tuning stays with each individual car for the session. Displaced or stuck NPC cars recover into clear road space when more than 35 meters from the player.

Start beside a traffic car, ready to steal it, or capture a preview:

```bash
./build/ucrt64/forzaambazon.exe --traffic
./build/ucrt64/forzaambazon.exe --traffic --screenshot build/traffic-preview.png
```

### Airport and flying

The airport sits west of downtown at X=-880, Z=-420. Follow the westbound connector at Z=-240 through the airport neighborhood to the apron access road. Workshops and residential streets occupy the surrounding land. The airport has a level 336-meter north/south runway, taxiway, aircraft parking, terminal, hangar and control tower. The access road, apron, taxiway, and runway are continuous paved surfaces.

The **yellow triangle** on the minimap marks the plane. It starts at the south end of the runway, facing north away from the city, with a clear departure corridor. Stop nearby, press E to leave the car, walk near the plane's cockpit, and press E to board. Entry selects the nearest available vehicle and checks distance and a clear path. Aircraft exit requires a slow speed and ground contact, with clear space beside the nose or wing tips.

Key West has a second 336-meter runway on its west side, with a taxiway, apron, road access and one small terminal. Departures face south over the water. Both airfields appear on the minimap; aircraft recovery uses the nearest one. Combine `--key-west` with `--airport` to start beside it, or with `--plane` to start in its aircraft.

| Key (in plane) | Action |
| --- | --- |
| Shift / Ctrl | Increase / decrease throttle; setting stays when released |
| W / S | Pitch nose down / up |
| A / D | Bank left / right |
| Left / Right arrows | Rudder and nose-wheel steering |
| Space | Landing gear wheel brakes |
| F | Toggle flaps for low-speed takeoff/landing |
| E | Exit after landing and stopping |
| R | Reset plane at the nearest airfield, retaining pilot mode |
| Mouse / wheel | Orbit / zoom the flight camera |

For takeoff, hold Shift until throttle reaches 100%, release the brake, accelerate straight down the runway, then gently pull S above roughly **100 km/h**. Release or tap S after lifting off to manage your climb. A/D banks the wings; the resulting tilted lift turns the flight path. Reduce throttle with Ctrl for descent, use F for flaps, and approach the runway at a shallow angle. Touch down on the wheels, reduce throttle to zero, and hold Space to stop before exiting. A stall warning means the wing's angle of attack is excessive: lower the nose and add power. A hard collision disables the engine and displays a recovery prompt.

The plane is an **850 kg Jolt rigid body** with collision shapes for the fuselage, wings, horizontal tail, and fin, plus three raycast spring/damper landing struts and tire friction. Aerodynamics use air-density and speed squared, angle-dependent lift, parasite/induced drag, reduced lift beyond stall, sideslip forces, propeller thrust, and damped elevator/aileron/rudder moments. The equations follow [NASA's lift equation](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/lift-equation/) and [drag equation](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/modern-drag-equation/), with coefficients tuned for this game. It is a simplified flight model. Gravity acts without power or airspeed, and the plane can glide with the engine off.

Flying continues beyond the island over the ocean. Entering the water recovers the piloted aircraft at the nearest airfield; R also clears damage, velocity, and throttle. The flight camera has a wider zoom range, and the HUD shows airspeed, altitude above ground, throttle, flaps, stall, and damage. Escape, focus loss, and aerial view pause flight. F3 car tuning is available after leaving the plane.

Start directly beside the airport or already seated in the plane:

```bash
./build/ucrt64/forzaambazon.exe --airport
./build/ucrt64/forzaambazon.exe --plane
./build/ucrt64/forzaambazon.exe --key-west --plane
./build/ucrt64/forzaambazon.exe --plane --screenshot build/plane-preview.png
```

The character uses Jolt's virtual capsule controller for slopes, steps, jumping, and collision with buildings, trees, and the vehicle. Its body turns toward movement and animates while walking or running. Mouse movement orbits the camera in either mode; the driving camera gradually follows the car again after you stop moving the mouse. Camera sweeps keep the view clear of walls and terrain. Focus loss and aerial view pause gameplay and release the mouse.

Press **F3** to tune the current car. The panel releases the mouse, frames the car for inspection, disables driving/character input, and applies the parking brake. Physics keeps running so suspension changes settle in front of you. Drag the sliders or scroll over a row; Up/Down selects a setting and Left/Right adjusts it. Hold Shift for finer edits. Right-drag outside the panel to orbit the car and scroll outside it to zoom. Panel clicks cannot recapture the mouse or move the camera. F3, Escape, or the Close button returns to the previous camera and mouse capture state. Losing window focus pauses the simulation and cancels slider dragging; click to recapture after closing the panel if focus was lost.

The Suspension tab edits tire radius, spring rest length, suspension travel, mount height, spring stiffness, damping, and the force cap per wheel. The Handling tab edits normal grip, rear handbrake grip, drive traction, steering angle, wheelbase, and track width. Wheelbase is the front-to-rear distance between wheel centers (default 2.50 m); track width is the left-to-right distance (default 1.480 m), shared by both axles. These sliders move the wheels, suspension mounts, and ground contacts together around the fixed car body. Steering's speed limit also uses the current wheelbase. Tire size updates both the GLB wheel meshes and ground contact calculations. Settings are bounded, and travel is constrained to keep the compressed suspension length positive. Live readings show each wheel's contact, compression, and spring force. **Defaults** restores the original setup; **Reset car** recovers the car and clears skid marks while retaining your tuning. R and water recovery also retain the setup. Edits last for the current game session.

The **Performance** tab controls **Top speed** (80–400 km/h, default 240 km/h) and **Acceleration** (2–16 m/s², default 9 m/s²). Engine force acts through the tires, so tire grip and Drive traction still affect acceleration. The default Drive traction is 0.9. Lowering top speed while moving slows the car through tire forces. Reverse retains its 39.6 km/h target. Performance settings belong to each car, survive recovery, and work on stolen cars. NPC controllers retain their normal cruising speeds with the faster engine. Start directly in this tab with `--performance`.

## Miami and Florida Keys map

The sea world spans **10.24 by 10.24 kilometers**, with land extending roughly **4 by 5.7 kilometers**. Distances between districts are 40% shorter than the original layout, while cars, people, doors and building floors retain meter-based dimensions. This is compressed, stylized gameplay geography inspired by Miami and the Keys.

Downtown and Brickell mix glass towers up to 96 meters tall, offices, apartments, shops, cafes and two malls. Angled blocks connect to small industrial areas, dense residential neighborhoods and the airport village. Buildings sit close to sidewalks, with extra frontage and back lots filling deep blocks. Local roads are 6.5-8 meters wide; main streets and causeways are 9-12 meters wide. Intersection markings leave junctions open instead of crossing each other. Miami Beach has pastel hotels, apartments, cafes and clubs, plus a sandy Atlantic beach. MacArthur and Venetian causeways connect it to downtown across Biscayne Bay. PortMiami has warehouses, a solid pier and a cruise ship.

Follow **US 1 south** from downtown through Key Largo, Islamorada, Marathon, the Lower Keys and Key West. Each Key has its own street layout: skewed village blocks in Largo, branching marina streets in Islamorada, asymmetric blocks in Marathon, winding streets in the Lower Keys, and small old-town blocks in Key West. Narrow lanes and cul-de-sacs serve dense one- and two-story houses with varied roofs, colors and porches. Each Key has at most one non-house building: a small cafe on the first four Keys and the airport terminal on Key West. There are no hotels, towers, shops or gas stations in the Keys. Marinas have docks and boats. Eight bridge sections connect the region, including a compressed Seven Mile Bridge, with smooth ramps, concrete barriers, support piers and streetlights. Bridge decks share their geometry with suspension and character collision. The road network, including airport and port approaches, forms one connected component.

F2 shows the whole region with district labels. The minimap follows the player and covers 1.4 kilometers. Start directly in another district:

```bash
./build/ucrt64/forzaambazon.exe --beach
./build/ucrt64/forzaambazon.exe --keys
./build/ucrt64/forzaambazon.exe --key-west
./build/ucrt64/forzaambazon.exe --bridge
```

Water is animated scenery. Driving into the ocean automatically returns the car to its downtown spawn and clears skid marks. Press R to recover manually. The map is generated deterministically.

Grass uses `assets/textures/grass.png`, beaches use `assets/textures/beach-sand.png`, and roads use `assets/textures/asphalt.png`. All three tile every four meters with mipmap filtering. Road markings are drawn above the asphalt. CMake copies `assets` beside each executable when building; the game loads that copy independently of the working directory.

All interface text uses `assets/fonts/big-blue-terminal-plus.ttf`: the HUD, prompts, pause notices, minimap, map labels, and tuning panel. Business signs use the same font. Text measurement, alignment, and wrapping share the loaded font, which is released before the window closes.

All trees use `assets/models/tree1.glb`, including park, beach and Keys trees. Roadside rows are planted first, roughly every 20 meters on both sides where there is room, with trunks clear of sidewalks, junctions, buildings and both airports' departure corridors. Its embedded autumn-leaf texture retains its transparent gaps and renders from both sides. The model is loaded once and batched across deterministic locations, with varied height and rotation. Cylindrical trunk colliders follow its scaled trunk dimensions; foliage remains passable.

## Code and physics

The car body uses `assets/models/trueno.glb`. The exported half-body is mirrored at load time, turned to face the driving direction, and scaled/positioned to match the existing 2.5-meter wheelbase. White paint, dark trim, and tinted glass fill the export's default white material slots; lamp colors come from its emissive materials. Each wheel uses `assets/models/trueno-wheel.glb`, recentered from its exported position and uniformly scaled to the physics tire radius of 0.30 meters. The detailed rims face outward on both sides; all four wheels follow the raycast suspension and rotate as the car moves, with steering on the front pair. Silver rims and dark rubber/tread fill the wheel export's default white slots. Both models are loaded once and copied beside the executable with the other assets. Missing or invalid models fall back to the respective procedural body or wheels.

- `include/vehicle.hpp`: vehicle input, wheel state, and physics interface.
- `src/vehicle.cpp`: Jolt world, chassis collisions, raycast suspension, tire grip, and handbrake behavior.
- `include/plane.hpp`, plane implementation in `src/vehicle.cpp`: aircraft rigid body, aerodynamics, flight controls, landing gear, damage, and reset.
- `include/airport.hpp`: shared runway/apron/access-road layout used by terrain, rendering, and aircraft spawn.
- `src/main.cpp`: rendering, keyboard input, fixed step loop, chase camera, and a bounded skid mark buffer.
- `src/car_renderer.cpp`: Trueno body/wheel loading, symmetry, wheel alignment, steering/spin animation, material lighting, and chassis orientation.
- `include/car_tuning.hpp`, `src/tuning_panel.cpp`: runtime tuning defaults/limits, sliders, precision input, and wheel telemetry.
- `include/player.hpp`, `src/player.cpp`: on-foot control and entering/exiting the vehicle; the Jolt capsule implementation lives alongside the physics world in `src/vehicle.cpp`.
- `include/traffic.hpp`, `src/traffic.cpp`: regional lane routes and traffic streaming, NPC driving and braking, pedestrian yielding, ownership transfer, and distant recovery.
- `src/third_person_camera.cpp`: mouse orbit, zoom, and camera-relative movement.
- `include/environment.hpp`, `src/environment.cpp`: shared terrain mesh, city layout, buildings, trees, spawn, and water recovery bounds.
- `src/environment_renderer.cpp`: terrain lighting, road markings, building details, vegetation, beach props, animated ocean, and minimap.
- `tests/physics_tests.cpp`: headless checks for driving behavior, terrain/collider alignment, Miami street driving, building collisions, and recovery. The original flat test track and suspension ridges remain as test fixtures.
- `tests/player_tests.cpp`: walking, sprinting, jumping, slopes, collisions, parking, entry/exit conditions, and mouse camera movement.
- `tests/tuning_tests.cpp`: live suspension changes, tire contact, validation, per-car isolation, setup retention, and slider input/focus behavior.
- `tests/plane_tests.cpp`: road access, runway collision alignment, parking, takeoff distance, lift/control response, stalls, gravity, gliding, landing/braking, taxi steering, aircraft collisions, entry/exit, and recovery.
- `tests/traffic_tests.cpp`: sustained road following, pedestrian/vehicle braking, stealing, switching cars, parking, per-car tuning/recovery, and plane boarding with traffic.
- `tests/map_tests.cpp`: regional road connectivity, landmarks, road/deck collision alignment, driving across all eight bridges, solid ports, and distant traffic streaming.

Physics runs at 120 Hz independently of rendering. The chassis weighs 1100 kg and has a lowered center of mass. Steering grip receives priority in the tire friction circle, and steering angle decreases as speed rises. The handbrake reduces rear grip and brakes those tires, allowing the rear to slide while the front wheels steer and drive. Suspension rays query only ground collision objects. Dampers respond to suspension movement relative to the road, so horizontal speed alone cannot lift the body. Rendered tires use the chassis's current pose and sampled spring length, keeping the axles aligned after physics integration. The vehicle checks verify steady ride height, ground contact, suspension travel, and wheel alignment at 80, 240, and 400 km/h.

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

`controller_mapping` checks default and additional bindings, generic USB button/axis/D-pad capture, held-button handling, raw versus standardized layouts, analog deadzones/triggers, one-shot actions, disconnected pads, file replacement, round-trip persistence, and invalid-file handling. Preview the new UI with `--controllers`, `--menu`, or `--help-menu`, optionally combined with `--screenshot`.

For a rendered map preview (saves the image and exits):

```bash
./build/ucrt64/forzaambazon.exe --overview --screenshot build/city-overview.png
```

Start on foot, or capture a character preview:

```bash
./build/ucrt64/forzaambazon.exe --on-foot
./build/ucrt64/forzaambazon.exe --on-foot --screenshot build/character-preview.png
```
