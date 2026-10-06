#include "city.hpp"
#include "ui_font.hpp"
#include "menu_bar.hpp"
#include "graphics_panel.hpp"
#include "plane.hpp"
#include "car_renderer.hpp"
#include "character_renderer.hpp"
#include "tuning_panel.hpp"
#include "environment_renderer.hpp"
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace ambaretto {
namespace {
constexpr Color background = ui::dos_blue, panel = ui::dos_blue, border = ui::dos_white;
constexpr Color ink = ui::dos_white, muted = ui::dos_white, accent = ui::dos_yellow;
constexpr int sidebar = 280;
enum class Tool { Select, Land, Road, Building, Spawn, Vehicle, Bulldoze, Trees, Ground, Elevation };
const char* tools[] = {"Select / edit", "Island / expand", "Road", "Building", "Player spawn", "Vehicle", "Bulldoze", "Tree brush", "Ground texture", "Elevation"};
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
bool button(Rectangle r, const std::string& label, bool selected = false, bool enabled = true, bool interactive = true, int size = 18) {
    const bool hover = interactive && enabled && hit(r);
    DrawRectangleRec({r.x+2,r.y+2,r.width,r.height},border);
    DrawRectangleRec(r,enabled && (selected || hover) ? accent : panel);
    DrawRectangleLinesEx(r,1,border);
    text(fit(label,size,r.width-20),r.x+10,r.y+(r.height-size)/2,size,enabled ? (selected || hover ? background : ink) : ui::dos_light_blue);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
void input(std::string& value, Rectangle r) {
    if ((IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_A)) value.clear();
    DrawRectangleRec(r,background); DrawRectangleLinesEx(r,2,accent);
    std::string visible = value+(int(GetTime()*2)%2 ? "_" : "");
    while (!visible.empty() && ui::measure_text(visible.c_str(),21)>r.width-24) visible.erase(visible.begin());
    text(visible,r.x+12,r.y+(r.height-21)/2,21);
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
                editing ? "V: top/dimetric view   G: grid   Delete: remove selection" : "Choose File or City in the menu bar for all commands.",
                editing ? "Shift: flip road bends / clear trees with the brush" : "Play needs a spawn and a saved Player character.",
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
std::optional<std::string> ground_texture_picker(const std::string& current) {
    std::string status; auto textures = building_textures(building_texture_directory(),status);
    std::map<std::string,Texture2D> images;
    int selected = 0, scroll = 0; bool interactive = false;
    std::optional<std::string> result;
    const auto unload = [&] { for (const auto& image : images) if (image.second.id) UnloadTexture(image.second); images.clear(); };
    const auto select_current = [&] {
        const auto found = std::find(textures.begin(),textures.end(),current);
        selected = found==textures.end() ? 0 : int(found-textures.begin()); scroll = selected;
    };
    select_current();
    while (!WindowShouldClose()) {
        const float height = std::min(660.f,float(GetScreenHeight()-64));
        const Rectangle r{(GetScreenWidth()-600)/2.f,(GetScreenHeight()-height)/2,600,height};
        const int rows = std::max(1,int((height-190)/42));
        if (interactive && !textures.empty()) {
            if (IsKeyPressed(KEY_UP)) selected = std::max(0,selected-1);
            if (IsKeyPressed(KEY_DOWN)) selected = std::min(int(textures.size())-1,selected+1);
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_DOWN)) scroll = std::clamp(scroll,selected-rows+1,selected);
            if (hit({r.x+20,r.y+104,560,float(rows*42)})) scroll -= int(GetMouseWheelMove()*3);
        }
        scroll = std::clamp(scroll,0,std::max(0,int(textures.size())-rows));
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(r,"Ground texture"); ui::draw_window_close(r);
        text("assets/textures / choose an image",r.x+20,r.y+63,16);
        if (button({r.x+404,r.y+54,176,32},"Refresh folder",false,true,interactive,16)) {
            unload(); textures = building_textures(building_texture_directory(),status); select_current();
        }
        for (int row = 0; row<rows && scroll+row<int(textures.size()); ++row) {
            const int index = scroll+row; const auto& name = textures[index];
            if (!images.count(name)) images[name] = load_building_texture(name);
            const auto thumb = images[name]; const Rectangle item{r.x+20,r.y+104+row*42,560,38};
            if (button(item,"",selected==index,thumb.id!=0,interactive)) selected = index;
            if (thumb.id) DrawTexturePro(thumb,{0,0,float(thumb.width),float(thumb.height)},{item.x+4,item.y+3,32,32},{0,0},0,WHITE);
            text(fit(name,17,500),item.x+44,item.y+10,17,selected==index || hit(item) ? background : ink);
        }
        if (textures.empty()) text("Add image files to assets/textures, then refresh.",r.x+20,r.y+120,16);
        text(fit(status,15,560),r.x+20,r.y+height-78,15,accent);
        const bool usable = !textures.empty() && images.count(textures[selected]) && images.at(textures[selected]).id;
        const bool accept = button({r.x+20,r.y+height-54,272,34},"Use texture",true,usable,interactive)
            || (interactive && usable && IsKeyPressed(KEY_ENTER));
        const bool cancel = close_clicked(r,interactive) || button({r.x+308,r.y+height-54,272,34},"Cancel",false,true,interactive)
            || (interactive && IsKeyPressed(KEY_ESCAPE));
        EndDrawing(); interactive = true;
        if (accept) { result = textures[selected]; break; }
        if (cancel) break;
    }
    unload(); return result;
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
void draw_city(const City& city, bool grid,const CarRenderer& cars,const Camera3D& camera) {
    for (int z = 0; z < City::width; ++z) for (int x = 0; x < City::width; ++x) {
        const CityCell p{x,z}; if (city.tile(p)==CityTile::Water) continue;
        if (grid) tile_outline(city,p,{97,135,106,255});
    }
    for (auto v : city.vehicles) {
        if (v.kind==CityVehicleKind::Car && v.car) { cars.draw_design(*v.car,v.position,v.rotation*PI/2,camera); continue; }
        const auto p = vector(v.position);
        rlPushMatrix(); rlTranslatef(p.x,p.y,p.z); rlRotatef(float(v.rotation*90),0,1,0);
        if (v.kind==CityVehicleKind::Car) {
            DrawCubeWires({0,.9f,0},1.85f,1.2f,3.7f,ORANGE);
        } else {
            const auto& spec = plane_specs(PlaneType(int(v.kind)-1));
            DrawCube({0,spec.body_radius+1,0},spec.body_radius*2,spec.body_radius*2,spec.length,ink);
            DrawCube({0,spec.body_radius+1,0},spec.span,.25f,spec.length*.15f,ink);
        }
        rlPopMatrix();
    }
    if (city.spawn) {
        tile_outline(city,*city.spawn,accent);
    }
}
void building_outline(const CityBuilding& b,float ground,Color color) {
    const auto shape = b.shape(); const auto p = b.points(ground);
    for (const auto& edge : shape.edges()) DrawLine3D(vector(p[edge[0]]),vector(p[edge[1]]),color);
}
int pick_building(const City& city,Camera3D camera,Vector2 mouse) {
    const Ray ray = GetScreenToWorldRay(mouse,camera); float distance = 1e9f; int selected = -1;
    for (std::size_t i = 0; i<city.buildings.size(); ++i) {
        const auto& b = city.buildings[i];
        for (const auto& t : b.triangles(city.tile_height(b.cell))) {
            if (t.decal) continue;
            const auto hit = GetRayCollisionTriangle(ray,vector(t.points[0]),vector(t.points[1]),vector(t.points[2]));
            if (hit.hit && hit.distance<distance) { distance = hit.distance; selected = int(i); }
        }
    }
    return selected;
}
Vec3 mesh_axis(int axis) { return axis==0 ? Vec3::sAxisX() : axis==1 ? Vec3::sAxisY() : Vec3::sAxisZ(); }
float mesh_coordinate(Vec3 p,int axis) { return axis==0 ? p.GetX() : axis==1 ? p.GetY() : p.GetZ(); }
Vector2 project_mesh(Vec3 p,Camera3D camera,Rectangle view) {
    return Vector2Add(GetWorldToScreenEx(vector(p),camera,int(view.width),int(view.height)),{view.x,view.y});
}
float segment_distance(Vector2 p,Vector2 a,Vector2 b) {
    const auto d = Vector2Subtract(b,a); const float length = Vector2LengthSqr(d);
    const float t = length>.0001f ? std::clamp(Vector2DotProduct(Vector2Subtract(p,a),d)/length,0.f,1.f) : 0;
    return Vector2Distance(p,Vector2Add(a,Vector2Scale(d,t)));
}
// Handles stay the same size in pixels in the orthographic editor.
int mesh_gizmo(Vec3 pivot,Camera3D camera,Rectangle view,Vector2 mouse,bool draw,int active = -1) {
    const Color colors[] = {RED,GREEN,SKYBLUE}; const int planes[3][2] = {{0,1},{0,2},{1,2}};
    const float length = camera.fovy/view.height*82;
    const auto origin = project_mesh(pivot,camera,view); int picked = -1;
    for (int i = 0; i<3; ++i) {
        const auto a = mesh_axis(planes[i][0])*length, b = mesh_axis(planes[i][1])*length;
        const Vector2 p[] = {project_mesh(pivot+(a+b)*.18f,camera,view),project_mesh(pivot+a*.38f+b*.18f,camera,view),
            project_mesh(pivot+(a+b)*.38f,camera,view),project_mesh(pivot+a*.18f+b*.38f,camera,view)};
        const float area = std::abs((p[1].x-p[0].x)*(p[3].y-p[0].y)-(p[1].y-p[0].y)*(p[3].x-p[0].x));
        if (area<36) continue;
        if (CheckCollisionPointTriangle(mouse,p[0],p[1],p[2]) || CheckCollisionPointTriangle(mouse,p[0],p[2],p[3])) picked = i+3;
        if (draw) {
            const Color color = active==i+3 ? accent : ColorAlpha(colors[planes[i][0]],.45f);
            // raylib's 2D triangles use counterclockwise screen winding.
            for (auto t : {std::array<int,3>{0,1,2},std::array<int,3>{0,2,3}}) {
                if ((p[t[1]].x-p[t[0]].x)*(p[t[2]].y-p[t[0]].y)-(p[t[1]].y-p[t[0]].y)*(p[t[2]].x-p[t[0]].x)>0) std::swap(t[1],t[2]);
                DrawTriangle(p[t[0]],p[t[1]],p[t[2]],color);
            }
            for (int j = 0; j<4; ++j) DrawLineEx(p[j],p[(j+1)%4],1,colors[planes[i][0]]);
        }
    }
    for (int i = 0; i<3; ++i) {
        const auto tip = project_mesh(pivot+mesh_axis(i)*length,camera,view);
        if (Vector2Distance(origin,tip)<15) continue;
        const auto direction = Vector2Normalize(Vector2Subtract(tip,origin)); const Vector2 side{-direction.y,direction.x};
        if (segment_distance(mouse,Vector2Add(origin,Vector2Scale(direction,12)),tip)<8) picked = i;
        if (draw) {
            const Color color = active==i ? accent : colors[i];
            DrawLineEx(origin,tip,3,color);
            const auto base = Vector2Subtract(tip,Vector2Scale(direction,12));
            DrawTriangle(tip,Vector2Subtract(base,Vector2Scale(side,6)),Vector2Add(base,Vector2Scale(side,6)),color);
            text(std::string(1,"XYZ"[i]),tip.x+6,tip.y-8,15,color);
        }
    }
    if (draw) DrawCircleV(origin,4,WHITE);
    return picked;
}
Vec3 mesh_drag_delta(Vector2 mouse,Camera3D camera,Rectangle view,int handle) {
    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target,camera.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward,camera.up)), up = Vector3CrossProduct(right,forward);
    const float scale = camera.fovy/view.height;
    const auto projected = [&](int axis) { const auto v = vector(mesh_axis(axis)); return Vector2{Vector3DotProduct(v,right),-Vector3DotProduct(v,up)}; };
    if (handle<3) {
        const auto axis = projected(handle); const float length = Vector2LengthSqr(axis);
        return mesh_axis(handle)*(length>.001f ? Vector2DotProduct(mouse,axis)*scale/length : -mouse.y*scale);
    }
    if (handle<6) {
        const int planes[3][2] = {{0,1},{0,2},{1,2}}; const int a = planes[handle-3][0], b = planes[handle-3][1];
        const auto u = projected(a), v = projected(b); const float det = u.x*v.y-u.y*v.x;
        if (std::abs(det)<.01f) return Vec3::sZero();
        return (mesh_axis(a)*((mouse.x*v.y-mouse.y*v.x)/det)+mesh_axis(b)*((u.x*mouse.y-u.y*mouse.x)/det))*scale;
    }
    const auto d = Vector3Scale(Vector3Subtract(Vector3Scale(right,mouse.x),Vector3Scale(up,mouse.y)),scale); return Vec3(d.x,d.y,d.z);
}
bool mesh_slider(Rectangle r,float& value,float lo,float hi,int id,int& dragging,bool interactive) {
    if (interactive && hit(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) dragging = id;
    const float old = value;
    if (interactive && dragging==id && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) value = lo+(hi-lo)*std::clamp((GetMouseX()-r.x)/r.width,0.f,1.f);
    DrawRectangleRec(r,background); DrawRectangleLinesEx(r,1,border);
    const float t = hi>lo ? (value-lo)/(hi-lo) : 0;
    DrawRectangleRec({r.x,r.y,r.width*t,r.height},ColorAlpha(accent,.45f)); DrawRectangleRec({r.x+r.width*t-3,r.y-2,6,r.height+4},accent);
    return old!=value;
}
}

std::optional<BuildingMesh> building_builder(BuildingMesh mesh,const std::filesystem::path& directory,const std::string& screenshot,int initial_tab,std::vector<BuildingMesh>* saved_designs) {
    int tab = initial_tab==4 ? 3 : initial_tab==2 ? 1 : initial_tab==3 ? 2 : 0, mode = 0, decal = 0;
    int field = -1, scroll = 0, library_selection = -1, frames = 0, slider_drag = -1, handle = -1;
    std::array<std::vector<int>,3> selected{{{2},{},{4}}}; std::vector<int> moving;
    std::vector<BuildingMesh> undo, redo; std::optional<BuildingMesh> grab;
    Vector2 grab_start{}; bool keyboard_grab = false, slice = false, xray = false, interactive = false, decal_texture = false;
    float yaw = 2.44f, pitch = .5f, zoom = 1, cut_fraction = .5f; int cut_axis = 1;
    std::string status, buffer;
    const auto align = [&](BuildingMesh& value) { const auto tiles = value.footprint(); return value.set_footprint(tiles[0],tiles[1],status,true); };
    if (!align(mesh)) return {};
    auto textures = building_textures(building_texture_directory(),status);
    auto library = saved_buildings(directory,status);
    std::map<std::string,Texture2D> images; RenderTexture2D preview{}; MenuBar menu(MenuMode::Creator);
    const Shader shader = LoadShaderFromMemory(nullptr,R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main() {
    vec4 pixel = texture(texture0,fragTexCoord)*fragColor*colDiffuse;
    if (pixel.a<0.3) discard;
    finalColor = pixel;
})GLSL");
    std::optional<BuildingMesh> result;
    const auto image = [&](const std::string& name) {
        if (name.empty()) return Texture2D{};
        if (!images.count(name)) images[name] = load_building_texture(name);
        return images[name];
    };
    const auto commit = [&](BuildingMesh next) {
        if (!next.validate(status)) return false;
        undo.push_back(mesh); if (undo.size()>32) undo.erase(undo.begin()); redo.clear();
        mesh = std::move(next); field = -1; status = "Updated"; return true;
    };
    while (!WindowShouldClose()) {
        const float height = float(GetScreenHeight());
        const Rectangle window{8,44,float(GetScreenWidth()-16),height-108}, view{414,84,float(GetScreenWidth()-438),height-244};
        MenuState menu_state; menu_state.undo = !undo.empty() && !grab; menu_state.redo = !redo.empty() && !grab;
        const auto command = interactive && !grab ? menu.update(menu_state) : MenuCommand::None;
        if (command!=MenuCommand::None) field = -1;
        const bool can_edit = interactive && !menu.blocking() && !menu.interacted();
        const bool click = can_edit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT), in_view = can_edit && hit(view);
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL), shift = IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT);
        if (can_edit && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) slider_drag = -1;
        if (command==MenuCommand::CloseCreator || (can_edit && close_clicked(window))) break;
        if (can_edit && IsKeyPressed(KEY_ESCAPE) && !grab) { if (slice) slice = false; else if (field>=0) field = -1; else break; }
        if (command==MenuCommand::SavedBuildings) { tab = 3; slice = false; field = -1; }
        if (command==MenuCommand::NewBuilding) { commit(BuildingMesh{}); selected = {{{2},{},{4}}}; tab = 0; slice = false; }
        if ((can_edit || command==MenuCommand::Undo || command==MenuCommand::Redo) && !grab && field<0) {
            if ((command==MenuCommand::Undo || (ctrl && IsKeyPressed(KEY_Z))) && !undo.empty()) { redo.push_back(mesh); mesh = std::move(undo.back()); undo.pop_back(); selected = {}; slice = false; status = "Undone"; }
            if ((command==MenuCommand::Redo || (ctrl && IsKeyPressed(KEY_Y))) && !redo.empty()) { undo.push_back(mesh); mesh = std::move(redo.back()); redo.pop_back(); selected = {}; slice = false; status = "Redone"; }
            if (tab==0 && !ctrl) {
                if (IsKeyPressed(KEY_ONE)) mode = 0;
                if (IsKeyPressed(KEY_TWO)) mode = 1;
                if (IsKeyPressed(KEY_THREE)) mode = 2;
                if (IsKeyPressed(KEY_Z) && (IsKeyDown(KEY_LEFT_ALT)||IsKeyDown(KEY_RIGHT_ALT))) xray = !xray;
                if (IsKeyPressed(KEY_K)) { slice = !slice; cut_fraction = .5f; }
            }
        }
        auto edges = mesh.edges();
        for (int m = 0; m<3; ++m) {
            const int count = m==0 ? int(mesh.vertices.size()) : m==1 ? int(edges.size()) : int(mesh.faces.size());
            auto& list = selected[m]; list.erase(std::remove_if(list.begin(),list.end(),[&](int i){return i<0 || i>=count;}),list.end());
        }
        decal = mesh.decals.empty() ? -1 : std::clamp(decal,0,int(mesh.decals.size())-1);
        const auto targets = [&]() {
            if (mode==0) return selected[0];
            std::set<int> vertices;
            for (int i : selected[mode]) {
                if (mode==1) { vertices.insert(edges[i][0]); vertices.insert(edges[i][1]); }
                else for (int v : mesh.faces[i].vertices) vertices.insert(v);
            }
            return std::vector<int>(vertices.begin(),vertices.end());
        };
        if (can_edit && tab==0 && !grab && !slice && field<0 && !ctrl && IsKeyPressed(KEY_A)) {
            const int count = mode==0 ? int(mesh.vertices.size()) : mode==1 ? int(edges.size()) : int(mesh.faces.size());
            selected[mode].clear(); for (int i = 0; i<count; ++i) selected[mode].push_back(i);
        }
        if (in_view && !grab) {
            zoom = std::clamp(zoom*std::pow(.85f,GetMouseWheelMove()),.2f,5.f);
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) { yaw -= GetMouseDelta().x*.01f; pitch = std::clamp(pitch+GetMouseDelta().y*.01f,-1.45f,1.45f); }
        }
        if (preview.texture.width!=int(view.width) || preview.texture.height!=int(view.height)) {
            if (preview.id) UnloadRenderTexture(preview);
            preview = LoadRenderTexture(int(view.width),int(view.height));
        }
        const auto frame_size = grab ? grab->size : mesh.size;
        const float radius = frame_size.Length(); const Vector3 target{0,frame_size.GetY()/2,0};
        Camera3D camera{{std::sin(yaw)*std::cos(pitch)*radius*2,target.y+std::sin(pitch)*radius*2,std::cos(yaw)*std::cos(pitch)*radius*2},
            target,{0,1,0},radius*1.45f*zoom,CAMERA_ORTHOGRAPHIC};
        auto vertices = targets(); auto points = mesh.points(); Vec3 pivot = Vec3::sZero(); for (int v : vertices) pivot += points[v]/float(vertices.size());
        if (grab) {
            if (keyboard_grab) {
                if (IsKeyPressed(KEY_X)) handle = 0;
                if (IsKeyPressed(KEY_Y)) handle = 1;
                if (IsKeyPressed(KEY_Z)) handle = 2;
            }
            BuildingMesh next = *grab;
            if (next.move_vertices(moving,mesh_drag_delta(Vector2Subtract(GetMousePosition(),grab_start),camera,view,handle),status)) mesh = std::move(next);
            if (IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { mesh = *grab; grab.reset(); status = "Move cancelled"; }
            else if ((keyboard_grab && (click || IsKeyPressed(KEY_ENTER))) || (!keyboard_grab && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))) {
                BuildingMesh moved = mesh; mesh = *grab; grab.reset(); commit(std::move(moved));
            }
        } else if (can_edit && tab==0 && !slice && field<0 && !ctrl && !vertices.empty() && IsKeyPressed(KEY_G)) {
            grab = mesh; moving = vertices; grab_start = GetMousePosition(); handle = 6; keyboard_grab = true;
        } else if (in_view && click && !slice && (tab==0 || tab==2)) {
            const int gizmo = tab==0 && !shift && !vertices.empty() ? mesh_gizmo(pivot,camera,view,GetMousePosition(),false) : -1;
            if (gizmo>=0) { grab = mesh; moving = vertices; grab_start = GetMousePosition(); handle = gizmo; keyboard_grab = false; }
            else {
                const Vector2 mouse{GetMouseX()-view.x,GetMouseY()-view.y};
                const Ray ray = GetScreenToWorldRayEx(mouse,camera,int(view.width),int(view.height)); float closest = 1e9f; int face = -1, picked = -1;
                for (const auto& t : mesh.triangles()) if (!t.decal) {
                    const auto hit = GetRayCollisionTriangle(ray,vector(t.points[0]),vector(t.points[1]),vector(t.points[2]));
                    if (hit.hit && hit.distance<closest) { closest = hit.distance; face = t.face; }
                }
                const auto visible = [&](Vec3 p) { return xray || Vector3Distance(ray.position,vector(p))<=closest+radius*.02f; };
                if (tab==2 || mode==2) picked = face;
                else {
                    float best = 12;
                    const int count = mode==0 ? int(points.size()) : int(edges.size());
                    for (int i = 0; i<count; ++i) {
                        float distance; Vec3 point;
                        if (mode==0) { point = points[i]; distance = Vector2Distance(GetMousePosition(),project_mesh(point,camera,view)); }
                        else {
                            const Vec3 a = points[edges[i][0]], b = points[edges[i][1]];
                            const auto sa = project_mesh(a,camera,view), sb = project_mesh(b,camera,view), d = Vector2Subtract(sb,sa);
                            const float length = Vector2LengthSqr(d), t = length>.0001f ? std::clamp(Vector2DotProduct(Vector2Subtract(GetMousePosition(),sa),d)/length,0.f,1.f) : 0;
                            point = a+(b-a)*t; distance = segment_distance(GetMousePosition(),sa,sb);
                        }
                        if (distance<best && visible(point)) { best = distance; picked = i; }
                    }
                }
                if (tab==2 && picked>=0 && decal>=0) { auto next = mesh; next.decals[decal].face = picked; commit(std::move(next)); }
                else {
                    auto& list = selected[mode];
                    if (!shift) list.clear();
                    if (picked>=0) { const auto it = std::find(list.begin(),list.end(),picked); if (it==list.end()) list.push_back(picked); else list.erase(it); }
                }
                field = -1;
            }
        }
        points = mesh.points(); vertices = targets(); pivot = Vec3::sZero(); for (int v : vertices) pivot += points[v]/float(vertices.size());
        const auto triangles = mesh.triangles(); const auto tiles = mesh.footprint();
        const float cut_extent = mesh_coordinate(mesh.size,cut_axis), cut_position = cut_axis==1 ? cut_fraction*cut_extent : (cut_fraction-.5f)*cut_extent;
        BeginTextureMode(preview); ClearBackground({18,28,45,255}); BeginMode3D(camera); BeginShaderMode(shader);
        const auto ground = image("soil.png"); const float hx = mesh.size.GetX()/2, hz = mesh.size.GetZ()/2;
        rlSetTexture(ground.id); rlBegin(RL_QUADS); rlColor4ub(125,145,160,255); rlNormal3f(0,1,0);
        rlTexCoord2f(0,0); rlVertex3f(-hx,-.04f,-hz); rlTexCoord2f(0,float(tiles[1])); rlVertex3f(-hx,-.04f,hz);
        rlTexCoord2f(float(tiles[0]),float(tiles[1])); rlVertex3f(hx,-.04f,hz); rlTexCoord2f(float(tiles[0]),0); rlVertex3f(hx,-.04f,-hz); rlEnd(); rlSetTexture(0);
        for (const auto& t : triangles) {
            const auto texture = image(t.texture); if (t.decal && !texture.id) continue;
            const Vec3 normal = (t.points[1]-t.points[0]).Cross(t.points[2]-t.points[0]).NormalizedOr(Vec3::sAxisY());
            const float shade = .6f+.4f*std::max(0.f,normal.Dot(Vec3(.4f,.8f,.3f).Normalized()));
            const Color color = ColorBrightness(texture.id ? WHITE : Color{197,204,211,255},shade-1);
            rlSetTexture(texture.id); rlBegin(RL_TRIANGLES); rlColor4ub(color.r,color.g,color.b,255); rlNormal3f(normal.GetX(),normal.GetY(),normal.GetZ());
            for (int i = 0; i<3; ++i) { rlTexCoord2f(t.uv[i][0],t.uv[i][1]); rlVertex3f(t.points[i].GetX(),t.points[i].GetY(),t.points[i].GetZ()); }
            rlEnd(); rlSetTexture(0);
        }
        EndShaderMode();
        rlDisableDepthTest(); rlDisableDepthMask();
        for (int x = 0; x<=tiles[0]; ++x) DrawLine3D({-hx+x*BuildingMesh::tile_size,.015f,-hz},{-hx+x*BuildingMesh::tile_size,.015f,hz},accent);
        for (int z = 0; z<=tiles[1]; ++z) DrawLine3D({-hx,.015f,-hz+z*BuildingMesh::tile_size},{hx,.015f,-hz+z*BuildingMesh::tile_size},accent);
        rlEnableDepthMask(); rlEnableDepthTest();
        if (tab==0 || tab==2) {
            if (mode==2 || tab==2) for (const auto& t : triangles) if (!t.decal && (tab==2 ? decal>=0 && t.face==mesh.decals[decal].face : std::find(selected[2].begin(),selected[2].end(),t.face)!=selected[2].end())) {
                const auto offset = (t.points[1]-t.points[0]).Cross(t.points[2]-t.points[0]).NormalizedOr(Vec3::sAxisY())*.025f;
                DrawTriangle3D(vector(t.points[0]+offset),vector(t.points[1]+offset),vector(t.points[2]+offset),{255,161,0,90});
            }
            if (xray) { rlDisableDepthTest(); rlDisableDepthMask(); }
            for (int i = 0; i<int(edges.size()); ++i) DrawLine3D(vector(points[edges[i][0]]),vector(points[edges[i][1]]),mode==1 && std::find(selected[1].begin(),selected[1].end(),i)!=selected[1].end() ? ORANGE : accent);
            if (tab==0 && mode==0) for (int i = 0; i<int(points.size()); ++i) DrawSphere(vector(points[i]),camera.fovy*.007f,std::find(vertices.begin(),vertices.end(),i)!=vertices.end() ? ORANGE : ink);
            if (xray) { rlEnableDepthMask(); rlEnableDepthTest(); }
        }
        if (slice) {
            rlDisableDepthTest(); rlDisableDepthMask();
            const int a = cut_axis==0 ? 1 : 0, b = cut_axis==2 ? 1 : 2;
            Vec3 center = mesh_axis(cut_axis)*cut_position; if (cut_axis!=1) center.SetY(mesh.size.GetY()/2);
            const auto u = mesh_axis(a)*(mesh_coordinate(mesh.size,a)/2), v = mesh_axis(b)*(mesh_coordinate(mesh.size,b)/2);
            const Vec3 border_points[] = {center-u-v,center+u-v,center+u+v,center-u+v};
            for (int i = 0; i<4; ++i) DrawLine3D(vector(border_points[i]),vector(border_points[(i+1)%4]),SKYBLUE);
            for (const auto& t : triangles) if (!t.decal) {
                std::vector<Vec3> cut_points;
                for (int i = 0; i<3; ++i) {
                    const auto a = t.points[i], b = t.points[(i+1)%3]; const float da = mesh_coordinate(a,cut_axis)-cut_position, db = mesh_coordinate(b,cut_axis)-cut_position;
                    if (std::abs(da)<1e-5f) cut_points.push_back(a);
                    else if (da*db<0) cut_points.push_back(a+(b-a)*(da/(da-db)));
                }
                if (cut_points.size()>=2) DrawLine3D(vector(cut_points[0]),vector(cut_points[1]),SKYBLUE);
            }
            rlEnableDepthMask(); rlEnableDepthTest();
        }
        EndMode3D(); EndTextureMode();
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(window,"Building Creator"); ui::draw_window_close(window);
        DrawTextureRec(preview.texture,{0,0,float(preview.texture.width),-float(preview.texture.height)},{view.x,view.y},WHITE); DrawRectangleLinesEx(view,1,border);
        if (tab==0 && !slice && !vertices.empty()) { BeginScissorMode(int(view.x),int(view.y),int(view.width),int(view.height)); mesh_gizmo(pivot,camera,view,GetMousePosition(),true,grab ? handle : -1); EndScissorMode(); }
        text(std::to_string(tiles[0])+" x "+std::to_string(tiles[1])+" tiles",view.x+10,view.y+10,16,accent);
        text(slice ? "Slice preview / drag the position slider" : "Drag handles / Shift: select more / G: move",view.x+10,view.y+view.height-28,15,accent);
        text("Right drag: orbit / Wheel: zoom",view.x,view.y+view.height+10,16);
        const auto control = [&](Rectangle r,const std::string& label,bool chosen=false,bool enabled=true) { return button(r,label,chosen,enabled,can_edit && !grab,16); };
        text("Name",20,88,17); const Rectangle name_box{84,80,310,44};
        if (!grab && click && hit(name_box)) field = 0;
        if (field==0) { input(mesh.name,name_box); if (IsKeyPressed(KEY_ENTER) || (click && !hit(name_box))) field = -1; }
        else { DrawRectangleLinesEx(name_box,1,border); text(fit(mesh.name,18,286),96,93); }
        text("FOOTPRINT",20,132,16,accent);
        int width = tiles[0], depth = tiles[1]; bool resized = false;
        const auto stepper = [&](const char* label,int& value,float y,int id) {
            text(label,20,y+5,17); const Rectangle box{150,y,152,30};
            if (!grab && click && hit(box)) { field = id; buffer = std::to_string(value); }
            if (field==id) {
                input(buffer,box);
                if (IsKeyPressed(KEY_ENTER) || (click && !hit(box))) {
                    try { std::size_t used; const int number = std::stoi(buffer,&used); if (used!=buffer.size() || number<1 || number>128) throw std::out_of_range("size"); value = number; resized = true; }
                    catch (const std::exception&) { status = "Enter a tile count between 1 and 128."; }
                    field = -1;
                }
            } else { DrawRectangleLinesEx(box,1,border); text(std::to_string(value),160,y+5,17); }
            if (control({312,y,34,30},"-")) { value = std::max(1,value-1); resized = true; field = -1; }
            if (control({356,y,34,30},"+")) { value = std::min(128,value+1); resized = true; field = -1; }
        };
        stepper("Tiles X",width,158,1); stepper("Tiles Z",depth,194,2);
        if (resized) { auto next = mesh; if (next.set_footprint(width,depth,status,true)) commit(std::move(next)); }
        const char* tabs[] = {"Mesh","Texture","Decals","Saved"}; const Rectangle tab_boxes[] = {{20,244,82,30},{108,244,108,30},{222,244,90,30},{318,244,78,30}};
        for (int i = 0; i<4; ++i) if (control(tab_boxes[i],tabs[i],tab==i)) { tab = i; scroll = 0; field = -1; slice = false; if (i==1) decal_texture = false; }
        BuildingMesh next = mesh; bool changed = false;
        const bool keys = can_edit && !grab && field<0 && !ctrl;
        if (tab==0) {
            if (slice) {
                for (int i = 0; i<3; ++i) if (control({20+i*126.f,288,118,30},std::string(1,"XYZ"[i])+" slice",cut_axis==i)) cut_axis = i;
                text("Cut position",20,338,16); mesh_slider({20,366,370,18},cut_fraction,.02f,.98f,0,slider_drag,can_edit);
                text(TextFormat("%.0f%% / %.2f m",cut_fraction*100,cut_position),20,396,16);
                if (control({20,434,180,30},"Apply slice",true)) {
                    const int old = int(next.vertices.size()); changed = next.slice(cut_axis,cut_position,status);
                    if (changed) { selected = {}; mode = 0; for (int i = old; i<int(next.vertices.size()); ++i) selected[0].push_back(i); slice = false; }
                }
                if (control({210,434,180,30},"Cancel slice")) slice = false;
            } else {
                const char* modes[] = {"1 Vertex","2 Edge","3 Face"};
                for (int i = 0; i<3; ++i) if (control({20+i*126.f,288,118,30},modes[i],mode==i)) { mode = i; field = -1; }
                if (control({20,326,118,26},xray ? "Xray ON" : "Xray OFF",xray)) xray = !xray;
                text(std::to_string(selected[mode].size())+" selected / "+std::to_string(mesh.vertices.size())+" verts",150,330,15);
                if (control({20,364,118,30},"Slice K")) { slice = true; cut_fraction = .5f; }
                const auto faces_action = [&](bool extrude) {
                    if (mode!=2 || selected[2].empty()) { status = "Select one or more faces."; return false; }
                    for (int f : selected[2]) if (!(extrude ? next.extrude_face(f,1,status) : next.inset_face(f,.2f,status))) return false;
                    return true;
                };
                if (control({146,364,118,30},"Extrude",false,mode==2 && !selected[2].empty()) || (keys && IsKeyPressed(KEY_E))) changed = faces_action(true);
                if (control({272,364,118,30},"Inset I",false,mode==2 && !selected[2].empty()) || (keys && IsKeyPressed(KEY_I))) changed = faces_action(false);
                if (control({20,400,180,30},"+ Add vertex")) { const int v = next.add_vertex(pivot+Vec3(0,1,0),status); if (v>=0) { selected = {}; selected[0] = {v}; mode = 0; changed = true; } }
                if (control({210,400,180,30},"Make face F",false,mode==0 && selected[0].size()>=3) || (keys && IsKeyPressed(KEY_F))) {
                    changed = next.add_face(selected[0],status); if (changed) { selected = {}; selected[2] = {int(next.faces.size())-1}; mode = 2; }
                }
                if (control({20,436,180,30},"Subdivide")) {
                    changed = !selected[mode].empty();
                    if (mode==2) { for (int f : selected[2]) if (!next.subdivide_face(f,status)) { changed = false; break; } }
                    else if (mode==1) { for (int i : selected[1]) if (next.split_edge(edges[i][0],edges[i][1],status)<0) { changed = false; break; } }
                    else if (selected[0].size()==2) changed = next.split_edge(selected[0][0],selected[0][1],status)>=0;
                    else { changed = false; status = "Select edges, faces, or two connected vertices."; }
                    if (changed) selected = {};
                }
                if (control({210,436,180,30},"Delete Del") || (keys && IsKeyPressed(KEY_DELETE))) {
                    if (mode==0) changed = next.remove_vertices(selected[0],status);
                    else {
                        std::vector<int> faces = selected[2];
                        if (mode==1) {
                            std::set<int> removed;
                            for (int e : selected[1]) for (int f = 0; f<int(mesh.faces.size()); ++f) {
                                const auto& corners = mesh.faces[f].vertices;
                                for (std::size_t j = 0; j<corners.size(); ++j) if (std::minmax(corners[j],corners[(j+1)%corners.size()])==std::minmax(edges[e][0],edges[e][1])) removed.insert(f);
                            }
                            faces = {removed.begin(),removed.end()};
                        }
                        changed = next.remove_faces(faces,status);
                    }
                    if (changed) selected = {};
                }
            }
        } else if (tab==1) {
            if (control({20,288,180,30},"No texture",mesh.texture.empty(),!decal_texture)) { next.texture.clear(); changed = true; }
            if (control({210,288,180,30},"Refresh folder")) { for (const auto& entry : images) if (entry.second.id) UnloadTexture(entry.second); images.clear(); textures = building_textures(building_texture_directory(),status); scroll = 0; }
            const Rectangle list{20,330,370,height-484}; const int rows = std::max(1,int(list.height/36));
            if (can_edit && hit(list)) scroll -= int(GetMouseWheelMove());
            scroll = std::clamp(scroll,0,std::max(0,int(textures.size())-rows));
            if (textures.empty()) text("Add images to assets/textures",20,340,16);
            for (int row = 0; row<rows && scroll+row<int(textures.size()); ++row) {
                const auto& name = textures[scroll+row]; const Rectangle r{20,330+row*36.f,370,32}; const auto thumb = image(name);
                if (control(r,"      "+fit(name,16,296),decal_texture && decal>=0 ? mesh.decals[decal].texture==name : mesh.texture==name)) {
                    if (thumb.id) { if (decal_texture && decal>=0) next.decals[decal].texture = name; else next.texture = name; changed = true; }
                    else status = "Could not load "+name;
                }
                if (thumb.id) DrawTexturePro(thumb,{0,0,float(thumb.width),float(thumb.height)},{r.x+4,r.y+2,28,28},{0,0},0,WHITE);
            }
        } else if (tab==2) {
            if (control({20,288,180,28},"+ Add decal",false,!textures.empty() && mesh.decals.size()<64)) { next.decals.push_back({selected[2].empty() ? 0 : selected[2].back(),textures.front()}); decal = int(next.decals.size())-1; changed = true; }
            if (control({210,288,180,28},"Remove decal",false,decal>=0)) { next.decals.erase(next.decals.begin()+decal); decal = next.decals.empty() ? -1 : std::min(decal,int(next.decals.size())-1); changed = true; }
            if (decal>=0) {
                if (control({20,324,370,28},"Decal "+std::to_string(decal+1)+" / "+std::to_string(next.decals.size()))) decal = (decal+1)%int(next.decals.size());
                auto& d = next.decals[decal]; text("Face "+std::to_string(d.face)+": click preview to change",20,360,14);
                if (control({20,382,370,28},"Texture: "+fit(d.texture,16,260))) { tab = 1; decal_texture = true; scroll = 0; }
                const auto uv = [&](const char* label,float& value,float lo,float hi,float y,int id) { text(label,20,y-2,14); changed |= mesh_slider({150,y,236,10},value,lo,hi,id,slider_drag,can_edit); };
                uv("Left",d.u,0,1-d.width,421,1); uv("Top",d.v,0,1-d.height,436,2); uv("Width",d.width,.01f,1-d.u,451,3); uv("Height",d.height,.01f,1-d.v,466,4);
            } else text("Add a texture overlay, then click its face.",20,338,14);
        } else {
            if (control({20,288,180,30},"Refresh saved")) { library = saved_buildings(directory,status); library_selection = -1; scroll = 0; }
            if (control({210,288,180,30},"Load selected",false,library_selection>=0)) { next = library[library_selection]; changed = align(next); selected = {}; tab = 0; }
            const Rectangle list{20,330,370,height-482}; const int rows = std::max(1,int(list.height/34));
            if (can_edit && hit(list)) scroll -= int(GetMouseWheelMove());
            scroll = std::clamp(scroll,0,std::max(0,int(library.size())-rows));
            if (library.empty()) text("Save a design to list it here",20,340,16);
            for (int row = 0; row<rows && scroll+row<int(library.size()); ++row) if (control({20,330+row*34.f,370,30},fit(library[scroll+row].name,16,345),library_selection==scroll+row)) library_selection = scroll+row;
        }
        if (changed) commit(std::move(next));
        if (control({20,height-122,180,36},"Save building",true) || command==MenuCommand::SaveBuilding || (can_edit && !grab && ctrl && IsKeyPressed(KEY_S))) {
            if (mesh.save(directory,status)) {
                if (saved_designs) {
                    const auto found = std::find_if(saved_designs->begin(),saved_designs->end(),[&](const auto& design){return design.name==mesh.name;});
                    if (found==saved_designs->end()) saved_designs->push_back(mesh); else *found = mesh;
                }
                library = saved_buildings(directory,status); status = "Building saved";
            }
        }
        const bool use = control({210,height-122,180,36},"Use in map",true) || command==MenuCommand::UseBuilding;
        const bool cancel = control({view.x+view.width-142,height-122,142,36},"Close");
        text(fit(status,17,GetScreenWidth()-40.f),20,height-49,17,accent); menu.draw(menu_state);
        text("BUILDING CREATOR",GetScreenWidth()-212.f,8,16,background); EndDrawing(); interactive = true;
        if (!screenshot.empty() && ++frames>=3) { auto capture = LoadImageFromScreen(); ExportImage(capture,screenshot.c_str()); UnloadImage(capture); break; }
        if (cancel) break;
        if (use && mesh.validate(status)) { result = mesh; break; }
    }
    if (preview.id) UnloadRenderTexture(preview);
    for (const auto& entry : images) if (entry.second.id) UnloadTexture(entry.second);
    UnloadShader(shader); return result;
}

