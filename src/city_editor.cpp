#include "city.hpp"
#include "ui_font.hpp"
#include "menu_bar.hpp"
#include "graphics_panel.hpp"
#include "plane.hpp"
#include "environment_renderer.hpp"
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>

namespace ambaretto {
namespace {
constexpr Color background = ui::dos_blue, panel = ui::dos_blue, border = ui::dos_white;
constexpr Color ink = ui::dos_white, muted = ui::dos_white, accent = ui::dos_yellow;
constexpr int sidebar = 280;
enum class Tool { Select, Land, Road, Building, Spawn, Vehicle, Bulldoze, Trees, Ground, Elevation };
const char* tools[] = {"Select / edit", "Island / expand", "Road", "Building block", "Player spawn", "Vehicle", "Bulldoze", "Tree brush", "Ground texture", "Elevation"};
const char* ground_names[] = {"Soil", "Grass", "Beach sand", "Asphalt"};
const char* densities[] = {"Sparse", "Medium", "Dense"};
const char* vehicle_names[] = {"Car", "Trainer plane", "F-18", "Boeing 747"};
Vector3 vector(Vec3 p) { return {p.GetX(),p.GetY(),p.GetZ()}; }
void text(const std::string& value, float x, float y, int size = 18, Color color = ink) { ui::draw_text(value.c_str(),int(x),int(y),size,color); }
std::string fit(std::string value,int size,float width) {
    if (ui::measure_text(value.c_str(),size)<=width) return value;
    while (!value.empty() && ui::measure_text((value+"...").c_str(),size)>width) value.pop_back();
    return value+"...";
}
bool hit(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(),r); }
bool close_clicked(Rectangle r, bool interactive = true) { return interactive && hit(ui::window_close(r)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }
bool button(Rectangle r, const std::string& label, bool selected = false, bool enabled = true, bool interactive = true) {
    const bool hover = interactive && enabled && hit(r);
    DrawRectangleRec({r.x+2,r.y+2,r.width,r.height},border);
    DrawRectangleRec(r,enabled && (selected || hover) ? accent : panel);
    DrawRectangleLinesEx(r,1,border);
    text(label,r.x+10,r.y+(r.height-18)/2,18,enabled ? (selected || hover ? background : ink) : ui::dos_light_blue);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
void input(std::string& value, Rectangle r) {
    if ((IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_A)) value.clear();
    DrawRectangleRec(r,background); DrawRectangleLinesEx(r,2,accent);
    std::string visible = value+(int(GetTime()*2)%2 ? "_" : "");
    while (!visible.empty() && ui::measure_text(visible.c_str(),21)>r.width-24) visible.erase(visible.begin());
    text(visible,r.x+12,r.y+13,21);
    for (int c = GetCharPressed(); c; c = GetCharPressed()) if (c>=32 && c<127 && value.size()<48) value += char(c);
    if ((IsKeyPressed(KEY_BACKSPACE)||IsKeyPressedRepeat(KEY_BACKSPACE)) && !value.empty()) value.pop_back();
}
bool save(City& city,const std::filesystem::path& directory,std::string& status,bool& dirty) {
    if (!city.save(directory,status)) return false;
    dirty = false; status = "City saved"; return true;
}
std::optional<int> start_clock(int minutes) {
    std::string value = TextFormat("%02i:%02i",minutes/60,minutes%60), error;
    bool interactive = false;
    while (!WindowShouldClose()) {
        BeginDrawing(); ui::draw_desktop();
        Rectangle r{(GetScreenWidth()-620)/2.f,(GetScreenHeight()-276)/2.f,620,276};
        ui::draw_window(r,"City start time");
        ui::draw_window_close(r);
        text("HH:MM",r.x+24,r.y+61,17,muted);
        input(value,{r.x+24,r.y+96,572,52});
        text(error,r.x+24,r.y+166,16,accent);
        const bool accept = button({r.x+24,r.y+208,278,44},"Apply",true,true,interactive)||(interactive && IsKeyPressed(KEY_ENTER));
        const bool cancel = close_clicked(r,interactive) || button({r.x+318,r.y+208,278,44},"Cancel",false,true,interactive)||(interactive && IsKeyPressed(KEY_ESCAPE));
        MenuBar(MenuMode::Editor).draw();
        EndDrawing();
        interactive = true;
        if (cancel) return {};
        if (accept) {
            DayNight clock;
            if (clock.set_time(value)) return ((value[0]-'0')*10+value[1]-'0')*60+(value[3]-'0')*10+value[4]-'0';
            error = "Use HH:MM, for example 07:30 or 18:45.";
        }
    }
    return {};
}
void help_dialog(bool editing, bool about) {
    bool interactive = false;
    while (!WindowShouldClose()) {
        BeginDrawing(); ui::draw_desktop();
        Rectangle r{(GetScreenWidth()-760)/2.f,(GetScreenHeight()-394)/2.f,760,394};
        ui::draw_window(r,about ? "About Ambaretto" : "Keyboard reference");
        ui::draw_window_close(r);
        if (about) {
            text("Ambaretto",r.x+28,r.y+64,28,accent);
            text("Build an island. Make it your own. Take it for a drive.",r.x+28,r.y+120,18);
            text("City builder / C++17 / raylib / Jolt Physics",r.x+28,r.y+158,18);
        } else {
            const char* lines[] = {
                "F10: open menus   Arrows: navigate   Enter: choose",
                editing ? "1-9 / 0: tools   Ctrl+S: save   Ctrl+Z/Y: undo/redo" : "Insert: new city   E: edit   Enter: play",
                editing ? "Right/middle drag: pan   Wheel: zoom   Q/E: orbit" : "Up/Down: select city   F2: rename   Delete: delete",
                editing ? "V: top/isometric view   G: grid   Delete: remove selection" : "Choose File or City in the menu bar for all commands.",
                editing ? "Shift: flip road bends / clear trees with the brush" : "A city needs a player spawn before it can be played.",
                "Esc: close menus and dialogs / return to Cities"
            };
            for (int i = 0; i < 6; ++i) text(lines[i],r.x+28,r.y+62+i*38,18);
        }
        const bool close = close_clicked(r,interactive) || button({r.x+r.width-152,r.y+r.height-64,124,36},"OK",true,true,interactive)||(interactive && (IsKeyPressed(KEY_ESCAPE)||IsKeyPressed(KEY_ENTER)));
        MenuBar(editing ? MenuMode::Editor : MenuMode::Cities).draw();
        EndDrawing();
        interactive = true;
        if (close) return;
    }
}
bool pick(const Environment* environment, Camera3D camera, Vector2 mouse, CityCell& cell, Vec3& point) {
    const Ray ray = GetScreenToWorldRay(mouse,camera);
    if (std::abs(ray.direction.y)<.0001f) return false;
    float distance = (City::level-ray.position.y)/ray.direction.y;
    if (distance < 0) return false;
    // ponytail: scan the cached 128 x 128 terrain mesh; add a picking index if larger maps need it.
    if (environment) for (const auto& t : environment->triangles()) {
        const auto& vertices = environment->vertices();
        const auto hit = GetRayCollisionTriangle(ray,vector(vertices[t.a]),vector(vertices[t.b]),vector(vertices[t.c]));
        if (hit.hit && hit.distance<distance && hit.point.y>=City::level-.001f) distance = hit.distance;
    }
    const auto p = Vector3Add(ray.position,Vector3Scale(ray.direction,distance));
    point = Vec3(p.x,p.y,p.z);
    cell = City::cell(p.x,p.z); return City::contains(cell);
}
void tile_outline(const City& city, CityCell p, Color color) {
    const auto patch = city.ground_patch(p);
    for (int i = 0; i < 4; ++i)
        DrawLine3D(vector(patch[i]+Vec3(0,.16f,0)),vector(patch[(i+1)%4]+Vec3(0,.16f,0)),color);
}
void draw_city(const City& city, bool grid) {
    for (int z = 0; z < City::width; ++z) for (int x = 0; x < City::width; ++x) {
        const CityCell p{x,z}; if (city.tile(p)==CityTile::Water) continue;
        if (grid) tile_outline(city,p,{97,135,106,255});
    }
    for (auto v : city.vehicles) {
        const auto p = vector(v.position);
        rlPushMatrix(); rlTranslatef(p.x,p.y,p.z); rlRotatef(float(v.rotation*90),0,1,0);
        if (v.kind==CityVehicleKind::Car) {
            DrawCube({0,.9f,0},1.85f,1.2f,3.7f,{226,158,75,255});
            DrawCube({0,1.65f,.2f},1.45f,.4f,1.6f,{39,58,73,255});
        } else {
            const auto& spec = plane_specs(PlaneType(int(v.kind)-1));
            DrawCube({0,spec.body_radius+1,0},spec.body_radius*2,spec.body_radius*2,spec.length,ink);
            DrawCube({0,spec.body_radius+1,0},spec.span,.25f,spec.length*.15f,ink);
        }
        rlPopMatrix();
    }
    if (city.spawn) {
        auto c = City::center(*city.spawn); c.SetY(city.height(c.GetX(),c.GetZ())+.2f);
        tile_outline(city,*city.spawn,accent); DrawCylinder(vector(c),.7f,.7f,3,10,accent);
        DrawSphere(vector(c+Vec3(0,3.5f,0)),1,accent);
    }
}
enum class EditorResult { Back, Play, Quit };
EditorResult editor(City& city,const std::filesystem::path& directory,const std::string& screenshot) {
    Tool tool = Tool::Land;
    int building_size = 1, building_height = 12, vehicle_kind = 0, rotation = 0;
    int brush_size = 3, density = 2, ground_texture = 0, elevation_direction = 1, revision = 0, preview_revision = -1;
    int selected_building = -1, selected_vehicle = -1;
    bool dirty = false, dragging = false, top = false, grid = true, close_failed = false, diagonal_roads = true;
    bool painting = false, stroke_saved = false, brush_remove = false, brush_sampled = false;
    float save_time = 0, preview_time = 0, yaw = .78539816f;
    Vec3 brush_point = Vec3::sZero(), last_brush = Vec3::sZero();
    std::unique_ptr<Environment> preview_environment;
    std::unique_ptr<EnvironmentRenderer> preview_renderer;
    std::unique_ptr<City> elevation_preview;
    CityCell elevation_anchor{}, elevation_hover{};
    int elevation_revision = -1, elevation_preview_delta = 0;
    bool elevation_valid = false;
    SceneLighting lighting;
    GraphicsSettings graphics; graphics.shadows = 0; graphics.view_distance = 6000;
    DayNight preview_day; preview_day.set_time("09:00");
    Vec3 focus = city.spawn ? City::center(*city.spawn,0) : Vec3::sZero();
    Camera3D camera{{0,500,500},vector(focus),{0,1,0},400,CAMERA_ORTHOGRAPHIC};
    CityCell anchor{}, hover{}; std::string status;
    std::vector<City> undo, redo;
    MenuBar menu(MenuMode::Editor);
    const auto choose_tool = [&](int index) { tool = Tool(index); painting = dragging = false; selected_building = selected_vehicle = -1; };
    const auto commit = [&](City next) {
        undo.push_back(city); if (undo.size()>32) undo.erase(undo.begin()); redo.clear();
        city = std::move(next); dirty = true; save_time = 0;
        ++revision;
        selected_building = selected_vehicle = -1;
    };
    const auto finish = [&](EditorResult result) {
        if (dirty && !save(city,directory,status,dirty)) return false;
        return result==EditorResult::Play ? city.spawn.has_value() : true;
    };
    int frames = 0; bool first_frame = true;
    while (true) {
        float dt = std::min(GetFrameTime(),.1f); save_time += dt; preview_time += dt;
        if (screenshot.empty() && WindowShouldClose() && !close_failed) {
            if (finish(EditorResult::Quit)) return EditorResult::Quit;
            close_failed = true;
        }
        if (dirty && !painting && save_time>1) { save(city,directory,status,dirty); save_time = -4; }
        MenuState menu_state;
        menu_state.undo = !undo.empty(); menu_state.redo = !redo.empty(); menu_state.play = city.spawn.has_value();
        menu_state.selection = selected_building>=0 || selected_vehicle>=0;
        menu_state.top = top; menu_state.grid = grid; menu_state.tool = int(tool);
        const auto command = first_frame ? MenuCommand::None : menu.update(menu_state);
        const bool can_edit = !first_frame && !menu.blocking() && !menu.interacted() && IsWindowFocused();
        const Rectangle document{float(sidebar+12),44,float(GetScreenWidth()-sidebar-24),float(GetScreenHeight()-108)};
        if (!can_edit) painting = dragging = false;
        if (command==MenuCommand::Controls || command==MenuCommand::About) { help_dialog(true,command==MenuCommand::About); continue; }
        if (command==MenuCommand::StartTime) {
            const auto minutes = start_clock(city.start_minutes);
            if (minutes && *minutes != city.start_minutes) { City next = city; next.start_minutes = *minutes; commit(std::move(next)); status = "Starting time updated"; }
            continue;
        }
        if ((command==MenuCommand::Cities || (can_edit && (IsKeyPressed(KEY_ESCAPE)||close_clicked(document)))) && finish(EditorResult::Back)) return EditorResult::Back;
        if (command==MenuCommand::Quit && finish(EditorResult::Quit)) return EditorResult::Quit;
        if (command==MenuCommand::PlayCity && finish(EditorResult::Play)) return EditorResult::Play;
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL);
        if (command==MenuCommand::SaveCity || (can_edit && ctrl && IsKeyPressed(KEY_S))) save(city,directory,status,dirty);
        if ((command==MenuCommand::Undo || (can_edit && ctrl && IsKeyPressed(KEY_Z))) && !undo.empty()) { redo.push_back(city); city = std::move(undo.back()); undo.pop_back(); dirty = true; save_time = 0; ++revision; painting = dragging = false; selected_building = selected_vehicle = -1; }
        if ((command==MenuCommand::Redo || (can_edit && ctrl && IsKeyPressed(KEY_Y))) && !redo.empty()) { undo.push_back(city); city = std::move(redo.back()); redo.pop_back(); dirty = true; save_time = 0; ++revision; painting = dragging = false; selected_building = selected_vehicle = -1; }
        if (command>=MenuCommand::SelectTool && command<=MenuCommand::ElevationTool) choose_tool(int(command)-int(MenuCommand::SelectTool));
        for (int i = 0; i<10; ++i) if (can_edit && !ctrl && IsKeyPressed(i==9 ? KEY_ZERO : KEY_ONE+i)) choose_tool(i);
        if (command==MenuCommand::TopView || (can_edit && IsKeyPressed(KEY_V))) top = !top;
        if (command==MenuCommand::Grid || (can_edit && IsKeyPressed(KEY_G))) grid = !grid;
        if (command==MenuCommand::RotateLeft || (can_edit && IsKeyPressed(KEY_Q))) yaw -= 1.57079633f;
        if (command==MenuCommand::RotateRight || (can_edit && IsKeyPressed(KEY_E))) yaw += 1.57079633f;
        if (command==MenuCommand::ZoomIn || command==MenuCommand::ZoomOut) camera.fovy = std::clamp(camera.fovy*(command==MenuCommand::ZoomIn ? .85f : 1/.85f),80.f,3400.f);
        const Vector2 mouse = GetMousePosition();
        const bool in_view = can_edit && mouse.x>sidebar+16 && mouse.x<GetScreenWidth()-16 && mouse.y>80 && mouse.y<GetScreenHeight()-68;
        if (in_view) {
            camera.fovy = std::clamp(camera.fovy*std::pow(.85f,GetMouseWheelMove()),80.f,3400.f);
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)||IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
                const auto delta = GetMouseDelta();
                const float scale = camera.fovy/GetScreenHeight();
                focus += (top ? Vec3(-delta.x,0,-delta.y) : Vec3(-std::cos(yaw)*delta.x-std::sin(yaw)*delta.y,0,std::sin(yaw)*delta.x-std::cos(yaw)*delta.y))*scale;
                focus.SetX(std::clamp(focus.GetX(),-City::extent,City::extent)); focus.SetZ(std::clamp(focus.GetZ(),-City::extent,City::extent));
            }
        }
        const Vec3 pan = top ? Vec3::sAxisX() : Vec3(std::cos(yaw),0,-std::sin(yaw));
        const Vec3 forward = top ? Vec3::sAxisZ() : Vec3(std::sin(yaw),0,std::cos(yaw));
        if (can_edit && !ctrl) focus += (pan*float(IsKeyDown(KEY_RIGHT)-IsKeyDown(KEY_LEFT))+forward*float(IsKeyDown(KEY_DOWN)-IsKeyDown(KEY_UP)))*dt*camera.fovy*.6f;
        camera.target = vector(focus); camera.position = vector(focus+(top ? Vec3(0,800,.01f) : Vec3(std::sin(yaw)*600,700,std::cos(yaw)*600)));
        camera.up = top ? Vector3{0,0,-1} : Vector3{0,1,0};
        const bool picked = in_view && pick(preview_environment.get(),camera,mouse,hover,brush_point);
        if (!in_view || IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) painting = false;
        if (!IsWindowFocused()) dragging = false;
        if (picked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            anchor = hover;
            if (tool==Tool::Trees) {
                painting = true; stroke_saved = brush_sampled = false; last_brush = brush_point;
                brush_remove = IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT);
            } else if (tool==Tool::Select) {
                selected_building = city.building_at(hover); selected_vehicle = city.vehicle_at(brush_point);
                if (selected_building>=0) { building_size = city.buildings[selected_building].size; building_height = city.buildings[selected_building].height; }
                if (selected_vehicle>=0) { vehicle_kind = int(city.vehicles[selected_vehicle].kind); rotation = city.vehicles[selected_vehicle].rotation; }
            } else if (tool==Tool::Spawn || tool==Tool::Vehicle || tool==Tool::Bulldoze) {
                City next = city;
                const bool okay = tool==Tool::Spawn ? next.set_spawn(hover,status) : tool==Tool::Vehicle ? next.add_vehicle({CityVehicleKind(vehicle_kind),brush_point,rotation},status) : next.erase(brush_point,status);
                if (okay) { commit(std::move(next)); status = "Change applied"; }
            } else dragging = true;
        }
        if (painting && picked && IsMouseButtonDown(MOUSE_BUTTON_LEFT)
            && (!brush_sampled || (brush_point-last_brush).LengthSq() >= brush_size*brush_size*4.f)) {
            City next = city; int changed = 0; std::string brush_error;
            const Vec3 delta = brush_point-last_brush;
            const int steps = std::max(1,int(std::ceil(delta.Length()/(brush_size*4.f))));
            for (int i = 1; i <= steps; ++i) changed += std::max(0,next.brush_trees(last_brush+delta*(float(i)/steps),brush_size*8.f,density,brush_remove,brush_error));
            last_brush = brush_point; brush_sampled = true;
            if (changed) {
                if (!stroke_saved) { commit(std::move(next)); stroke_saved = true; }
                else { city = std::move(next); dirty = true; save_time = 0; ++revision; }
                status = std::to_string(city.trees.size())+" trees / "+(brush_remove ? "Clearing trees" : "Planting trees");
            }
            if (!brush_error.empty()) status = brush_error;
        }
        CityBuilding preview{{anchor.x,anchor.z},building_size,building_height};
        if (dragging && tool==Tool::Building && hover!=anchor) {
            preview.size = std::clamp(std::max(std::abs(hover.x-anchor.x),std::abs(hover.z-anchor.z))+1,1,8);
            preview.cell = {anchor.x-(hover.x<anchor.x ? preview.size-1 : 0),anchor.z-(hover.z<anchor.z ? preview.size-1 : 0)};
        }
        const bool z_first = IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT);
        const int elevation_delta = z_first ? -elevation_direction : elevation_direction;
        const auto road_cells = dragging && tool==Tool::Road ? City::road_stroke(anchor,hover,z_first,diagonal_roads) : std::vector<CityCell>{};
        if (dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            dragging = false;
            if (picked) {
                City next = city;
                const bool okay = tool==Tool::Land ? next.add_land(anchor,hover,status) : tool==Tool::Road ? next.add_road(road_cells,status)
                    : tool==Tool::Ground ? next.paint_ground(anchor,hover,CityGround(ground_texture),status)
                    : tool==Tool::Elevation ? next.change_elevation(anchor,hover,elevation_delta,status) : next.add_building(preview,status);
                if (okay) { commit(std::move(next)); status = "Change applied"; }
            }
        }
        if ((command==MenuCommand::DeleteSelection || (can_edit && tool==Tool::Select && IsKeyPressed(KEY_DELETE))) && (selected_building>=0||selected_vehicle>=0)) {
            City next = city;
            if (selected_vehicle>=0) next.vehicles.erase(next.vehicles.begin()+selected_vehicle);
            else next.erase(city.buildings[selected_building].cell,status);
            commit(std::move(next));
        }
        // ponytail: reuse the gameplay renderer at most 4 times/sec while painting; incremental meshes can serve larger forests.
        if (preview_revision != revision && (!painting || preview_time >= .25f || !preview_renderer)) {
            preview_renderer.reset(); preview_environment = std::make_unique<Environment>(city);
            preview_renderer = std::make_unique<EnvironmentRenderer>(*preview_environment);
            preview_revision = revision; preview_time = 0;
        }
        BeginDrawing(); ClearBackground(background); rlSetClipPlanes(.2,30000); BeginMode3D(camera);
        preview_renderer->draw(camera,float(GetTime()),preview_day.lighting(),lighting,graphics);
        draw_city(city,grid);
        if (grid) {
            const int range = std::min(64,int(camera.fovy/City::block)+4);
            const auto c = City::cell(focus.GetX(),focus.GetZ());
            const int x0 = std::max(0,c.x-range), x1 = std::min(City::width,c.x+range), z0 = std::max(0,c.z-range), z1 = std::min(City::width,c.z+range);
            for (int x = x0; x<=x1; ++x) DrawLine3D({(x-64)*City::block,.025f,(z0-64)*City::block},{(x-64)*City::block,.025f,(z1-64)*City::block},{53,103,126,255});
            for (int z = z0; z<=z1; ++z) DrawLine3D({(x0-64)*City::block,.025f,(z-64)*City::block},{(x1-64)*City::block,.025f,(z-64)*City::block},{53,103,126,255});
        }
        if (picked) {
            if (tool==Tool::Trees) {
                for (int i = 0; i < 48; ++i) {
                    const float a = i*6.2831853f/48, b = (i+1)*6.2831853f/48, r = brush_size*8.f;
                    DrawLine3D(vector(brush_point+Vec3(std::cos(a)*r,.2f,std::sin(a)*r)),vector(brush_point+Vec3(std::cos(b)*r,.2f,std::sin(b)*r)),brush_remove ? ORANGE : accent);
                }
            } else if (dragging && tool==Tool::Road) {
                City proposed = city; std::string error;
                const bool valid = proposed.add_road(road_cells,error);
                const Color color = valid ? Color{255,255,85,160} : Color{255,70,70,160};
                if (valid) for (const auto& node : proposed.road_network()) {
                    if (std::none_of(road_cells.begin(),road_cells.end(),[&](CityCell p) {
                        return p.x>=node.first.x && p.x<=node.last.x && p.z>=node.first.z && p.z<=node.last.z;
                    })) continue;
                    auto inner = node.path(0,.7f), outer = node.path(1,-.7f);
                    if (!inner.empty()) {
                        for (auto& p : inner) p.SetY(proposed.height(p.GetX(),p.GetZ())+.2f);
                        for (auto& p : outer) p.SetY(proposed.height(p.GetX(),p.GetZ())+.2f);
                        for (std::size_t i = 1; i<inner.size(); ++i) {
                            DrawTriangle3D(vector(inner[i-1]),vector(outer[i-1]),vector(outer[i]),color);
                            DrawTriangle3D(vector(inner[i-1]),vector(outer[i]),vector(inner[i]),color);
                            DrawLine3D(vector(inner[i-1]),vector(inner[i]),accent); DrawLine3D(vector(outer[i-1]),vector(outer[i]),accent);
                        }
                    } else if (auto border = node.outline(); !border.empty()) {
                        for (auto& p : border) p.SetY(proposed.height(p.GetX(),p.GetZ())+.2f);
                        for (std::size_t i = 0; i<border.size(); ++i) DrawLine3D(vector(border[i]),vector(border[(i+1)%border.size()]),accent);
                    } else DrawCube(vector(node.center()+Vec3(0,.18f,0)),node.half_size().GetX()*2-1.4f,.15f,node.half_size().GetZ()*2-1.4f,color);
                }
                else for (auto p : road_cells) DrawCube(vector(City::center(p,City::level+.18f)),8.4f,.15f,8.4f,color);
            } else if (dragging && tool==Tool::Elevation) {
                if (elevation_revision!=revision || elevation_anchor!=anchor || elevation_hover!=hover || elevation_preview_delta!=elevation_delta) {
                    elevation_preview = std::make_unique<City>(city); std::string error;
                    elevation_valid = elevation_preview->change_elevation(anchor,hover,elevation_delta,error);
                    elevation_revision = revision; elevation_anchor = anchor; elevation_hover = hover; elevation_preview_delta = elevation_delta;
                }
                for (int z = 0; z<City::width; ++z) for (int x = 0; x<City::width; ++x) {
                    const CityCell p{x,z}; if (!city.land(p)) continue;
                    const bool selected = x>=std::min(anchor.x,hover.x) && x<=std::max(anchor.x,hover.x)
                        && z>=std::min(anchor.z,hover.z) && z<=std::max(anchor.z,hover.z);
                    const auto before = city.ground_patch(p), after = elevation_preview->ground_patch(p);
                    bool changed = false;
                    for (int i = 0; i<4; ++i) changed |= before[i].GetY()!=after[i].GetY();
                    if (selected || changed) tile_outline(*elevation_preview,p,elevation_valid ? accent : RED);
                }
            } else if (dragging && (tool==Tool::Land || tool==Tool::Ground)) {
                for (int z = std::min(anchor.z,hover.z); z<=std::max(anchor.z,hover.z); ++z)
                    for (int x = std::min(anchor.x,hover.x); x<=std::max(anchor.x,hover.x); ++x)
                        tile_outline(city,{x,z},accent);
            } else if (tool==Tool::Building) {
                if (!dragging) preview.cell = hover;
                const auto c = City::center(preview.cell,city.tile_height(preview.cell))+Vec3((preview.size-1)*City::half_block,preview.height/2.f,(preview.size-1)*City::half_block);
                DrawCubeWires(vector(c),preview.size*City::block,float(preview.height),preview.size*City::block,accent);
            } else if (tool==Tool::Vehicle) {
                const CityVehicle v{CityVehicleKind(vehicle_kind),brush_point,rotation};
                const Vec3 half = v.half_size();
                DrawCubeWires(vector(brush_point+Vec3(0,1,0)),half.GetX()*2,2,half.GetZ()*2,accent);
            } else tile_outline(city,hover,accent);
        }
        if (selected_building>=0) {
            auto b = city.buildings[selected_building]; const auto c = City::center(b.cell,city.tile_height(b.cell))+Vec3((b.size-1)*City::half_block,b.height/2.f,(b.size-1)*City::half_block);
            DrawCubeWires(vector(c),b.size*City::block+.1f,b.height+.1f,b.size*City::block+.1f,accent);
        }
        if (selected_vehicle>=0) {
            const auto& v = city.vehicles[selected_vehicle]; const Vec3 half = v.half_size();
            DrawCubeWires(vector(v.position+Vec3(0,1,0)),half.GetX()*2,2,half.GetZ()*2,accent);
        }
        EndMode3D();
        // Mask the scene outside its Macintosh-style document window.
        DrawRectangle(0,menu_height,GetScreenWidth(),12,background);
        DrawRectangle(0,44,sidebar+12,GetScreenHeight()-44,background);
        DrawRectangle(GetScreenWidth()-12,44,12,GetScreenHeight()-44,background);
        DrawRectangle(0,GetScreenHeight()-64,GetScreenWidth(),64,background);
        ui::draw_window({8,44,264,float(GetScreenHeight()-108)},"TOOLBOX");
        DrawRectangleLinesEx(document,2,border);
        DrawRectangleLinesEx({document.x+4,document.y+35,document.width-8,document.height-39},1,border);
        const auto title = fit(city.name,18,document.width-160)+(dirty ? " *" : "");
        ui::draw_window_title(document,title.c_str());
        ui::draw_window_close(document);
        text("BUILD TOOLS",20,86,17,accent);
        const auto control = [&](Rectangle r,const std::string& label,bool selected=false,bool enabled=true) { return button(r,label,selected,enabled,can_edit); };
        for (int i = 0; i<10; ++i) if (control({20.f,112.f+i*27,240,24},std::to_string((i+1)%10)+"  "+tools[i],tool==Tool(i))) choose_tool(i);
        DrawLine(20,390,260,390,border); text("INSPECTOR",20,400,17,accent);
        float y = 426;
        if (tool==Tool::Road) {
            if (control({20,y-5,240,32},diagonal_roads ? "Straight / diagonal" : "L-shaped")) diagonal_roads = !diagonal_roads;
            y += 40;
        }
        if (tool==Tool::Trees) {
            text("Brush "+std::to_string(brush_size*8)+" m",20,y,17);
            if (control({180,y-5,34,30},"-")) brush_size = std::max(1,brush_size-1);
            if (control({224,y-5,34,30},"+")) brush_size = std::min(12,brush_size+1);
            y += 38;
            if (control({20,y-5,240,32},std::string("Density: ")+densities[density-1])) density = density%3+1;
            y += 40;
        }
        if (tool==Tool::Ground) {
            if (control({20,y-5,240,32},std::string("Texture: ")+ground_names[ground_texture])) ground_texture = (ground_texture+1)%4;
            y += 40;
        }
        if (tool==Tool::Elevation) {
            if (control({20,y-5,240,32},elevation_direction>0 ? "Raise +2 m" : "Lower -2 m")) elevation_direction = -elevation_direction;
            y += 40; text("Shift: reverse",20,y,17);
        }
        const bool building_controls = tool==Tool::Building || selected_building>=0;
        const bool vehicle_controls = tool==Tool::Vehicle || selected_vehicle>=0;
        if (building_controls) {
            text("Square "+std::to_string(building_size)+" x "+std::to_string(building_size),20,y,17);
            if (control({180,y-5,34,30},"-")) building_size = std::max(1,building_size-1);
            if (control({224,y-5,34,30},"+")) building_size = std::min(8,building_size+1);
            y += 38; text("Height "+std::to_string(building_height)+" m",20,y,17);
            if (control({180,y-5,34,30},"-")) building_height = std::max(4,building_height-4);
            if (control({224,y-5,34,30},"+")) building_height = std::min(120,building_height+4);
            y += 38;
        }
        if (vehicle_controls) {
            if (control({20,y-5,240,32},vehicle_names[vehicle_kind])) vehicle_kind = (vehicle_kind+1)%4;
            y += 38;
            if (control({20,y-5,240,32},"Rotate: "+std::to_string(rotation*90)+" degrees")) rotation = (rotation+1)%4;
            y += 38;
        }
        if (tool==Tool::Select && (selected_building>=0||selected_vehicle>=0)) {
            if (control({20,y-5,240,32},"Apply to selection",true)) {
                City next = city; bool okay;
                if (selected_building>=0) okay = next.add_building({city.buildings[selected_building].cell,building_size,building_height},status,selected_building);
                else okay = next.add_vehicle({CityVehicleKind(vehicle_kind),city.vehicles[selected_vehicle].position,rotation},status,selected_vehicle);
                if (okay) commit(std::move(next));
            }
            y += 40;
        }
        DrawLine(8,GetScreenHeight()-54,GetScreenWidth()-8,GetScreenHeight()-54,border);
        const std::string clock = TextFormat("%02i:%02i",city.start_minutes/60,city.start_minutes%60);
        const std::string info = std::string(dirty ? "[UNSAVED] " : "[SAVED] ")+"Start "+clock+" | "+(city.spawn ? "Ready to play" : "Set a spawn to play");
        text(fit(status,17,GetScreenWidth()-ui::measure_text(info.c_str(),16)-48.f),20,float(GetScreenHeight()-44),17,accent);
        text(info,GetScreenWidth()-ui::measure_text(info.c_str(),16)-20.f,float(GetScreenHeight()-43),16);
        menu_state.top = top; menu_state.grid = grid; menu_state.tool = int(tool); menu_state.play = city.spawn.has_value();
        menu.draw(menu_state);
        text("CITY EDITOR",GetScreenWidth()-150.f,8,16,background);
        EndDrawing();
        first_frame = false;
        if (!screenshot.empty() && ++frames>=3) { auto image = LoadImageFromScreen(); ExportImage(image,screenshot.c_str()); UnloadImage(image); return EditorResult::Quit; }
    }
}
}
bool city_menu(City& selected,const std::filesystem::path& directory,ControllerMapping& controls,
               GraphicsSettings& graphics,bool edit_selected,const std::string& screenshot,bool preview_editor,MenuCommand initial_settings) {
    EnableCursor(); SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    bool open_editor = edit_selected || preview_editor;
    if (preview_editor && selected.id.empty()) selected = City::create("New city");
    std::vector<City> cities; std::string status;
    int selection = -1, scroll = 0, modal = 0; std::string name;
    MenuBar menu(MenuMode::Cities);
    GraphicsPanel graphics_panel;
    GraphicsSettings graphics_original = graphics;
    const auto mapping_path = std::filesystem::path(GetApplicationDirectory()) / "controller-mappings.ini";
    const auto graphics_path = std::filesystem::path(GetApplicationDirectory()) / "graphics-settings.ini";
    std::string settings_status;
    const auto pacing = [&]() {
        if (graphics.vsync) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT);
        SetTargetFPS(graphics.fps_limit);
    };
    if (initial_settings==MenuCommand::Graphics) graphics_panel.open(graphics);
    if (initial_settings==MenuCommand::Controllers) menu.show(MenuCommand::Controllers);
    const auto refresh = [&]() {
        cities.clear(); std::error_code ec; std::filesystem::create_directories(directory,ec);
        if (ec) { status = "Cannot open the cities folder"; return; }
        for (const auto& entry : std::filesystem::directory_iterator(directory,ec)) if (entry.path().extension()==".city") {
            City city; std::string error;
            if (City::load(entry.path(),city,error)) cities.push_back(std::move(city));
            else status = "Cannot load "+entry.path().filename().string()+": "+error;
        }
        std::sort(cities.begin(),cities.end(),[](const City& a,const City& b){return a.name<b.name;});
        selection = cities.empty() ? -1 : 0;
        for (std::size_t i = 0; i<cities.size(); ++i) if (cities[i].id==selected.id) selection = int(i);
    };
    refresh(); int frames = 0; bool accept_input = false;
    while (!WindowShouldClose() || !screenshot.empty()) {
        if (open_editor) {
            open_editor = false;
            auto result = editor(selected,directory,screenshot);
            if (result==EditorResult::Play) return true;
            if (result==EditorResult::Quit) return false;
            refresh(); accept_input = false;
        }
        const float x = std::max(30.f,(GetScreenWidth()-940)/2.f), y = 52;
        const float height = GetScreenHeight()-144.f;
        const int rows = std::max(1,int((height-118)/66));
        const bool has = selection>=0 && selection<int(cities.size());
        const int previous_modal = modal;
        MenuState menu_state; menu_state.selection = has; menu_state.play = has && cities[selection].spawn.has_value();
        const auto command = modal || !accept_input ? MenuCommand::None : menu.dialog_open()
            ? menu.update(controls,mapping_path,true,read_controllers()) : menu.update(menu_state);
        bool can_choose = accept_input && !modal && !graphics_panel.visible() && !menu.blocking() && !menu.interacted() && IsWindowFocused();
        if (graphics_panel.visible() && command!=MenuCommand::None && command!=MenuCommand::Graphics) {
            graphics = graphics_original; pacing(); graphics_panel.close();
        }
        if (command==MenuCommand::Controllers) menu.show(command);
        if (command==MenuCommand::Graphics && !graphics_panel.visible()) {
            graphics_original = graphics; graphics_panel.open(graphics); settings_status.clear();
        }
        if (graphics_panel.visible() && accept_input && !menu.blocking() && !menu.interacted()) {
            const auto action = graphics_panel.update();
            if (action==GraphicsPanelAction::Preview) {
                graphics = graphics_panel.pending(); pacing(); settings_status.clear();
            } else if (action==GraphicsPanelAction::Apply) {
                if (graphics_panel.pending().save(graphics_path,settings_status)) {
                    graphics = graphics_panel.pending(); pacing(); graphics_panel.close(); status = "Graphics settings saved";
                }
            } else if (action==GraphicsPanelAction::Cancel) {
                graphics = graphics_original; pacing(); graphics_panel.close();
            }
        }
        if (menu.blocking() || menu.interacted()) graphics_panel.cancel_drag();
        if (menu.blocking() || menu.interacted() || graphics_panel.visible()) can_choose = false;
        if (command==MenuCommand::Quit) return false;
        if (command==MenuCommand::Controls || command==MenuCommand::About) { help_dialog(false,command==MenuCommand::About); continue; }
        if (command==MenuCommand::NewCity) { modal = 1; name = "New city"; }
        if (command==MenuCommand::EditCity) { selected = cities[selection]; open_editor = true; continue; }
        if (command==MenuCommand::PlayCity) { selected = cities[selection]; return true; }
        if (command==MenuCommand::RenameCity) { modal = 2; name = cities[selection].name; }
        if (command==MenuCommand::DeleteCity) modal = 3;
        if (can_choose && !open_editor && !cities.empty()) {
            if (IsKeyPressed(KEY_DOWN)) selection = std::min(int(cities.size())-1,selection+1);
            if (IsKeyPressed(KEY_UP)) selection = std::max(0,selection-1);
            if (IsKeyPressed(KEY_UP)||IsKeyPressed(KEY_DOWN)) scroll = std::clamp(selection-rows+1,0,std::max(0,int(cities.size())-rows));
            if (IsKeyPressed(KEY_E)) { selected = cities[selection]; open_editor = true; continue; }
            if (IsKeyPressed(KEY_ENTER) && cities[selection].spawn) { selected = cities[selection]; return true; }
            if (IsKeyPressed(KEY_F2)) { modal = 2; name = cities[selection].name; }
            if (IsKeyPressed(KEY_DELETE)) modal = 3;
        }
        if (modal) can_choose = false;
        scroll = std::clamp(scroll,0,std::max(0,int(cities.size())-rows));
        if (can_choose && hit({x,y+98,600,height-118})) scroll = std::clamp(scroll-int(GetMouseWheelMove()),0,std::max(0,int(cities.size())-rows));
        BeginDrawing(); ui::draw_desktop();
        ui::draw_window({x,y,940,height},"City directory");
        ui::draw_window_close({x,y,940,height});
        if (close_clicked({x,y,940,height},can_choose)) { EndDrawing(); return false; }
        text("Ambaretto / CITIES",x+20,y+48,20,accent);
        DrawRectangleRec({x,y+108,600,height-118},panel);
        if (cities.empty()) text("No cities",x+24,y+138,22);
        for (int row = 0; row<rows && row+scroll<int(cities.size()); ++row) {
            const int i = row+scroll; const auto& c = cities[i];
            Rectangle r{x+8,y+116+row*66,584,60};
            DrawRectangleRec(r,selection==i ? accent : panel);
            DrawRectangleLinesEx(r,1,border);
            text(fit(c.name,20,r.width-32),r.x+16,r.y+9,20,selection==i ? background : ink);
            text(std::to_string(c.land_count())+" land / "+std::to_string(c.buildings.size())+" buildings / "+std::to_string(c.trees.size())+" trees / starts "+TextFormat("%02i:%02i",c.start_minutes/60,c.start_minutes%60),r.x+16,r.y+35,14,selection==i ? background : muted);
            if (can_choose && hit(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) selection = i;
        }
        float bx = x+632, by = y+108;
        const float spacing = std::clamp((GetScreenHeight()-160)/560.f,.8f,1.f);
        // Modal drawing happens last; only its own controls can receive input.
        if (modal==0) {
            if (button({bx,by,280,44},"+ New city",true,true,can_choose)||(can_choose && IsKeyPressed(KEY_INSERT))) { modal = 1; name = "New city"; }
            if (button({bx,by+64*spacing,280,44},"Edit / build",false,has,can_choose)) { selected = cities[selection]; open_editor = true; }
            if (button({bx,by+124*spacing,280,44},"Play city",true,has && cities[selection].spawn.has_value(),can_choose)) { selected = cities[selection]; EndDrawing(); return true; }
            if (button({bx,by+194*spacing,280,40},"Rename",false,has,can_choose)) { modal = 2; name = cities[selection].name; }
            if (button({bx,by+248*spacing,280,40},"Delete city",false,has,can_choose)) modal = 3;
            if (button({bx,by+318*spacing,280,40},"Quit",false,true,can_choose)) { EndDrawing(); return false; }
        } else {
            button({bx,by,280,44},"+ New city",true,false);
            button({bx,by+64*spacing,280,44},"Edit / build",false,false);
            button({bx,by+124*spacing,280,44},"Play city",true,false);
            button({bx,by+194*spacing,280,40},"Rename",false,false);
            button({bx,by+248*spacing,280,40},"Delete city",false,false);
        }
        text(status,x,float(GetScreenHeight()-42),16,accent);
        if (modal) {
            const bool modal_input = modal == previous_modal;
            ui::draw_desktop();
            Rectangle r{(GetScreenWidth()-620)/2.f,(GetScreenHeight()-236)/2.f,620,236};
            ui::draw_window(r,modal==1 ? "Create a city" : modal==2 ? "Rename city" : "Delete this city?");
            ui::draw_window_close(r);
            if (modal!=3) input(name,{r.x+24,r.y+72,572,52});
            else { text(fit(cities[selection].name,22,r.width-48),r.x+24,r.y+76,22,accent); text("This permanently removes its saved map.",r.x+24,r.y+111,17,muted); }
            text(fit(status,14,r.width-48),r.x+24,r.y+136,14,accent);
            const bool accept = button({r.x+24,r.y+162,278,44},modal==3 ? "Delete" : "Save",true,true,modal_input)||(modal_input && IsKeyPressed(KEY_ENTER));
            if (close_clicked(r,modal_input) || button({r.x+318,r.y+162,278,44},"Cancel",false,true,modal_input)||(modal_input && IsKeyPressed(KEY_ESCAPE))) modal = 0;
            else if (accept) {
                if (modal==3) {
                    std::error_code ec; const bool removed = std::filesystem::remove(directory/(cities[selection].id+".city"),ec);
                    if (removed && !ec) { status = "City deleted"; modal = 0; refresh(); } else status = "Could not delete the city";
                } else {
                    City next = modal==1 ? City::create(name) : cities[selection]; next.name = name;
                    if (next.save(directory,status)) {
                        selected = next; open_editor = modal==1; modal = 0; refresh(); status = "City saved";
                    }
                }
            }
        }
        if (graphics_panel.visible()) graphics_panel.draw(float(GetFPS()),settings_status);
        if (menu.dialog_open()) menu.draw(controls,mapping_path); else menu.draw(menu_state);
        text("Ambaretto",GetScreenWidth()-ui::measure_text("Ambaretto",16)-20.f,8,16,background);
        EndDrawing();
        accept_input = true;
        if (!screenshot.empty() && ++frames>=3) { auto image = LoadImageFromScreen(); ExportImage(image,screenshot.c_str()); UnloadImage(image); return false; }
    }
    if (graphics_panel.visible()) { graphics = graphics_original; pacing(); }
    if (!menu.save_pending(controls,mapping_path)) TraceLog(LOG_ERROR,"Could not save controller mappings; previous file retained");
    return false;
}
} // namespace ambaretto
