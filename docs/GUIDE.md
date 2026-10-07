# Ambaretto guide

Feature documentation, controls, architecture, and development checks. For installation and compilation, see the [README](../README.md).

- [City builder](#city-builder)
- [Building builder](#building-builder)
- [Police and wanted levels](#police-and-wanted-levels)
- [Controls](#controls)
- [Character creator](#character-creator)
- [Code and physics](#code-and-physics)
- [Checks and debugging](#checks-and-debugging)

## City builder

The main menu offers **Play**, **Create / edit**, **Settings**, and **Quit**. **Play** opens a city list: click a playable city or select it with arrows and Enter. This selector has no creation, editing, rename, or delete actions; a city needs a spawn and valid Player character models to play. **Create / edit** opens the **Cities**, **Buildings**, **Cars**, and **Characters** menus. Use Up/Down and Enter, or click a row. **File** offers Play, Create / edit, and Quit; **Settings** offers Graphics and Controller mapping; **Help** offers Controls and About. F10 opens the menu bar. Each menu and editor opens inside the same game window. Buildings, Cars, and Characters offer **Create new** or **Open / edit selected**; **Refresh saved** reloads their lists. Leaving or replacing a modified design asks **Save / Discard / Cancel**. Saves use the existing libraries beside the executable.

In Cities, choose **Create new city** for a name, centered **Starter tiles X/Z** area (1-128 per axis), and seed. The live **Terrain preview** shows the full map with green land, blue water, and a yellow outline of the starter area. **Regenerate** (Ctrl+R) rolls a new seed and updates the image; editing the seed or dimensions updates it too. **Create city** saves the exact terrain shown and opens the editor. Previewing and Cancel do not save a map. **Perlin terrain** generates flat land and sea inside that area while the rest of the map stays water; **Empty water** previews and creates a blank map. **Open / edit selected** (or Enter) opens the selected map in the editor. Cities offers only creation and editing, plus Back; Play is available from the main menu. **Back** returns to the previous menu. **File > End Game / Main menu** returns there from gameplay. **Escape / Start** pauses physics and the clock on the world map; Escape again or **File > Resume** continues.

Map editors open asset creators as in-screen menus. **Objects > Choose car** and **Choose player character** select saved designs. Use **File > Refresh saved designs** to apply library edits to an open map, or reopen/play the map. Refresh records map changes as an undo action and checks placement collisions. The map still autosaves its own edits.

Direct menu flags: `--cities`, `--building-builder`, `--car-editor`, `--character-creator`, `--settings`. Add `--new` to an asset editor to go straight to a blank design; `--screenshot <path.png>` captures its opening menu, or its editor with `--new`.

City saves use the `AMBARETTO_CITY` header. For saves exported before the project was renamed, replace only the first word of the first line with `AMBARETTO_CITY`; keep the version number and remaining data unchanged.

### Building builder

Open **Buildings > Building Creator** from the map editor's menu bar. **Tiles X / Tiles Z** set a footprint from 1×1 to 128×128 tiles, with an 11.2-meter ground grid beneath the mesh. Increasing the footprint adds editing room while preserving the building's physical shape; decreasing it scales the horizontal geometry only if needed to fit. Older designs are padded to whole tiles without changing their physical geometry. Use the creator's **File > New building** to start with a cube.

In **Mesh**, switch between vertex, edge, and face selection with **1/2/3** or the three mode buttons. Click geometry to select it, Shift-click to add/remove selections, and **A** selects all in the current mode. Drag the colored **X/Y/Z arrows** to move along an axis, or the square handles to move in **XY/XZ/YZ** planes. **G** also starts a mouse move; **X/Y/Z** locks an axis, click/Enter applies, and Esc/right-click cancels. Every drag stays inside the chosen X/Z footprint and between **0 and 120 meters** in Y. Multi-selection moves together until a selected vertex reaches a boundary. **Xray** (Alt+Z) exposes hidden vertices and edges. Right-drag the preview to orbit and use the wheel to zoom.

**Slice (K)** adds a connected cut through the mesh. Choose X, Y, or Z, then drag the position slider while watching the cyan cut preview. **Apply slice** creates new shared vertices and edges and selects the new vertices. For a trapezoid or sloped roof, slice a cube, select its upper edges/faces, then drag their handles inward or upward. Texture coordinates and decals retain their coverage across the cut.

**Subdivide** adds a midpoint to an edge (including its neighboring faces), or splits a face into editable triangles with new center vertices. **Inset (I)** makes a smaller face inside a convex face; **Extrude (E)** extends the selected face by one meter with connected sides. Move its new vertices afterward to adjust the extension. **Add vertex** creates a loose vertex above the selection; move it into position, Shift-select three or more vertices in perimeter order, then **Make face (F)** connects them. **Delete** removes selected vertices and their incident faces, an edge's incident faces, or a selected face. Open surfaces and concave polygons are supported. **Ctrl+Z/Y** undoes/redoes mesh edits. Collapsed faces, crossing corners, inconsistent shared-edge winding, and invalid dimensions are rejected. A mesh supports up to 4096 vertices, 8192 faces, and 64 corners per face.

In **Parts**, choose Box, Wedge, Gable, or Hip, enter dimensions and a base position in meters, then **Add free shape**. Increase the footprint first if necessary. Click a shape to select its part; **Transform part** provides translation, rotation, scale, duplication, deletion, and **Bake / clean**. Scroll the controls in smaller windows. **Attach to face** uses the selected face outline and the entered height: boxes extrude any suitable face; roofs attach to flat rectangular upward-facing faces. Attached parts share their seam vertices, so use Mesh vertex tools to reshape them. Independent part transforms are rejected at shared seams. Bake removes unused vertices and exactly coincident opposite faces; partially overlapping solids require manual editing or clean surface attachments, rather than an automatic Boolean union.

The **Texture** tab lists PNG, JPG/JPEG, BMP, TGA, and GIF filenames from `assets/textures/`, with thumbnails. Add an image to that folder and click **Refresh folder**. When launched from the repository, the source folder is used; otherwise use the assets folder beside the executable. Choose **Whole building** to change the default texture, or select faces in the preview with Shift for multiple selection and use **Selected faces** to assign wall, roof, base, or trim textures. **Auto tile / 2 metres per repeat** enables physical tiling for the chosen scope. Existing buildings retain their original mapping until automatic tiling is selected.

The **UV** tab edits selected faces (Shift-click for multiple selection), or **All faces**. Choose **Planar** for a face-aligned projection, **Box** for automatic projection on each face, or **Original** for the saved chart. Enter metres per tile, U/V offsets, independent U/V scales, and rotation, then press Enter; the preview updates immediately. **Re-project** captures a fresh planar frame without resetting the tweaks; **Reset UV tweaks** clears offset, scale, and rotation. Geometry edits retain the settings and update generated coordinates; subdivision/slicing inherit the parent surface coordinates. Planar/Box tiling follows physical size on sloping and non-uniform faces. Original mapping preserves old saves and may stretch until re-projected.

In **Decals**, pick a **Texture**, choose **Place on wall**, and click the preview. New decals align to the wall with dimensions in metres; clicking repeatedly places more, and Escape finishes placing. Click an existing decal to select it: a yellow outline and centre marker show its footprint, and the preview identifies its number and texture. Drag it directly and release to confirm, or use **Move (G)** / **G** for a live movement preview and click to confirm. Moving onto another wall retains size and rotation. Escape, right click, losing focus, or releasing off the wall cancels the move; one Undo restores the entire gesture. Clicking empty space clears selection. Scroll for physical width/height, rotation, **Duplicate**, **Remove**, and **Repeat along wall**; the scrollbar shows when more controls are below. A duplicate follows the cursor until confirmed; cancelling removes it. Repeat count includes the source; spacing is centre-to-centre in metres, with Horizontal/Vertical direction. An array that would place a centre outside the wall is rejected together. Decals clip at wall edges and remain attached through subdivision and UV edits. Up to 64 logical decals are supported; repeated windows are baked into the shared geometry and texture array, with no additional material or draw per window. Older decals preserve their saved UV chart; their size fields display UV units until moved onto a different wall. Alpha is cut out at 0.3; translucent glass blending is not supported.

Building surfaces and decals share native texture arrays with isolated mipmaps and repeating/clamped sampler states. Images of equal dimensions share an array page; imported image dimensions are preserved, so mixed resolutions can add pages and draws. Runtime geometry is combined across buildings into 256-meter spatial chunks for distance culling. Each populated chunk/page uses one color draw and, when enabled, one shadow draw. There are no materials or image allocations per decal. The preview reports mesh count, emitted triangles, and logical GPU texture/buffer bytes. Building textures and geometry each have an 8 MiB accepted-bake GPU budget; driver overhead and temporary allocations while rebaking are additional. An invalid bake keeps the prior renderer and prevents saving; an over-budget city shows a clear load error instead of starting gameplay with invisible buildings. Use **Low** graphics on the 4 GB RAM / 128 MB VRAM target: shadows and local lights are off, and **4x MSAA (restart)** is optional. The rest of the scene also consumes VRAM, so this is a building resource budget, not a guarantee for every map or GPU.

The `building_render_budget` check uses a three-part building, four surface textures, and 24 repeated windows. Five 128×128 RGBA images plus the fallback layer use **524,280 bytes including all mipmaps**, shared by every copy. The joins bake from 18 to 14 faces. The measured payload is:

| Copies | Triangles | Visible building color draws | GPU geometry | GPU textures | Total building GPU payload |
|---:|---:|---:|---:|---:|---:|
| 1 | 88 | 1 | 11,616 B | 524,280 B | 0.511 MiB |
| 100 | 8,800 | 1 | 1,161,600 B | 524,280 B | 1.608 MiB |

The 100 copies fit in one spatial chunk. Wider layouts add chunks; differing image sizes add array pages. Shadows add one draw per visible chunk/page when enabled. CPU mesh payload equals the GPU geometry column, and decoded source images are released after upload. These counters exclude the rest of the game, driver bookkeeping, and temporary bake allocations. Reproduce with `ctest --test-dir build/ucrt64-release -R building_render_budget -V`; it also prints bake time and synchronized 720p host median/p95 frame time, verifies physical checker tiling and transparent shadows, and rejects over-budget allocations without replacing the previous renderer. Target-hardware FPS still needs a test on the actual 128 MB GPU. Desktop OpenGL 3.3 is required, as with the existing scene shaders.

**Save building** (Ctrl+S or creator **File > Save building**) writes a reusable `.building` file into `buildings/` beside the executable; saving the same name updates it. **File > Open Saved Building** opens a pop-up list: select a design, then **Open building**. **Save and close** saves the design and returns to the previous menu. Refresh the map's saved designs to use it. The toolbox offers **Choose saved...**, which opens a building list pop-up, and a plain block. **Buildings > Choose saved building** opens the same selector. Use **Buildings > Place selected building** and click/release on empty flat land to place the selected design. Its N×M footprint reserves the matching grid cells; a red wire preview means it cannot be placed there. Select an existing building with **Edit > Select / edit**, then choose **Buildings > Edit selected building** to resize or reshape that instance. City undo/redo includes building edits and placement. Saving a building updates its library file. **File > Refresh saved designs** in the map applies saved edits to placed copies with the same name. Closing a modified draft asks Save, Discard, or Cancel. A design update is one city undo action, and a footprint that would overlap another object or leave buildable land is rejected for all its copies. Names identify designs; saving under a different name creates another design. Cities continue to embed meshes, textures' filenames, UVs, and decals, so the updated map saves and loads without needing its library files. Rendering and collision use the same edited triangles in gameplay. Building version 1-2 and city version 1-13 saves remain readable. New building saves use version 3; cities containing custom building meshes use version 14 and embed part IDs, materials, projection settings, and decal anchors. Other city versions retain their existing layout.

You can also open the standalone builder with `Ambaretto.exe --building-builder`; the existing `--screenshot <path.png>` flag captures its preview.

To start a variation of a saved city, building, car, or character, select it in its creation menu and click **Duplicate selected** (or press **Ctrl+D**). Saved-item popups inside editors also offer **Duplicate**. The copy is saved immediately under a fresh name (`Name copy`, `Name copy 2`, etc.) and selected; choose **Open / edit** to modify it. Cities receive a new save ID. Geometry, components, roles, tuning, textures, and map contents are preserved, while later edits to the copy leave the original design intact. GLB and texture files are shared assets. The Play city selector stays read-only.

Cities use a **128 x 128 grid of 11.2-meter blocks** (1.4336 km square). Create multiple islands anywhere by dragging rectangles with **Island / expand**. Bulldozing can separate land into islands. Roads drawn over water automatically become bridges with decks, railings, supports, and matching collision; diagonal bridges follow the diagonal pavement. Removing a bridge restores water. New hand-painted land starts at 3.2 m above water. Perlin generation only chooses land or water; all generated land stays flat at the default 3.2 m height. The shoreline slopes gently outward through shallow water to the seabed over 32 m, with rounded outer corners and matching terrain collision. All land and shorelines default to `soil.png`, with no automatic beach detection. Use Ground texture to draw rectangles of `grass.png`, `beach-sand.png`, `soil.png`, or `asphalt.png`; the saved textures appear in both the editor and gameplay.

| Editor tool | How it works |
| --- | --- |
| 1 — Select / edit | **Edit > Select / edit** selects a building or vehicle. **Buildings > Edit selected building** edits its mesh; **Buildings > Rotate building +90** or **R** rotates it in 90° steps. **Objects > Rotate vehicle +90** rotates a selected vehicle. Delete removes the selection. |
| 2 — Island / expand | Drag a rectangle to create land or expand the island. |
| 3 — Road | Choose L-shaped or diagonal under **Tiles**, then drag single-block roads. Off-axis diagonal drags draw a 45-degree section and continue straight when necessary to reach the end cell. Shift flips the L-shaped drawing order. The live preview follows the road's pavement. Neighboring road blocks connect edge to edge. Simple 90-degree bends have rounded pavement, curved yellow centerlines, and white edge markings. Tight opposite bends form diagonal parallelogram roads. Junctions have curved shoulders and zebra crosswalks on each approach. |
| 4 — Building | Choose a design in the toolbox or open **Buildings > Building Creator**, then click to place it. **R** or **Buildings > Rotate building +90** turns the placement preview and its N×M footprint. A rotation that would overlap occupied tiles, sloping ground, water, or the map edge is rejected. Drag a plain block to make the original 1–8 block squares. |
| 5 — Player spawn | Click land, a road, or a bridge to set the city's single player spawn at the exact pointer position. The yellow box marks that point. Buildings, vehicles, and trees may overlap it. Play requires a spawn and a Player design selected under **Objects > Choose player character**. |
| 6 — Vehicle | Choose a saved car, Trainer, F-18, or Boeing 747 under **Objects**, use its rotation action if needed, then click land, a road, or a bridge. Positions follow the pointer without snapping to blocks. Buildings, the player spawn, trees, and other vehicles may overlap; selecting a vehicle takes priority over the building tile beneath it. The whole rotated footprint still needs level ground inside the map. Overlapping objects use normal gameplay physics. |
| 7 — Bulldoze | Click to remove a building, vehicle, spawn, road, or land block. Vehicles are picked by their footprint. Land roads become land; bridges become water. |
| 8 — Tree brush | Choose Sparse/Medium/Dense coverage and a smaller/larger brush (8–96 m) under **Objects**, then hold and drag to paint clusters. Shift + paint clears trees. |
| 9 — Ground texture | Open **Tiles > Ground texture** (or press **9**) and choose any image from **assets/textures/** by filename and thumbnail. Add PNG, JPG/JPEG, BMP, TGA, or GIF files to the folder and click **Refresh folder** to load them. Click **Use texture**, then drag a rectangle and release to paint the selected land blocks. Painting also changes ground beneath roads; water and bridge decks stay untouched. Each rectangle is one undo action and saves its texture filename with the city. |
| 0 — Ground elevation | Choose Raise or Lower under **Tiles**, then drag a rectangle. Flat tiles change by **2 m**; an existing ramp first becomes flat at its upper/lower level. Selected tiles must share a target level. Shift reverses the direction. Levels run from 0 to 8; water stays unchanged. Each rectangle is one saved undo action. |

Raising a square lifts all four shared corners to a flat top. Its four edge neighbors become straight ramps and its four diagonal neighbors become corner ramps. Repeated edits propagate outward until adjacent corners differ by at most one 2 m step. Tiles use two straight triangles with separate face shading, without center peaks or averaged heights. Roads and markings follow flat ground or straight ramps aligned with the road; corner ramps, sideways crossings, and turns on slopes are rejected. Diagonal roads and bridges need flat terrain. Buildings and parked vehicles need flat ground across their footprints; trees follow the actual slope. Edits that conflict with existing objects or bridge approaches leave the city unchanged. Saves without elevation data retain flat terrain.

The tree brush uses varied positions, heights, and rotations within each block, on four-meter plots. Overlapping strokes do not duplicate trees. Trees avoid roads, buildings, the player spawn, and vehicles; new construction clears obstructing trees automatically. Trees use the existing `tree1.glb` model and trunk collisions. Up to 8192 trees are saved per city, and a complete brush stroke is one undo action.

The road patterns use SimCity 4 references from the NAM team's [network edge flags and adjacent-piece rules](https://www.sc4nam.com/docs/tech-specs/understanding-rul-flags/), [diagonal/S-curve rules](https://github.com/NAMTeam/Network-Addon-Mod/blob/staging/Controller/INRULs/RoadAdvanced/RUL08_Road_Advanced.rul), and [curve examples](https://www.sc4nam.com/docs/feature-guides/base-network-additions/#draggable-wide-radius-curves-and-fractional-angle-functionality). This game generates pavement and traffic paths from its own shared road geometry.

The editor uses a Macintosh-style desktop in DOS blue, white, and yellow. Its top menu bar contains **File** (Save city, Start time, Play city, Cities, Close editor), **Edit** (Undo, Redo, Delete, Select, Bulldoze), **Tiles** (islands, roads, ground textures, elevation), **Buildings** (placement, creator, editing and rotating a selected instance), **Objects** (spawn, vehicles, trees), **View** (top view, grid, rotation, zoom), and **Help**. All map tools and their settings are available from these menus; the toolbox shows the current building and opens a pop-up to choose a saved design. Press **F10**, then navigate with arrows and Enter, or click the menus. Escape closes a dropdown before returning to Cities. Unavailable actions are disabled, and open menus block canvas editing.

Choose **File > Start time** to enter the city's starting clock time. Use a 24-hour time such as `07:30` or `18:45`; Ctrl+A replaces the field. Each Play session starts at that saved time, then the existing day/night cycle advances normally. The editor uses fixed morning light so ramp directions remain visible. Window close controls sit inside the title bar; settings and editor windows omit inline tooltips and shortcut hints. `--time HH:MM` overrides the saved starting time for that launch.

Right/middle drag pans; the wheel zooms; arrows pan; Q/E rotates the dimetric camera; V toggles top view; G toggles the grid. The angled view uses a 2:1 ground grid, with a 45° horizontal angle and 30° elevation. Ctrl+Z/Y undoes/redoes up to 32 edits; Ctrl+S saves. Edits autosave after a short idle period and save before leaving the editor. The city menu supports Up/Down selection, Insert for New, E for Edit, Enter for Play, F2 for Rename, and Delete for the deletion dialog.

Save files live in **`cities/<id>.city` beside the executable**, independently of the working directory. Saves contain the name, land/road grid, building footprints and heights, player spawn, vehicle types/exact positions/headings, individual trees, starting clock time, road directions, and painted ground textures. Version 6 stores shared corner elevations; version 5 tile levels migrate to flat terraces, extending terrain where an existing road requires flat ground. Version 4 saves preserve ground painting. Versions 1–3 load with soil ground; version 1 and 2 positions migrate to the smaller grid, and version 1 defaults to noon with no trees. Saves validate before writing and replace the previous file atomically; failed saves keep the editor open and preserve the previous city. Invalid saves are reported in the menu.

Free player positions use city version 16; earlier versions load their spawns at the original tile center. Maps with centered spawns keep their existing save format. Terrain edits keep the player's horizontal position and adjust its height to the ground.

NPC traffic is generated on the connected road graph, with right-hand lanes, corner braking, junction yielding, queues, and turnarounds only at dead ends. Routes cover each street in both directions and continue through bends and junctions. Cars use a three-point turn when their steering cannot fit a dead-end circle, checking for obstacles before backing up. Up to 200 NPC cars circulate, and the nearest 48 use full physics. Placed cars remain available to enter; pedestrians follow connected street verges and pass opposing walkers where there is clear space. Water and obstructions end a walking route while keeping its walkable approaches populated. Police build their pursuit graph from those same streets; crime reporting, six wanted levels, weapons, arrests, searches, and pickups remain active. Small islands use shorter police spawn distances.

## Police and wanted levels

Player health below 50% regenerates at 5 health per second while stopped, up to exactly 50%. Walking, sprinting, jumping, swimming, ragdolling, or moving in a vehicle pauses regeneration; stopping resumes it. Parked vehicles allow recovery too. Regeneration never revives a dead player or heals NPCs. Player-caused kills trigger a neutral grey screen overlay that fades out over 0.18 seconds, including gunfire, runovers, and people killed by player-shot vehicles or explosion chains. Hits on corpses and deaths caused by NPCs do not trigger the flash.

Police recognize vehicle theft, gunfire, assault, homicide, attacks on officers, and police-car theft. Speeding does not count as a crime or raise the wanted level. Nearby officers respond immediately; civilian witnesses pause to report a crime, then flee along sidewalks. Isolated crimes without a witness or an officer within hearing range do not automatically alert police.

There are six wanted levels, dispatching 1, 3, 5, 7, 9, and 12 patrol cars respectively, with two officers per car. Higher stars bring faster reinforcements, faster pursuit, more accurate and frequent gunfire, and more frequent PIT attempts. One-star officers attempt a nonlethal arrest; PIT maneuvers start at two stars. Police cars use saved Police designs, preserving their GLB geometry and materials, with pursuit siren audio. Cars arrive on connected roads outside the camera and stop within 20 m of an on-foot, stopped, or slowly driving suspect so officers can exit. At two or more stars, foot officers can shoot an occupied player car within 30 m when they have a clear shot, including while it moves nearby. Once the player car pulls more than 40 m away, officers return to their patrol car and resume pursuit. Nearby casualties remain visible while reserve units reinforce the pursuit.

New officers carry pistols at one star. At two and three stars, they carry a pistol/SMG mix with 60% and 75% SMG chances. At four, five and six stars, they carry all three guns, with 60%, 70% and 80% AK-47 chances. Each officer keeps their assigned weapon; damage, firing speed, holding pose and sound follow that weapon.

Defeated officers drop their guns once. Walk within a three-meter radius of a dropped weapon on foot to collect one magazine of matching ammunition: it fills that gun's magazine first, then its reserve, without switching your selected weapon. A gold ground marker identifies dropped weapons. Pickups happen automatically and silently, with no prompts or collection messages; solid obstacles block collection. Full ammo leaves the pickup available. Drops survive patrol despawning, expire after two gameplay minutes, and clear on player reset or respawn.

The stars appear below the clock. The large map opens in an inset desktop window and closes with its title-bar X, F2, or Escape. Both maps hide civilian cars and all aircraft, including parked player vehicles. Police markers and the search area appear only while wanted. A police car has a marker only while at least one living officer is seated inside; dismounted officers have their own markers. Empty or destroyed police cars and dead officers have no marker. Your player arrow remains visible. Break police line of sight, leave the last-known search area (165–900 m radius, growing faster at higher stars), and remain unseen outside it for 10–30 seconds to clear the wanted level. Returning inside the area or being spotted resets the escape timer. A completed arrest displays **BUSTED** and respawns the player on foot.

Nonfatal bullets deal full damage to the player, with a 20% chance of knocking them down. Most shots let the player keep walking and shooting. Fatal hits, explosions, and vehicle impacts still knock the player down; NPC hit reactions remain unchanged.

## Controls

Gameplay keeps its menubar with **File** (Resume, End Game / Main menu), **Edit** (recover vehicle, restore car tuning), **Settings** (world map, car tuning, aim mode), and **Help** (controls, about). **Escape / Start** pauses on the world map; Escape again or File > Resume continues. F10 opens the menus. Navigate with arrows and Enter, or click menus; Escape closes a dropdown or dialog first. File > End Game returns to the main menu.

Select **Settings > Aim mode** to toggle **Auto lock** (default) and **Free aim**. Hold right mouse / LT with a gun equipped to aim. Auto lock acquires a visible character near the crosshair within 60 meters and follows the chest. Use the existing camera controls (mouse / right stick, including remapped camera bindings) to adjust upward to the actual head for headshots or downward along the body. Move farther left or right to switch to the nearest visible target on that side. Looking beyond available targets releases the lock into free aim; release aim and hold it again to reacquire. With no nearby target, or with Free aim selected, camera aiming remains manual. Cover blocks locks, and dead or inactive characters are excluded. The choice saves as `auto_lock=1` or `0` in `controller-mappings.ini`; restoring key bindings preserves it.

Open **Settings > Graphics** in the main city menu for the blue DOS graphics panel. The Settings menu supports Up/Down, Tab and Enter. **Low**, **Balanced** (default), and **High** presets adjust rendering cost; individual changes become **Custom**. Controls include sunlight shadows (Off / 512 / 1024 / 2048), soft edges, shadow distance (30–180 m), nearby city and vehicle lights, view distance (100–6000 m), brightness (60–150%), VSync, frame limits (30 / 60 / 120 / 144 / 240 / unlimited), and **4x MSAA (restart)**. The panel explains the hovered or selected control, its cost and any disabled dependency, and shows current FPS. Tab or Up/Down selects controls and buttons, skipping disabled settings. Left/Right adjusts values or selects footer buttons; Enter activates the focused control and only saves when **Apply & save** is focused. Shift makes smaller slider steps; dragging or scrolling adjusts sliders and scrolling cycles quality/frame limits without toggling switches. Ctrl+D restores Balanced. MSAA changes require restarting the app; other saved settings apply to the current window or next play session.

Graphics edits stay in a draft until applied and carry into the next play session. **Apply & save** writes `graphics-settings.ini` beside the executable; **Cancel / Escape** restores the previous settings. A failed save leaves the panel open and the previous file intact. Invalid settings files retain defaults and display a notice. Cars, buildings, and trees cast nearby sunlight shadows, including transparent leaf gaps; streetlights, shops, runway fixtures and directional headlights illuminate nearby surfaces at night. City and tree batches are split into spatial chunks so shorter view distances reduce GPU work. Distant illuminated windows remain visible when nearby lights are disabled. VSync can cap rendering to the monitor rate even with Unlimited selected. Use `--graphics` to start in the main menu's panel or `--quality low|balanced|high` for a temporary preset without saving it.

Open **Settings > Controller mapping** in the main city menu and choose **On foot**, **Car**, **Plane**, or **General** (click a tab, or use Tab / Shift+Tab). Movement, enter/exit, recovery, camera and zoom bindings are independent in each gameplay mode; remapping walking never changes driving or flight. Map and pause/resume remain shared under General. Select an action and **Add binding**, then press a keyboard key or gamepad button, or move a stick/trigger. Added bindings coexist with defaults; select a binding and **Remove binding** to replace a default. Up/Down selects actions, Left/Right selects bindings; scroll either list for more entries. Escape cancels capture; F10 is reserved for the menubar. **Defaults** restores all modes' keyboard/gamepad bindings. **Deadzone** cycles 5–50% in 5% steps to tune stick drift. Changes save immediately; **Save & close** or Escape also retries any pending save. Save failures keep the editor open, display an error, and leave the previous file intact. Loading old saves removes Escape from the map binding while retaining F2 and other mappings.

Mappings load from **`controller-mappings.ini` beside the executable**, independent of the working directory. The game creates the defaults on its first interactive launch. The separate bindings use **version 2**, with `foot_`, `car_`, and `plane_` section names; old combined version 1 files must be reset. New custom mappings persist normally. INI keeps the configuration readable and uses C++17 streams without a serialization dependency; it stores raylib key/button/axis codes, with `axis=code,direction` and directions of `-1` or `1`. Triggers use positive direction and normalize raylib's -1–1 range to 0–1. Each action accepts up to 64 bindings. Empty sections disable an action, omitted sections retain defaults, and invalid files preserve the current/default mappings with an error notice. F10 and Escape remain available for menu navigation even after remapping.

**Generic USB joysticks are supported without an Xbox/SDL gamepad mapping.** The game reads their raw buttons, axes, and D-pad hats through the same GLFW library used by raylib's desktop backend. The mapping editor shows connected controller names; choose **Add binding** and press a button or move a stick/D-pad to assign it. Generic layouts vary, so they use custom bindings instead of guessing which physical button is A/B/X/Y. These bindings display as **USB button**, **USB axis**, or **USB D-pad**, and save as zero-based `joystick_button=code`, `joystick_axis=code,direction`, and `joystick_hat=hat*4+direction` (up/right/down/left = 0/1/2/3). Raw axis 4/5 are ordinary axes, not assumed to be triggers. Up to 16 connected devices, 64 buttons, 16 axes, and four hats per raw device are supported. Standardized gamepad bindings remain available in each mode. A new capture ignores buttons already held when Add binding was selected; release and press them again.

```ini
version=2
deadzone=0.2
[foot_forward]
key=87
key=265
axis=1,-1
```

Gamepad defaults: left stick drives/walks and pitches/banks the plane; right stick orbits the camera. Y/Triangle enters/exits, B/Circle brakes, A/Cross jumps, and L-stick click sprints. RT/LT changes plane throttle, LB/RB controls the rudder, and X/Square toggles flaps. D-pad up recovers, right opens tuning, down zooms in, and left zooms out. Back/Select toggles the map; Start captures/releases the mouse. Keyboard defaults remain below.

| Key | Action |
| --- | --- |
| W / S | Drive forward / reverse; opposite direction brakes first |
| A / D | Steer left / right |
| Space | Rear handbrake / drift |
| H / gamepad right-stick click | Hold the car horn; remappable in Controller mapping |
| E | Enter the nearest car or plane / steal a nearby stopped traffic car / exit when stopped on the ground |
| WASD (on foot) | Move relative to the camera |
| Shift (on foot) | Sprint |
| Space (on foot) | Jump |
| Q / gamepad B (on foot) | Enter / leave cover beside a wall or barrier |
| Mouse | Orbit the third-person camera |
| Mouse wheel | Zoom the camera |
| R | Recover the car, or return the piloted plane to its placed spawn |
| F5 (on foot) | Respawn on foot; R reloads an equipped weapon |
| F2 | Toggle the city world map |
| F3 (in car) | Open / close live car tuning |
| F10 | Open / close the menubar |
| Escape | Pause on the world map / resume; close menus |
| Left click | Capture the mouse for gameplay controls |
| Window close button | Exit |

Player-controlled cars coast when you release W/S or the left stick. Hold the opposite direction to brake to a stop; after a 0.25-second pause, the car drives in that direction. Releasing the input cancels the change. This works in both directions and in stolen cars. NPC braking is unchanged.

Player and nearby NPC engines loop `assets/sounds/car-engine.wav`, with pitch and volume following speed and throttle. Hold **H** or **right-stick click** while driving to sound `assets/sounds/car-horn.wav`; release to stop. The horn's quiet tail is removed and its seam crossfaded in memory at startup, leaving the original asset unchanged. Raylib streams the continuous loop. Nearby cars use distance attenuation and stereo panning; shared audio data and a fixed voice pool bound the decoding and mixing cost. Vehicle audio fades while gameplay is paused, and the player's engine is silent on foot or in the plane. Run `powershell -NoProfile -ExecutionPolicy Bypass -File tests/engine_audio_smoke.ps1` to check audio initialization and WAV streaming on a machine with audio output.

Police pursuits loop `assets/sounds/siren.wav`. Player and police gunfire use `9mm.wav` for the pistol, `smg.wav` for the SMG, and `ak47.wav` for the AK-47. Every car or aircraft explosion plays `explosion.wav`, including chain reactions. Gunfire and blasts use distance falloff and stereo placement, allow overlapping shots, and stop when gameplay pauses.

Build speed before holding Space and A or D to start a slide. Use W to keep driving through it; release Space and straighten or countersteer to regain grip.

Press E while stopped or moving slowly to leave the car. Walk back within a few meters and press E to enter. The game checks for a free exit beside either door and behind the car, so buildings cannot trap the character inside a wall. The car applies its parking brake while you explore on foot.

Press **Q / gamepad B** beside a wall or barrier to take cover. Tall walls use standing cover with small steps; lower walls use a crouch, and very low barriers use a crawl. WASD / the left stick moves slowly along the wall and stops at its end. The whole body turns toward travel and keeps that facing direction when you stop. Aim to rise above low cover or lean around the end of a tall wall; releasing aim tucks the character back in. The aiming camera uses the shoulder on the exposed side of an edge, or your last travel side above low cover. The body and weapon blend through these movements, including quick direction and aim reversals, and shooting waits until the weapon is raised. The lowered collision shape and body pose keep the character behind the barrier. Q / B, sprint, jump, or moving away leaves cover. Cover is available in Controller mapping.

### NPC traffic and stealing cars

NPC cars circulate on your built roads. Up to 48 nearby NPC cars use full Jolt physics; distant traffic follows its lanes and becomes physical when you approach. Stolen cars remain physical and retain ownership during the play session. Traffic slows for corners, queues behind vehicles, and yields to the on-foot player. The existing warning horns, pedestrian yielding, passing, and vehicle theft remain active.

In the city editor, open **Settings > City settings...** to set **Walking NPC density (%)**. Type a whole number from 0 to 100 or drag the slider, then **Apply**. **0** disables walking pedestrians; **25** allows up to 16, **50** up to 32, and **100** keeps the existing maximum of 64. Actual population also depends on walkable streets and saved NPC designs. Lower settings allocate fewer pedestrian physics bodies. The setting affects pedestrians; traffic drivers and police retain their existing populations. Changes support city undo/redo and save with the map, taking effect the next time you Play. Cancel or Escape leaves the current value unchanged. Non-default density uses city save version 15; versions 1-14 still load at 100%, and default-density maps retain their existing save format.

After waiting behind a stationary vehicle, NPCs can pass on straight roads at least 9 meters wide. They check road edges, nearby obstacles, approaching cars and the complete merge path before changing lanes, pass at up to 4.5 m/s, and return to their route. Bridges, junctions, narrow streets and blocked passing lanes remain single-file. Passing searches run only for blocked cars, at most once per second; normal steering plans retain their existing rate. Six nearby NPC audio pairs reuse shared engine/horn recordings, with no file loading or audio allocation during gameplay.

Leave your car with E, walk beside a stopped traffic car, and press E when **Press E to steal traffic car** appears. Entry requires a clear path, a distance within 3.3 meters, and speed below 2.5 m/s. Stealing pulls the driver out as a physical ragdoll, stops the car's AI control, and gives you immediate WASD control. The driver gets up and flees after settling. Occupied police cars can also be stolen; their driver is pulled out and the passenger disembarks. The camera, speed display, drifting, F3 tuning, and R/water recovery all follow the car you take. Exit and reenter it as usual, or steal another car. Abandoned cars stay parked, and tuning stays with each individual car for the session. Displaced NPC cars recover into clear road space when more than 35 meters from the player; ordinary traffic queues wait or pass instead of teleporting.

Cars and aircraft start with 100 health. Hard crashes and bullets reduce it; gentle bumps and normal driving or flying do not. The HUD shows speed only in vehicles and hides vehicle health. On foot, ammunition appears in a matching container below the clock, or below wanted stars when present; weapon names appear only in the weapon wheel. Below half health, vehicles smoke while keeping their original paint. Smoke and explosions use animated shader billboards with feathered edges and smooth fades. At zero health a vehicle turns black and explodes once, damaging and pushing nearby characters and vehicles, with cover blocking the blast. Close vehicles can chain explosions. Occupants die, and wrecks cannot be driven or entered. Recovery repairs the current vehicle; ordinary position updates preserve damage. After **WASTED**, the player respawns on safe ground near the death location; offshore deaths use the closest safe road or bridge.

Start beside a traffic car, ready to steal it, or capture a preview:

```bash
./build/ucrt64/Ambaretto.exe --traffic
./build/ucrt64/Ambaretto.exe --traffic --screenshot build/traffic-preview.png
```

### Flying

Place aircraft on a sufficiently large clear part of your island. Walk beside the cockpit and press E to board. Existing flight controls and aircraft physics apply. R repairs and returns the selected aircraft to its placed spawn.

| Key (in plane) | Action |
| --- | --- |
| Shift / Ctrl | Increase / decrease throttle; setting stays when released |
| W / S | Pitch nose down / up |
| A / D | Bank left / right |
| Left / Right arrows | Rudder and nose-wheel steering |
| Space | Landing gear wheel brakes |
| F | Toggle flaps for low-speed takeoff/landing |
| E | Exit after landing and stopping |
| R | Return plane to its placed spawn, retaining pilot mode |
| Mouse / wheel | Orbit / zoom the flight camera |

Leave a long clear strip of land for takeoff and landing. Trainer, F-18, and 747 aircraft retain their existing aerodynamics, controls, damage, and camera behavior.

The character uses Jolt's virtual capsule controller for slopes, steps, jumping, and collision with buildings, trees, and the vehicle. Its body turns toward movement and animates while walking or running. On foot, the camera smoothly follows behind the character horizontally while walking, running or swimming, preserving the vertical angle you choose. Mouse or right-stick input overrides this; automatic follow resumes after 100 ms without camera input while moving. Looking around while standing still keeps your chosen view. Aiming, shooting, cover, the weapon wheel and ragdolls suspend automatic follow. Holding a movement direction keeps travel straight as the camera recenters; changing or releasing movement uses the current view again. The driving camera tracks the car's position directly at the selected zoom distance, without falling behind at speed. Its angle gradually recenters after you stop moving the mouse. Camera sweeps keep the view clear of walls and terrain. Focus loss and world map pause gameplay and release the mouse.

Press **F3** to tune the current car. The panel releases the mouse, frames the car for inspection, disables driving/character input, and applies the parking brake. Physics keeps running so suspension changes settle in front of you. Drag the sliders or scroll over a row; Up/Down selects a setting and Left/Right adjusts it. Hold Shift for finer edits. Right-drag outside the panel to orbit the car and scroll outside it to zoom. Panel clicks cannot recapture the mouse or move the camera. F3, Escape, or the Close button returns to the previous camera and mouse capture state. Losing window focus pauses the simulation and cancels slider dragging; click to recapture after closing the panel if focus was lost.

The Suspension tab edits tire radius, spring rest length, suspension travel, mount height, spring stiffness, damping, and the force cap per wheel. The Handling tab edits normal grip, rear handbrake grip, drive traction, steering angle, wheelbase, and track width. Wheelbase is the front-to-rear distance between wheel centers (default 2.50 m); track width is the left-to-right distance (default 1.480 m), shared by both axles. These sliders move the wheels, suspension mounts, and ground contacts together around the fixed car body. Steering's speed limit also uses the current wheelbase. Tire size updates both the GLB wheel meshes and ground contact calculations. Settings are bounded, and travel is constrained to keep the compressed suspension length positive. Live readings show each wheel's contact, compression, and spring force. **Defaults** restores the original setup; **Reset car** recovers the car and clears skid marks while retaining your tuning. R and water recovery also retain the setup. Edits last for the current game session.

The **Performance** tab controls **Top speed** (80–400 km/h, default 240 km/h) and **Acceleration** (2–16 m/s², default 9 m/s²). Engine force acts through the tires, so tire grip and Drive traction still affect acceleration. The default Drive traction is 0.9. Lowering top speed while moving slows the car through tire forces. Reverse retains its 39.6 km/h target. Performance settings belong to each car, survive recovery, and work on stolen cars. NPC controllers retain their normal cruising speeds with the faster engine. Start directly in this tab with `--performance`.

## Character creator

Open **Settings > Character creator** in Cities, **Objects > Character creator** in the map editor, or launch `Ambaretto.exe --character-creator`. Add component GLBs to these folders, then click **Refresh files**:

| Folder | Components |
| --- | --- |
| `assets/character/head/` | Head |
| `assets/character/body/` | Body and pelvis, selected separately |
| `assets/character/hand/` | Upper arm, forearm, and hand, selected separately |
| `assets/character/leg/` | Thigh and shin, selected separately |
| `assets/character/shoes/` | Shoe/foot |

Each component retains its imported materials and textures. GLBs use Y-up and +Z-front; limbs extend along Y. Models are centered and fitted to the existing 15-part articulated rig. Select a part and adjust its **Scale X/Y/Z**, **Offset X/Y/Z** in meters, and **Rotation X/Y/Z** in degrees. Supply a left-side arm, leg, hand, or shoe: the right side reflects its mesh and fitting across local X, including asymmetric details. The **Rig** button shows fitting guides; **Idle**, **Walk**, **Run**, and **Ragdoll** preview the existing poses and physics. Fitting changes visuals; the existing joints, collision sizes, movement, combat, swimming, cover, and recovery stay the same.

Choose **Player**, **NPC**, or **Police**, name the design, and **Save character** (Ctrl+S). Designs live in `characters/character-<name>.character` beside the executable. **File > Open Saved Character** opens a pop-up list to load a design for editing; saving the same name updates it. **Save and close** saves the design and returns to the previous menu. Select saved Player designs through the map's player picker. NPC and Police designs enter the existing pedestrian/traffic-driver and police spawn systems, using complete saved appearances without randomizing individual parts or colors.

Use **Objects > Choose player character** in the map editor to select a saved Player design. A map needs a spawn and a complete Player design with valid GLBs to Play; vehicles and NPCs are optional. Missing NPC designs produce no pedestrians or ambient traffic drivers/cars. Missing Police designs produce no police patrols, even when wanted. Each role is independent, and police also require a saved police car. Missing or corrupt component GLBs prevent saving/using a character and exclude it from gameplay; there is no block-character fallback.

Cities with a selected character use version 13 (version 14 when custom buildings are embedded) and embed its parts and fitting. Versions 1–12 remain editable; choose a Player character before playing them. Saved edits with the same Player name refresh its map appearance; an embedded design remains usable when its library file is absent. Component GLBs must accompany shared maps. Use `--character-creator --new --screenshot build/character-creator.png` for a creator preview. The `character_designs` check covers persistence, old maps, role selection, missing/corrupt models, mirrored fitting, rendering, ragdolls, and NPC/police spawning. `menu_workflow` exercises create/open, draft Save/Discard/Cancel and failed saves, Play and Create / edit navigation, read-only city selection, saved-design pop-ups, physics freezing while paused, both resume paths, and End Game.

## Code and physics

Cars are created in **Objects > Car editor** in the map editor, or **Create / edit > Cars** from the main menu. Add complete body GLBs to **`assets/cars/`** and a single wheel GLB to **`assets/wheels/`**, then click **Refresh files**. Their filenames appear in the Body/Wheel lists; embedded textures and material colors are retained. Body models use the [glTF Y-up, +Z-front convention](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#coordinate-system-and-units), automatically aligned to the driving direction. Wheels are centered automatically, their thinnest axis becomes the axle, and their mesh scales uniformly to the tuning's tire radius. No randomized paint or stock car fallback is used.

Set a name, Regular/Police type, body width/height/length, and **Offset X/Y/Z**, then adjust **Tuning** to fit wheelbase, track width, tire radius, suspension, and handling. Body offsets move the body relative to the wheels in meters, from -4 to +4 on each local axis; wheel positions stay fixed. Collision, lights, and the map placement footprint follow the body. The imported mesh and textures remain intact. **Save car** (Ctrl+S) writes `cars/car-<name>.car` beside the executable. **File > Open Saved Car** opens a pop-up list of existing designs. **Save and close** saves and returns to the previous menu; use **Objects > Choose car** to select a saved design, then click clear flat terrain to place it. **Objects > Edit selected car** opens an in-screen draft; refresh saved designs in the map after saving to update its copies, with undo and overlap checks. Opening or playing a map also refreshes all cars whose names match a saved design, including edits made from the main menu or the standalone editor. Opening the map records this refresh as one undo action and saves it with the city. Positions and rotations stay intact. If an enlarged or shifted design would overlap another object or leave buildable ground, its update is rejected for that map and reported. Cars keep their embedded settings when their library design is missing; saving under a different name creates a separate design.

Saved Regular designs supply civilian traffic; each police patrol or wanted reinforcement randomly chooses from all saved Police designs, retaining its body, offsets, tuning, and siren audio. An empty car library produces no cars: create and save one first. Missing or invalid GLBs prevent saving/using that design and are skipped in gameplay. Car save version 2 and city version 11 include body offsets; version 1 cars and version 10 cities load with zero offsets, preserving older city/building loading. Older placed cars with no design appear as orange wire outlines; select one and assign a design through **Edit selected car**. GLBs still need to accompany a shared city. `Ambaretto.exe --car-editor` opens the standalone editor; `--screenshot <path.png>` captures its preview.

- `include/vehicle.hpp`: vehicle input, wheel state, and physics interface.
- `src/vehicle.cpp`: Jolt world, chassis collisions, raycast suspension, tire grip, and handbrake behavior.
- `include/plane.hpp`, plane implementation in `src/vehicle.cpp`: aircraft rigid body, aerodynamics, flight controls, landing gear, damage, and reset.
- `include/city.hpp`, `src/city.cpp`, `src/city_editor.cpp`: grid editing, persistence, road graph routes, city management, and the editor.
- `include/day_night.hpp`: 24-minute clock, sky palette, sun direction and shared lighting.
- `src/main.cpp`: rendering, keyboard input, fixed step loop, chase camera, and a bounded skid mark buffer.
- `src/car_renderer.cpp`: imported body/wheel loading, automatic wheel alignment, steering/spin animation, material lighting, and body scaling.
- `include/car_tuning.hpp`, `src/tuning_panel.cpp`: runtime tuning defaults/limits, sliders, precision input, and wheel telemetry.
- `include/player.hpp`, `src/player.cpp`: on-foot control and entering/exiting the vehicle; the Jolt capsule implementation lives alongside the physics world in `src/vehicle.cpp`.
- `include/traffic.hpp`, `src/traffic.cpp`: regional lane routes and traffic streaming, NPC driving and braking, pedestrian yielding, ownership transfer, and distant recovery.
- `src/third_person_camera.cpp`: mouse orbit, zoom, and camera-relative movement.
- `include/environment.hpp`, `src/environment.cpp`: shared terrain mesh, city layout, buildings, trees, spawn, and water recovery bounds.
- `src/building_mesh.cpp`: editable building geometry, texture discovery, decals, and reusable building saves.
- `src/building_renderer.cpp`: shared texture arrays, combined spatial building batches, budget checks, and render statistics.
- `src/environment_renderer.cpp`: terrain, buildings, connected road tiles, animated ocean, shadows, and minimap.
- `tests/physics_tests.cpp`: headless checks for driving behavior, terrain/collider alignment, Miami street driving, building collisions, and recovery. The original flat test track and suspension ridges remain as test fixtures.
- `tests/player_tests.cpp`: walking, sprinting, jumping, slopes, collisions, parking, entry/exit conditions, and mouse camera movement.
- `tests/tuning_tests.cpp`: live suspension changes, tire contact, validation, per-car isolation, setup retention, and slider input/focus behavior.
- `tests/plane_tests.cpp`: road access, runway collision alignment, parking, takeoff distance, lift/control response, stalls, gravity, gliding, landing/braking, taxi steering, aircraft collisions, entry/exit, and recovery.
- `tests/traffic_tests.cpp`: sustained road following, pedestrian/vehicle braking, stealing, switching cars, parking, per-car tuning/recovery, and plane boarding with traffic.
- `tests/map_tests.cpp`: regional road connectivity, landmarks, road/deck collision alignment, driving across the original bridges and the skyway in both directions, solid ports, and distant traffic streaming.

Physics runs at 120 Hz independently of rendering. The chassis weighs 1100 kg and has a lowered center of mass. Steering grip receives priority in the tire friction circle, and steering angle decreases as speed rises. The handbrake reduces rear grip and brakes those tires, allowing the rear to slide while the front wheels steer and drive. Suspension rays query only ground collision objects. Dampers respond to suspension movement relative to the road, so horizontal speed alone cannot lift the body. Rendered tires use the chassis's current pose and sampled spring length, keeping the axles aligned after physics integration. The vehicle checks verify steady ride height, ground contact, suspension travel, and wheel alignment at 80, 240, and 400 km/h.

This is an arcade vehicle model. Wheel meshes are visual; Jolt handles the chassis rigid body and world collisions, while the controller applies suspension and tire impulses at the ground contact points. An offset-center-of-mass shape lowers the chassis center of mass by 0.5 m. Continuous collision detection protects the chassis at speed. Runtime tuning defaults and slider limits are in `include/car_tuning.hpp`.

`tests/city_tests.cpp` checks water-only cities, connected island growth, atomic placement, road intersections, building and vehicle footprints, multiple saves, rename/delete, invalid and failed saves, clustered tree brushes/clearing/collisions, saved clock times, original save compatibility, land collision alignment, sustained traffic and dead-end turns, NPCs, police routing and reinforcement, wanted levels, vehicle entry, and recovery. The old procedural environment remains a regression fixture for the existing gameplay checks.

`tests/menu_tests.cpp` checks the desktop menus' command dispatch, unavailable actions, keyboard/mouse navigation, focus loss, canvas input blocking, and DOS palette at 1024 x 600.

## Checks and debugging

For a Debug build on Windows, open MSYS2 UCRT64 in the project directory. Install GDB, configure the Debug preset, and build the game and tests:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gdb
cmake --preset ucrt64 -DBUILD_TESTING=ON
cmake --build --preset ucrt64 --parallel 2
ctest --preset ucrt64
gdb ./build/ucrt64/Ambaretto.exe
```

On Linux, use the README's dependency setup, then configure a separate `build/debug` directory with `-DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON`, build it, and run `ctest --test-dir build/debug --output-on-failure`. Render and UI checks require a working desktop/OpenGL session. The menu workflow test uses GNU linker `--wrap`, which Apple's linker does not support; keep tests disabled for the documented macOS game build.

Open directly in tuning mode, or capture the panel:

```bash
./build/ucrt64/Ambaretto.exe --city path/to/saved.city --tuning
./build/ucrt64/Ambaretto.exe --city path/to/saved.city --tuning --screenshot build/tuning-preview.png
```

All source, build configuration, and checks use C++; no Go toolchain is needed.

`day_night_cycle` checks the cycle length, pause, midnight rollover, 24-hour time validation, fractional frame timing and dawn/sunset colors.

`graphics_settings` checks presets, validation, atomic file replacement, failed-save preservation, reversible previews and keyboard/slider interaction at the minimum window size. `scene_lighting` creates a hidden OpenGL window and checks actual framebuffer pixels for sunlight shadows, quality changes, local light toggles, directional headlights and brightness.

`controller_mapping` checks default and additional bindings, generic USB button/axis/D-pad capture, held-button handling, raw versus standardized layouts, analog deadzones/triggers, one-shot actions, disconnected pads, file replacement, aim-mode and binding persistence, and invalid-file handling. `weapons_and_health` also checks chest acquisition, moving targets, camera-controlled headshots at 30/60/120 FPS, left/right target switching, free-aim release, range, eliminated targets, and cover occlusion. Preview the new UI with `--controllers`, `--menu`, or `--help-menu`, optionally combined with `--screenshot`.

`vehicle_audio` checks the supplied recordings' trimmed loop lengths, quiet gaps, circular crossfade seams, native WAV decoding and positional falloff/panning. Run `vehicle_audio_tests.exe --playback` for muted native streaming, overlapping effect voices, and pause checks. Weapon, police, and vehicle damage checks verify that actual shots and explosions emit the matching sound once. `traffic_and_theft` also checks danger/queue horn timing, pedestrian yielding, passing and merging, blocked/oncoming lanes, narrow streets and bridges.

For a rendered map preview (saves the image and exits):

```bash
./build/ucrt64/Ambaretto.exe --city path/to/saved.city --overview --screenshot build/city-overview.png
./build/ucrt64/Ambaretto.exe --city path/to/saved.city --map --screenshot build/player-map.png
```

Start on foot, or capture a character preview:

```bash
./build/ucrt64/Ambaretto.exe --city path/to/saved.city
./build/ucrt64/Ambaretto.exe --city path/to/saved.city --screenshot build/character-preview.png
```

Capture the empty editor with `--editor --screenshot build/editor.png`, or a saved city with `--city path/to/saved.city --editor --screenshot build/editor.png`. A plain `--screenshot` captures the main menu; add `--cities` for the city directory.