std::optional<CarDesign> car_builder(CarDesign design,const std::filesystem::path& directory,const std::string& screenshot,std::vector<CarDesign>* saved_designs) {
    CarRenderer renderer; SceneLighting lighting; GraphicsSettings graphics; graphics.shadows = 0;
    DayNight day; day.set_time("09:00");
    PhysicsWorld world(false); Car car(world); car.set_simulated(false); TuningPanel tuning;
    std::string status; auto bodies = car_models(car_asset_directory("cars"),status);
    auto wheels = car_models(car_asset_directory("wheels"),status); auto library = saved_cars(directory,status);
    int tab = design.body.empty() && !library.empty() ? 3 : 0, scroll = 0, drag = -1, frames = 0;
    float yaw = 2.44f, pitch = .35f, zoom = 1;
    bool interactive = false, naming = false; RenderTexture2D preview{}; std::optional<CarDesign> result;
    MenuBar menu(MenuMode::CarCreator);
    const auto sync = [&]() {
        auto physical = design; physical.name = "Preview"; physical.body = "preview.glb"; physical.wheel = "preview.glb";
        car.set_design(physical); car.reset({0,car.ride_height(),0},0);
    };
    sync();
    const auto save_design = [&]() {
        if (!renderer.available(design,status) || !design.save(directory,status)) return false;
        if (saved_designs) {
            auto found = std::find_if(saved_designs->begin(),saved_designs->end(),[&](const auto& d){return d.name==design.name;});
            if (found==saved_designs->end()) saved_designs->push_back(design); else *found = design;
        }
        library = saved_cars(directory,status); status = "Car saved"; return true;
    };
    while (!WindowShouldClose()) {
        const int width = GetScreenWidth(), height = GetScreenHeight();
        const auto command = interactive ? menu.update() : MenuCommand::None;
        const bool active = interactive && !menu.blocking() && !menu.interacted() && IsWindowFocused();
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL);
        if (command==MenuCommand::CloseCreator || (active && IsKeyPressed(KEY_ESCAPE))) break;
        if (command==MenuCommand::NewCar) { design = {}; sync(); tab = scroll = 0; naming = false; status.clear(); }
        if (!active || !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) drag = -1;
        if (tab==2 && active) {
            TuningPanelInput in; in.mouse = GetMousePosition(); in.pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
            in.down = IsMouseButtonDown(MOUSE_BUTTON_LEFT); in.focused = true; in.fine = IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT);
            in.horizontal = int(IsKeyPressed(KEY_RIGHT))-int(IsKeyPressed(KEY_LEFT)); in.vertical = int(IsKeyPressed(KEY_DOWN))-int(IsKeyPressed(KEY_UP));
            in.scroll = tuning.contains(in.mouse,width,height) ? GetMouseWheelMove() : 0;
            const auto action = tuning.update(car,width,height,in); design.tuning = car.tuning();
            if (action.close) tab = 0;
            if (action.reset_car) { yaw = 2.44f; pitch = .35f; zoom = 1; }
        } else tuning.cancel_drag();
        const Rectangle view = tab==2 ? Rectangle{20,84,float(width-440),float(height-204)} : Rectangle{414,84,float(width-438),float(height-204)};
        if (active && hit(view)) {
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) { yaw += GetMouseDelta().x*.01f; pitch = std::clamp(pitch+GetMouseDelta().y*.01f,.05f,1.4f); }
            zoom = std::clamp(zoom*std::pow(.9f,GetMouseWheelMove()),.4f,2.5f);
        }
        if (!preview.id || preview.texture.width!=int(view.width) || preview.texture.height!=int(view.height)) {
            if (preview.id) UnloadRenderTexture(preview);
            preview = LoadRenderTexture(int(view.width),int(view.height));
        }
        const float scale = std::max({design.width+2*std::abs(design.offset[0]),design.height+2*std::abs(design.offset[1]),
            design.length+2*std::abs(design.offset[2]),design.tuning.wheelbase+2*design.tuning.wheel_radius});
        const float target = design.height/2+car.ride_height()+design.offset[1]/2;
        Camera3D camera{{std::sin(yaw)*scale*2*std::cos(pitch),target+scale*2*std::sin(pitch),std::cos(yaw)*scale*2*std::cos(pitch)},
            {0,target,0},{0,1,0},scale*1.5f*zoom,CAMERA_ORTHOGRAPHIC};
        renderer.set_lighting(lighting,camera,day.lighting(),graphics);
        BeginTextureMode(preview); ClearBackground({18,30,49,255}); BeginMode3D(camera);
        DrawGrid(20,1); DrawLine3D({0,.02f,0},{0,.02f,-scale},YELLOW);
        renderer.draw_design(design,Vec3::sZero(),0,camera);
        EndMode3D(); EndTextureMode();
        BeginDrawing(); ui::draw_desktop();
        DrawTexturePro(preview.texture,{0,0,float(preview.texture.width),-float(preview.texture.height)},view,{0,0},0,WHITE);
        DrawRectangleLinesEx(view,1,border); text("Right-drag: orbit / Wheel: zoom / Yellow: front",view.x,view.y+view.height+12,14);
        const auto control = [&](Rectangle r,const std::string& label,bool selected=false,bool enabled=true) {return button(r,label,selected,enabled,active,16);};
        const char* tabs[] = {"Body","Wheel","Tuning","Saved"};
        for (int i=0;i<4;++i) if (control({20+i*96.f,44,88,30},tabs[i],tab==i)) { tab=i; scroll=0; naming=false; drag=-1; }
        if (tab==2) tuning.draw(car,width,height);
        else {
            const Rectangle name{20,102,370,34}; text("Name",20,82,14);
            if (active && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) naming = hit(name);
            if (naming && active) input(design.name,name);
            else control(name,design.name);
            if (control({20,146,180,30},"Regular",design.type==CarType::Civilian)) design.type=CarType::Civilian;
            if (control({210,146,180,30},"Police",design.type==CarType::Police)) design.type=CarType::Police;
            if (control({20,186,370,28},"Refresh files")) {
                renderer.refresh(); bodies=car_models(car_asset_directory("cars"),status); wheels=car_models(car_asset_directory("wheels"),status);
                library=saved_cars(directory,status); scroll=0;
            }
            const auto& models = tab==1 ? wheels : bodies;
            if (tab==0) {
                float* values[] = {&design.width,&design.height,&design.length,&design.offset[0],&design.offset[1],&design.offset[2]};
                const float lo[] = {.8f,.3f,1.5f,-4,-4,-4}, hi[] = {4,4,8,4,4,4};
                const char* labels[] = {"Width","Height","Length","Offset X","Offset Y","Offset Z"};
                for (int i=0;i<6;++i) {
                    text(TextFormat("%s: %.2f m",labels[i],double(*values[i])),20,224+i*34.f,14);
                    if (mesh_slider({164,228+i*34.f,226,10},*values[i],lo[i],hi[i],i,drag,active)) sync();
                }
            }
            const float y = tab==0 ? 436.f : 230.f;
            text(tab==3 ? "Saved cars / click to load" : tab==1 ? "assets/wheels / automatic tire radius" : "assets/cars / embedded textures",20,y-18,14);
            const Rectangle list{20,y,370,height-y-130}; const int rows = std::max(1,int(list.height/32));
            const int count = tab==3 ? int(library.size()) : int(models.size());
            if (active && hit(list)) scroll -= int(GetMouseWheelMove());
            scroll = std::clamp(scroll,0,std::max(0,count-rows));
            if (!count) { text(tab==3 ? "No saved cars yet" : "Add a GLB, then Refresh files",20,y+4,15); }
            for (int row=0;row<rows && scroll+row<count;++row) {
                const int index=scroll+row; const auto label=tab==3 ? library[index].name : models[index];
                const bool selected=tab==3 ? design.name==label : (tab==1 ? design.wheel : design.body)==label;
                if (control({20,y+row*32,370,28},label,selected)) {
                    if (tab==3) { design=library[index]; sync(); tab=0; scroll=0; break; }
                    if (tab==1) design.wheel=label; else design.body=label;
                    std::string error; status = renderer.available(design,error) ? "Body and wheel ready" : error;
                }
            }
        }
        const bool saving=control({20,float(height-72),172,34},"Save car",true) || command==MenuCommand::SaveCar || (active && ctrl && IsKeyPressed(KEY_S));
        const bool use=control({208,float(height-72),172,34},"Use in map",true) || command==MenuCommand::UseCar;
        const bool close=control({396,float(height-72),172,34},"Close");
        if ((saving||use) && save_design() && use) result=design;
        text(fit(status.empty() ? "Choose a body and wheel, size the body, then save." : status,16,width-40.f),20,height-27.f,16,accent);
        menu.draw(); text("CAR EDITOR",width-136.f,8,16,background); EndDrawing(); interactive=true;
        if (!screenshot.empty() && ++frames>=3) { auto capture=LoadImageFromScreen(); ExportImage(capture,screenshot.c_str()); UnloadImage(capture); break; }
        if (close || result) break;
    }
    if (preview.id) UnloadRenderTexture(preview);
    return result;
}

std::optional<CharacterDesign> character_builder(CharacterDesign design,const std::filesystem::path& directory,const std::string& screenshot) {
    CharacterRenderer renderer; SceneLighting lighting; GraphicsSettings graphics; graphics.shadows=0;
    DayNight day; day.set_time("09:00"); PhysicsWorld world(false); Character character(world);
    character.reset({0,.08f,0});
    std::string status; auto library=saved_characters(directory,status);
    std::array<std::vector<std::string>,9> files;
    const auto refresh=[&]() {
        renderer.refresh();
        for (std::size_t i=0;i<files.size();++i) files[i]=car_models(car_asset_directory((std::string("character/")+character_slot_folders[i]).c_str()),status);
        library=saved_characters(directory,status);
    };
    refresh();
    int slot=0,tab=0,scroll=0,drag=-1,frames=0,pose=0;
    float yaw=2.65f,pitch=.2f,zoom=1,accumulator=0;
    bool interactive=false,naming=false,guides=true; RenderTexture2D preview{}; std::optional<CharacterDesign> result;
    MenuBar menu(MenuMode::CharacterCreator);
    const auto save_design=[&]() {
        if (!renderer.available(design,status) || !design.save(directory,status)) return false;
        library=saved_characters(directory,status); status="Character saved"; return true;
    };
    while (!WindowShouldClose()) {
        const int width=GetScreenWidth(),height=GetScreenHeight();
        const auto command=interactive ? menu.update() : MenuCommand::None;
        const bool active=interactive && !menu.blocking() && !menu.interacted() && IsWindowFocused();
        const bool ctrl=IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL);
        if (command==MenuCommand::CloseCreator || (active && IsKeyPressed(KEY_ESCAPE))) break;
        if (command==MenuCommand::NewCharacter) { design={}; tab=slot=scroll=pose=0; naming=false; character.reset({0,.08f,0}); status.clear(); }
        if (!active || !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) drag=-1;
        accumulator+=std::min(GetFrameTime(),.1f);
        while (accumulator>=fixed_step) {
            world.step(); character.step({pose==1 || pose==2 ? Vec3(0,0,-1) : Vec3::sZero(),pose==2}); accumulator-=fixed_step;
        }
        const Rectangle view{414,84,float(width-438),float(height-(tab==0 ? 352 : 204))};
        if (active && hit(view)) {
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) { yaw+=GetMouseDelta().x*.01f; pitch=std::clamp(pitch+GetMouseDelta().y*.01f,-.3f,1.2f); }
            zoom=std::clamp(zoom*std::pow(.9f,GetMouseWheelMove()),.4f,2.5f);
        }
        if (!preview.id || preview.texture.width!=int(view.width) || preview.texture.height!=int(view.height)) {
            if (preview.id) UnloadRenderTexture(preview);
            preview=LoadRenderTexture(int(view.width),int(view.height));
        }
        const auto center=character.position()+Vec3(0,.9f,0);
        Camera3D camera{vector(center+Vec3(std::sin(yaw)*4*std::cos(pitch),4*std::sin(pitch),std::cos(yaw)*4*std::cos(pitch))),vector(center),{0,1,0},2.6f*zoom,CAMERA_ORTHOGRAPHIC};
        renderer.set_lighting(lighting,camera,day.lighting(),graphics);
        BeginTextureMode(preview); ClearBackground({18,30,49,255}); BeginMode3D(camera);
        DrawGrid(40,1); renderer.draw(character,camera,{},guides,slot,&design);
        EndMode3D(); EndTextureMode();
        BeginDrawing(); ui::draw_desktop();
        DrawTexturePro(preview.texture,{0,0,float(preview.texture.width),-float(preview.texture.height)},view,{0,0},0,WHITE);
        DrawRectangleLinesEx(view,1,border);
        text("Right-drag: orbit / Wheel: zoom",view.x,63,14);
        const auto control=[&](Rectangle r,const std::string& label,bool selected=false,bool enabled=true) {return button(r,label,selected,enabled,active,15);};
        if (control({20,44,180,30},"Parts",tab==0)) {tab=scroll=0; naming=false;}
        if (control({210,44,180,30},"Saved",tab==1)) {tab=1; scroll=0; naming=false;}
        const Rectangle name{20,102,370,34}; text("Name",20,82,14);
        if (active && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) naming=hit(name);
        if (naming && active) input(design.name,name); else control(name,design.name);
        for (int i=0;i<3;++i) if (control({20+i*126.f,146,118,30},character_type_names[i],int(design.type)==i)) design.type=CharacterType(i);
        if (control({20,186,370,28},"Refresh files")) {refresh(); scroll=0;}
        if (tab==0) {
            for (int i=0;i<9;++i) if (control({20+(i%3)*126.f,226+(i/3)*34.f,118,30},character_slot_names[i],slot==i)) {slot=i; scroll=0; drag=-1;}
            auto& part=design.parts[slot];
            text(std::string("assets/character/")+character_slot_folders[slot],20,336,14);
            text(fit(part.model.empty() ? "Choose a GLB below" : part.model,14,370),20,357,14,accent);
            const Rectangle list{20,382,370,float(height-472)}; const int rows=std::max(1,int(list.height/32));
            if (active && hit(list)) scroll-=int(GetMouseWheelMove());
            scroll=std::clamp(scroll,0,std::max(0,int(files[slot].size())-rows));
            if (files[slot].empty()) text("Add GLBs, then Refresh files",20,390,15);
            for (int row=0;row<rows && scroll+row<int(files[slot].size());++row) {
                const auto& file=files[slot][scroll+row];
                if (control({20,382+row*32.f,370,28},file,part.model==file)) {part.model=file; status.clear();}
            }
            const float column=(view.width-24)/3,y=float(height-198);
            const char* labels[]={"Scale","Offset (m)","Rotation (deg)"};
            std::array<float,3>* values[]={&part.scale,&part.offset,&part.rotation};
            const float lo[]={.1f,-1,-180},hi[]={3,1,180};
            for (int group=0;group<3;++group) {
                const float x=view.x+group*(column+12); text(labels[group],x,y,14,accent);
                for (int axis=0;axis<3;++axis) {
                    float& value=(*values[group])[axis]; const float row=y+28+axis*27.f;
                    text(TextFormat(group==2 ? "%c %.0f" : "%c %.2f","XYZ"[axis],double(value)),x,row,13);
                    mesh_slider({x+88,row+5,column-88,10},value,lo[group],hi[group],group*3+axis,drag,active);
                }
            }
            if (control({view.x,float(height-92),200,24},"Reset part fit")) {part.scale={1,1,1}; part.offset={}; part.rotation={};}
        } else {
            const Rectangle list{20,244,370,float(height-334)}; const int rows=std::max(1,int(list.height/32));
            if (active && hit(list)) scroll-=int(GetMouseWheelMove());
            scroll=std::clamp(scroll,0,std::max(0,int(library.size())-rows));
            if (library.empty()) text("No saved characters yet",20,246,15);
            for (int row=0;row<rows && scroll+row<int(library.size());++row) {
                const auto& saved=library[scroll+row];
                if (control({20,244+row*32.f,370,28},saved.name+" / "+character_type_names[int(saved.type)],saved.name==design.name)) {design=saved; tab=scroll=0; status.clear(); break;}
            }
        }
        const char* poses[]={"Idle","Walk","Run","Ragdoll"};
        for (int i=0;i<4;++i) if (button({view.x+i*114.f,view.y+view.height+10,106,26},poses[i],pose==i,true,active,14)) {
            pose=i; character.reset({0,.08f,0}); character.revive();
            if (i==3) character.ragdoll({0,1,0},{18,0,-25});
        }
        if (control({view.x+456,view.y+view.height+10,view.width-456,26},"Rig",guides)) guides=!guides;
        const bool saving=control({20,float(height-64),172,34},"Save character",true) || command==MenuCommand::SaveCharacter || (active && ctrl && IsKeyPressed(KEY_S));
        const bool use=control({208,float(height-64),172,34},"Use in map",true) || command==MenuCommand::UseCharacter;
        const bool close=control({396,float(height-64),140,34},"Close");
        if ((saving || use) && save_design() && use) result=design;
        text(fit(status.empty() ? "Fit GLBs to the rig; paired limbs and shoes mirror automatically." : status,15,width-40.f),20,height-24.f,15,accent);
        menu.draw(); text("CHARACTER CREATOR",width-224.f,8,16,background); EndDrawing(); interactive=true;
        if (!screenshot.empty() && ++frames>=3) {auto capture=LoadImageFromScreen(); ExportImage(capture,screenshot.c_str()); UnloadImage(capture); break;}
        if (close || result) break;
    }
    if (preview.id) UnloadRenderTexture(preview);
    return result;
}

namespace {
std::optional<CharacterDesign> choose_player_character(const std::optional<CharacterDesign>& current,const std::filesystem::path& directory) {
    CharacterRenderer renderer; SceneLighting lighting; GraphicsSettings graphics; graphics.shadows=0;
    DayNight day; day.set_time("09:00"); PhysicsWorld world(false); Character character(world); character.reset({0,.08f,0});
    std::string status; auto library=saved_characters(directory,status);
    library.erase(std::remove_if(library.begin(),library.end(),[](const auto& design){return design.type!=CharacterType::Player;}),library.end());
    if (current && std::none_of(library.begin(),library.end(),[&](const auto& design){return design.name==current->name;})) library.push_back(*current);
    int selection=library.empty() ? -1 : 0,scroll=0; bool interactive=false;
    if (current) for (int i=0;i<int(library.size());++i) if (library[i].name==current->name) selection=i;
    RenderTexture2D preview=LoadRenderTexture(360,300);
    std::optional<CharacterDesign> result;
    while (!WindowShouldClose()) {
        const Rectangle window{(GetScreenWidth()-800)/2.f,(GetScreenHeight()-466)/2.f,800,466};
        Camera3D camera{{2,1.7f,-4},{0,.9f,0},{0,1,0},2.5f,CAMERA_ORTHOGRAPHIC};
        const bool ready=selection>=0 && renderer.available(library[selection],status);
        if (library.empty()) status="Create and save a Player character first.";
        renderer.set_lighting(lighting,camera,day.lighting(),graphics);
        BeginTextureMode(preview); ClearBackground({18,30,49,255}); BeginMode3D(camera); DrawGrid(10,1);
        if (selection>=0) renderer.draw(character,camera,{},false,-1,&library[selection]);
        EndMode3D(); EndTextureMode();
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(window,"Choose player character"); ui::draw_window_close(window);
        DrawTexturePro(preview.texture,{0,0,360,-300},{window.x+416,window.y+60,360,300},{0,0},0,WHITE);
        const Rectangle list{window.x+24,window.y+60,368,300};
        if (interactive && hit(list)) scroll-=int(GetMouseWheelMove());
        scroll=std::clamp(scroll,0,std::max(0,int(library.size())-9));
        for (int row=0;row<9 && scroll+row<int(library.size());++row)
            if (button({list.x,list.y+row*32.f,list.width,28},library[scroll+row].name,selection==scroll+row,true,interactive,16)) selection=scroll+row;
        const bool use=button({window.x+24,window.y+392,220,36},"Use character",true,ready,interactive);
        const bool create=button({window.x+264,window.y+392,250,36},"Character creator",false,true,interactive);
        const bool close=close_clicked(window,interactive) || button({window.x+534,window.y+392,242,36},"Cancel",false,true,interactive) || (interactive && IsKeyPressed(KEY_ESCAPE));
        text(fit(status,14,750),window.x+24,window.y+368,14,accent); MenuBar(MenuMode::Cities).draw(); EndDrawing(); interactive=true;
        if (use) {result=library[selection]; break;}
        if (close) break;
        if (create) {
            const auto made=character_builder({},directory);
            if (made && made->type==CharacterType::Player) {result=made; break;}
            library=saved_characters(directory,status);
            library.erase(std::remove_if(library.begin(),library.end(),[](const auto& design){return design.type!=CharacterType::Player;}),library.end());
            renderer.refresh(); selection=library.empty() ? -1 : 0; scroll=0; interactive=false;
        }
    }
    UnloadRenderTexture(preview); return result;
}
enum class EditorResult { Back, Play, Quit };
EditorResult editor(City& city,const std::filesystem::path& directory,const std::string& screenshot) {
    Tool tool = Tool::Land;
    int building_size = 1, building_height = 12, building_rotation = 0, vehicle_kind = 0, rotation = 0;
    int brush_size = 3, density = 2, elevation_direction = 1, revision = 0, preview_revision = -1;
    std::string ground_texture = "soil.png";
    int selected_building = -1, selected_vehicle = -1;
    std::optional<BuildingMesh> building_mesh;
    std::optional<CarDesign> selected_car;
    CarRenderer car_renderer;
    CharacterRenderer character_renderer;
    PhysicsWorld character_preview_world(false); Character character_preview(character_preview_world);
    const auto characters_directory=directory.parent_path()/"characters";
    bool dirty = false, dragging = false, top = false, grid = true, close_failed = false, diagonal_roads = true;
    bool painting = false, stroke_saved = false, brush_remove = false, brush_sampled = false;
    float save_time = 0, preview_time = 0, yaw = .78539816f;
    int library_scroll = 0;
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
    const auto buildings_directory = directory.parent_path()/"buildings";
    auto designs = saved_buildings(buildings_directory,status);
    const auto refresh_designs = [&]() {
        designs = saved_buildings(buildings_directory,status);
        if (building_mesh) {
            const auto found = std::find_if(designs.begin(),designs.end(),[&](const auto& d){return d.name==building_mesh->name;});
            if (found==designs.end()) designs.insert(designs.begin(),*building_mesh); else *found = *building_mesh;
        }
    };
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
        if (result==EditorResult::Play && !character_renderer.available(city,status)) return false;
        if (dirty && !save(city,directory,status,dirty)) return false;
        return true;
    };
    std::string library_error, update_error;
    const auto cars = saved_cars(directory.parent_path()/"cars",library_error);
    City refreshed = city;
    const auto characters=saved_characters(characters_directory,library_error);
    const bool character_changed=refreshed.update_player_character(characters);
    if (refreshed.update_car_designs(cars,update_error)>0 || character_changed) {
        commit(std::move(refreshed)); status = "Saved designs updated / Ctrl+Z to undo";
    }
    if (!update_error.empty()) status = "Map update rejected: "+update_error;
    else if (!library_error.empty()) status = library_error;
    int frames = 0; bool first_frame = true;
    while (true) {
        float dt = std::min(GetFrameTime(),.1f); save_time += dt; preview_time += dt;
        if (screenshot.empty() && WindowShouldClose() && !close_failed) {
            if (finish(EditorResult::Quit)) return EditorResult::Quit;
            close_failed = true;
        }
        if (dirty && !painting && save_time>1) { save(city,directory,status,dirty); save_time = -4; }
        MenuState menu_state;
        std::string character_error;
        menu_state.undo = !undo.empty(); menu_state.redo = !redo.empty(); menu_state.play = character_renderer.available(city,character_error);
        menu_state.selection = selected_building>=0 || selected_vehicle>=0;
        menu_state.building_selection = selected_building>=0;
        menu_state.car_selection = selected_vehicle>=0 && city.vehicles[selected_vehicle].kind==CityVehicleKind::Car;
        menu_state.vehicle = vehicle_kind; menu_state.density = density;
        menu_state.diagonal = diagonal_roads; menu_state.elevation = elevation_direction;
        menu_state.top = top; menu_state.grid = grid; menu_state.tool = int(tool);
        const auto command = first_frame ? MenuCommand::None : menu.update(menu_state);
        if (command==MenuCommand::CharacterCreator || command==MenuCommand::ChoosePlayerCharacter) {
            const auto design=command==MenuCommand::CharacterCreator ? character_builder({},characters_directory) : choose_player_character(city.player_character,characters_directory);
            City next=city; const auto saved=saved_characters(characters_directory,status);
            bool changed=next.update_player_character(saved);
            if (design && design->type==CharacterType::Player) {changed|=!next.player_character || !(*next.player_character==*design); next.player_character=design;}
            if (changed) commit(std::move(next));
            character_renderer.refresh(); status=design ? std::string(character_type_names[int(design->type)])+" character saved: "+design->name : "Character selection closed";
            painting=dragging=false; first_frame=true; continue;
        }
        const int builder_request = command==MenuCommand::BuildingCreator || command==MenuCommand::EditBuilding ? 0 : -1;
        const bool car_request = command==MenuCommand::CarEditor || command==MenuCommand::EditCar || command==MenuCommand::PlaceCar;
        const bool edit_building = command==MenuCommand::EditBuilding && selected_building>=0;
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
        if (command==MenuCommand::GroundTool || (can_edit && !ctrl && IsKeyPressed(KEY_NINE))) {
            if (const auto chosen = ground_texture_picker(ground_texture)) {
                ground_texture = *chosen; choose_tool(8); preview_revision = -1;
            }
            continue;
        }
        if (command==MenuCommand::RotateBuilding || (can_edit && !ctrl && IsKeyPressed(KEY_R) && (tool==Tool::Building || selected_building>=0))) {
            if (selected_building>=0) {
                const int index = selected_building; City next = city; auto b = city.buildings[index];
                b.rotation = (b.rotation+1)%4;
                if (next.add_building(b,status,index)) { commit(std::move(next)); selected_building = index; }
            } else building_rotation = (building_rotation+1)%4;
        }
        if (command==MenuCommand::SaveCity || (can_edit && ctrl && IsKeyPressed(KEY_S))) save(city,directory,status,dirty);
        if ((command==MenuCommand::Undo || (can_edit && ctrl && IsKeyPressed(KEY_Z))) && !undo.empty()) { redo.push_back(city); city = std::move(undo.back()); undo.pop_back(); dirty = true; save_time = 0; ++revision; painting = dragging = false; selected_building = selected_vehicle = -1; }
        if ((command==MenuCommand::Redo || (can_edit && ctrl && IsKeyPressed(KEY_Y))) && !redo.empty()) { undo.push_back(city); city = std::move(redo.back()); redo.pop_back(); dirty = true; save_time = 0; ++revision; painting = dragging = false; selected_building = selected_vehicle = -1; }
        if (command>=MenuCommand::SelectTool && command<=MenuCommand::ElevationTool) choose_tool(int(command)-int(MenuCommand::SelectTool));
        if (command==MenuCommand::RoadBend || command==MenuCommand::RoadDiagonal) { choose_tool(2); diagonal_roads = command==MenuCommand::RoadDiagonal; }
        if (command==MenuCommand::RaiseGround || command==MenuCommand::LowerGround) { choose_tool(9); elevation_direction = command==MenuCommand::RaiseGround ? 1 : -1; }
        if (command>=MenuCommand::PlaceCar && command<=MenuCommand::PlaceBoeing) { choose_tool(5); vehicle_kind = int(command)-int(MenuCommand::PlaceCar); }
        if (command==MenuCommand::RotateObject) {
            rotation = (rotation+1)%4;
            if (selected_vehicle>=0) {
                City next = city; auto v = next.vehicles[selected_vehicle]; v.rotation = rotation;
                if (next.add_vehicle(v,status,selected_vehicle)) commit(std::move(next));
            } else choose_tool(5);
        }
        if (command>=MenuCommand::TreesSparse && command<=MenuCommand::TreesDense) { choose_tool(7); density = int(command)-int(MenuCommand::TreesSparse)+1; }
        if (command==MenuCommand::BrushSmaller || command==MenuCommand::BrushLarger) { choose_tool(7); brush_size = std::clamp(brush_size+(command==MenuCommand::BrushLarger ? 1 : -1),1,12); }
        for (int i = 0; i<10; ++i) if (i!=8 && can_edit && !ctrl && IsKeyPressed(i==9 ? KEY_ZERO : KEY_ONE+i)) {
            choose_tool(i); if (i==2) diagonal_roads = false; if (i==5) vehicle_kind = 0; if (i==7) density = 1; if (i==9) elevation_direction = 1;
        }
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
                focus += (top ? Vec3(-delta.x,0,-delta.y) : Vec3(-std::cos(yaw)*delta.x-std::sin(yaw)*delta.y*2,0,std::sin(yaw)*delta.x-std::cos(yaw)*delta.y*2))*scale;
                focus.SetX(std::clamp(focus.GetX(),-City::extent,City::extent)); focus.SetZ(std::clamp(focus.GetZ(),-City::extent,City::extent));
            }
        }
        const Vec3 pan = top ? Vec3::sAxisX() : Vec3(std::cos(yaw),0,-std::sin(yaw));
        const Vec3 forward = top ? Vec3::sAxisZ() : Vec3(std::sin(yaw),0,std::cos(yaw));
        if (can_edit && !ctrl) focus += (pan*float(IsKeyDown(KEY_RIGHT)-IsKeyDown(KEY_LEFT))+forward*float(IsKeyDown(KEY_DOWN)-IsKeyDown(KEY_UP)))*dt*camera.fovy*.6f;
        // 30-degree elevation produces a 2:1 dimetric ground grid.
        camera.target = vector(focus); camera.position = vector(focus+(top ? Vec3(0,800,.01f) : Vec3(std::sin(yaw)*600,600/std::sqrt(3.f),std::cos(yaw)*600)));
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
                selected_building = pick_building(city,camera,mouse); selected_vehicle = city.vehicle_at(brush_point);
                if (selected_building<0) selected_building = city.building_at(hover);
                if (selected_building>=0) selected_vehicle = -1;
                if (selected_vehicle>=0) { vehicle_kind = int(city.vehicles[selected_vehicle].kind); rotation = city.vehicles[selected_vehicle].rotation; selected_car=city.vehicles[selected_vehicle].car; }
            } else if (tool==Tool::Spawn || tool==Tool::Vehicle || tool==Tool::Bulldoze) {
                City next = city;
                CityVehicle vehicle{CityVehicleKind(vehicle_kind),brush_point,rotation};
                if (vehicle.kind==CityVehicleKind::Car) vehicle.car=selected_car;
                const bool configured = vehicle.kind!=CityVehicleKind::Car || selected_car.has_value();
                if (tool==Tool::Vehicle && !configured) status="Choose a car in Objects > Choose car";
                const bool okay = tool==Tool::Spawn ? next.set_spawn(hover,status) : tool==Tool::Vehicle ? configured && next.add_vehicle(vehicle,status) : next.erase(brush_point,status);
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
        CityBuilding preview{{anchor.x,anchor.z},building_size,building_height,building_mesh,building_rotation};
        if (dragging && tool==Tool::Building && hover!=anchor && !building_mesh) {
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
                    : tool==Tool::Ground ? next.paint_ground(anchor,hover,ground_texture,status)
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
        car_renderer.set_lighting(lighting,camera,preview_day.lighting(),graphics);
        draw_city(city,grid,car_renderer,camera);
        if (city.spawn && city.player_character) {
            auto feet=City::center(*city.spawn); feet.SetY(city.height(feet.GetX(),feet.GetZ())+.08f);
            character_preview.reset(feet);
            character_renderer.set_lighting(lighting,camera,preview_day.lighting(),graphics);
            character_renderer.draw(character_preview,camera,{},false,-1,&*city.player_character);
        }
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
                City proposed = city; std::string error;
                const bool valid = proposed.add_building(preview,error);
                building_outline(preview,city.tile_height(preview.cell),valid ? accent : RED);
            } else if (tool==Tool::Vehicle) {
                CityVehicle v{CityVehicleKind(vehicle_kind),brush_point,rotation};
                if (v.kind==CityVehicleKind::Car) {
                    v.car=selected_car;
                    if (selected_car) car_renderer.draw_design(*selected_car,brush_point,rotation*PI/2,camera);
                }
                const Vec3 half = v.half_size();
                DrawCubeWires(vector(brush_point+Vec3(0,1,0)),half.GetX()*2,2,half.GetZ()*2,accent);
            } else tile_outline(city,hover,accent);
        }
        if (selected_building>=0) {
            const auto& b = city.buildings[selected_building]; building_outline(b,city.tile_height(b.cell),accent);
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
        std::string active = tools[int(tool)];
        if (tool==Tool::Building || selected_building>=0) active += " / "+std::to_string((selected_building>=0 ? city.buildings[selected_building].rotation : building_rotation)*90)+" deg";
        if (tool==Tool::Road) active += diagonal_roads ? " / diagonal" : " / L-shaped";
        if (tool==Tool::Ground) active += " / "+ground_texture;
        if (tool==Tool::Trees) active += " / "+std::to_string(brush_size*8)+" m / "+densities[density-1];
        if (tool==Tool::Vehicle) active += " / "+(vehicle_kind==0 && selected_car ? selected_car->name : std::string(vehicle_names[vehicle_kind]))+" / "+std::to_string(rotation*90);
        if (tool==Tool::Elevation) active += elevation_direction>0 ? " / +2 m" : " / -2 m";
        const auto title = fit(city.name+" / "+active+(dirty ? " *" : ""),18,document.width-100);
        ui::draw_window_title(document,title.c_str());
        ui::draw_window_close(document);

        text("CHOOSE BUILDING",20,86,16,accent);
        if (button({20,116,240,32},"Plain block 1 x 1",!building_mesh,true,can_edit,16)) { building_mesh.reset(); building_size = 1; building_height = 12; choose_tool(3); }
        const Rectangle building_list{20,160,240,float(GetScreenHeight()-244)}; const int rows = std::max(1,int(building_list.height/58));
        if (can_edit && hit(building_list)) library_scroll -= int(GetMouseWheelMove());
        library_scroll = std::clamp(library_scroll,0,std::max(0,int(designs.size())-rows));
        for (int row = 0; row<rows && library_scroll+row<int(designs.size()); ++row) {
            const auto& design = designs[library_scroll+row]; const auto footprint = design.footprint();
            const Rectangle r{20,160+row*58.f,240,52};
            const bool chosen = building_mesh && building_mesh->name==design.name;
            if (button(r,"",chosen,true,can_edit,16)) { building_mesh = design; choose_tool(3); }
            const Color color = chosen || (can_edit && hit(r)) ? background : ink;
            text(fit(design.name,16,216),r.x+10,r.y+7,16,color);
            text(TextFormat("%i x %i tiles / %.0f m",footprint[0],footprint[1],design.size.GetY()),r.x+10,r.y+31,13,color);
        }
        if (designs.empty()) {
            text("No saved buildings",20,174,15);
            text("Buildings > Creator",20,204,15);
        }
        DrawLine(8,GetScreenHeight()-54,GetScreenWidth()-8,GetScreenHeight()-54,border);
        const std::string clock = TextFormat("%02i:%02i",city.start_minutes/60,city.start_minutes%60);
        const std::string info = std::string(dirty ? "[UNSAVED] " : "[SAVED] ")+"Start "+clock+" | "+(menu_state.play ? "Player: "+city.player_character->name : !city.spawn ? "Set a spawn to play" : "Choose a Player character");
        text(fit(status,17,GetScreenWidth()-ui::measure_text(info.c_str(),16)-48.f),20,float(GetScreenHeight()-44),17,accent);
        text(info,GetScreenWidth()-ui::measure_text(info.c_str(),16)-20.f,float(GetScreenHeight()-43),16);
        menu_state.top = top; menu_state.grid = grid; menu_state.tool = int(tool);
        menu.draw(menu_state);
        text("CITY EDITOR",GetScreenWidth()-150.f,8,16,background);
        EndDrawing();
        if (car_request) {
            const int edited_index = command==MenuCommand::EditCar && menu_state.car_selection ? selected_vehicle : -1;
            const auto initial = edited_index>=0 ? city.vehicles[edited_index].car.value_or(CarDesign{}) : selected_car.value_or(CarDesign{});
            std::vector<CarDesign> saved;
            const auto edited = car_builder(initial,directory.parent_path()/"cars",{},&saved);
            City next=city; bool changed=false; std::string update_error;
            for (const auto& design : saved) {
                const int count=next.update_car_design(design,status,edited_index>=0 && (initial.name==design.name || (edited && edited->name==design.name)) ? edited_index : -1);
                if (count<0) update_error=status; else changed |= count>0;
                if (selected_car && selected_car->name==design.name) selected_car=design;
            }
            if (changed) commit(std::move(next));
            if (edited) { selected_car=*edited; choose_tool(5); vehicle_kind=0; }
            else if (edited_index>=0) selected_vehicle=edited_index;
            car_renderer.refresh();
            status = !update_error.empty() ? "Map update rejected: "+update_error : changed ? "Placed cars updated / Ctrl+Z to undo" : edited ? "Click the map to place "+edited->name : "Car editor closed";
            painting=dragging=false; first_frame=true; continue;
        }
        if (builder_request>=0) {
            const int edited_index = edit_building ? selected_building : -1;
            const BuildingMesh initial = edit_building ? city.buildings[edited_index].shape() : building_mesh.value_or(preview.shape());
            std::vector<BuildingMesh> saved;
            const auto edited = building_builder(initial,buildings_directory,{},0,&saved);
            City next = city; bool map_changed = false; std::string update_error;
            const auto update_design = [&](const BuildingMesh& design,int replace = -1) {
                const int changed = next.update_building_design(design,status,replace);
                if (changed<0) update_error = design.name+": "+status;
                else map_changed |= changed>0;
            };
            for (const auto& design : saved) {
                update_design(design,edited_index>=0 && initial.name==design.name ? edited_index : -1);
                if (building_mesh && building_mesh->name==design.name) building_mesh = design;
            }
            if (edited) {
                building_mesh = *edited;
                update_design(*edited,edited_index);
            }
            if (map_changed) commit(std::move(next));
            if (edited_index>=0) selected_building = edited_index;
            else if (edited) choose_tool(3);
            refresh_designs();
            if (!update_error.empty()) status = "Map update rejected: "+update_error;
            else if (map_changed) status = "Placed buildings updated / Ctrl+Z to undo";
            painting = dragging = false; first_frame = true; continue;
        }
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
    CharacterRenderer character_renderer;
    const auto characters_directory=directory.parent_path()/"characters";
    int selection = -1, scroll = 0, modal = 0; std::string name;
    int new_field = 0; bool generated_terrain = true;
    std::string terrain_x = "48", terrain_z = "48", terrain_seed;
    const auto new_city = [&]() {
        modal = 1; name = "New city"; new_field = 0; generated_terrain = true; status.clear();
        terrain_x = terrain_z = "48"; terrain_seed = std::to_string(GetRandomValue(1,2147483647));
    };
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
        std::string character_error;
        MenuState menu_state; menu_state.selection = has; menu_state.choose_character=has;
        menu_state.play = has && character_renderer.available(cities[selection],character_error);
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
        if (command==MenuCommand::CarEditor) { car_builder({},directory.parent_path()/"cars"); accept_input=false; continue; }
        if (command==MenuCommand::CharacterCreator) { character_builder({},characters_directory); character_renderer.refresh(); accept_input=false; continue; }
        if (command==MenuCommand::ChoosePlayerCharacter && has) {
            if (const auto chosen=choose_player_character(cities[selection].player_character,characters_directory)) {
                City next=cities[selection]; next.player_character=chosen;
                if (next.save(directory,status)) {cities[selection]=next; selected=next; status="Player character selected: "+chosen->name;}
            }
            character_renderer.refresh(); accept_input=false; continue;
        }
        if (command==MenuCommand::Controls || command==MenuCommand::About) { help_dialog(false,command==MenuCommand::About); continue; }
        if (command==MenuCommand::NewCity) new_city();
        if (command==MenuCommand::EditCity) { selected = cities[selection]; open_editor = true; continue; }
        if (command==MenuCommand::PlayCity) { selected = cities[selection]; return true; }
        if (command==MenuCommand::RenameCity) { modal = 2; name = cities[selection].name; }
        if (command==MenuCommand::DeleteCity) modal = 3;
        if (can_choose && !open_editor && !cities.empty()) {
            if (IsKeyPressed(KEY_DOWN)) selection = std::min(int(cities.size())-1,selection+1);
            if (IsKeyPressed(KEY_UP)) selection = std::max(0,selection-1);
            if (IsKeyPressed(KEY_UP)||IsKeyPressed(KEY_DOWN)) scroll = std::clamp(selection-rows+1,0,std::max(0,int(cities.size())-rows));
            if (IsKeyPressed(KEY_E)) { selected = cities[selection]; open_editor = true; continue; }
            if (IsKeyPressed(KEY_ENTER)) {
                if (menu_state.play) { selected = cities[selection]; return true; }
                status=character_error;
            }
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
            if (button({bx,by,280,44},"+ New city",true,true,can_choose)||(can_choose && IsKeyPressed(KEY_INSERT))) new_city();
            if (button({bx,by+64*spacing,280,44},"Edit / build",false,has,can_choose)) { selected = cities[selection]; open_editor = true; }
            if (button({bx,by+124*spacing,280,44},"Play city",true,menu_state.play,can_choose)) { selected = cities[selection]; EndDrawing(); return true; }
            if (button({bx,by+194*spacing,280,40},"Rename",false,has,can_choose)) { modal = 2; name = cities[selection].name; }
            if (button({bx,by+248*spacing,280,40},"Delete city",false,has,can_choose)) modal = 3;
            if (button({bx,by+318*spacing,280,40},"Quit",false,true,can_choose)) { EndDrawing(); return false; }
            if (button({bx,by+374*spacing,280,40},"Choose player character",false,has,can_choose,16)) {
                EndDrawing();
                if (const auto chosen=choose_player_character(cities[selection].player_character,characters_directory)) {
                    City next=cities[selection]; next.player_character=chosen;
                    if (next.save(directory,status)) {cities[selection]=next; selected=next; status="Player character selected: "+chosen->name;}
                }
                character_renderer.refresh(); accept_input=false; continue;
            }
        } else {
            button({bx,by,280,44},"+ New city",true,false);
            button({bx,by+64*spacing,280,44},"Edit / build",false,false);
            button({bx,by+124*spacing,280,44},"Play city",true,false);
            button({bx,by+194*spacing,280,40},"Rename",false,false);
            button({bx,by+248*spacing,280,40},"Delete city",false,false);
        }
        text(fit(status.empty() && has && !menu_state.play ? character_error : status,16,940),x,float(GetScreenHeight()-42),16,accent);
        if (modal) {
            const bool modal_input = modal == previous_modal;
            ui::draw_desktop();
            const float modal_height = modal==1 ? 444.f : 236.f;
            Rectangle r{(GetScreenWidth()-620)/2.f,(GetScreenHeight()-modal_height)/2.f,620,modal_height};
            ui::draw_window(r,modal==1 ? "Create a city" : modal==2 ? "Rename city" : "Delete this city?");
            ui::draw_window_close(r);
            if (modal==1) {
                if (modal_input && IsKeyPressed(KEY_TAB)) new_field = (new_field+(IsKeyDown(KEY_LEFT_SHIFT) ? 3 : 1))%(generated_terrain ? 4 : 1);
                const auto entry = [&](std::string& value,Rectangle box,int id,bool enabled = true) {
                    if (enabled && modal_input && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hit(box)) new_field = id;
                    if (enabled && new_field==id && modal_input) input(value,box);
                    else { DrawRectangleLinesEx(box,1,enabled ? border : ui::dos_light_blue); text(fit(value,21,box.width-24),box.x+12,box.y+(box.height-21)/2,21,enabled ? ink : ui::dos_light_blue); }
                };
                text("Name",r.x+24,r.y+48,16); entry(name,{r.x+24,r.y+72,572,44},0);
                if (button({r.x+24,r.y+132,278,30},"Perlin terrain",generated_terrain,true,modal_input,16)) generated_terrain = true;
                if (button({r.x+318,r.y+132,278,30},"Empty water",!generated_terrain,true,modal_input,16)) { generated_terrain = false; new_field = 0; }
                const auto dimension = [&](const char* label,std::string& value,float x,int id) {
                    text(label,x,r.y+190,16); entry(value,{x,r.y+218,170,36},id,generated_terrain);
                    for (int direction : {-1,1}) if (button({x+(direction<0 ? 180.f : 220.f),r.y+218,32,36},direction<0 ? "-" : "+",false,generated_terrain,modal_input,16)) {
                        int count = 48; try { count = std::stoi(value); } catch (const std::exception&) {}
                        value = std::to_string(std::clamp(std::clamp(count,1,City::width)+direction,1,City::width)); new_field = id;
                    }
                };
                dimension("Starter tiles X",terrain_x,r.x+24,1); dimension("Starter tiles Z",terrain_z,r.x+318,2);
                text("Seed",r.x+24,r.y+286,17); entry(terrain_seed,{r.x+120,r.y+278,322,36},3,generated_terrain);
                if (button({r.x+452,r.y+278,144,36},"New seed",false,generated_terrain,modal_input,16)) terrain_seed = std::to_string(GetRandomValue(1,2147483647));
                text(generated_terrain ? "1-128 tiles per axis / centered in the 128 x 128 map" : "Start with the original empty water map",r.x+24,r.y+334,15);
            } else if (modal==2) input(name,{r.x+24,r.y+72,572,52});
            else { text(fit(cities[selection].name,22,r.width-48),r.x+24,r.y+76,22,accent); text("This permanently removes its saved map.",r.x+24,r.y+111,17,muted); }
            const float footer = modal==1 ? 386.f : 162.f;
            text(fit(status,14,r.width-48),r.x+24,r.y+footer-26,14,accent);
            const bool accept = button({r.x+24,r.y+footer,278,44},modal==3 ? "Delete" : modal==1 ? "Create city" : "Save",true,true,modal_input)||(modal_input && IsKeyPressed(KEY_ENTER));
            if (close_clicked(r,modal_input) || button({r.x+318,r.y+footer,278,44},"Cancel",false,true,modal_input)||(modal_input && IsKeyPressed(KEY_ESCAPE))) modal = 0;
            else if (accept) {
                if (modal==3) {
                    std::error_code ec; const bool removed = std::filesystem::remove(directory/(cities[selection].id+".city"),ec);
                    if (removed && !ec) { status = "City deleted"; modal = 0; refresh(); } else status = "Could not delete the city";
                } else {
                    City next = modal==1 ? City::create(name) : cities[selection]; next.name = name;
                    bool generated = true;
                    if (modal==1 && generated_terrain) {
                        try {
                            for (const auto& value : {terrain_x,terrain_z,terrain_seed})
                                if (value.empty() || value.find_first_not_of("0123456789")!=std::string::npos) throw std::out_of_range("terrain");
                            const auto seed = std::stoull(terrain_seed);
                            if (seed>std::numeric_limits<std::uint32_t>::max()) throw std::out_of_range("seed");
                            generated = next.generate_terrain(std::stoi(terrain_x),std::stoi(terrain_z),std::uint32_t(seed),status);
                        } catch (const std::exception&) { generated = false; status = "Use 1-128 tiles and a seed from 0 to 4294967295."; }
                    }
                    if (generated && next.save(directory,status)) {
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
