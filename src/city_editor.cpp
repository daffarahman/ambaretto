#include "city.hpp"
#include "race.hpp"
#include "saved_copy.hpp"
#include "saved_delete.hpp"
#include "ui_font.hpp"
#include "menu_bar.hpp"
#include "graphics_panel.hpp"
#include "plane.hpp"
#include "car_renderer.hpp"
#include "character_renderer.hpp"
#include "tuning_panel.hpp"
#include "environment_renderer.hpp"
#include "building_renderer.hpp"
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <functional>

namespace ambaretto {
namespace {
constexpr Color background = ui::face, border = ui::shadow;
constexpr Color ink = ui::ink, muted = ui::disabled, accent = ui::title_blue, scene_accent = YELLOW;
constexpr int sidebar = 108;
enum class Tool { Select, Land, Road, Building, Spawn, Vehicle, Bulldoze, Trees, Ground, Water, Pan, Race };
const char* tools[] = {"Select / edit", "Land pen", "Bezier road", "Object / building", "Player spawn", "Vehicle", "Bulldoze", "Trees", "Texture pen", "Water pen", "Hand / pan", "Race checkpoints"};
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
bool duplicate_pressed() { return (IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_D); }
bool close_clicked(Rectangle r, bool interactive = true) { return interactive && hit(ui::window_close(r)) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }
bool button(Rectangle r, const std::string& label, bool selected = false, bool enabled = true, bool interactive = true, int size = 18) {
    const bool hover = interactive && enabled && hit(r);
    const bool pressed = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    ui::draw_button(r,pressed,selected,enabled);
    text(fit(label,size,r.width-20),r.x+10+pressed,r.y+(r.height-size)/2+pressed,size,enabled ? ink : muted);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
bool list_item(Rectangle r, const std::string& label, bool selected, bool enabled = true, bool interactive = true, int size = 18) {
    return button(r,label,selected,enabled,interactive,size);
}
void input(std::string& value, Rectangle r) {
    if ((IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_A)) value.clear();
    ui::draw_panel(r,true,ui::highlight);
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
struct EditorControl { MenuCommand command; const char* label; std::array<unsigned short,16> icon; };
const EditorControl editor_controls[] = {
    {MenuCommand::SelectTool,"Select / edit (1)",{0x0000,0x4000,0x5000,0x4800,0x4400,0x4200,0x4100,0x4080,0x4040,0x43e0,0x4a00,0x5200,0x4200,0x0100,0x0000,0x0000}},
    {MenuCommand::PanView,"Hand: click to center or drag (H)",{0x0000,0x06c0,0x36d8,0x36d8,0x36d8,0x36d8,0x3018,0x3008,0x3008,0x3008,0x2008,0x2010,0x1010,0x1020,0x0fc0,0x0000}},
    {MenuCommand::RotateLeft,"Rotate camera left (Q)",{0x0000,0x03e0,0x0410,0x0808,0x1004,0x7004,0x3004,0x7004,0x0004,0x0004,0x0008,0x0010,0x07e0,0x0000,0x0000,0x0000}},
    {MenuCommand::RotateRight,"Rotate camera right (E)",{0x0000,0x07c0,0x0820,0x1010,0x2008,0x200e,0x200c,0x200e,0x2000,0x2000,0x1000,0x0800,0x07e0,0x0000,0x0000,0x0000}},
    {MenuCommand::ZoomIn,"Zoom in",{0x0000,0x0f80,0x1040,0x2020,0x4210,0x4210,0x4f90,0x4210,0x4210,0x2020,0x1060,0x0fe0,0x0018,0x000c,0x0006,0x0000}},
    {MenuCommand::ZoomOut,"Zoom out",{0x0000,0x0f80,0x1040,0x2020,0x4010,0x4010,0x4f90,0x4010,0x4010,0x2020,0x1060,0x0fe0,0x0018,0x000c,0x0006,0x0000}},
    {MenuCommand::TopView,"Top view (V)",{0x0000,0x03e0,0x0410,0x0808,0x1004,0x3ffc,0x2244,0x2244,0x3ffc,0x2244,0x2244,0x3ffc,0x2244,0x2244,0x3ffc,0x0000}},
    {MenuCommand::Grid,"Show grid (G)",{0x0000,0x3ffc,0x2244,0x2244,0x2244,0x3ffc,0x2244,0x2244,0x3ffc,0x2244,0x2244,0x3ffc,0x2244,0x2244,0x3ffc,0x0000}},
    {MenuCommand::BulldozeTool,"Bulldoze: click an object or tile (7)",{0x0000,0x00f8,0x0108,0x0208,0x3c08,0x2008,0x200f,0x4001,0x7fff,0x1ff9,0x300d,0x2495,0x300d,0x1ffb,0x0000,0x0000}},
    {MenuCommand::DeleteSelection,"Delete selected object (Del)",{0x0000,0x01c0,0x0220,0x3ffe,0x01c0,0x1ffc,0x1004,0x1494,0x1494,0x1494,0x1494,0x1494,0x1494,0x1004,0x0ff8,0x0000}},
    {MenuCommand::Undo,"Undo (Ctrl+Z)",{0x0000,0x0400,0x0c00,0x1c00,0x3e00,0x7f00,0x3ee0,0x1c10,0x0c08,0x0404,0x0004,0x0004,0x0008,0x01f0,0x0000,0x0000}},
    {MenuCommand::Redo,"Redo (Ctrl+Y)",{0x0000,0x0020,0x0030,0x0038,0x007c,0x00fe,0x0e7c,0x1038,0x2030,0x4020,0x4000,0x4000,0x2000,0x1f00,0x0000,0x0000}},
    {MenuCommand::SaveCity,"Save city (Ctrl+S)",{0x0000,0x7ffe,0x4812,0x4812,0x4852,0x4852,0x4ff2,0x4002,0x4ff2,0x4812,0x4812,0x4812,0x4812,0x4ff2,0x7ffe,0x0000}},
    {MenuCommand::PlayCity,"Play city",{0x0000,0x1000,0x1800,0x1c00,0x1e00,0x1f00,0x1f80,0x1fc0,0x1fe0,0x1fc0,0x1f80,0x1f00,0x1e00,0x1c00,0x1800,0x1000}}
};
Rectangle editor_control_rect(int index) {
    const int row=index/2;
    return {float(16+(index%2)*44),float(56+row*44+(row>=4 ? 8 : 0)+(row>=6 ? 8 : 0)),40,40};
}
bool editor_control_enabled(MenuCommand command,const MenuState& state) {
    if (command==MenuCommand::Undo) return state.undo;
    if (command==MenuCommand::Redo) return state.redo;
    if (command==MenuCommand::DeleteSelection) return state.selection;
    if (command==MenuCommand::PlayCity) return state.play;
    return true;
}
void draw_editor_controls(const MenuState& state,bool interactive) {
    ui::draw_panel({8,44,100,float(GetScreenHeight()-108)});
    for (int i=0;i<int(std::size(editor_controls));++i) {
        const auto& control=editor_controls[i]; const auto r=editor_control_rect(i);
        const bool enabled=editor_control_enabled(control.command,state);
        const bool selected=(control.command==MenuCommand::SelectTool && state.tool==int(Tool::Select))
            || (control.command==MenuCommand::PanView && state.tool==int(Tool::Pan))
            || (control.command==MenuCommand::BulldozeTool && state.tool==int(Tool::Bulldoze))
            || (control.command==MenuCommand::TopView && state.top) || (control.command==MenuCommand::Grid && state.grid);
        const bool pressed=interactive && enabled && hit(r) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        ui::draw_button(r,pressed||selected,false,enabled);
        const Color color=!enabled ? muted : control.command==MenuCommand::PlayCity ? Color{0,100,0,255}
            : control.command==MenuCommand::SaveCity ? accent : control.command==MenuCommand::DeleteSelection ? MAROON : ink;
        for (int y=0;y<16;++y) for (int x=0;x<16;++x) if (control.icon[y] & (1u<<(15-x)))
            DrawRectangle(int(r.x)+4+x*2+pressed,int(r.y)+4+y*2+pressed,2,2,color);
    }
}

template<class T> std::string snapshot(const T& design) { std::ostringstream file; design.write(file); return file.str(); }
class SavedPreview {
public:
    SavedPreview() { graphics_.apply_preset(GraphicsPreset::Low); graphics_.view_distance=6000; day_.set_time("09:00"); }
    ~SavedPreview() { if (texture_.id) UnloadRenderTexture(texture_); }
    void invalidate() { selected_=nullptr; dirty_=true; }
    void forget(const std::string& name) {
        invalidate();
        building_cache_.erase(std::remove_if(building_cache_.begin(),building_cache_.end(),[&](const auto& entry){return entry.name==name;}),building_cache_.end());
    }
    void refresh() {
        invalidate(); buildings_=nullptr; building_cache_.clear();
        if (cars_) cars_->refresh();
        if (characters_) characters_->refresh();
    }
    template<class Design> void render(const Design* design,Rectangle view,bool animate) {
        const float resolution=std::min(1.f,720/std::max(view.width,view.height));
        const int width=std::max(1,int(view.width*resolution)),height=std::max(1,int(view.height*resolution));
        const bool resized=texture_.texture.width!=width || texture_.texture.height!=height;
        if (resized) {
            if (texture_.id) UnloadRenderTexture(texture_);
            texture_=LoadRenderTexture(width,height);
            SetTextureFilter(texture_.texture,TEXTURE_FILTER_BILINEAR);
        }
        const bool changed=dirty_ || selected_!=design;
        if (changed) {
            selected_=design; dirty_=false; error_.clear(); valid_=false;
            // shortcut: cold assets still upload on the render thread; split loading if large imports stall.
            if (design) prepare(*design);
        }
        if (!changed && !resized && (!animate || !design || !valid_)) return;
        if (animate) yaw_=std::fmod(yaw_+std::min(GetFrameTime(),.1f)*.5f,2*PI);
        const float diameter=std::hypot(size_.GetX(),size_.GetZ()), scale=std::max(size_.GetY(),diameter);
        const float framing=std::max(size_.GetY()+diameter*.56f,diameter*view.height/view.width)*1.2f;
        Camera3D camera{vector(center_+Vec3(std::sin(yaw_)*scale*1.5f,scale,std::cos(yaw_)*scale*1.5f)),vector(center_),{0,1,0},framing,CAMERA_ORTHOGRAPHIC};
        BeginTextureMode(texture_); ClearBackground({18,30,49,255});
        if (design && valid_) {
            if (!lighting_) lighting_=std::make_unique<SceneLighting>();
            BeginMode3D(camera); scene(*design,camera); EndMode3D();
        }
        EndTextureMode();
    }
    void draw(Rectangle view) const {
        DrawTexturePro(texture_.texture,{0,0,float(texture_.texture.width),-float(texture_.texture.height)},view,{0,0},0,WHITE);
        ui::draw_frame(view,true);
        if (!selected_ || !valid_) text(fit(selected_ ? error_ : "No saved items yet",15,view.width-24),view.x+12,view.y+view.height/2,15,RAYWHITE);
    }
    bool valid() const { return selected_ && valid_; }
    const std::string& error() const { return error_; }
private:
    void bounds(const std::vector<Vec3>& points) {
        Vec3 lo=points.front(),hi=lo;
        for (const auto& p : points) { lo=Vec3::sMin(lo,p); hi=Vec3::sMax(hi,p); }
        center_=(lo+hi)/2; size_=Vec3::sMax(hi-lo,Vec3(1,1,1));
    }
    void prepare(const CarDesign& design) {
        if (!cars_) cars_=std::make_unique<CarRenderer>();
        valid_=cars_->available(design,error_);
        const auto& t=design.tuning;
        const float ride=t.rest_length+t.wheel_radius-t.mount_height;
        bounds({{std::min(design.offset[0]-design.width/2,-t.track_width/2-t.wheel_radius),std::min(0.f,ride-.15f+design.offset[1]),std::min(design.offset[2]-design.length/2,-t.wheelbase/2-t.wheel_radius)},
            {std::max(design.offset[0]+design.width/2,t.track_width/2+t.wheel_radius),std::max(2*t.wheel_radius,ride-.15f+design.offset[1]+design.height),std::max(design.offset[2]+design.length/2,t.wheelbase/2+t.wheel_radius)}});
    }
    void prepare(const CharacterDesign& design) {
        if (!characters_) {
            characters_=std::make_unique<CharacterRenderer>(); world_=std::make_unique<PhysicsWorld>(false);
            character_=std::make_unique<Character>(*world_); character_->reset({0,.08f,0});
        }
        valid_=characters_->available(design,error_);
        std::vector<Vec3> points;
        const auto pose=character_->body_parts();
        for (std::size_t i=0;i<pose.size();++i) {
            const auto& part=design.parts[character_body_slots[i]];
            const auto size=pose[i].size*Vec3(part.scale[0],part.scale[1],part.scale[2]);
            const auto offset=Vec3(character_body_mirrored[i] ? -part.offset[0] : part.offset[0],part.offset[1],part.offset[2]);
            const auto center=pose[i].position+pose[i].rotation*offset;
            points.push_back(center-size.Length()/2*Vec3(1,1,1)); points.push_back(center+size.Length()/2*Vec3(1,1,1));
        }
        bounds(points);
    }
    void prepare(const BuildingMesh& design) {
        auto found=std::find_if(building_cache_.begin(),building_cache_.end(),[&](const auto& entry){return entry.name==design.name;});
        if (found!=building_cache_.end()) {
            std::rotate(building_cache_.begin(),found,found+1);
        } else {
            CachedBuilding entry; entry.name=design.name; entry.renderer=std::make_unique<BuildingRenderer>();
            entry.valid=entry.renderer->build(design.triangles(),entry.error,false);
            bounds(design.points()); entry.center=center_; entry.size=size_;
            building_cache_.insert(building_cache_.begin(),std::move(entry));
            std::size_t bytes=0;
            for (const auto& cached : building_cache_) bytes+=cached.renderer->stats().gpu_geometry_bytes+cached.renderer->stats().gpu_texture_bytes;
            while (building_cache_.size()>4 || bytes>32*1024*1024) {
                const auto& stats=building_cache_.back().renderer->stats(); bytes-=stats.gpu_geometry_bytes+stats.gpu_texture_bytes;
                building_cache_.pop_back();
            }
        }
        const auto& entry=building_cache_.front();
        buildings_=entry.renderer.get(); valid_=entry.valid; error_=entry.error; center_=entry.center; size_=entry.size;
    }
    void scene(const CarDesign& design,const Camera3D& camera) {
        cars_->set_lighting(*lighting_,camera,day_.lighting(),graphics_);
        DrawCylinder({center_.GetX(),center_.GetY()-size_.GetY()/2-.08f,center_.GetZ()},size_.Length()*.55f,size_.Length()*.55f,.06f,48,{38,52,68,255});
        cars_->draw_design(design,Vec3::sZero(),0,camera);
    }
    void scene(const CharacterDesign& design,const Camera3D& camera) {
        characters_->set_lighting(*lighting_,camera,day_.lighting(),graphics_);
        DrawPlane({0,0,0},{4,4},{38,52,68,255}); characters_->draw(*character_,camera,{},false,-1,&design);
    }
    void scene(const BuildingMesh&,const Camera3D& camera) {
        DrawPlane({center_.GetX(),-.04f,center_.GetZ()},{size_.GetX()*1.2f,size_.GetZ()*1.2f},{38,52,68,255});
        buildings_->draw(camera,day_.lighting(),*lighting_,graphics_);
    }
    RenderTexture2D texture_{};
    const void* selected_=nullptr;
    bool valid_=false,dirty_=true;
    float yaw_=2.44f;
    Vec3 center_=Vec3::sZero(),size_=Vec3(1,1,1);
    std::string error_;
    GraphicsSettings graphics_; DayNight day_;
    std::unique_ptr<SceneLighting> lighting_;
    std::unique_ptr<CarRenderer> cars_;
    std::unique_ptr<CharacterRenderer> characters_;
    std::unique_ptr<PhysicsWorld> world_;
    std::unique_ptr<Character> character_;
    struct CachedBuilding {
        std::string name,error;
        Vec3 center=Vec3::sZero(),size=Vec3(1,1,1);
        bool valid=false;
        std::unique_ptr<BuildingRenderer> renderer;
    };
    std::vector<CachedBuilding> building_cache_;
    BuildingRenderer* buildings_=nullptr;
};
void cycle_saved(int& selection,int count,bool active,Rectangle view) {
    if (!active || count<2) return;
    const bool click=IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const int direction=int(IsKeyPressed(KEY_RIGHT)||IsKeyPressed(KEY_DOWN)||(click && hit({view.x+view.width-140,view.y+view.height+40,140,30})))
        -int(IsKeyPressed(KEY_LEFT)||IsKeyPressed(KEY_UP)||(click && hit({view.x,view.y+view.height+40,140,30})));
    if (direction) selection=(selection+direction+count)%count;
}
template<class Design> void draw_saved_carousel(SavedPreview& preview,const std::vector<Design>& library,int selection,Rectangle view,bool active) {
    preview.draw(view);
    std::string name=selection>=0 ? library[selection].name : "No saved items yet";
    if constexpr (std::is_same_v<Design,BuildingMesh>) if (selection>=0) name+=library[selection].type==ObjectType::Object ? " / Object" : " / Building";
    const auto label=fit(name,18,view.width-20);
    text(label,view.x+(view.width-ui::measure_text(label.c_str(),18))/2,view.y+view.height+8,18);
    button({view.x,view.y+view.height+40,140,30},"< Previous",false,library.size()>1,active,16);
    button({view.x+view.width-140,view.y+view.height+40,140,30},"Next >",false,library.size()>1,active,16);
    const auto count=selection>=0 ? std::to_string(selection+1)+" / "+std::to_string(library.size()) : "0 / 0";
    text(count,view.x+(view.width-ui::measure_text(count.c_str(),15))/2,view.y+view.height+47,15,muted);
}
void draw_city_list(const std::vector<City>& cities,int& selection,int& scroll,Rectangle view,bool active) {
    const int rows=std::max(1,int((view.height-8)/36)),count=int(cities.size());
    if (active && count) {
        const int direction=int(IsKeyPressed(KEY_DOWN)||IsKeyPressed(KEY_RIGHT))-int(IsKeyPressed(KEY_UP)||IsKeyPressed(KEY_LEFT));
        if (direction) selection=(selection+direction+count)%count;
        if (IsKeyPressed(KEY_HOME)) selection=0;
        if (IsKeyPressed(KEY_END)) selection=count-1;
        if (IsKeyPressed(KEY_PAGE_DOWN)) selection=std::min(count-1,selection+rows);
        if (IsKeyPressed(KEY_PAGE_UP)) selection=std::max(0,selection-rows);
        if (direction || IsKeyPressed(KEY_HOME) || IsKeyPressed(KEY_END) || IsKeyPressed(KEY_PAGE_DOWN) || IsKeyPressed(KEY_PAGE_UP)) {
            if (selection<scroll) scroll=selection;
            if (selection>=scroll+rows) scroll=selection-rows+1;
        }
        if (hit(view)) scroll-=int(GetMouseWheelMove()*3);
    }
    scroll=std::clamp(scroll,0,std::max(0,count-rows));
    ui::draw_panel(view,true,ui::highlight);
    for (int i=scroll;i<std::min(count,scroll+rows);++i)
        if (list_item({view.x+4,view.y+4+(i-scroll)*36.f,view.width-8,32},cities[i].name,i==selection,true,active)) selection=i;
    if (!count) text("No saved cities yet",view.x+12,view.y+12,17,muted);
    text(count ? std::to_string(selection+1)+" / "+std::to_string(count)+" cities" : "0 cities",view.x+4,view.y+view.height+12,15,muted);
}
template<class T, class Save> bool leave_draft(const T& design, const std::string& original, Save save_design, std::string& status) {
    if (snapshot(design)==original) return true;
    bool interactive=false;
    while (true) {
        const bool native_close=WindowShouldClose();
        BeginDrawing(); ui::draw_desktop();
        const Rectangle r{(GetScreenWidth()-720)/2.f,(GetScreenHeight()-274)/2.f,720,274};
        ui::draw_window(r,"Unsaved changes"); ui::draw_window_close(r);
        text("Save changes to "+fit(design.name,18,430)+"?",r.x+24,r.y+64,18);
        text("Save your changes before leaving this editor.",r.x+24,r.y+103,17);
        text(fit(status,16,672),r.x+24,r.y+146,16,accent);
        const bool saving=button({r.x+24,r.y+210,208,40},"Save",true,true,interactive);
        const bool discard=button({r.x+256,r.y+210,208,40},"Discard",false,true,interactive);
        const bool cancel=native_close || close_clicked(r,interactive) || button({r.x+488,r.y+210,208,40},"Cancel",false,true,interactive) || (interactive && IsKeyPressed(KEY_ESCAPE));
        EndDrawing(); interactive=true;
        if (cancel) {return false;}
        if (discard || (saving && save_design())) return true;
    }
}
template<class Design> bool delete_selected_saved(std::vector<Design>& library,int& selection,
    const std::filesystem::path& directory,std::string& status) {
    if (selection<0 || selection>=int(library.size())) return false;
    bool interactive=false,confirm=false;
    while (!WindowShouldClose()) {
        const Rectangle r{(GetScreenWidth()-720)/2.f,(GetScreenHeight()-274)/2.f,720,274};
        const bool active=interactive && IsWindowFocused();
        if (active) {
            if (IsKeyPressed(KEY_LEFT)) confirm=true;
            if (IsKeyPressed(KEY_RIGHT)) confirm=false;
            if (IsKeyPressed(KEY_TAB)) confirm=!confirm;
        }
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(r,"Delete saved item?"); ui::draw_window_close(r);
        text("Delete "+fit(library[selection].name,18,540)+"?",r.x+24,r.y+64,18);
        text(std::is_same_v<Design,City> ? "The city and its play progress will leave the saved list."
            : "Copies already placed in cities will stay there.",r.x+24,r.y+103,17);
        if constexpr (std::is_same_v<Design,CarDesign> || std::is_same_v<Design,CharacterDesign>)
            text("Traffic, NPCs and police use the remaining saved designs.",r.x+24,r.y+132,16);
        text("Files will be kept in the saved folder's deleted subfolder.",r.x+24,r.y+163,16,accent);
        text("Left/Right or Tab: choose / Enter: select / Esc: cancel",r.x+24,r.y+187,13);
        const bool deleting=button({r.x+24,r.y+210,208,40},"Delete",confirm,true,active)
            || (active && confirm && IsKeyPressed(KEY_ENTER));
        const bool cancel=close_clicked(r,active) || button({r.x+488,r.y+210,208,40},"Cancel",!confirm,true,active)
            || (active && (IsKeyPressed(KEY_ESCAPE) || (!confirm && IsKeyPressed(KEY_ENTER))));
        EndDrawing(); interactive=true;
        if (cancel) return false;
        if (deleting) {
            if (!delete_saved(library[selection],directory,status)) return false;
            library.erase(library.begin()+selection);
            selection=library.empty() ? -1 : std::min(selection,int(library.size())-1);
            return true;
        }
    }
    return false;
}
template<class Load> auto saved_picker(Load load,const char* title,const char* action,const std::string& current,std::string& status,
    const std::filesystem::path& directory={},std::function<bool(const typename decltype(load())::value_type&,std::string&)> ready={},
    std::function<void()> create={},bool require_preview=false)
    -> std::optional<typename decltype(load())::value_type> {
    using Design=typename decltype(load())::value_type;
    status.clear(); auto library=load();
    int selection=library.empty() ? -1 : 0; bool interactive=false; SavedPreview preview;
    for (int i=0;i<int(library.size());++i) if (library[i].name==current) selection=i;
    int scroll=std::max(0,selection);
    auto capture=LoadImageFromScreen(); const auto backdrop=LoadTextureFromImage(capture); UnloadImage(capture);
    std::optional<Design> result;
    while (!WindowShouldClose()) {
        const Rectangle r{(GetScreenWidth()-720)/2.f,(GetScreenHeight()-480)/2.f,720,480};
        const Rectangle view{r.x+24,r.y+92,r.width-48,r.height-274};
        const bool active=interactive && IsWindowFocused();
        if constexpr (!std::is_same_v<Design,City>) {
            cycle_saved(selection,int(library.size()),active,view);
            preview.render(selection>=0 ? &library[selection] : nullptr,view,active);
        }
        BeginDrawing();
        DrawTexturePro(backdrop,{0,0,float(backdrop.width),float(backdrop.height)},{0,0,float(GetScreenWidth()),float(GetScreenHeight())},{0,0},0,WHITE);
        DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),{0,0,0,150});
        ui::draw_window(r,title); ui::draw_window_close(r);
        text(std::is_same_v<Design,City> ? "Up / Down: select   Wheel: scroll   Enter: choose   Esc: back" : "Left / Right: cycle   Enter: choose   Esc: back",r.x+24,r.y+54,create || std::is_same_v<Design,City> ? 13 : 16,accent);
        if constexpr (std::is_same_v<Design,City>) draw_city_list(library,selection,scroll,view,active);
        else draw_saved_carousel(preview,library,selection,view,active);
        std::string reason; const bool available=selection>=0 && (!ready || ready(library[selection],reason)) && (!require_preview || preview.valid());
        if (require_preview && !preview.valid()) reason=preview.error();
        text(fit(reason.empty() ? status : reason,15,672),r.x+24,r.y+r.height-99,15,accent);
        const bool copies=!directory.empty();
        const auto open_label=copies ? (std::string(action).find("Open")==0 ? "Open" : "Choose") : action;
        const bool open=button({r.x+24,r.y+r.height-62,copies ? 124.f : 208.f,38},open_label,true,available,active,15) || (active && IsKeyPressed(KEY_ENTER));
        const bool create_new=create && button({r.x+472,r.y+48,224,30},std::is_same_v<Design,CarDesign> ? "Car editor" : "Character creator",false,true,active,15);
        const bool duplicate=copies && (button({r.x+161,r.y+r.height-62,124,38},"Duplicate",false,selection>=0,active,15) || (active && selection>=0 && duplicate_pressed()));
        const bool deleting=copies && (button({r.x+298,r.y+r.height-62,124,38},"Delete",false,selection>=0,active,15) || (active && selection>=0 && IsKeyPressed(KEY_DELETE)));
        const bool refresh=button({r.x+(copies ? 435.f : 256.f),r.y+r.height-62,copies ? 124.f : 208.f,38},copies ? "Refresh" : "Refresh saved",false,true,active,15);
        const bool cancel=close_clicked(r,active) || button({r.x+(copies ? 572.f : 488.f),r.y+r.height-62,copies ? 124.f : 208.f,38},"Cancel",false,true,active,15) || (active && IsKeyPressed(KEY_ESCAPE));
        EndDrawing(); interactive=true;
        if (cancel) break;
        if (open && available) {result=library[selection]; break;}
        if (deleting) {
            const auto name=library[selection].name;
            if (delete_selected_saved(library,selection,directory,status)) preview.forget(name);
            interactive=false; continue;
        }
        if (duplicate) if (auto copy=duplicate_saved(library[selection],directory,library,status)) {
            library.push_back(std::move(*copy)); selection=int(library.size())-1; preview.invalidate(); scroll=selection;
        }
        if (create_new) {create(); interactive=false;}
        if (refresh || create_new) {
            const auto name=selection>=0 ? library[selection].name : "";
            status.clear(); library=load(); selection=library.empty() ? -1 : 0;
            for (int i=0;i<int(library.size());++i) if (library[i].name==name) selection=i;
            preview.refresh(); scroll=std::max(0,selection);
        }
    }
    UnloadTexture(backdrop); return result;
}
std::vector<City> saved_cities(const std::filesystem::path& directory,std::string& status) {
    std::vector<City> cities; std::error_code ec;
    if (!std::filesystem::exists(directory,ec) && !ec) return cities;
    for (std::filesystem::directory_iterator it(directory,ec),end; !ec && it!=end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension()!=".city") continue;
        City city; std::string error;
        if (City::load(it->path(),city,error)) cities.push_back(std::move(city));
        else status="Cannot load "+it->path().filename().string()+": "+error;
    }
    if (ec) status="Cannot open the cities folder: "+ec.message();
    std::sort(cities.begin(),cities.end(),[](const City& a,const City& b){return a.name<b.name;});
    return cities;
}
template<class Load, class Builder> void open_design_menu(Load load, const char* title, const std::filesystem::path& directory, std::string& status, const std::string& screenshot, Builder builder) {
    auto library=load(); using Design=typename decltype(library)::value_type;
    int selection=library.empty() ? -1 : 0, frames=0; bool interactive=false; SavedPreview preview;
    while (!WindowShouldClose()) {
        const Rectangle r{(GetScreenWidth()-880)/2.f,70,880,float(GetScreenHeight()-140)};
        const Rectangle view{r.x+24,r.y+104,520,r.height-250};
        const bool active=interactive && IsWindowFocused();
        cycle_saved(selection,int(library.size()),active,view);
        preview.render(selection>=0 ? &library[selection] : nullptr,view,active);
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(r,title); ui::draw_window_close(r);
        text("Start with a new design or open a saved one.",r.x+24,r.y+63,20,accent);
        draw_saved_carousel(preview,library,selection,view,active);
        const bool create=button({r.x+576,r.y+128,280,46},"Create new",true,true,interactive) || (interactive && IsKeyPressed(KEY_INSERT));
        const bool open=button({r.x+576,r.y+192,280,46},"Open / edit selected",false,selection>=0,interactive) || (interactive && selection>=0 && IsKeyPressed(KEY_ENTER));
        const bool duplicate=button({r.x+576,r.y+256,134,40},"Duplicate",false,selection>=0,interactive,16) || (interactive && selection>=0 && duplicate_pressed());
        const bool deleting=button({r.x+722,r.y+256,134,40},"Delete",false,selection>=0,interactive) || (interactive && selection>=0 && IsKeyPressed(KEY_DELETE));
        const bool refresh=button({r.x+576,r.y+312,280,40},"Refresh saved",false,true,interactive);
        const bool close=close_clicked(r,interactive) || button({r.x+576,r.y+r.height-64,280,40},"Back",false,true,interactive) || (interactive && IsKeyPressed(KEY_ESCAPE));
        text(fit(status.empty() ? "Ctrl+D: duplicate / Del: delete selected" : status,15,540),r.x+24,r.y+r.height-47,15,accent);
        EndDrawing(); interactive=true;
        if (!screenshot.empty() && ++frames>=3) {auto image=LoadImageFromScreen(); ExportImage(image,screenshot.c_str()); UnloadImage(image); break;}
        if (close) break;
        if (deleting) {
            const auto name=library[selection].name;
            if (delete_selected_saved(library,selection,directory,status)) preview.forget(name);
            interactive=false; continue;
        }
        if (duplicate) if (auto copy=duplicate_saved(library[selection],directory,library,status)) {
            library.push_back(std::move(*copy)); selection=int(library.size())-1; preview.invalidate();
        }
        if (refresh) {library=load(); selection=library.empty() ? -1 : 0; preview.refresh();}
        if (create) {
            Design design;
            if constexpr (std::is_same_v<Design,BuildingMesh>) {design.name="New object";design.type=ObjectType::Object;}
            builder(std::move(design)); break;
        }
        if (open) {builder(library[selection]); break;}
    }
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
                editing ? "H: hand / click to center / drag   Wheel: zoom   Q/E: orbit" : "Up/Down: select city   F2: rename   Delete: delete",
                editing ? "V: top/dimetric view   G: grid   Delete: remove selection" : "Choose File or City in the menu bar for all commands.",
                editing ? "Hold R: rotate design / Shift: clear brush" : "Play needs a spawn and a saved Player character.",
                editing ? "Pen: click/drag points, Enter fills, Esc cancels" : "Esc: close menus and dialogs / return to Cities"
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
std::optional<std::string> ground_texture_picker(const std::string& current,const char* title = "Ground texture") {
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
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(r,title); ui::draw_window_close(r);
        text("assets/textures / choose an image",r.x+20,r.y+63,16);
        if (button({r.x+404,r.y+54,176,32},"Refresh folder",false,true,interactive,16)) {
            unload(); textures = building_textures(building_texture_directory(),status); select_current();
        }
        for (int row = 0; row<rows && scroll+row<int(textures.size()); ++row) {
            const int index = scroll+row; const auto& name = textures[index];
            if (!images.count(name)) images[name] = load_building_texture(name);
            const auto thumb = images[name]; const Rectangle item{r.x+20,r.y+104+row*42,560,38};
            if (list_item(item,"",selected==index,thumb.id!=0,interactive)) selected = index;
            if (thumb.id) DrawTexturePro(thumb,{0,0,float(thumb.width),float(thumb.height)},{item.x+4,item.y+3,32,32},{0,0},0,WHITE);
            text(fit(name,17,500),item.x+44,item.y+10,17,selected==index ? ui::highlight : ink);
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
std::optional<RoadStyle> road_editor(const RoadStyle& original) {
    RoadStyle style=original, rendered;
    City sample=City::create("Road preview"); std::string status;
    if (!sample.add_land({55,58},{71,70},status)
        || !sample.add_road(City::road_stroke({59,59},{59,68}),status)
        || !sample.add_road(City::road_stroke({55,64},{73,64}),status)
        || !sample.add_road(City::road_stroke({59,68},{64,66}),status)
        || !sample.add_road(City::road_stroke({64,64},{70,58},false,true),status)) return {};
    std::unique_ptr<Environment> environment;
    std::unique_ptr<EnvironmentRenderer> renderer;
    RenderTexture2D target{};
    SceneLighting lighting; GraphicsSettings graphics; graphics.shadows=0; graphics.view_distance=6000;
    DayNight day; day.set_time("09:00");
    const Vec3 focus=City::center({64,64});
    float zoom=150, yaw=.55f; bool top=false, interactive=false;
    int selected=0;
    const auto release=[&] { if (target.id) UnloadRenderTexture(target); };
    while (!WindowShouldClose()) {
        const bool active=interactive && IsWindowFocused();
        const Rectangle r{16,44,float(GetScreenWidth()-32),float(GetScreenHeight()-64)};
        const Rectangle view{r.x+282,r.y+76,r.width-302,r.height-158};
        if (active && IsKeyPressed(KEY_TAB)) selected=(selected+(IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT) ? 6 : 1))%7;
        if (active && hit(view)) {
            zoom=std::clamp(zoom*std::pow(.85f,GetMouseWheelMove()),65.f,300.f);
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) yaw-=GetMouseDelta().x*.01f;
        }
        if (active && IsKeyPressed(KEY_V)) top=!top;
        Camera3D camera{vector(focus+Vec3(top ? 0 : std::sin(yaw)*160,top ? 240.f : 160.f,top ? .01f : std::cos(yaw)*160)),vector(focus),top ? Vector3{0,0,-1} : Vector3{0,1,0},zoom,CAMERA_ORTHOGRAPHIC};
        if (!renderer || rendered!=style) {
            sample.road_style=style;
            try {
                auto next_environment=std::make_unique<Environment>(sample);
                auto next_renderer=std::make_unique<EnvironmentRenderer>(*next_environment);
                renderer=std::move(next_renderer); environment=std::move(next_environment); rendered=style;
                status=renderer->road_warning();
            } catch (const std::exception& e) { status=e.what(); }
        }
        if (target.texture.width!=int(view.width) || target.texture.height!=int(view.height)) {
            release(); target=LoadRenderTexture(int(view.width),int(view.height));
        }
        if (renderer && target.id) {
            BeginTextureMode(target); ClearBackground({122,169,194,255}); BeginMode3D(camera);
            renderer->draw(camera,0,day.lighting(),lighting,graphics); EndMode3D(); EndTextureMode();
        }
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(r,"Road editor / entire city"); ui::draw_window_close(r);
        text("One style for every road and bridge",r.x+20,r.y+46,17,accent);
        text("Road surface",r.x+20,r.y+84,18,accent);
        text(fit(style.road_texture,16,236),r.x+20,r.y+112,16);
        const bool keyboard=active && IsKeyPressed(KEY_ENTER);
        const bool road=button({r.x+20,r.y+142,242,34},"Change texture...",selected==0,true,active,16) || (keyboard && selected==0);
        if (button({r.x+20,r.y+200,242,34},style.sidewalks ? "[x] Sidewalks" : "[ ] Sidewalks",selected==1,true,active,16) || (keyboard && selected==1)) style.sidewalks=!style.sidewalks;
        text("Sidewalk / 1.2 m",r.x+20,r.y+256,16,accent);
        text(fit(style.sidewalk_texture,16,236),r.x+20,r.y+284,16,style.sidewalks ? ink : muted);
        const bool sidewalk=button({r.x+20,r.y+314,242,34},"Change texture...",selected==2,style.sidewalks,active,16) || (keyboard && selected==2 && style.sidewalks);
        if (button({r.x+20,r.y+372,242,34},"Restore defaults",selected==3,true,active,16) || (keyboard && selected==3)) style=RoadStyle{};
        ui::draw_panel(view,true);
        if (target.id) DrawTexturePro(target.texture,{0,0,float(target.texture.width),-float(target.texture.height)},view,{0,0},0,WHITE);
        text("LIVE PREVIEW / turns, junctions, diagonal, bridge",view.x+10,view.y+10,14,ink);
        if (button({view.x,view.y+view.height+10,144,30},top ? "3D view" : "Top view",selected==4,true,active,16) || (keyboard && selected==4)) top=!top;
        text("Wheel: zoom / Right drag: orbit / V: view",view.x+160,view.y+view.height+17,13);
        text(fit(status.empty() ? "Global style / Tab: focus / Esc: cancel" : status,14,r.width-360),r.x+20,r.y+r.height-37,14,accent);
        const bool accept=button({r.x+r.width-320,r.y+r.height-50,142,34},"Apply",selected==5,renderer && target.id && rendered==style,active,16) || (keyboard && selected==5 && renderer && target.id && rendered==style);
        const bool cancel=close_clicked(r,active) || button({r.x+r.width-162,r.y+r.height-50,142,34},"Cancel",selected==6,true,active,16) || (active && (IsKeyPressed(KEY_ESCAPE) || (keyboard && selected==6)));
        EndDrawing(); interactive=true;
        if (cancel) { release(); return {}; }
        if (accept) { release(); return style; }
        if (road || sidewalk) {
            if (const auto texture=ground_texture_picker(road ? style.road_texture : style.sidewalk_texture,road ? "Road texture / entire city" : "Sidewalk texture / entire city")) {
                if (road) style.road_texture=*texture; else style.sidewalk_texture=*texture;
            }
            interactive=false;
        }
    }
    release(); return {};
}
bool pick(const Environment* environment, Camera3D camera, Vector2 mouse, CityCell& cell, Vec3& point, bool shoreline = false) {
    const Ray ray = GetScreenToWorldRay(mouse,camera);
    if (std::abs(ray.direction.y)<.0001f) return false;
    const float floor = shoreline ? Environment::water_level : City::level;
    float distance = (floor-ray.position.y)/ray.direction.y;
    if (distance < 0) return false;
    // Freeform land and road decks share the water plane; no terrain can be above it.
    if (environment && (!environment->city() || !environment->city()->freeform)) for (const auto& t : environment->triangles()) {
        const auto& vertices = environment->vertices();
        const auto hit = GetRayCollisionTriangle(ray,vector(vertices[t.a]),vector(vertices[t.b]),vector(vertices[t.c]));
        if (hit.hit && hit.distance<distance && hit.point.y>=floor-.001f) distance = hit.distance;
    }
    const auto p = Vector3Add(ray.position,Vector3Scale(ray.direction,distance));
    point = Vec3(p.x,p.y,p.z);
    cell = City::cell(p.x,p.z);
    if (shoreline && environment && environment->city() && !environment->city()->freeform) {
        if (environment->terrain_height(p.x,p.z)<Environment::water_level) return false;
        const auto owner = environment->city()->shore_cell(point);
        if (!owner) return false;
        cell = *owner;
    }
    return City::contains(cell);
}
void tile_outline(const City& city, CityCell p, Color color) {
    const auto patch = city.ground_patch(p);
    for (int i = 0; i < 4; ++i)
        DrawLine3D(vector(patch[i]+Vec3(0,.16f,0)),vector(patch[(i+1)%4]+Vec3(0,.16f,0)),color);
    if (city.tile(p)!=CityTile::Water && city.ramp_axis(p)==3) {
        const auto triangles = city.ground_triangles(p);
        // These vertices form the shared diagonal in either terrain split.
        DrawLine3D(vector(triangles[0][2]+Vec3(0,.16f,0)),vector(triangles[1][0]+Vec3(0,.16f,0)),color);
    }
}
void draw_city(const City& city, bool grid,const CarRenderer& cars,const Camera3D& camera) {
    if (grid && !city.freeform) for (int z = 0; z < City::width; ++z) for (int x = 0; x < City::width; ++x) {
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
            DrawCube({0,spec.body_radius+1,0},spec.body_radius*2,spec.body_radius*2,spec.length,ui::highlight);
            DrawCube({0,spec.body_radius+1,0},spec.span,.25f,spec.length*.15f,ui::highlight);
        }
        rlPopMatrix();
    }
    if (city.spawn) {
        DrawCubeWires(vector(*city.spawn+Vec3(0,.8f,0)),.8f,1.6f,.8f,scene_accent);
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
            const Color color = active==i+3 ? scene_accent : ColorAlpha(colors[planes[i][0]],.45f);
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
            const Color color = active==i ? scene_accent : colors[i];
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
    if (interactive && hit({r.x-4,r.y-4,r.width+8,r.height+8}) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) dragging = id;
    const float old = value;
    if (interactive && dragging==id && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) value = lo+(hi-lo)*std::clamp((GetMouseX()-r.x)/r.width,0.f,1.f);
    const float t = hi>lo ? (value-lo)/(hi-lo) : 0;
    ui::draw_slider(r,t);
    return old!=value;
}
struct BuildingPick {
    int face = -1, decal = -1;
    float distance = 1e9f, decal_distance = 1e9f;
    Vec3 point = Vec3::sZero();
};
BuildingPick pick_building(const std::vector<BuildingTriangle>& triangles,Vector2 mouse,Camera3D camera,Rectangle view) {
    const auto ray = GetScreenToWorldRayEx({mouse.x-view.x,mouse.y-view.y},camera,int(view.width),int(view.height));
    BuildingPick picked;
    for (const auto& t : triangles) {
        const auto hit = GetRayCollisionTriangle(ray,vector(t.points[0]),vector(t.points[1]),vector(t.points[2]));
        if (!hit.hit) continue;
        if (t.decal) { if (hit.distance<picked.decal_distance) { picked.decal_distance = hit.distance; picked.decal = t.decal_index; } }
        else if (hit.distance<picked.distance) { picked.distance = hit.distance; picked.face = t.face; picked.point = Vec3(hit.point.x,hit.point.y,hit.point.z); }
    }
    if (picked.decal_distance>picked.distance+.03f) picked.decal = -1;
    return picked;
}
std::optional<int> city_settings(int density) {
    std::string value = std::to_string(density), error;
    float percent = float(density); int dragging = -1, selected = 0; bool interactive = false;
    const auto parsed = [&]() -> std::optional<int> {
        try {
            std::size_t used; const int number = std::stoi(value,&used);
            if (used==value.size() && number>=0 && number<=100) return number;
        } catch (const std::exception&) {}
        return {};
    };
    while (!WindowShouldClose()) {
        const bool active = interactive && IsWindowFocused();
        if (!active || IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) dragging = -1;
        if (active && IsKeyPressed(KEY_TAB)) selected = (selected+(IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT) ? 2 : 1))%3;
        const Rectangle r{(GetScreenWidth()-620)/2.f,(GetScreenHeight()-388)/2.f,620,388};
        const Rectangle field{r.x+24,r.y+100,176,44}, slider{r.x+24,r.y+162,572,14};
        if (active && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hit(field)) selected = 0;
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(r,"City settings"); ui::draw_window_close(r);
        text("Walking NPC density (%)",r.x+24,r.y+62,18,accent);
        if (selected==0 && active) input(value,field);
        else { ui::draw_panel(field,true,ui::highlight); text(value,field.x+12,field.y+10,21); }
        if (const auto number = parsed()) percent = float(*number);
        if (mesh_slider(slider,percent,0,100,0,dragging,active)) { value = std::to_string(int(std::round(percent))); selected = 0; error.clear(); }
        text("0: no pedestrians / 100: current maximum",r.x+24,r.y+198,16);
        text("Saved with this city / applies when Play starts",r.x+24,r.y+223,14);
        text("Type 0-100 or drag / Tab: focus / Esc: cancel",r.x+24,r.y+248,13);
        text(error,r.x+24,r.y+275,14,accent);
        const bool accept = button({r.x+24,r.y+312,278,44},"Apply",selected==1,true,active)
            || (active && IsKeyPressed(KEY_ENTER) && selected!=2);
        const bool cancel = close_clicked(r,active) || button({r.x+318,r.y+312,278,44},"Cancel",selected==2,true,active)
            || (active && (IsKeyPressed(KEY_ESCAPE) || (IsKeyPressed(KEY_ENTER) && selected==2)));
        MenuBar(MenuMode::Editor).draw(); EndDrawing(); interactive = true;
        if (cancel) return {};
        if (accept) { if (const auto number = parsed()) return number; error = "Enter a whole number from 0 to 100."; selected = 0; }
    }
    return {};
}
}

std::optional<BuildingMesh> building_builder(BuildingMesh mesh,const std::filesystem::path& directory,const std::string& screenshot,int initial_tab,std::vector<BuildingMesh>* saved_designs) {
    int tab = initial_tab==2 ? 1 : initial_tab==3 ? 2 : 0, mode = 0, decal = -1;
    int field = -1, scroll = 0, frames = 0, slider_drag = -1, handle = -1, part = 0, shape = 0, part_page = 0, grab_decal = -1;
    float tool_scroll = 0, decal_width = 1, decal_height = 1.5f, repeat_count = 6, repeat_spacing = 1.6f;
    std::string decal_brush;
    bool place_decal = false, move_decal = false, repeat_vertical = false, uv_all = false;
    Vec3 shape_dimensions(4,4,4), shape_position(0,0,0), part_translation = Vec3::sZero(), part_rotation = Vec3::sZero(), part_scale(1,1,1);
    std::array<std::vector<int>,3> selected{{{2},{},{4}}}; std::vector<int> moving;
    std::vector<BuildingMesh> undo, redo; std::optional<BuildingMesh> grab;
    Vector2 grab_start{}; bool keyboard_grab = false, decal_grab = false, slice = false, xray = false, interactive = false, decal_texture = false;
    bool decal_move_valid = true, decal_has_moved = false;
    Vector2 decal_grab_start{};
    float yaw = 2.44f, pitch = .5f, zoom = 1, cut_fraction = .5f; int cut_axis = 1;
    std::string status, buffer;
    const auto align = [&](BuildingMesh& value) { const auto tiles = value.footprint(); return value.set_footprint(tiles[0],tiles[1],status,true); };
    if (!align(mesh)) return {};
    auto textures = building_textures(building_texture_directory(),status);
    RenderTexture2D preview{}; MenuBar menu(MenuMode::Creator);
    BuildingRenderer renderer; SceneLighting preview_lighting; GraphicsSettings preview_graphics;
    preview_graphics.apply_preset(GraphicsPreset::Low); preview_graphics.view_distance = 6000;
    const auto preview_daylight = DayNight().lighting();
    bool preview_dirty = true, preview_valid = false, texture_selection = false;
    std::vector<BuildingTriangle> triangles;
    std::optional<BuildingMesh> result;
    const auto rebuild_preview = [&]() {
        if (!preview_dirty) return preview_valid;
        triangles = mesh.triangles(); std::string error;
        preview_valid = renderer.build(triangles,error,false,textures); preview_dirty = false;
        if (!preview_valid) status = error;
        else if (!renderer.warning().empty()) status = renderer.warning();
        return preview_valid;
    };
    const auto commit = [&](BuildingMesh next) {
        if (!next.validate(status)) return false;
        undo.push_back(mesh); if (undo.size()>32) undo.erase(undo.begin()); redo.clear();
        mesh = std::move(next); preview_dirty = true; field = -1; status = "Updated"; return true;
    };
    const auto cancel_move = [&]() {
        mesh = *grab; grab.reset(); if (decal_grab) decal = grab_decal;
        decal_grab = false; preview_dirty = true; status = "Move cancelled";
    };
    std::string original=snapshot(mesh);
    const auto save_design=[&]() {
        if (!rebuild_preview()) return false;
        if (!mesh.save(directory,status)) return false;
        if (saved_designs) {
            const auto found=std::find_if(saved_designs->begin(),saved_designs->end(),[&](const auto& d){return d.name==mesh.name;});
            if (found==saved_designs->end()) saved_designs->push_back(mesh); else *found=mesh;
        }
        original=snapshot(mesh); status="Design saved"; return true;
    };
    const auto leave=[&]() {return leave_draft(mesh,original,save_design,status);};
    while (true) {
        if (WindowShouldClose()) { if (grab) cancel_move(); if (leave()) break; interactive=false; continue; }
        const float height = float(GetScreenHeight());
        const Rectangle window{8,44,float(GetScreenWidth()-16),height-108}, view{414,84,float(GetScreenWidth()-438),height-244};
        MenuState menu_state; menu_state.undo = !undo.empty() && !grab; menu_state.redo = !redo.empty() && !grab;
        const auto command = interactive && !grab ? menu.update(menu_state) : MenuCommand::None;
        if (command!=MenuCommand::None) field = -1;
        const bool can_edit = interactive && IsWindowFocused() && !menu.blocking() && !menu.interacted();
        const bool click = can_edit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT), in_view = can_edit && hit(view);
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL), shift = IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT);
        if (can_edit && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) slider_drag = -1;
        if (command==MenuCommand::CloseCreator || (can_edit && !grab && close_clicked(window))) { if (leave()) break; interactive=false; continue; }
        if (can_edit && IsKeyPressed(KEY_ESCAPE) && !grab) { if (slice) slice = false; else if (place_decal || move_decal) place_decal = move_decal = false; else if (field>=0) field = -1; else if (tab==2 && decal>=0) { decal = -1; selected[2].clear(); status = "Selection cleared"; } else {if (leave()) break; interactive=false; continue;} }
        if (command==MenuCommand::NewBuilding) {
            if (leave()) { mesh={}; mesh.name="New object"; mesh.type=ObjectType::Object; align(mesh); original=snapshot(mesh); undo.clear(); redo.clear(); selected={{{2},{},{4}}}; tab=0; slice=false; preview_dirty=true; }
            interactive=false; continue;
        }
        if ((can_edit || command==MenuCommand::Undo || command==MenuCommand::Redo) && !grab && field<0) {
            if ((command==MenuCommand::Undo || (ctrl && IsKeyPressed(KEY_Z))) && !undo.empty()) { redo.push_back(mesh); mesh = std::move(undo.back()); undo.pop_back(); selected = {}; slice = false; preview_dirty=true; status = "Undone"; }
            if ((command==MenuCommand::Redo || (ctrl && IsKeyPressed(KEY_Y))) && !redo.empty()) { undo.push_back(mesh); mesh = std::move(redo.back()); redo.pop_back(); selected = {}; slice = false; preview_dirty=true; status = "Redone"; }
            if ((tab==0 || tab==3) && !ctrl) {
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
        decal = mesh.decals.empty() ? -1 : std::clamp(decal,-1,int(mesh.decals.size())-1);
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
        const auto start_decal_move = [&](bool keyboard,bool keep_offset) {
            Vec3 center;
            if (!mesh.decal_center(decal,center)) { move_decal = false; status = "This decal needs a valid wall centre to move."; return; }
            grab = mesh; grab_decal = decal; decal_grab = true; keyboard_grab = keyboard; decal_move_valid = true;
            decal_grab_start = GetMousePosition(); decal_has_moved = !keep_offset;
            grab_start = keep_offset ? Vector2Subtract(GetMousePosition(),project_mesh(center,camera,view)) : Vector2{};
            place_decal = move_decal = false; field = -1; status = "Moving decal / Esc: cancel";
        };
        if (!grab && can_edit && tab==2 && decal>=0 && field<0 && (move_decal || (!ctrl && IsKeyPressed(KEY_G)))) start_decal_move(true,!move_decal);
        auto vertices = targets(); auto points = mesh.points(); Vec3 pivot = Vec3::sZero(); for (int v : vertices) pivot += points[v]/float(vertices.size());
        if (grab) {
            if (keyboard_grab && !decal_grab) {
                if (IsKeyPressed(KEY_X)) handle = 0;
                if (IsKeyPressed(KEY_Y)) handle = 1;
                if (IsKeyPressed(KEY_Z)) handle = 2;
            }
            if (decal_grab) {
                const auto picked = pick_building(triangles,Vector2Subtract(GetMousePosition(),grab_start),camera,view);
                auto next = mesh; std::string error;
                decal_has_moved |= Vector2Distance(GetMousePosition(),decal_grab_start)>2;
                decal_move_valid = in_view && picked.face>=0 && (!decal_has_moved || next.move_decal(decal,picked.face,picked.point,error));
                if (decal_move_valid && decal_has_moved) {
                    const auto& before = mesh.decals[decal]; const auto& after = next.decals[decal];
                    if (before.face!=after.face || before.u!=after.u || before.v!=after.v || before.width!=after.width || before.height!=after.height) { mesh = std::move(next); preview_dirty = true; }
                }
            } else {
                BuildingMesh next = *grab;
                if (next.move_vertices(moving,mesh_drag_delta(Vector2Subtract(GetMousePosition(),grab_start),camera,view,handle),status)
                    && (next.vertices!=mesh.vertices || next.size!=mesh.size)) { mesh = std::move(next); preview_dirty=true; }
            }
            const bool finish = keyboard_grab ? (click && in_view) || IsKeyPressed(KEY_ENTER) : IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
            if (!IsWindowFocused() || IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || (decal_grab && finish && !decal_move_valid)) {
                cancel_move();
            } else if (finish) {
                BuildingMesh moved = mesh; mesh = *grab; grab.reset(); decal_grab = false;
                if (snapshot(moved)!=snapshot(mesh)) commit(std::move(moved));
                else status = "Selection unchanged";
            }
        } else if (can_edit && (tab==0 || tab==3) && !slice && field<0 && !ctrl && !vertices.empty() && IsKeyPressed(KEY_G)) {
            grab = mesh; moving = vertices; grab_start = GetMousePosition(); handle = 6; keyboard_grab = true;
        } else if (in_view && click && !slice) {
            const int gizmo = (tab==0 || tab==3) && !shift && !vertices.empty() ? mesh_gizmo(pivot,camera,view,GetMousePosition(),false) : -1;
            if (gizmo>=0) { grab = mesh; moving = vertices; grab_start = GetMousePosition(); handle = gizmo; keyboard_grab = false; }
            else {
                const Vector2 mouse{GetMouseX()-view.x,GetMouseY()-view.y};
                const Ray ray = GetScreenToWorldRayEx(mouse,camera,int(view.width),int(view.height));
                const auto collision = pick_building(mesh.triangles(),GetMousePosition(),camera,view);
                const float closest = collision.distance; const int face = collision.face; int picked = -1;
                const auto visible = [&](Vec3 p) { return xray || Vector3Distance(ray.position,vector(p))<=closest+radius*.02f; };
                if (tab!=0 || mode==2) picked = face;
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
                if (tab==3 && picked>=0) {
                    part = mesh.faces[picked].part; selected = {}; selected[0] = mesh.part_vertices(part); selected[2] = {picked}; mode = 0;
                    shape_position = points[mesh.faces[picked].vertices[0]];
                }
                else if (tab==2) {
                    if (picked>=0 && place_decal) {
                        auto next = mesh; const int added = next.place_decal(picked,collision.point,decal_brush,decal_width,decal_height,status);
                        if (added>=0 && commit(std::move(next))) { decal = added; selected[2] = {picked}; }
                    } else if (collision.decal>=0) {
                        decal = collision.decal; decal_brush = mesh.decals[decal].texture; selected[2] = {mesh.decals[decal].face};
                        start_decal_move(false,true);
                    } else { decal = -1; selected[2].clear(); status = "No decal selected"; }
                }
                else {
                    if (tab==1 || tab==4) mode = 2;
                    auto& list = selected[mode];
                    if (!shift) list.clear();
                    if (picked>=0) { const auto it = std::find(list.begin(),list.end(),picked); if (it==list.end()) list.push_back(picked); else list.erase(it); }
                }
                field = -1;
            }
        }
        points = mesh.points(); vertices = targets(); pivot = Vec3::sZero(); for (int v : vertices) pivot += points[v]/float(vertices.size());
        rebuild_preview();
        const auto tiles = mesh.footprint();
        const float cut_extent = mesh_coordinate(mesh.size,cut_axis), cut_position = cut_axis==1 ? cut_fraction*cut_extent : (cut_fraction-.5f)*cut_extent;
        BeginTextureMode(preview); ClearBackground({18,28,45,255}); BeginMode3D(camera);
        const float hx = mesh.size.GetX()/2, hz = mesh.size.GetZ()/2;
        DrawPlane({0,-.04f,0},{mesh.size.GetX(),mesh.size.GetZ()},{66,82,92,255});
        if (preview_valid) renderer.draw(camera,preview_daylight,preview_lighting,preview_graphics);
        rlDisableDepthTest(); rlDisableDepthMask();
        for (int x = 0; x<=tiles[0]; ++x) DrawLine3D({-hx+x*BuildingMesh::tile_size,.015f,-hz},{-hx+x*BuildingMesh::tile_size,.015f,hz},scene_accent);
        for (int z = 0; z<=tiles[1]; ++z) DrawLine3D({-hx,.015f,-hz+z*BuildingMesh::tile_size},{hx,.015f,-hz+z*BuildingMesh::tile_size},scene_accent);
        rlEnableDepthMask(); rlEnableDepthTest();
        {
            if (mode==2 && tab!=2) for (const auto& t : triangles) if (!t.decal && std::find(selected[2].begin(),selected[2].end(),t.face)!=selected[2].end()) {
                const auto offset = (t.points[1]-t.points[0]).Cross(t.points[2]-t.points[0]).NormalizedOr(Vec3::sAxisY())*.025f;
                DrawTriangle3D(vector(t.points[0]+offset),vector(t.points[1]+offset),vector(t.points[2]+offset),{255,161,0,90});
            }
            if (xray) { rlDisableDepthTest(); rlDisableDepthMask(); }
            for (int i = 0; i<int(edges.size()); ++i) DrawLine3D(vector(points[edges[i][0]]),vector(points[edges[i][1]]),mode==1 && std::find(selected[1].begin(),selected[1].end(),i)!=selected[1].end() ? ORANGE : scene_accent);
            if (tab==0 && mode==0) for (int i = 0; i<int(points.size()); ++i) DrawSphere(vector(points[i]),camera.fovy*.007f,std::find(vertices.begin(),vertices.end(),i)!=vertices.end() ? ORANGE : ui::highlight);
            if (xray) { rlEnableDepthMask(); rlEnableDepthTest(); }
        }
        if (tab==2 && decal>=0) {
            std::map<std::array<std::array<int,3>,2>,std::array<Vec3,2>> outline; Vec3 normal = Vec3::sAxisY();
            const auto endpoint = [](Vec3 p) { return std::array<int,3>{int(std::lround(p.GetX()*10000)),int(std::lround(p.GetY()*10000)),int(std::lround(p.GetZ()*10000))}; };
            for (const auto& t : triangles) if (t.decal_index==decal) {
                normal = (t.points[1]-t.points[0]).Cross(t.points[2]-t.points[0]).NormalizedOr(normal);
                for (int i = 0; i<3; ++i) {
                    const std::array<Vec3,2> edge{t.points[i],t.points[(i+1)%3]};
                    std::array<std::array<int,3>,2> key{endpoint(edge[0]),endpoint(edge[1])}; if (key[1]<key[0]) std::swap(key[0],key[1]);
                    const auto added = outline.emplace(key,edge); if (!added.second) outline.erase(added.first);
                }
            }
            const Color color = decal_grab && !decal_move_valid ? RED : YELLOW;
            rlDrawRenderBatchActive(); rlSetLineWidth(3);
            for (const auto& item : outline) DrawLine3D(vector(item.second[0]+normal*.01f),vector(item.second[1]+normal*.01f),color);
            rlDrawRenderBatchActive(); rlSetLineWidth(1);
            Vec3 center; if (mesh.decal_center(decal,center)) DrawSphere(vector(center+normal*.025f),camera.fovy/view.height*2.5f,color);
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
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(window,"Object Builder"); ui::draw_window_close(window);
        DrawTextureRec(preview.texture,{0,0,float(preview.texture.width),-float(preview.texture.height)},{view.x,view.y},WHITE); ui::draw_frame(view,true);
        if ((tab==0 || tab==3) && !slice && !vertices.empty()) { BeginScissorMode(int(view.x),int(view.y),int(view.width),int(view.height)); mesh_gizmo(pivot,camera,view,GetMousePosition(),true,grab ? handle : -1); EndScissorMode(); }
        text(std::to_string(tiles[0])+" x "+std::to_string(tiles[1])+" tiles",view.x+10,view.y+10,16,ui::highlight);
        const auto& stats = renderer.stats();
        text(TextFormat("%zu meshes / %zu triangles / %.2f MiB GPU",stats.meshes,stats.emitted_triangles,
            double(stats.gpu_geometry_bytes+stats.gpu_texture_bytes)/(1024*1024)),view.x+10,view.y+32,14,ui::highlight);
        if (!preview_valid) text("Preview unavailable: undo or reduce the texture/geometry budget.",view.x+10,view.y+58,14,ORANGE);
        if (tab==2 && decal>=0) {
            DrawRectangle(int(view.x+8),int(view.y+56),int(view.width-16),25,background);
            text(fit("Selected decal "+std::to_string(decal+1)+" / "+std::to_string(mesh.decals.size())+" : "+mesh.decals[decal].texture,14,view.width-32),view.x+16,view.y+61,14,ink);
        }
        const char* hint = slice ? "Slice preview / drag the position slider" : tab==2 ? decal_grab ? !decal_move_valid ? "Off wall / move back onto a wall / Esc: cancel" : keyboard_grab ? "Move preview / Click: confirm / Esc: cancel" : "Dragging decal / Release: confirm / Esc: cancel" : place_decal ? "Click walls to place / Esc: finish" : "Click / drag a decal / G: move / Esc: cancel" : tab==1 || tab==4 ? "Click faces / Shift: select more" : "Drag handles / Shift: select more / G: move";
        text(hint,view.x+10,view.y+view.height-28,15,ui::highlight);
        SetMouseCursor(in_view && tab==2 && (place_decal || decal_grab) ? MOUSE_CURSOR_CROSSHAIR : MOUSE_CURSOR_DEFAULT);
        text("Right drag: orbit / Wheel: zoom",view.x,view.y+view.height+10,16);
        const auto control = [&](Rectangle r,const std::string& label,bool chosen=false,bool enabled=true) { return button(r,label,chosen,enabled,can_edit && !grab,16); };
        text("Name",20,88,17); const Rectangle name_box{84,80,310,44};
        if (!grab && click && hit(name_box)) field = 0;
        if (field==0) { input(mesh.name,name_box); if (IsKeyPressed(KEY_ENTER) || (click && !hit(name_box))) field = -1; }
        else { ui::draw_panel(name_box,true,ui::highlight); text(fit(mesh.name,18,286),96,93); }
        text("Type",20,135,16,accent);
        if (control({84,130,145,24},"Object",mesh.type==ObjectType::Object) && mesh.type!=ObjectType::Object) {auto next=mesh;next.type=ObjectType::Object;commit(std::move(next));}
        if (control({239,130,155,24},"Building",mesh.type==ObjectType::Building) && mesh.type!=ObjectType::Building) {auto next=mesh;next.type=ObjectType::Building;commit(std::move(next));}
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
            } else { ui::draw_panel(box,true,ui::highlight); text(std::to_string(value),160,y+5,17); }
            if (control({312,y,34,30},"-")) { value = std::max(1,value-1); resized = true; field = -1; }
            if (control({356,y,34,30},"+")) { value = std::min(128,value+1); resized = true; field = -1; }
        };
        stepper("Tiles X",width,158,1); stepper("Tiles Z",depth,194,2);
        if (resized) { auto next = mesh; if (next.set_footprint(width,depth,status,true)) commit(std::move(next)); }
        const char* tabs[] = {"Mesh","Parts","Texture","UV","Decals"}; const int tab_ids[] = {0,3,1,4,2};
        const float tab_widths[] = {62,70,90,48,84}; float tab_x = 20;
        for (int i = 0; i<5; ++i) {
            if (button({tab_x,244,tab_widths[i],30},tabs[i],tab==tab_ids[i],true,can_edit && !grab,13)) {
                tab = tab_ids[i]; scroll = 0; tool_scroll = 0; field = -1; slice = false; if (tab==1) decal_texture = false;
                place_decal = move_decal = false;
                if (tab==3) { mode = 0; selected[0] = mesh.part_vertices(part); }
                if (tab==1 || tab==4) mode = 2;
            }
            tab_x += tab_widths[i]+4;
        }
        const Rectangle tools_area{18,284,378,height-440};
        const auto box = [&](float x,float y,float w,float h) { return Rectangle{x,y-tool_scroll,w,h}; };
        const auto choose = [&](float x,float y,float w,const std::string& label,bool chosen=false,bool enabled=true) { return control(box(x,y,w,28),label,chosen,enabled) && hit(tools_area); };
        const auto number = [&](const char* label,float& value,float y,int id,float lo,float hi) {
            const float old = value;
            text(label,20,y+5-tool_scroll,15); const auto r = box(150,y,236,28);
            if (!grab && click && hit(r) && hit(tools_area)) { field = id; buffer = TextFormat("%.3f",value); }
            if (field==id) {
                input(buffer,r);
                if (IsKeyPressed(KEY_ENTER) || (click && !hit(r))) {
                    try { std::size_t used; const float n = std::stof(buffer,&used); if (used!=buffer.size() || !std::isfinite(n) || n<lo || n>hi) throw std::out_of_range("number"); value = n; }
                    catch (const std::exception&) { status = TextFormat("Enter a number from %.2f to %.2f.",lo,hi); }
                    field = -1;
                }
            } else { ui::draw_panel(r,true,ui::highlight); text(TextFormat("%.3f",value),r.x+10,r.y+5,16); }
            return value!=old;
        };
        const auto scroll_tools = [&](float content_height) {
            tool_scroll = std::clamp(tool_scroll-(can_edit && !grab && hit(tools_area) ? GetMouseWheelMove()*32 : 0),0.f,std::max(0.f,content_height-tools_area.height));
            BeginScissorMode(int(tools_area.x),int(tools_area.y),int(tools_area.width),int(tools_area.height));
            if (content_height>tools_area.height) {
                const float thumb = tools_area.height*tools_area.height/content_height;
                DrawRectangleRec({tools_area.x+tools_area.width-4,tools_area.y,4,tools_area.height},ColorAlpha(border,.3f));
                DrawRectangleRec({tools_area.x+tools_area.width-4,tools_area.y+tool_scroll/content_height*tools_area.height,4,thumb},accent);
            }
        };
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
        } else if (tab==3) {
            scroll_tools(part_page==0 ? 392.f : 530.f);
            if (choose(20,288,180,"Add shape",part_page==0)) part_page = 0;
            if (choose(210,288,180,"Transform part",part_page==1)) part_page = 1;
            if (part_page==0) {
                const char* shapes[] = {"Box","Wedge","Gable","Hip"};
                for (int i = 0; i<4; ++i) if (choose(20+i*94.f,326,88,shapes[i],shape==i)) shape = i;
                float dx=shape_dimensions.GetX(),dy=shape_dimensions.GetY(),dz=shape_dimensions.GetZ();
                number("Width m",dx,366,10,.1f,1433.6f); number("Height m",dy,402,11,.1f,120); number("Depth m",dz,438,12,.1f,1433.6f);
                shape_dimensions = Vec3(dx,dy,dz);
                float px=shape_position.GetX(),py=shape_position.GetY(),pz=shape_position.GetZ();
                number("Base X m",px,478,13,-716.8f,716.8f); number("Base Y m",py,514,14,0,120); number("Base Z m",pz,550,15,-716.8f,716.8f);
                shape_position = Vec3(px,py,pz);
                if (choose(20,590,180,"Add free shape")) {
                    const int added = next.add_shape(static_cast<BuildingShape>(shape),shape_dimensions,shape_position,status);
                    if (added>=0) { part = added; mode = 0; selected = {}; selected[0] = next.part_vertices(part); changed = true; }
                }
                if (choose(210,590,180,"Attach to face",false,selected[2].size()==1)) {
                    const int face = selected[2].front(); changed = next.attach_shape(face,static_cast<BuildingShape>(shape),shape_dimensions.GetY(),status);
                    if (changed) { part = next.faces[face].part; selected[0] = next.part_vertices(part); mode = 0; }
                }
                text("Attach uses the face outline and Height.",20,632-tool_scroll,14);
                text("Increase footprint before adding outside it.",20,655-tool_scroll,14);
            } else {
                std::set<int> parts; for (const auto& face : mesh.faces) parts.insert(face.part);
                if (choose(20,326,370,"Part "+std::to_string(part)+" / click a shape")) {
                    auto it = parts.upper_bound(part); if (it==parts.end()) it = parts.begin();
                    if (it!=parts.end()) { part = *it; mode = 0; selected = {}; selected[0] = mesh.part_vertices(part); }
                }
                float tx=part_translation.GetX(),ty=part_translation.GetY(),tz=part_translation.GetZ();
                number("Move X m",tx,366,20,-1433.6f,1433.6f); number("Move Y m",ty,402,21,-120,120); number("Move Z m",tz,438,22,-1433.6f,1433.6f);
                part_translation = Vec3(tx,ty,tz); float rx=part_rotation.GetX(),ry=part_rotation.GetY(),rz=part_rotation.GetZ();
                number("Rotate X deg",rx,478,23,-360,360); number("Rotate Y deg",ry,514,24,-360,360); number("Rotate Z deg",rz,550,25,-360,360);
                part_rotation = Vec3(rx,ry,rz); float sx=part_scale.GetX(),sy=part_scale.GetY(),sz=part_scale.GetZ();
                number("Scale X",sx,590,26,.01f,100); number("Scale Y",sy,626,27,.01f,100); number("Scale Z",sz,662,28,.01f,100);
                part_scale = Vec3(sx,sy,sz);
                if (choose(20,704,180,"Apply transform")) changed = next.transform_part(part,part_translation,part_rotation,part_scale,status);
                if (choose(210,704,180,"Duplicate")) {
                    const int added = next.duplicate_part(part,part_translation,status);
                    if (added>=0) { part = added; selected = {}; selected[0] = next.part_vertices(part); mode = 0; changed = true; }
                }
                if (choose(20,742,180,"Delete part")) { changed = next.remove_part(part,status); if (changed) selected = {}; }
                if (choose(210,742,180,"Bake / clean")) { next = next.baked(); changed = true; selected = {}; }
                text("G / handles move the selected part.",20,786-tool_scroll,14);
            }
            EndScissorMode();
            if (tool_scroll>0 || tools_area.height<536) text("Scroll for more part controls",20,height-147,13,accent);
        } else if (tab==4) {
            scroll_tools(432);
            if (choose(20,288,180,"Selected faces",!uv_all)) uv_all = false;
            if (choose(210,288,180,"All faces",uv_all)) uv_all = true;
            auto faces = selected[2]; if (uv_all) { faces.clear(); for (int i=0;i<int(next.faces.size());++i) faces.push_back(i); }
            if (!faces.empty()) {
                const auto& source = mesh.faces[faces.back()]; auto projection = source.projection;
                auto offset = source.offset, scale = source.scale; float density = source.meters_per_repeat, rotation = source.rotation;
                bool update = false;
                const char* projections[] = {"Original","Planar","Box"};
                for (int i=0;i<3;++i) if (choose(20+i*126.f,326,118,projections[i],int(projection)==i)) { projection = BuildingProjection(i); update = true; }
                update |= number("Metres/tile",density,366,30,.01f,1000000);
                update |= number("Offset U",offset[0],402,31,-1000000,1000000);
                update |= number("Offset V",offset[1],438,32,-1000000,1000000);
                update |= number("Scale U",scale[0],478,33,.0001f,10000);
                update |= number("Scale V",scale[1],514,34,.0001f,10000);
                update |= number("Rotate deg",rotation,550,35,-360,360);
                if (choose(20,590,180,"Re-project")) update = true;
                if (choose(210,590,180,"Reset UV tweaks")) { offset = {0,0}; scale = {1,1}; rotation = 0; update = true; }
                if (update) changed = next.set_uv(faces,projection,density,offset,scale,rotation,status);
                text(std::to_string(faces.size())+" faces / Enter applies each value",20,632-tool_scroll,14);
                text("Projection follows geometry; tweaks are kept.",20,655-tool_scroll,14);
                text("Re-project captures a new planar frame.",20,678-tool_scroll,14);
            } else text("Click faces in the preview, or choose All faces.",20,366-tool_scroll,14);
            EndScissorMode();
            text("Scroll for UV controls",20,height-147,13,accent);
        } else if (tab==1) {
            if (control({20,288,180,30},"Whole design",!texture_selection,!decal_texture)) texture_selection = false;
            if (control({210,288,180,30},"Selected faces",texture_selection,!decal_texture && !selected[2].empty())) texture_selection = true;
            if (control({20,324,180,30},"Use default",false,!decal_texture)) {
                if (texture_selection) changed = next.set_face_material(selected[2],-1,status);
                else { next.texture.clear(); changed = true; }
            }
            if (control({210,324,180,30},"Refresh folder")) { textures = building_textures(building_texture_directory(),status); scroll = 0; preview_dirty = true; }
            if (control({20,360,370,30},"Auto tile / 2 metres per repeat",false,!decal_texture)) {
                auto faces = selected[2]; if (!texture_selection) { faces.clear(); for (int i=0;i<int(next.faces.size());++i) faces.push_back(i); }
                changed = next.set_uv(faces,BuildingProjection::Box,2,{0,0},{1,1},0,status);
            }
            const Rectangle list{20,402,370,height-556}; const int rows = std::max(1,int(list.height/36));
            if (can_edit && hit(list)) scroll -= int(GetMouseWheelMove());
            scroll = std::clamp(scroll,0,std::max(0,int(textures.size())-rows));
            if (textures.empty()) text("Add images to assets/textures",20,412,16);
            for (int row = 0; row<rows && scroll+row<int(textures.size()); ++row) {
                const auto& name = textures[scroll+row]; const Rectangle r{20,402+row*36.f,370,32};
                const bool chosen = decal_texture ? (decal>=0 ? mesh.decals[decal].texture : decal_brush)==name : texture_selection && !selected[2].empty()
                    ? mesh.faces[selected[2].back()].material>=0 && mesh.materials[mesh.faces[selected[2].back()].material]==name : mesh.texture==name;
                if (control(r,"      "+fit(name,16,296),chosen)) {
                    if (decal_texture) { decal_brush = name; if (decal>=0) { next.decals[decal].texture = name; changed = true; } tab = 2; tool_scroll = 0; }
                    else if (texture_selection) { const int material = next.add_material(name,status); if (material>=0) changed = next.set_face_material(selected[2],material,status); }
                    else { next.texture = name; changed = true; }
                }
                renderer.thumbnail(name,{r.x+4,r.y+2,28,28});
            }
        } else if (tab==2) {
            scroll_tools(decal>=0 ? 530.f : 252.f);
            if (decal_brush.empty() && !textures.empty()) {
                const auto window = std::find_if(textures.begin(),textures.end(),[](const auto& name){return name.find("window")==0;});
                decal_brush = window==textures.end() ? textures.front() : *window;
            }
            if (choose(20,288,370,"Texture: "+fit(decal_brush.empty() ? "Choose image" : decal_brush,16,260))) {
                tab = 1; decal_texture = true; scroll = int(std::find(textures.begin(),textures.end(),decal_brush)-textures.begin());
            }
            if (choose(20,326,180,place_decal ? "Finish placing" : "Place on wall",place_decal,!decal_brush.empty() && mesh.decals.size()<64)) { place_decal = !place_decal; move_decal = false; }
            if (choose(210,326,180,"Move (G)",decal_grab || move_decal,decal>=0)) { move_decal = true; place_decal = false; }
            number("New width m",decal_width,366,40,.05f,120);
            number("New height m",decal_height,402,41,.05f,120);
            if (decal>=0) {
                if (choose(20,440,370,"Decal "+std::to_string(decal+1)+" / "+std::to_string(next.decals.size())+" (click to cycle)")) { decal = (decal+1)%int(next.decals.size()); decal_brush = next.decals[decal].texture; field = -1; }
                auto& d = next.decals[decal];
                float width_m = d.width*d.meters[0], height_m = d.height*d.meters[1];
                const float center_u = d.u+d.width/2, center_v = d.v+d.height/2;
                const bool width_changed = number(d.projected ? "Width m" : "Width (UV)",width_m,478,42,d.projected ? .05f : .01f,d.projected ? 120.f : 1.f), height_changed = number(d.projected ? "Height m" : "Height (UV)",height_m,514,43,d.projected ? .05f : .01f,d.projected ? 120.f : 1.f);
                if (width_changed || height_changed) { d.width = width_m/d.meters[0]; d.height = height_m/d.meters[1]; d.u = center_u-d.width/2; d.v = center_v-d.height/2; changed = true; }
                changed |= number("Rotate deg",d.rotation,550,44,-360,360);
                if (choose(20,590,180,"Duplicate",false,next.decals.size()<64)) {
                    const int added = next.duplicate_decal(decal,{0,0},status);
                    if (added>=0) {
                        grab = mesh; grab_decal = decal; mesh = next; decal = added; decal_grab = keyboard_grab = true; grab_start = {}; decal_move_valid = false; changed = false;
                        decal_has_moved = true; decal_grab_start = GetMousePosition();
                        preview_dirty = true; place_decal = move_decal = false; status = "Place duplicate / Click: confirm / Esc: cancel";
                    }
                }
                if (choose(210,590,180,"Remove")) { next.decals.erase(next.decals.begin()+decal); decal = next.decals.empty() ? -1 : std::min(decal,int(next.decals.size())-1); changed = true; }
                number("Repeat count",repeat_count,632,45,1,64);
                number("Spacing m",repeat_spacing,668,46,.01f,120);
                if (choose(20,706,180,"Horizontal",!repeat_vertical)) repeat_vertical = false;
                if (choose(210,706,180,"Vertical",repeat_vertical)) repeat_vertical = true;
                if (choose(20,744,370,"Repeat along wall",false,decal>=0)) changed = next.repeat_decal(decal,int(std::round(repeat_count)),repeat_spacing,repeat_vertical,status);
                text("Count includes source / centres stay on wall.",20,786-tool_scroll,14);
            } else {
                text("Pick a texture, Place, then click a wall.",20,444-tool_scroll,14);
                text("Decals snap to the wall and clip at its edges.",20,467-tool_scroll,14);
            }
            EndScissorMode();
            text("Scroll for size, rotation and repeats",20,height-147,13,accent);
        }
        if (changed) commit(std::move(next));
        if (control({20,height-122,180,36},"Save design",true) || command==MenuCommand::SaveBuilding || (can_edit && !grab && ctrl && IsKeyPressed(KEY_S))) {
            save_design();
        }
        const bool use = control({210,height-122,180,36},"Save and close",true) || command==MenuCommand::UseBuilding;
        const bool cancel = control({view.x+view.width-142,height-122,142,36},"Close");
        text(fit(status,17,GetScreenWidth()-40.f),20,height-49,17,accent); menu.draw(menu_state);
        text("OBJECT BUILDER",GetScreenWidth()-212.f,8,16,ink); EndDrawing(); interactive = true;
        if (!screenshot.empty() && ++frames>=3) { auto capture = LoadImageFromScreen(); ExportImage(capture,screenshot.c_str()); UnloadImage(capture); break; }
        if (cancel) { if (leave()) break; interactive=false; }
        if (use && save_design()) { result = mesh; break; }
        if (command==MenuCommand::SavedBuildings) {
            const auto loaded=saved_picker([&]{return saved_buildings(directory,status);},"Saved objects and buildings","Open design",mesh.name,status,directory);
            if (loaded && leave()) { mesh=*loaded; align(mesh); original=snapshot(mesh); undo.clear(); redo.clear(); selected={}; tab=0; slice=false; preview_dirty=true; }
            interactive=false; field=slider_drag=-1;
        }
    }
    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    if (preview.id) UnloadRenderTexture(preview);
    return result;
}

std::optional<CarDesign> car_builder(CarDesign design,const std::filesystem::path& directory,const std::string& screenshot,std::vector<CarDesign>* saved_designs) {
    CarRenderer renderer; SceneLighting lighting; GraphicsSettings graphics; graphics.shadows = 0;
    DayNight day; day.set_time("09:00");
    PhysicsWorld world(false); Car car(world); car.set_simulated(false); TuningPanel tuning;
    std::string status; auto bodies = car_models(car_asset_directory("cars"),status);
    auto wheels = car_models(car_asset_directory("wheels"),status);
    int tab = 0, scroll = 0, drag = -1, frames = 0;
    float yaw = 2.44f, pitch = .35f, zoom = 1;
    bool interactive = false, naming = false; RenderTexture2D preview{}; std::optional<CarDesign> result;
    MenuBar menu(MenuMode::CarCreator);
    const auto sync = [&]() {
        auto physical = design; physical.name = "Preview"; physical.body = "preview.glb"; physical.wheel = "preview.glb";
        car.set_design(physical); car.reset({0,car.ride_height(),0},0);
    };
    sync();
    std::string original=snapshot(design);
    const auto save_design = [&]() {
        if (!renderer.available(design,status) || !design.save(directory,status)) return false;
        if (saved_designs) {
            auto found = std::find_if(saved_designs->begin(),saved_designs->end(),[&](const auto& d){return d.name==design.name;});
            if (found==saved_designs->end()) saved_designs->push_back(design); else *found = design;
        }
        original=snapshot(design); status = "Car saved"; return true;
    };
    const auto leave=[&]() {return leave_draft(design,original,save_design,status);};
    while (true) {
        if (WindowShouldClose()) { if (leave()) break; interactive=false; continue; }
        const int width = GetScreenWidth(), height = GetScreenHeight();
        const auto command = interactive ? menu.update() : MenuCommand::None;
        const bool active = interactive && !menu.blocking() && !menu.interacted() && IsWindowFocused();
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL);
        if (command==MenuCommand::CloseCreator || (active && IsKeyPressed(KEY_ESCAPE))) { if (leave()) break; interactive=false; continue; }
        if (command==MenuCommand::NewCar) {
            if (leave()) {design={}; original=snapshot(design); sync(); tab=scroll=0; naming=false; status.clear();}
            interactive=false; continue;
        }
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
        ui::draw_panel({8,40,float(width-16),float(height-80)});
        if (tab!=2) ui::draw_window({view.x-4,44,view.width+8,view.height+44},"Car preview");
        DrawTexturePro(preview.texture,{0,0,float(preview.texture.width),-float(preview.texture.height)},view,{0,0},0,WHITE);
        ui::draw_frame(view,true); text("Right-drag: orbit / Wheel: zoom / Yellow: front",view.x,view.y+view.height+12,14);
        const auto control = [&](Rectangle r,const std::string& label,bool selected=false,bool enabled=true) {return button(r,label,selected,enabled,active,16);};
        const char* tabs[] = {"Body","Wheel","Tuning"};
        for (int i=0;i<3;++i) if (control({20+i*96.f,44,88,30},tabs[i],tab==i)) { tab=i; scroll=0; naming=false; drag=-1; }
        if (tab==2) tuning.draw(car,width,height);
        else {
            const Rectangle name{20,102,370,34}; text("Name",20,82,14);
            if (active && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) naming = hit(name);
            if (naming && active) input(design.name,name);
            else { ui::draw_panel(name,true,ui::highlight); text(fit(design.name,16,name.width-20),name.x+10,name.y+9,16); }
            if (control({20,146,180,30},"Regular",design.type==CarType::Civilian)) design.type=CarType::Civilian;
            if (control({210,146,180,30},"Police",design.type==CarType::Police)) design.type=CarType::Police;
            if (control({20,186,370,28},"Refresh files")) {
                renderer.refresh(); bodies=car_models(car_asset_directory("cars"),status); wheels=car_models(car_asset_directory("wheels"),status);
                scroll=0;
            }
            const auto& models = tab==1 ? wheels : bodies;
            if (tab==0) {
                float* values[] = {&design.width,&design.height,&design.length,&design.offset[0],&design.offset[1],&design.offset[2]};
                const float lo[] = {.8f,.3f,1.5f,-4,-4,-4}, hi[] = {4,4,8,4,4,4};
                const char* labels[] = {"Width","Height","Length","Offset X","Offset Y","Offset Z"};
                for (int i=0;i<6;++i) {
                    text(TextFormat("%s: %.2f m",labels[i],double(*values[i])),20,224+i*34.f,13);
                    if (mesh_slider({164,228+i*34.f,226,10},*values[i],lo[i],hi[i],i,drag,active)) sync();
                }
            }
            const float y = tab==0 ? 436.f : 230.f;
            text(tab==1 ? "assets/wheels / automatic tire radius" : "assets/cars / embedded textures",20,y-18,14);
            const Rectangle list{20,y,370,height-y-130}; const int rows = std::max(1,int(list.height/32));
            const int count = int(models.size());
            if (active && hit(list)) scroll -= int(GetMouseWheelMove());
            scroll = std::clamp(scroll,0,std::max(0,count-rows));
            if (!count) { text("Add a GLB, then Refresh files",20,y+4,15); }
            for (int row=0;row<rows && scroll+row<count;++row) {
                const int index=scroll+row; const auto label=models[index];
                const bool selected=(tab==1 ? design.wheel : design.body)==label;
                if (list_item({20,y+row*32,370,28},label,selected,true,active,16)) {
                    if (tab==1) design.wheel=label; else design.body=label;
                    std::string error; status = renderer.available(design,error) ? "Body and wheel ready" : error;
                }
            }
        }
        const bool saving=control({20,float(height-72),172,34},"Save car",true) || command==MenuCommand::SaveCar || (active && ctrl && IsKeyPressed(KEY_S));
        const bool use=button({208,float(height-72),172,34},"Save and close",true,true,active,14) || command==MenuCommand::UseCar;
        const bool close=control({396,float(height-72),172,34},"Close");
        if ((saving||use) && save_design() && use) result=design;
        text(fit(status.empty() ? "Choose a body and wheel, size the body, then save." : status,16,width-40.f),20,height-27.f,16,accent);
        menu.draw(); text("CAR EDITOR",width-136.f,8,16,ink); EndDrawing(); interactive=true;
        if (!screenshot.empty() && ++frames>=3) { auto capture=LoadImageFromScreen(); ExportImage(capture,screenshot.c_str()); UnloadImage(capture); break; }
        if (result) break;
        if (close) { if (leave()) break; interactive=false; }
        if (command==MenuCommand::SavedCars) {
            const auto loaded=saved_picker([&]{return saved_cars(directory,status);},"Saved cars","Open car",design.name,status,directory);
            if (loaded && leave()) { design=*loaded; original=snapshot(design); sync(); tab=scroll=0; status.clear(); }
            interactive=false; naming=false; drag=-1;
        }
    }
    if (preview.id) UnloadRenderTexture(preview);
    return result;
}

std::optional<CharacterDesign> character_builder(CharacterDesign design,const std::filesystem::path& directory,const std::string& screenshot) {
    CharacterRenderer renderer; SceneLighting lighting; GraphicsSettings graphics; graphics.shadows=0;
    DayNight day; day.set_time("09:00"); PhysicsWorld world(false); Character character(world);
    character.reset({0,.08f,0});
    std::string status;
    std::array<std::vector<std::string>,9> files;
    const auto refresh=[&]() {
        renderer.refresh();
        for (std::size_t i=0;i<files.size();++i) files[i]=car_models(car_asset_directory((std::string("character/")+character_slot_folders[i]).c_str()),status);
    };
    refresh();
    int slot=0,scroll=0,drag=-1,frames=0,pose=0;
    float yaw=2.65f,pitch=.2f,zoom=1,accumulator=0;
    bool interactive=false,naming=false,guides=true; RenderTexture2D preview{}; std::optional<CharacterDesign> result;
    MenuBar menu(MenuMode::CharacterCreator);
    std::string original=snapshot(design);
    const auto save_design=[&]() {
        if (!renderer.available(design,status) || !design.save(directory,status)) return false;
        original=snapshot(design); status="Character saved"; return true;
    };
    const auto leave=[&]() {return leave_draft(design,original,save_design,status);};
    while (true) {
        if (WindowShouldClose()) { if (leave()) break; interactive=false; continue; }
        const int width=GetScreenWidth(),height=GetScreenHeight();
        const auto command=interactive ? menu.update() : MenuCommand::None;
        const bool active=interactive && !menu.blocking() && !menu.interacted() && IsWindowFocused();
        const bool ctrl=IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL);
        if (command==MenuCommand::CloseCreator || (active && IsKeyPressed(KEY_ESCAPE))) { if (leave()) break; interactive=false; continue; }
        if (command==MenuCommand::NewCharacter) {
            if (leave()) {design={}; original=snapshot(design); slot=scroll=pose=0; naming=false; character.reset({0,.08f,0}); status.clear();}
            interactive=false; continue;
        }
        if (!active || !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) drag=-1;
        accumulator+=std::min(GetFrameTime(),.1f);
        while (accumulator>=fixed_step) {
            world.step(); character.step({pose==1 || pose==2 ? Vec3(0,0,-1) : Vec3::sZero(),pose==2}); accumulator-=fixed_step;
        }
        const Rectangle view{414,84,float(width-438),float(height-352)};
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
        ui::draw_panel({8,40,float(width-16),float(height-68)});
        ui::draw_window({view.x-4,44,view.width+8,view.height+44},"Character preview / Right-drag: orbit / Wheel: zoom");
        DrawTexturePro(preview.texture,{0,0,float(preview.texture.width),-float(preview.texture.height)},view,{0,0},0,WHITE);
        ui::draw_frame(view,true);
        const auto control=[&](Rectangle r,const std::string& label,bool selected=false,bool enabled=true) {return button(r,label,selected,enabled,active,15);};
        text("Character parts",20,50,18,accent);
        const Rectangle name{20,102,370,34}; text("Name",20,82,14);
        if (active && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) naming=hit(name);
        if (naming && active) input(design.name,name); else { ui::draw_panel(name,true,ui::highlight); text(fit(design.name,15,name.width-20),name.x+10,name.y+9,15); }
        for (int i=0;i<3;++i) if (control({20+i*126.f,146,118,30},character_type_names[i],int(design.type)==i)) design.type=CharacterType(i);
        if (control({20,186,370,28},"Refresh files")) {refresh(); scroll=0;}
        {
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
                if (list_item({20,382+row*32.f,370,28},file,part.model==file,true,active,15)) {part.model=file; status.clear();}
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
        }
        const char* poses[]={"Idle","Walk","Run","Ragdoll"};
        for (int i=0;i<4;++i) if (button({view.x+i*114.f,view.y+view.height+10,106,26},poses[i],pose==i,true,active,14)) {
            pose=i; character.reset({0,.08f,0}); character.revive();
            if (i==3) character.ragdoll({0,1,0},{18,0,-25});
        }
        if (control({view.x+456,view.y+view.height+10,view.width-456,26},"Rig",guides)) guides=!guides;
        const bool saving=control({20,float(height-64),172,34},"Save character",true) || command==MenuCommand::SaveCharacter || (active && ctrl && IsKeyPressed(KEY_S));
        const bool use=control({208,float(height-64),172,34},"Save and close",true) || command==MenuCommand::UseCharacter;
        const bool close=control({396,float(height-64),140,34},"Close");
        if ((saving || use) && save_design() && use) result=design;
        text(fit(status.empty() ? "Fit GLBs to the rig; paired limbs and shoes mirror automatically." : status,15,width-40.f),20,height-24.f,15,accent);
        menu.draw(); text("CHARACTER CREATOR",width-224.f,8,16,ink); EndDrawing(); interactive=true;
        if (!screenshot.empty() && ++frames>=3) {auto capture=LoadImageFromScreen(); ExportImage(capture,screenshot.c_str()); UnloadImage(capture); break;}
        if (result) break;
        if (close) { if (leave()) break; interactive=false; }
        if (command==MenuCommand::SavedCharacters) {
            const auto loaded=saved_picker([&]{return saved_characters(directory,status);},"Saved characters","Open character",design.name,status,directory);
            if (loaded && leave()) {design=*loaded; original=snapshot(design); scroll=0; pose=0; character.reset({0,.08f,0}); status.clear();}
            interactive=false; naming=false; drag=-1;
        }
    }
    if (preview.id) UnloadRenderTexture(preview);
    return result;
}

bool main_menu(City& city,const std::filesystem::path& directory,ControllerMapping& controls,GraphicsSettings& graphics,const std::string& screenshot) {
    EnableCursor(); SetWindowTitle("Ambaretto - Main menu");
    const char* home[]={"Play", "Create / edit", "Settings", "Quit"};
    const char* editors[]={"Cities", "Object Builder", "Cars", "Characters", "Back"};
    int selection=0,frames=0; bool interactive=false,editing=false;
    MenuBar menu(MenuMode::Main);
    const auto mapping_path=directory.parent_path()/"controller-mappings.ini";
    while (!WindowShouldClose()) {
        const auto command=!interactive ? MenuCommand::None : menu.dialog_open()
            ? menu.update(controls,mapping_path,true,read_controllers()) : menu.update();
        const bool active=interactive && !menu.blocking() && !menu.interacted() && IsWindowFocused();
        const int count=editing ? int(std::size(editors)) : int(std::size(home));
        const auto entries=editing ? editors : home;
        if (active && IsKeyPressed(KEY_DOWN)) selection=(selection+1)%count;
        if (active && IsKeyPressed(KEY_UP)) selection=(selection+count-1)%count;
        int chosen=active && IsKeyPressed(KEY_ENTER) ? selection : -1;
        const float height=164.f+count*56;
        const Rectangle r{(GetScreenWidth()-620)/2.f,(GetScreenHeight()-height)/2.f,620,height};
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(r,editing ? "Create / edit" : "Ambaretto / Main menu"); ui::draw_window_close(r);
        text(editing ? "Choose an editor" : "Play a city or create something new",r.x+24,r.y+48,19,accent);
        for (int i=0;i<count;++i) {
            const Rectangle row{r.x+24,r.y+82+i*56.f,572,42};
            if (active && hit(row) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) chosen=selection=i;
            button(row,entries[i],selection==i,true,active,20);
        }
        text("Up / Down: select   Enter: open   F10: menu",r.x+24,r.y+r.height-40,15);
        const bool back=close_clicked(r,active) || (active && IsKeyPressed(KEY_ESCAPE));
        if (menu.dialog_open()) menu.draw(controls,mapping_path); else menu.draw();
        EndDrawing(); interactive=true;
        if (!screenshot.empty() && ++frames>=3) {auto image=LoadImageFromScreen(); ExportImage(image,screenshot.c_str()); UnloadImage(image); return false;}
        if (command==MenuCommand::Controls || command==MenuCommand::About) {menu.show(command); interactive=false; continue;}
        if (command==MenuCommand::Quit || (!editing && (back || chosen==3))) return false;
        if ((editing && (back || chosen==4)) || command==MenuCommand::Editors || (!editing && chosen==1)) {
            editing=command==MenuCommand::Editors || (!editing && chosen==1); selection=0;
        } else if (command==MenuCommand::Graphics || command==MenuCommand::Controllers) {
            settings_menu(controls,graphics,{},command);
        } else if (command==MenuCommand::PlayCity || (!editing && chosen==0)) {
            SetWindowTitle("Ambaretto - Play"); std::string status;
            const auto selected=saved_picker([&]{return saved_cities(directory,status);},"Play / Choose a city","Play city",city.name,status,{},
                [](const City& value,std::string& reason){if (value.spawn) return true; reason="Set a player spawn in the city editor to play."; return false;});
            if (selected) {city=*selected; return true;}
        } else if (editing && chosen==0) {
            if (city_menu(city,directory,controls,graphics)) return true;
        } else if (editing && chosen>0) {
            design_menu(DesignKind(chosen-1),directory.parent_path());
        } else if (!editing && chosen==2) {
            settings_menu(controls,graphics);
        } else continue;
        SetWindowTitle(editing ? "Ambaretto - Create / edit" : "Ambaretto - Main menu"); interactive=false;
    }
    return false;
}

void design_menu(DesignKind kind,const std::filesystem::path& root,const std::string& screenshot,bool create_new) {
    EnableCursor(); std::string error;
    if (kind==DesignKind::Building) {
        SetWindowTitle("Ambaretto - Object Builder"); BuildingMesh mesh;mesh.name="New object";mesh.type=ObjectType::Object;
        const auto build=[&](BuildingMesh value){building_builder(std::move(value),root/"buildings",screenshot);};
        if (create_new) build(mesh);
        else open_design_menu([&]{return saved_buildings(root/"buildings",error);},"Object Builder / Create or open",root/"buildings",error,screenshot,build);
    } else if (kind==DesignKind::Car) {
        SetWindowTitle("Ambaretto - Cars"); CarDesign design;
        const auto build=[&](CarDesign value){car_builder(std::move(value),root/"cars",screenshot);};
        if (create_new) build(design);
        else open_design_menu([&]{return saved_cars(root/"cars",error);},"Cars / Create or open",root/"cars",error,screenshot,build);
    } else if (kind==DesignKind::Character) {
        SetWindowTitle("Ambaretto - Characters"); CharacterDesign design;
        const auto build=[&](CharacterDesign value){character_builder(std::move(value),root/"characters",screenshot);};
        if (create_new) build(design);
        else open_design_menu([&]{return saved_characters(root/"characters",error);},"Characters / Create or open",root/"characters",error,screenshot,build);
    }
}
void settings_menu(ControllerMapping& controls,GraphicsSettings& graphics,const std::string& screenshot,MenuCommand initial_settings) {
    SetWindowTitle("Ambaretto - Settings"); EnableCursor();
    const auto root=std::filesystem::path(GetApplicationDirectory());
    MenuBar menu(MenuMode::Cities); GraphicsPanel panel;
    if (initial_settings==MenuCommand::Graphics) panel.open(graphics);
    if (initial_settings==MenuCommand::Controllers) menu.show(initial_settings);
    std::string status; bool interactive=false; int frames=0, selected=0;
    while (true) {
        if (WindowShouldClose()) {
            if (menu.save_pending(controls,root/"controller-mappings.ini")) break;
            status="Cannot save controller mappings; retry Save & close.";
        }
        const bool dialog_was_open = panel.visible() || menu.blocking();
        if (interactive && menu.dialog_open()) menu.update(controls,root/"controller-mappings.ini",true,read_controllers());
        if (panel.visible() && interactive && !menu.blocking()) {
            const auto action=panel.update();
            if (action==GraphicsPanelAction::Apply && panel.pending().save(root/"graphics-settings.ini",status)) {
                graphics=panel.pending(); panel.close(); status="Graphics settings saved";
                if (graphics.vsync) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT);
                SetTargetFPS(graphics.fps_limit);
            } else if (action==GraphicsPanelAction::Cancel) panel.close();
        }
        const bool active=interactive && !dialog_was_open && !panel.visible() && !menu.blocking();
        if (active) {
            const int direction = int(IsKeyPressed(KEY_DOWN))-int(IsKeyPressed(KEY_UP));
            if (direction) selected=(selected+direction+3)%3;
            if (IsKeyPressed(KEY_TAB)) selected=(selected+(IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT) ? 2 : 1))%3;
        }
        BeginDrawing(); ui::draw_desktop();
        const Rectangle r{(GetScreenWidth()-600)/2.f,(GetScreenHeight()-320)/2.f,600,320};
        ui::draw_window(r,"Settings"); ui::draw_window_close(r);
        const bool enter=active && IsKeyPressed(KEY_ENTER);
        const bool open_graphics=button({r.x+24,r.y+76,552,46},"Graphics",selected==0,true,active) || (enter && selected==0);
        const bool open_controls=button({r.x+24,r.y+146,552,46},"Controller mapping",selected==1,true,active) || (enter && selected==1);
        text("Presets, shadows, draw distance and performance",r.x+30,r.y+125,14);
        text("Keyboard, gamepad bindings and stick deadzone",r.x+30,r.y+195,14);
        const bool close=close_clicked(r,active) || button({r.x+24,r.y+230,552,40},"Back",selected==2,true,active) || (active && (IsKeyPressed(KEY_ESCAPE) || (enter && selected==2)));
        text(status,r.x+24,r.y+280,16,accent);
        if (panel.visible()) panel.draw(float(GetFPS()),status);
        if (menu.dialog_open()) menu.draw(controls,root/"controller-mappings.ini");
        EndDrawing(); interactive=true;
        if (!screenshot.empty() && ++frames>=3) {auto image=LoadImageFromScreen(); ExportImage(image,screenshot.c_str()); UnloadImage(image); break;}
        if (initial_settings!=MenuCommand::None && dialog_was_open && !panel.visible() && !menu.blocking()) break;
        if (close) break;
        if (open_graphics) {selected=0; panel.open(graphics); interactive=false;}
        if (open_controls) {selected=1; menu.show(MenuCommand::Controllers); interactive=false;}
    }
    menu.save_pending(controls,root/"controller-mappings.ini");
}

std::optional<CarDesign> choose_play_car(const std::filesystem::path& directory) {
    SetWindowTitle("Ambaretto - Choose your car");
    std::string status; auto library=saved_cars(directory,status);
    int selected=library.empty() ? -1 : 0;
    bool interactive=false; SavedPreview preview; std::optional<CarDesign> result;
    while (!WindowShouldClose()) {
        const float width=std::min(920.f,float(GetScreenWidth()-48));
        const Rectangle r{(GetScreenWidth()-width)/2,40,width,float(GetScreenHeight()-80)};
        const Rectangle view{r.x+24,r.y+64,r.width-48,r.height-236};
        const bool active=interactive && IsWindowFocused();
        const auto cycle=[&](int direction) {
            selected=(selected+direction+int(library.size()))%int(library.size());
        };
        if (active && library.size()>1) {
            const int direction=int(IsKeyPressed(KEY_RIGHT)||IsKeyPressed(KEY_DOWN))-int(IsKeyPressed(KEY_LEFT)||IsKeyPressed(KEY_UP));
            if (direction) cycle(direction);
        }
        preview.render(selected>=0 ? &library[selected] : nullptr,view,active);
        const bool ready=preview.valid();
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(r,"Car only / Choose your car"); ui::draw_window_close(r);
        preview.draw(view);
        const auto centered=[&](const std::string& value,float y,int size,Color color) {
            const auto label=fit(value,size,view.width-32);
            text(label,r.x+(r.width-ui::measure_text(label.c_str(),size))/2,y,size,color);
        };
        centered(selected>=0 ? library[selected].name : "No saved cars yet",r.y+r.height-160,22,ink);
        centered(selected>=0 ? std::to_string(selected+1)+" / "+std::to_string(library.size()) : "",r.y+r.height-132,16,muted);
        const bool previous=button({r.x+24,r.y+r.height-108,140,36},"< Previous",false,library.size()>1,active,17);
        const bool next=button({r.x+r.width-164,r.y+r.height-108,140,36},"Next >",false,library.size()>1,active,17);
        const bool drive=button({r.x+(r.width-200)/2,r.y+r.height-108,200,36},"Drive this car",true,ready,active,18)
            || (active && IsKeyPressed(KEY_ENTER));
        const bool back=close_clicked(r,active) || button({r.x+24,r.y+r.height-52,100,30},"Back",false,true,active,16)
            || (active && IsKeyPressed(KEY_ESCAPE));
        const bool refresh=button({r.x+r.width-164,r.y+r.height-52,140,30},"Refresh saved",false,true,active,16);
        text(fit(status.empty() ? "Left / Right: cycle   Enter: drive" : status,14,r.width-340),r.x+148,r.y+r.height-44,14,accent);
        EndDrawing(); interactive=true;
        if (back) break;
        if (drive && ready) {result=library[selected]; break;}
        if (previous || next) cycle(next ? 1 : -1);
        if (refresh) {
            const auto name=selected>=0 ? library[selected].name : "";
            status.clear(); library=saved_cars(directory,status); preview.refresh(); selected=library.empty() ? -1 : 0;
            for (int i=0;i<int(library.size());++i) if (library[i].name==name) selected=i;
        }
    }
    return result;
}

namespace {
std::optional<CarDesign> choose_car(const std::optional<CarDesign>& current,const std::filesystem::path& directory) {
    std::string status; bool first_load=true;
    const auto load=[&] {
        auto library=saved_cars(directory,status);
        if (first_load && current && std::none_of(library.begin(),library.end(),[&](const auto& d){return d.name==current->name;})) library.push_back(*current);
        first_load=false;
        return library;
    };
    return saved_picker(load,"Choose a saved car","Choose car",current ? current->name : "",status,directory,{},
        [&]{design_menu(DesignKind::Car,directory.parent_path());},true);
}
std::optional<CharacterDesign> choose_player_character(const std::optional<CharacterDesign>& current,const std::filesystem::path& directory) {
    std::string status; bool first_load=true;
    const auto load=[&] {
        auto library=saved_characters(directory,status);
        library.erase(std::remove_if(library.begin(),library.end(),[](const auto& design){return design.type!=CharacterType::Player;}),library.end());
        if (first_load && current && std::none_of(library.begin(),library.end(),[&](const auto& d){return d.name==current->name;})) library.push_back(*current);
        first_load=false;
        return library;
    };
    return saved_picker(load,"Choose player character","Use",current ? current->name : "",status,directory,{},
        [&]{design_menu(DesignKind::Character,directory.parent_path());},true);
}
enum class EditorResult { Back, Play, Quit };
std::optional<RaceMap> race_settings(RaceMap race, bool existing, bool& remove) {
    bool interactive=false; remove=false;
    while (!WindowShouldClose()) {
        const Rectangle panel{(GetScreenWidth()-720)/2.f,(GetScreenHeight()-310)/2.f,720,310};
        const bool active=interactive && IsWindowFocused();
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(panel,"Race name and laps"); ui::draw_window_close(panel);
        if (active) race.laps=std::clamp(race.laps+int(IsKeyPressed(KEY_UP))-int(IsKeyPressed(KEY_DOWN)),1,99);
        text("Race name",panel.x+24,panel.y+52);
        if (active) input(race.name,{panel.x+24,panel.y+78,672,40}); else text(race.name,panel.x+36,panel.y+88);
        text("Laps: "+std::to_string(race.laps),panel.x+24,panel.y+142);
        text("Up / Down: adjust laps",panel.x+296,panel.y+142,16);
        if (button({panel.x+168,panel.y+130,48,36},"-",false,race.laps>1,active)) --race.laps;
        if (button({panel.x+224,panel.y+130,48,36},"+",false,race.laps<99,active)) ++race.laps;
        text("Click the start, then checkpoints. The last point returns to the start.",panel.x+24,panel.y+192,16);
        const bool accept=button({panel.x+24,panel.y+242,224,40},"Map checkpoints",true,!race.name.empty(),active) || (active && IsKeyPressed(KEY_ENTER) && !race.name.empty());
        const bool deleting=existing && button({panel.x+264,panel.y+242,200,40},"Delete race",false,true,active);
        const bool cancel=close_clicked(panel,active) || button({panel.x+480,panel.y+242,216,40},"Cancel",false,true,active) || (active && IsKeyPressed(KEY_ESCAPE));
        EndDrawing(); interactive=true;
        if (cancel) return {};
        if (deleting) { remove=true; return race; }
        if (accept) return race;
    }
    return {};
}
EditorResult editor(City& city,const std::filesystem::path& directory,const std::string& screenshot, bool playing = false, bool car_only = false, bool create_race = false) {
    const bool converted=!city.freeform;
    city.make_freeform();
    Tool tool = Tool::Select;
    int building_size = 1, building_height = 12, vehicle_kind = 0, rotation = 0;
    float building_rotation = 0, road_width = 9.8f;
    int brush_size = 3, density = 2, revision = 0, preview_revision = -1;
    std::string ground_texture = "soil.png";
    int selected_building = -1, selected_vehicle = -1;
    std::optional<BuildingMesh> building_mesh;
    std::optional<CarDesign> selected_car;
    CarRenderer car_renderer;
    CharacterRenderer character_renderer;
    PhysicsWorld character_preview_world(false); Character character_preview(character_preview_world);
    const auto characters_directory=directory.parent_path()/"characters";
    bool dirty = converted, dragging = false, top = false, grid = false, close_failed = false;
    bool painting = false, stroke_saved = false, brush_remove = false, brush_sampled = false;
    bool object_brush = false, tree_brush = true;
    bool panning = false, pan_moved = false;
    Vector2 pan_start{}; Vec3 pan_focus = Vec3::sZero();
    float save_time = 0, preview_time = 0, yaw = .78539816f;
    Vec3 brush_point = Vec3::sZero(), last_brush = Vec3::sZero();
    std::unique_ptr<Environment> preview_environment;
    std::unique_ptr<EnvironmentRenderer> preview_renderer;
    BuildingRenderer placement_renderer;
    std::string placement_signature;
    int preview_selection=-1;
    std::string preview_error;
    CityPath pen;
    using PenState=std::pair<CityPath,float>;
    std::vector<PenState> pen_undo,pen_redo;
    std::optional<PenState> pen_before_drag;
    int editing_area=-1, editing_road=-1, pen_node=-1, pen_handle=0;
    bool pen_dragging=false, pen_closing=false, rotation_saved=false, moving_building=false;
    Vector2 pen_drag_start{};
    Vec3 building_offset=Vec3::sZero();
    Vector2 building_drag_start{};
    SceneLighting lighting;
    GraphicsSettings graphics; graphics.shadows = 0; graphics.view_distance = 6000;
    DayNight preview_day; preview_day.set_time("09:00");
    Vec3 focus = city.spawn.value_or(Vec3::sZero()); focus.SetY(0);
    Camera3D camera{{0,500,500},vector(focus),{0,1,0},400,CAMERA_ORTHOGRAPHIC};
    CityCell anchor{}, hover{}; std::string status;
    std::optional<RaceMap> race_draft;
    int race_index=-1, race_point=-1;
    std::vector<RaceLeg> race_preview;
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
    MenuBar menu(playing ? MenuMode::PlayEditor : MenuMode::Editor);
    const auto clear_pen=[&]() {pen={};editing_area=editing_road=pen_node=-1;pen_handle=0;pen_dragging=pen_closing=false;pen_undo.clear();pen_redo.clear();pen_before_drag.reset();};
    const auto pen_state=[&]() {return PenState{pen,road_width};};
    const auto remember_pen=[&](PenState before) {
        if (before==pen_state()) return;
        pen_undo.push_back(std::move(before));if (pen_undo.size()>32) pen_undo.erase(pen_undo.begin());pen_redo.clear();
    };
    const auto choose_tool = [&](int index) { tool = Tool(index); painting = dragging = panning = moving_building = false; selected_building = selected_vehicle = -1; clear_pen(); };
    const auto commit = [&](City next) {
        undo.push_back(city); if (undo.size()>32) undo.erase(undo.begin()); redo.clear();
        city = std::move(next); dirty = true; save_time = 0;
        ++revision;
        selected_building = selected_vehicle = -1;
    };
    const auto pen_tool=[&]() {return tool==Tool::Land || tool==Tool::Water || tool==Tool::Road || tool==Tool::Ground;};
    const auto brush_tool=[&]() {return (tool==Tool::Trees && tree_brush) || (tool==Tool::Building && object_brush && building_mesh && building_mesh->type==ObjectType::Object);};
    const auto apply_pen=[&]() {
        if (pen.nodes.empty()) return true;
        if (editing_road>=0 && city.paths[editing_road].path==pen && city.paths[editing_road].width==road_width) {clear_pen();return true;}
        City next=city; CityPath path=pen;
        if (tool!=Tool::Road) path.closed=true;
        bool okay;
        if (tool==Tool::Road) {PathRoad road;road.path=std::move(path);road.width=road_width;okay=next.add_path_road(std::move(road),status,editing_road);}
        else {CityArea area;area.path=std::move(path);area.kind=tool==Tool::Land ? AreaKind::Land : tool==Tool::Water ? AreaKind::Water : AreaKind::Texture;area.texture=ground_texture;okay=next.add_area(std::move(area),status,editing_area);}
        if (!okay) return false;
        commit(std::move(next));clear_pen();status="Path applied / Ctrl+Z to undo";return true;
    };
    const auto apply_race = [&]() {
        if (!race_draft) return true;
        City next=city;
        if (race_index>=0 && race_index<int(next.races.size())) next.races[race_index]=*race_draft; else next.races.push_back(*race_draft);
        if (!next.validate(status)) return false;
        commit(std::move(next)); race_draft.reset(); race_preview.clear(); choose_tool(int(Tool::Select)); status="Race saved / Ctrl+Z to undo"; return true;
    };
    const auto finish = [&](EditorResult result) {
        if (pen_tool() && !apply_pen()) return false;
        if (!apply_race()) return false;
        if (result==EditorResult::Play && !preview_error.empty()) { status = preview_error; return false; }
        if (result==EditorResult::Play && car_only && !city.spawn) { status="Set a spawn to return to Car only."; return false; }
        if (result==EditorResult::Play && !car_only && !character_renderer.available(city,status)) return false;
        if (dirty && !save(city,directory,status,dirty)) return false;
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        return true;
    };
    std::string library_error, update_error;
    const auto cars = saved_cars(directory.parent_path()/"cars",library_error);
    City refreshed = city;
    const auto characters=saved_characters(characters_directory,library_error);
    bool designs_changed=refreshed.update_player_character(characters);
    designs_changed|=refreshed.update_car_designs(cars,update_error)>0;
    for (const auto& design : designs) {
        std::string error; const int count=refreshed.update_building_design(design,error);
        if (count<0) update_error+=design.name+": "+error+"; "; else designs_changed|=count>0;
    }
    if (designs_changed) {
        commit(std::move(refreshed)); status = "Saved designs updated / Ctrl+Z to undo";
    }
    if (!update_error.empty()) status = "Map update rejected: "+update_error;
    else if (!library_error.empty()) status = library_error;
    int frames = 0; bool first_frame = true;
    while (true) {
        if (first_frame) SetWindowTitle("Ambaretto - City editor");
        float dt = std::min(GetFrameTime(),.1f); save_time += dt; preview_time += dt;
        const bool close_requested=screenshot.empty() && WindowShouldClose();
        if (!close_requested) close_failed=false;
        if (close_requested && !close_failed) {
            if (finish(EditorResult::Quit)) return EditorResult::Quit;
            close_failed = true;
        }
        if (dirty && !painting && save_time>1) { save(city,directory,status,dirty); save_time = -4; }
        if (pen_before_drag && (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) || !IsWindowFocused())) {
            remember_pen(std::move(*pen_before_drag));pen_before_drag.reset();
        }
        MenuState menu_state;
        std::string character_error;
        menu_state.undo = (!pen_undo.empty() || !undo.empty()) && !race_draft && !pen_dragging;
        menu_state.redo = (!pen_redo.empty() || (pen_undo.empty() && !redo.empty())) && !race_draft && !pen_dragging;
        menu_state.play = preview_error.empty() && (car_only ? city.spawn.has_value() : character_renderer.available(city,character_error));
        menu_state.selection = selected_building>=0 || selected_vehicle>=0;
        menu_state.building_selection = selected_building>=0;
        menu_state.car_selection = selected_vehicle>=0 && city.vehicles[selected_vehicle].kind==CityVehicleKind::Car;
        menu_state.vehicle = vehicle_kind; menu_state.density = density;
        menu_state.object_design=building_mesh && building_mesh->type==ObjectType::Object;
        menu_state.object_brush=object_brush;menu_state.tree_brush=tree_brush;
        menu_state.top = top; menu_state.grid = grid; menu_state.tool = int(tool);
        auto command = first_frame ? MenuCommand::None : menu.update(menu_state);
        if (create_race) { command=MenuCommand::NewRace; create_race=false; }
        const bool can_edit = !first_frame && !menu.blocking() && !menu.interacted() && IsWindowFocused();
        std::string control_hint;
        for (int i=0;i<int(std::size(editor_controls));++i) if (can_edit && hit(editor_control_rect(i))) {
            const auto& control=editor_controls[i];
            const bool enabled=editor_control_enabled(control.command,menu_state);
            control_hint=control.label;
            if (!enabled) control_hint+=" / unavailable";
            if (enabled && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) command=control.command;
        }
        bool choose_building=command==MenuCommand::SavedBuildings;
        if (command==MenuCommand::NewRace || command==MenuCommand::Races) {
            if (race_draft && !apply_race()) continue;
            const int chosen=command==MenuCommand::NewRace ? -2 : choose_race(city,true);
            if (chosen!=-1) {
                bool remove=false; RaceMap draft=chosen>=0 ? city.races[chosen] : RaceMap{};
                if (const auto configured=race_settings(draft,chosen>=0,remove)) {
                    if (remove) { City next=city; next.races.erase(next.races.begin()+chosen); commit(std::move(next)); status="Race deleted / Ctrl+Z to undo"; }
                    else { race_draft=*configured; race_index=chosen; race_point=-1; choose_tool(int(Tool::Race));
                        if (!race_draft->checkpoints.empty()) { focus=race_draft->checkpoints.front(); focus.SetY(0); }
                        build_race_route(city,*race_draft,race_preview,status); status="Click start, then checkpoints / click a marker to move it / Delete removes it"; }
                }
            }
            first_frame=true; continue;
        }
        if (command==MenuCommand::CharacterCreator) {design_menu(DesignKind::Character,directory.parent_path()); character_renderer.refresh(); first_frame=true; continue;}
        if (command==MenuCommand::ChoosePlayerCharacter) {
            if (const auto design=choose_player_character(city.player_character,characters_directory)) {
                City next=city; next.player_character=design; commit(std::move(next)); status="Player selected: "+design->name;
            }
            character_renderer.refresh(); painting=dragging=false; first_frame=true; continue;
        }
        if (command==MenuCommand::RefreshDesigns) {
            City next=city; std::string errors, issue;
            bool changed=next.update_player_character(saved_characters(characters_directory,status));
            changed|=next.update_car_designs(saved_cars(directory.parent_path()/"cars",status),issue)>0;
            errors=issue;
            for (const auto& design : saved_buildings(buildings_directory,status)) {
                const int count=next.update_building_design(design,issue);
                if (count<0) errors+=design.name+": "+issue+"; "; else changed|=count>0;
                if (building_mesh && building_mesh->name==design.name) building_mesh=design;
            }
            if (selected_car) for (const auto& design : saved_cars(directory.parent_path()/"cars",status)) if (design.name==selected_car->name) selected_car=design;
            if (changed) commit(std::move(next));
            refresh_designs(); car_renderer.refresh(); character_renderer.refresh();
            status=errors.empty() ? "Saved designs refreshed / Ctrl+Z to undo map changes" : "Some designs could not update: "+errors;
            continue;
        }
        const int builder_request = command==MenuCommand::BuildingCreator || command==MenuCommand::EditBuilding ? 0 : -1;
        const bool car_request = command==MenuCommand::CarEditor || command==MenuCommand::EditCar || command==MenuCommand::PlaceCar;
        const bool edit_building = command==MenuCommand::EditBuilding && selected_building>=0;
        const Rectangle document{float(sidebar+12),44,float(GetScreenWidth()-sidebar-24),float(GetScreenHeight()-108)};
        if (!can_edit) painting = dragging = panning = pen_dragging = moving_building = false;
        if (command==MenuCommand::Controls || command==MenuCommand::About) { help_dialog(true,command==MenuCommand::About); continue; }
        if (command==MenuCommand::StartTime) {
            const auto minutes = start_clock(city.start_minutes);
            if (minutes && *minutes != city.start_minutes) { City next = city; next.start_minutes = *minutes; commit(std::move(next)); status = "Starting time updated"; }
            continue;
        }
        if (command==MenuCommand::CitySettings) {
            const auto value = city_settings(city.pedestrian_density);
            if (value && *value!=city.pedestrian_density) { City next = city; next.pedestrian_density = *value; commit(std::move(next)); status = "Walking NPC density updated"; }
            painting=dragging=false; first_frame=true; continue;
        }
        if (command==MenuCommand::RoadEditor) {
            if (const auto style=road_editor(city.road_style); style && *style!=city.road_style) {
                City next=city; next.road_style=*style; commit(std::move(next)); status="Road style applied to entire city / Ctrl+Z to undo";
            }
            painting=dragging=false; first_frame=true; continue;
        }
        if (command==MenuCommand::Cities && finish(EditorResult::Back)) return EditorResult::Back;
        if (can_edit && (IsKeyPressed(KEY_ESCAPE)||close_clicked(document))) {
            if (!pen.nodes.empty()) {clear_pen();status="Path cancelled";continue;}
            const auto result = playing ? EditorResult::Play : EditorResult::Back;
            if (finish(result)) return result;
        }
        if (command==MenuCommand::Quit && finish(EditorResult::Quit)) return EditorResult::Quit;
        if (command==MenuCommand::PlayCity && finish(EditorResult::Play)) return EditorResult::Play;
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL);
        if (command==MenuCommand::GroundTool || (can_edit && !ctrl && IsKeyPressed(KEY_NINE))) {
            if (const auto chosen = ground_texture_picker(ground_texture)) {
                ground_texture = *chosen; choose_tool(8); preview_revision = -1;
            }
            continue;
        }
        const bool rotating=can_edit && !ctrl && IsKeyDown(KEY_R) && (tool==Tool::Building || selected_building>=0);
        if (command==MenuCommand::RotateBuilding || rotating) {
            const float delta=rotating ? dt*90.f : 5.f;
            if (selected_building>=0) {
                const int index = selected_building; City next = city; auto b = city.buildings[index];
                b.yaw = std::fmod(b.yaw+delta,360.f);
                if (next.add_building(b,status,index)) {
                    if (!rotation_saved) {commit(std::move(next));rotation_saved=true;}
                    else {city=std::move(next);dirty=true;save_time=0;++revision;}
                    selected_building=index;
                }
            } else building_rotation=std::fmod(building_rotation+delta,360.f);
        }
        if (!rotating) rotation_saved=false;
        if (command==MenuCommand::SaveCity || (can_edit && ctrl && IsKeyPressed(KEY_S))) { if ((!pen_tool() || apply_pen()) && apply_race()) save(city,directory,status,dirty); }
        if ((command==MenuCommand::Undo || (can_edit && ctrl && IsKeyPressed(KEY_Z))) && !race_draft && !pen_dragging) {
            if (!pen_undo.empty()) {
                pen_redo.push_back(pen_state());pen=std::move(pen_undo.back().first);road_width=pen_undo.back().second;pen_undo.pop_back();pen_node=-1;status="Path edit undone / Ctrl+Y to redo";
            } else if (!undo.empty()) {redo.push_back(city);city=std::move(undo.back());undo.pop_back();dirty=true;save_time=0;++revision;painting=dragging=false;selected_building=selected_vehicle=-1;clear_pen();status="Undone / Ctrl+Y to redo";}
        }
        if ((command==MenuCommand::Redo || (can_edit && ctrl && IsKeyPressed(KEY_Y))) && !race_draft && !pen_dragging) {
            if (!pen_redo.empty()) {
                pen_undo.push_back(pen_state());pen=std::move(pen_redo.back().first);road_width=pen_redo.back().second;pen_redo.pop_back();pen_node=-1;status="Path edit redone";
            } else if (pen_undo.empty() && !redo.empty()) {undo.push_back(city);city=std::move(redo.back());redo.pop_back();dirty=true;save_time=0;++revision;painting=dragging=false;selected_building=selected_vehicle=-1;clear_pen();status="Redone";}
        }
        if (command>=MenuCommand::SelectTool && command<=MenuCommand::WaterTool) choose_tool(int(command)-int(MenuCommand::SelectTool));
        if (command==MenuCommand::PanView || (can_edit && !ctrl && IsKeyPressed(KEY_H))) choose_tool(int(Tool::Pan));
        if (command==MenuCommand::PlainBuilding) { building_mesh.reset(); building_size=1; building_height=12; choose_tool(3); }
        if (command>=MenuCommand::PlaceCar && command<=MenuCommand::PlaceBoeing) { choose_tool(5); vehicle_kind = int(command)-int(MenuCommand::PlaceCar); }
        if (command==MenuCommand::RotateObject) {
            rotation = (rotation+1)%4;
            if (selected_vehicle>=0) {
                City next = city; auto v = next.vehicles[selected_vehicle]; v.rotation = rotation;
                if (next.add_vehicle(v,status,selected_vehicle)) commit(std::move(next));
            } else choose_tool(5);
        }
        if (command>=MenuCommand::TreesSparse && command<=MenuCommand::TreesDense) { choose_tool(7); tree_brush=true; density = int(command)-int(MenuCommand::TreesSparse)+1; }
        if (command==MenuCommand::BrushSmaller || command==MenuCommand::BrushLarger) { choose_tool(7); tree_brush=true; brush_size = std::clamp(brush_size+(command==MenuCommand::BrushLarger ? 1 : -1),1,12); }
        if (command==MenuCommand::TreesSingle || command==MenuCommand::TreesBrush) {choose_tool(7);tree_brush=command==MenuCommand::TreesBrush;}
        if (command==MenuCommand::ObjectSingle || (command==MenuCommand::ObjectBrush && menu_state.object_design)) {choose_tool(3);object_brush=command==MenuCommand::ObjectBrush;}
        if (menu_state.object_design && (command==MenuCommand::ObjectBrushSmaller || command==MenuCommand::ObjectBrushLarger)) {
            choose_tool(3);object_brush=true;brush_size=std::clamp(brush_size+(command==MenuCommand::ObjectBrushLarger ? 1 : -1),1,12);
        }
        if (menu_state.object_design && command>=MenuCommand::ObjectSparse && command<=MenuCommand::ObjectDense) {choose_tool(3);object_brush=true;density=int(command)-int(MenuCommand::ObjectSparse)+1;}
        for (int i = 0; i<10; ++i) if (i!=8 && can_edit && !ctrl && IsKeyPressed(i==9 ? KEY_ZERO : KEY_ONE+i)) {
            choose_tool(i); if (i==5) vehicle_kind = 0; if (i==7) density = 1;
        }
        if (command==MenuCommand::TopView || (can_edit && IsKeyPressed(KEY_V))) top = !top;
        if (command==MenuCommand::Grid || (can_edit && IsKeyPressed(KEY_G))) grid = !grid;
        if (command==MenuCommand::RotateLeft || (can_edit && IsKeyPressed(KEY_Q))) yaw -= 1.57079633f;
        if (command==MenuCommand::RotateRight || (can_edit && IsKeyPressed(KEY_E))) yaw += 1.57079633f;
        if (command==MenuCommand::ZoomIn || command==MenuCommand::ZoomOut) camera.fovy = std::clamp(camera.fovy*(command==MenuCommand::ZoomIn ? .85f : 1/.85f),80.f,3400.f);
        const Vector2 mouse = GetMousePosition();
        const Rectangle race_toolbar{float(GetScreenWidth()-600),86,570,76};
        const Rectangle pen_toolbar{float(GetScreenWidth()-600),86,570,98};
        const bool in_view = can_edit && mouse.x>sidebar+16 && mouse.x<GetScreenWidth()-16 && mouse.y>80 && mouse.y<GetScreenHeight()-68
            && !(race_draft && CheckCollisionPointRec(mouse,race_toolbar)) && !(pen_tool() && CheckCollisionPointRec(mouse,pen_toolbar));
        SetMouseCursor(in_view && tool==Tool::Pan ? MOUSE_CURSOR_POINTING_HAND : in_view && tool==Tool::Bulldoze ? MOUSE_CURSOR_CROSSHAIR : MOUSE_CURSOR_DEFAULT);
        if (in_view) {
            camera.fovy = std::clamp(camera.fovy*std::pow(.85f,GetMouseWheelMove()),80.f,3400.f);
            if (tool==Tool::Pan && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                panning=true; pan_moved=false; pan_start=mouse; pan_focus=focus;
            }
            const auto hand_delta=Vector2Subtract(mouse,pan_start);
            if (panning && Vector2Length(hand_delta)>4) pan_moved=true;
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)||IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)
                || (panning && pan_moved && IsMouseButtonDown(MOUSE_BUTTON_LEFT))) {
                const auto delta = panning ? hand_delta : GetMouseDelta();
                const float scale = camera.fovy/GetScreenHeight();
                focus = (panning ? pan_focus : focus)+(top ? Vec3(-delta.x,0,-delta.y) : Vec3(-std::cos(yaw)*delta.x-std::sin(yaw)*delta.y*2,0,std::sin(yaw)*delta.x-std::cos(yaw)*delta.y*2))*scale;
                focus.SetX(std::clamp(focus.GetX(),-City::extent,City::extent)); focus.SetZ(std::clamp(focus.GetZ(),-City::extent,City::extent));
            }
            if (panning && !pan_moved && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)
                && pick(preview_environment.get(),camera,mouse,hover,brush_point)) { focus=brush_point; focus.SetY(0); }
        }
        if (!in_view || IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) panning=false;
        const Vec3 pan = top ? Vec3::sAxisX() : Vec3(std::cos(yaw),0,-std::sin(yaw));
        const Vec3 forward = top ? Vec3::sAxisZ() : Vec3(std::sin(yaw),0,std::cos(yaw));
        if (can_edit && !ctrl) focus += (pan*float(IsKeyDown(KEY_RIGHT)-IsKeyDown(KEY_LEFT))+forward*float(IsKeyDown(KEY_DOWN)-IsKeyDown(KEY_UP)))*dt*camera.fovy*.6f;
        // 30-degree elevation produces a 2:1 dimetric ground grid.
        camera.target = vector(focus); camera.position = vector(focus+(top ? Vec3(0,800,.01f) : Vec3(std::sin(yaw)*600,600/std::sqrt(3.f),std::cos(yaw)*600)));
        camera.up = top ? Vector3{0,0,-1} : Vector3{0,1,0};
        const bool picked = in_view && pick(preview_environment.get(),camera,mouse,hover,brush_point,tool==Tool::Ground);
        const auto road_snap=picked && tool==Tool::Road && !IsKeyDown(KEY_LEFT_ALT) && !IsKeyDown(KEY_RIGHT_ALT)
            ? city.snap_road_point(brush_point,12*camera.fovy/GetScreenHeight()*(top ? 1.f : 2.f),editing_road) : std::optional<Vec3>{};
        if (!in_view || IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) painting = false;
        if (!IsWindowFocused()) dragging = false;
        if (picked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            anchor = hover;
            if (tool==Tool::Race && race_draft) {
                int marker=-1;
                for (int i = 0; i < int(race_draft->checkpoints.size()); ++i) {
                    const auto screen=GetWorldToScreen(vector(race_draft->checkpoints[i]+Vec3(0,1,0)),camera);
                    if (Vector2Distance(screen,mouse)<14) marker=i;
                }
                Vec3 point;
                if (marker>=0 && race_point<0) { race_point=marker; status="Click the road to move point "+std::to_string(marker+1)+" / Delete to remove"; }
                else if (!snap_race_point(city,brush_point,point)) status="Race points must be placed on the road.";
                else {
                    if (race_point>=0) { race_draft->checkpoints[race_point]=point; race_point=-1; }
                    else if (race_draft->checkpoints.size()<256) race_draft->checkpoints.push_back(point);
                    build_race_route(city,*race_draft,race_preview,status);
                    if (status.empty()) status="Road route ready / Save race to finish";
                }
            } else if (pen_tool()) {
                pen_before_drag=pen_state();
                bool closing=false;
                if (!pen.closed && pen.nodes.size()>=3) {
                    const auto first=GetWorldToScreen(vector(pen.nodes.front().position),camera);
                    closing=Vector2Distance(first,mouse)<12;
                }
                if (closing) {pen_closing=pen_dragging=true;pen_node=0;pen_handle=0;pen_drag_start=mouse;}
                else {
                    pen_node=-1;pen_handle=0;
                    for (int i=0;i<int(pen.nodes.size());++i) {
                        const auto& node=pen.nodes[i];
                        for (int handle : {-1,1,0}) {
                            if (handle!=0 && node.tangent.LengthSq()<.01f) continue;
                            const auto screen=GetWorldToScreen(vector(node.position+node.tangent*float(handle)),camera);
                            if (Vector2Distance(screen,mouse)<8) {pen_node=i;pen_handle=handle;break;}
                        }
                        if (pen_node>=0) break;
                    }
                    if (pen_node<0 && pen.nodes.size()<1024) {
                        Vec3 point(brush_point.GetX(),City::level,brush_point.GetZ());
                        if (road_snap) point=*road_snap;
                        pen_node=int(pen.nodes.size());pen.nodes.push_back({point,Vec3::sZero()});pen_handle=1;
                    }
                    pen_dragging=pen_node>=0;pen_drag_start=mouse;
                }
            } else if (brush_tool()) {
                painting = true; stroke_saved = brush_sampled = false; last_brush = brush_point;
                brush_remove = IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT);
            } else if (tool==Tool::Trees) {
                City next=city;
                if (next.place_tree(brush_point,status)) {commit(std::move(next));status="Tree placed / Ctrl+Z to undo";}
            } else if (tool==Tool::Select) {
                selected_building = pick_building(city,camera,mouse); selected_vehicle = city.vehicle_at(brush_point);
                if (selected_vehicle>=0) selected_building = -1;
                else if (selected_building<0) selected_building = city.building_at(brush_point);
                if (selected_vehicle>=0) { vehicle_kind = int(city.vehicles[selected_vehicle].kind); rotation = city.vehicles[selected_vehicle].rotation; selected_car=city.vehicles[selected_vehicle].car; }
                else if (selected_building>=0) {moving_building=true;building_offset=city.buildings[selected_building].base(City::level)-brush_point;building_drag_start=mouse;}
                else {
                    int road=-1;
                    for (int r=int(city.paths.size())-1;r>=0 && road<0;--r) for (std::size_t i=1;i<city.paths[r].centerline.size();++i)
                        if(path_distance(brush_point,city.paths[r].centerline[i-1],city.paths[r].centerline[i])<=city.paths[r].width/2){road=r;break;}
                    if(road>=0){choose_tool(int(Tool::Road));editing_road=road;pen=city.paths[road].path;road_width=city.paths[road].width;status="Editing road / drag nodes or handles / Enter: apply / Ctrl+Z: undo";}
                    else for(int a=int(city.areas.size())-1;a>=0;--a) if(polygon_contains(city.areas[a].boundary,brush_point)) {
                        const auto& area=city.areas[a];choose_tool(int(area.kind==AreaKind::Land ? Tool::Land : area.kind==AreaKind::Water ? Tool::Water : Tool::Ground));editing_area=a;pen=area.path;ground_texture=area.texture;break;
                    }
                }
            } else if (tool==Tool::Spawn || tool==Tool::Vehicle || tool==Tool::Bulldoze) {
                City next = city;
                CityVehicle vehicle{CityVehicleKind(vehicle_kind),brush_point,rotation};
                if (vehicle.kind==CityVehicleKind::Car) vehicle.car=selected_car;
                const bool configured = vehicle.kind!=CityVehicleKind::Car || selected_car.has_value();
                if (tool==Tool::Vehicle && !configured) status="Choose a car in Objects > Car > Choose car";
                const bool okay = tool==Tool::Spawn ? next.set_spawn(brush_point,status) : tool==Tool::Vehicle ? configured && next.add_vehicle(vehicle,status) : next.erase(brush_point,status);
                if (okay) { commit(std::move(next)); status = "Change applied"; }
            } else if (tool!=Tool::Pan) dragging = true;
        }
        if (pen_dragging && picked && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && pen_node>=0 && pen_node<int(pen.nodes.size()) && Vector2Distance(mouse,pen_drag_start)>3) {
            auto& node=pen.nodes[pen_node];const Vec3 point(brush_point.GetX(),City::level,brush_point.GetZ());
            if(pen_handle==0)node.position=road_snap.value_or(point);
            else {node.tangent=(point-node.position)*float(pen_handle);node.tangent.SetY(0);}
        }
        if (pen_closing && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && can_edit && Vector2Distance(mouse,pen_drag_start)<=3) {
            pen.closed=true;apply_pen();
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) || !IsWindowFocused() || !can_edit) pen_dragging=pen_closing=false;
        if (can_edit && pen_tool()) {
            if (IsKeyPressed(KEY_ENTER)) apply_pen();
            if ((IsKeyPressed(KEY_BACKSPACE)||IsKeyPressed(KEY_DELETE)) && !pen.nodes.empty()) {
                const auto before=pen_state();
                pen.nodes.erase(pen.nodes.begin()+(pen_node>=0 && pen_node<int(pen.nodes.size()) ? pen_node : int(pen.nodes.size())-1));pen_node=-1;
                remember_pen(before);
            }
        }
        if (moving_building && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            moving_building=false;
            if (picked && selected_building>=0 && Vector2Distance(mouse,building_drag_start)>3) {
                const int index=selected_building;City next=city;auto b=city.buildings[index];b.position=brush_point+building_offset;
                if(next.add_building(b,status,index)){commit(std::move(next));selected_building=index;status="Building moved / Ctrl+Z to undo";}
            }
        }
        if (painting && picked && IsMouseButtonDown(MOUSE_BUTTON_LEFT)
            && (!brush_sampled || (brush_point-last_brush).LengthSq() >= brush_size*brush_size*4.f)) {
            City next = city; int changed = 0; std::string brush_error;
            const Vec3 delta = brush_point-last_brush;
            const int steps = std::max(1,int(std::ceil(delta.Length()/(brush_size*4.f))));
            for (int i = 1; i <= steps; ++i) {
                const Vec3 point=last_brush+delta*(float(i)/steps);
                changed+=std::max(0,tool==Tool::Trees ? next.brush_trees(point,brush_size*8.f,density,brush_remove,brush_error)
                    : next.brush_objects(*building_mesh,point,brush_size*8.f,density,brush_remove,brush_error));
            }
            last_brush = brush_point; brush_sampled = true;
            if (changed) {
                if (!stroke_saved) { commit(std::move(next)); stroke_saved = true; }
                else { city = std::move(next); dirty = true; save_time = 0; ++revision; }
                status = tool==Tool::Trees ? std::to_string(city.trees.size())+" trees / "+(brush_remove ? "Clearing trees" : "Planting trees")
                    : brush_remove ? "Clearing "+building_mesh->name : "Painting "+building_mesh->name;
            }
            if (!brush_error.empty()) status = brush_error;
        }
        CityBuilding preview;preview.cell=hover;preview.size=building_size;preview.height=building_height;preview.mesh=building_mesh;
        preview.position=brush_point;preview.yaw=building_rotation;
        preview.position->SetY(city.terrain_height(brush_point.GetX(),brush_point.GetZ()));
        if (dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            dragging = false;
            if (picked) {
                City next = city;
                const bool okay = tool==Tool::Building && next.add_building(preview,status);
                if (okay) { commit(std::move(next)); status = "Change applied"; }
            }
        }
        if ((command==MenuCommand::DeleteSelection || (can_edit && tool==Tool::Select && IsKeyPressed(KEY_DELETE))) && (selected_building>=0||selected_vehicle>=0)) {
            City next = city;
            if (selected_vehicle>=0) next.vehicles.erase(next.vehicles.begin()+selected_vehicle);
            else next.buildings.erase(next.buildings.begin()+selected_building);
            commit(std::move(next));
        }
        if (can_edit && race_draft && IsKeyPressed(KEY_DELETE) && race_point>=0) {
            race_draft->checkpoints.erase(race_draft->checkpoints.begin()+race_point); race_point=-1; build_race_route(city,*race_draft,race_preview,status);
        }
        // ponytail: reuse the gameplay renderer at most 4 times/sec while painting; incremental meshes can serve larger forests.
        if ((preview_revision != revision || preview_selection!=selected_building)
            && ((!painting && !rotating) || (!rotating && preview_time>=.25f) || preview_selection!=selected_building || !preview_renderer)) {
            preview_renderer.reset(); preview_environment.reset(); preview_error.clear();
            try {
                City render_city=city;
                if(selected_building>=0)render_city.buildings.erase(render_city.buildings.begin()+selected_building);
                preview_environment = std::make_unique<Environment>(render_city);
                preview_renderer = std::make_unique<EnvironmentRenderer>(*preview_environment);
                if (!preview_renderer->building_warning().empty()) status = preview_renderer->building_warning();
            } catch (const std::exception& error) { preview_error = error.what(); status = preview_error; }
            preview_revision = revision; preview_selection=selected_building; preview_time = 0;
        }
        std::optional<CityBuilding> live_building;
        if(selected_building>=0) {
            live_building=city.buildings[selected_building];
            if(moving_building && picked && Vector2Distance(mouse,building_drag_start)>3) {
                live_building->position=brush_point+building_offset;
                live_building->position->SetY(city.terrain_height(brush_point.GetX(),brush_point.GetZ()));
            }
        } else if(picked && tool==Tool::Building && !brush_tool())live_building=preview;
        bool placement_ready=false;
        if(live_building) {
            const auto signature=snapshot(live_building->shape());
            if(signature!=placement_signature) {
                auto local=*live_building;local.position=Vec3::sZero();local.yaw=0;local.rotation=0;
                if(placement_renderer.build(local.triangles(0),status,false))placement_signature=signature;
            }
            placement_ready=placement_signature==signature;
        }
        BeginDrawing(); ClearBackground(background); rlSetClipPlanes(.2,30000); BeginMode3D(camera);
        if (preview_renderer) preview_renderer->draw(camera,float(GetTime()),preview_day.lighting(),lighting,graphics);
        car_renderer.set_lighting(lighting,camera,preview_day.lighting(),graphics);
        draw_city(city,grid,car_renderer,camera);
        if(live_building && placement_ready) {
            const auto p=live_building->base(City::level);
            const Matrix transform=MatrixMultiply(MatrixRotateY((live_building->yaw+live_building->rotation*90)*PI/180),MatrixTranslate(p.GetX(),p.GetY(),p.GetZ()));
            placement_renderer.draw(camera,preview_day.lighting(),lighting,graphics,&transform);
        }
        if (city.spawn && city.player_character) {
            auto feet=*city.spawn; feet.SetY(city.height(feet.GetX(),feet.GetZ())+.08f);
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
            if (brush_tool()) {
                for (int i = 0; i < 48; ++i) {
                    const float a = i*6.2831853f/48, b = (i+1)*6.2831853f/48, r = brush_size*8.f;
                    DrawLine3D(vector(brush_point+Vec3(std::cos(a)*r,.2f,std::sin(a)*r)),vector(brush_point+Vec3(std::cos(b)*r,.2f,std::sin(b)*r)),brush_remove ? ORANGE : scene_accent);
                }
            } else if (tool==Tool::Trees) {
                DrawCylinderWires(vector(brush_point),.3f,.3f,6,8,city.tree_clear(brush_point) ? scene_accent : RED);
            } else if (tool==Tool::Building) {
                City proposed = city; std::string error;
                const bool valid = proposed.add_building(preview,error);
                building_outline(preview,City::level,valid ? scene_accent : RED);
            } else if (tool==Tool::Spawn) {
                DrawCubeWires(vector(brush_point+Vec3(0,.8f,0)),.8f,1.6f,.8f,scene_accent);
            } else if (tool==Tool::Vehicle) {
                CityVehicle v{CityVehicleKind(vehicle_kind),brush_point,rotation};
                if (v.kind==CityVehicleKind::Car) {
                    v.car=selected_car;
                    if (selected_car) car_renderer.draw_design(*selected_car,brush_point,rotation*PI/2,camera);
                }
                const Vec3 half = v.half_size();
                DrawCubeWires(vector(brush_point+Vec3(0,1,0)),half.GetX()*2,2,half.GetZ()*2,scene_accent);
            }
        }
        if (selected_building>=0) {
            auto b = city.buildings[selected_building];
            if(moving_building && picked && Vector2Distance(mouse,building_drag_start)>3)b.position=brush_point+building_offset;
            building_outline(b,City::level,scene_accent);
        }
        if (selected_vehicle>=0) {
            const auto& v = city.vehicles[selected_vehicle]; const Vec3 half = v.half_size();
            DrawCubeWires(vector(v.position+Vec3(0,1,0)),half.GetX()*2,2,half.GetZ()*2,scene_accent);
        }
        if (pen_tool() && !pen.nodes.empty()) {
            auto path=pen;
            if (tool!=Tool::Road && path.nodes.size()>=3) path.closed=true;
            const auto points=path.sample();
            if(tool!=Tool::Road && path.nodes.size()>=3) for(auto t:polygon_triangles(points)) {
                for(auto& p:t)p.SetY(std::max(City::level,preview_environment ? preview_environment->height(p.GetX(),p.GetZ()) : City::level)+.18f);
                DrawTriangle3D(vector(t[0]),vector(t[1]),vector(t[2]),tool==Tool::Water ? Color{70,170,255,70} : Color{255,255,85,55});
            }
            for(std::size_t i=1;i<points.size();++i)DrawLine3D(vector(points[i-1]+Vec3(0,.22f,0)),vector(points[i]+Vec3(0,.22f,0)),YELLOW);
            if(tool==Tool::Road) for(float sign:{-1.f,1.f}) {
                const auto edge=offset_path(points,sign*road_width/2);
                for(std::size_t i=1;i<edge.size();++i)DrawLine3D(vector(edge[i-1]+Vec3(0,.22f,0)),vector(edge[i]+Vec3(0,.22f,0)),YELLOW);
            }
        }
        if (race_draft) {
            for (const auto& leg : race_preview) for (std::size_t i = 1; i < leg.points.size(); ++i)
                DrawLine3D(vector(leg.points[i-1]+Vec3(0,.25f,0)),vector(leg.points[i]+Vec3(0,.25f,0)),YELLOW);
            for (int i = 0; i < int(race_draft->checkpoints.size()); ++i)
                DrawCylinder(vector(race_draft->checkpoints[i]),1.2f,1.2f,2,12,i==race_point ? RED : i==0 ? GREEN : YELLOW);
        }
        EndMode3D();
        if (tool==Tool::Road || tool==Tool::Select) for (int r=0;r<int(city.paths.size());++r) if (r!=editing_road)
            for (const auto& node:city.paths[r].path.nodes) {
                const auto screen=GetWorldToScreen(vector(node.position+Vec3(0,.22f,0)),camera);
                DrawCircleV(screen,4,SKYBLUE);DrawCircleLines(int(screen.x),int(screen.y),4,BLACK);
            }
        if (road_snap && (!pen_dragging || pen_handle==0)) {
            const auto screen=GetWorldToScreen(vector(*road_snap+Vec3(0,.22f,0)),camera);
            DrawCircleLines(int(screen.x),int(screen.y),10,GREEN);text("Connect",screen.x+13,screen.y-8,14,BLACK);
        }
        if(pen_tool()) for(int i=0;i<int(pen.nodes.size());++i) {
            const auto& node=pen.nodes[i];const auto screen=GetWorldToScreen(vector(node.position+Vec3(0,.22f,0)),camera);
            for(int sign:{-1,1}) if(node.tangent.LengthSq()>.01f) {
                const auto handle=GetWorldToScreen(vector(node.position+node.tangent*float(sign)+Vec3(0,.22f,0)),camera);
                DrawLineV(screen,handle,YELLOW);DrawCircleV(handle,4,ORANGE);
            }
            DrawCircleV(screen,6,i==0 ? GREEN : i==pen_node ? ORANGE : YELLOW);DrawCircleLines(int(screen.x),int(screen.y),6,BLACK);
        }
        if (race_draft) for (int i = 0; i < int(race_draft->checkpoints.size()); ++i) {
            const auto p=GetWorldToScreen(vector(race_draft->checkpoints[i]+Vec3(0,2.2f,0)),camera);
            text(i==0 ? "START / FINISH" : std::to_string(i),p.x,p.y+(i==0 ? -18 : 4),16,BLACK);
        }
        // Keep the 3D scene inside the document's client area.
        DrawRectangle(0,menu_height,GetScreenWidth(),12,background);
        DrawRectangle(0,44,sidebar+12,GetScreenHeight()-44,background);
        DrawRectangle(GetScreenWidth()-12,44,12,GetScreenHeight()-44,background);
        DrawRectangle(0,GetScreenHeight()-64,GetScreenWidth(),64,background);
        menu_state.undo=(!pen_undo.empty() || !undo.empty()) && !race_draft && !pen_dragging;
        menu_state.redo=(!pen_redo.empty() || (pen_undo.empty() && !redo.empty())) && !race_draft && !pen_dragging;
        menu_state.selection=selected_building>=0 || selected_vehicle>=0;
        menu_state.top=top; menu_state.grid=grid; menu_state.tool=int(tool);
        draw_editor_controls(menu_state,can_edit);
        ui::draw_frame(document);
        ui::draw_frame({document.x+4,document.y+35,document.width-8,document.height-39},true);
        std::string active = tools[int(tool)];
        if (tool==Tool::Building) active=std::string(building_mesh && building_mesh->type==ObjectType::Object ? object_brush ? "Object brush" : "Object / Single" : "Building / Single")+" / "+(building_mesh ? building_mesh->name : "Plain block");
        if ((tool==Tool::Building && !brush_tool()) || selected_building>=0) active += " / "+std::to_string(int(selected_building>=0 ? city.buildings[selected_building].yaw : building_rotation))+" deg";
        if (tool==Tool::Road) active += std::string(editing_road>=0 ? " / editing / " : " / ")+TextFormat("%.1f m",road_width);
        if (tool==Tool::Ground) active += " / "+ground_texture;
        if (tool==Tool::Trees || brush_tool())
            active+=tool==Tool::Trees && !tree_brush ? " / Single placement" : " / "+std::to_string(brush_size*8)+" m / "+densities[density-1]+" / Shift: clear";
        if (tool==Tool::Vehicle) active += " / "+(vehicle_kind==0 && selected_car ? selected_car->name : std::string(vehicle_names[vehicle_kind]))+" / "+std::to_string(rotation*90);
        const auto title = fit(city.name+" / "+active+(dirty ? " *" : ""),18,document.width-100);
        ui::draw_window_title(document,title.c_str());
        ui::draw_window_close(document);
        if(pen_tool()) {
            ui::draw_panel(pen_toolbar);
            text("Click: point / drag: curve / Enter: finish / Esc: cancel",pen_toolbar.x+10,pen_toolbar.y+8,14);
            text(tool==Tool::Road ? "Drag nodes / Ctrl+Z/Y: undo/redo / Alt: no snap" : "Click first node: close loop / Backspace: remove point",pen_toolbar.x+10,pen_toolbar.y+28,13);
            const float y=pen_toolbar.y+54;
            if(button({pen_toolbar.x+10,y,100,30},"Finish",false,!pen.nodes.empty(),can_edit,14))apply_pen();
            if(button({pen_toolbar.x+120,y,100,30},"Cancel",false,!pen.nodes.empty(),can_edit,14))clear_pen();
            if(button({pen_toolbar.x+230,y,100,30},"Delete path",false,editing_area>=0 || editing_road>=0,can_edit,14)) {
                City next=city;
                if(editing_area>=0)next.areas.erase(next.areas.begin()+editing_area);
                if(editing_road>=0)next.paths.erase(next.paths.begin()+editing_road);
                next.rebuild_paths();next.clear_trees();
                if(next.validate(status)){commit(std::move(next));clear_pen();status="Path deleted / Ctrl+Z to undo";}
            }
            if(tool==Tool::Road) {
                const auto before=pen_state();
                if(button({pen_toolbar.x+340,y,36,30},"-",false,road_width>4,can_edit,16))road_width=std::max(4.f,road_width-1);
                text(TextFormat("%.1f m",road_width),pen_toolbar.x+388,y+6,15);
                if(button({pen_toolbar.x+454,y,36,30},"+",false,road_width<40,can_edit,16))road_width=std::min(40.f,road_width+1);
                remember_pen(before);
            }
        }
        if (race_draft) {
            ui::draw_panel(race_toolbar); text(fit(race_draft->name+" / "+std::to_string(race_draft->laps)+" laps / "+std::to_string(race_draft->checkpoints.size())+" points",16,540),race_toolbar.x+10,race_toolbar.y+8,16);
            const float y=race_toolbar.y+36;
            const bool saving=button({race_toolbar.x+10,y,128,30},"Save race",false,true,can_edit,14);
            const bool settings=button({race_toolbar.x+148,y,128,30},"Name/laps",false,true,can_edit,14);
            const bool remove_point=button({race_toolbar.x+286,y,128,30},"Remove",false,!race_draft->checkpoints.empty(),can_edit,14);
            const bool cancel=button({race_toolbar.x+424,y,136,30},"Cancel",false,true,can_edit,14);
            if (saving) apply_race();
            else if (cancel) { race_draft.reset(); race_preview.clear(); choose_tool(int(Tool::Select)); }
            else if (remove_point) { race_draft->checkpoints.erase(race_draft->checkpoints.begin()+(race_point>=0 ? race_point : int(race_draft->checkpoints.size())-1)); race_point=-1; build_race_route(city,*race_draft,race_preview,status); }
            else if (settings) {
                EndDrawing(); bool remove=false;
                if (const auto configured=race_settings(*race_draft,race_index>=0,remove)) {
                    if (remove) { City next=city; next.races.erase(next.races.begin()+race_index); commit(std::move(next)); race_draft.reset(); race_preview.clear(); choose_tool(int(Tool::Select)); }
                    else race_draft=*configured;
                }
                first_frame=true; continue;
            }
        }

        ui::draw_panel({8,float(GetScreenHeight()-54),float(GetScreenWidth()-16),42},true);
        const std::string clock = TextFormat("%02i:%02i",city.start_minutes/60,city.start_minutes%60);
        const std::string info = std::string(race_draft ? "[RACE DRAFT] " : dirty ? "[UNSAVED] " : "[SAVED] ")+"Start "+clock+" | "+(!city.spawn ? "Set a spawn to play"
            : car_only ? "Car only" : menu_state.play ? "Player: "+city.player_character->name : "Choose a Player character");
        text(fit(control_hint.empty() ? status : control_hint,17,GetScreenWidth()-ui::measure_text(info.c_str(),16)-48.f),20,float(GetScreenHeight()-44),17,accent);
        text(info,GetScreenWidth()-ui::measure_text(info.c_str(),16)-20.f,float(GetScreenHeight()-43),16);
        menu_state.top = top; menu_state.grid = grid; menu_state.tool = int(tool);
        menu.draw(menu_state);
        EndDrawing();
        if (choose_building) {
            const auto chosen=saved_picker([&]{return saved_buildings(buildings_directory,status);},"Choose an object or building","Choose design",building_mesh ? building_mesh->name : "",status,buildings_directory);
            if (chosen) {building_mesh=*chosen; choose_tool(3); status="Click the map to place "+chosen->name;}
            painting=dragging=false; first_frame=true; continue;
        }
        if (car_request) {
            if (command==MenuCommand::PlaceCar) {
                if (const auto chosen=choose_car(selected_car,directory.parent_path()/"cars")) {selected_car=chosen; choose_tool(5); vehicle_kind=0; status="Click the map to place "+chosen->name;}
            } else if (command==MenuCommand::EditCar && menu_state.car_selection) {
                car_builder(city.vehicles[selected_vehicle].car.value_or(CarDesign{}),directory.parent_path()/"cars");
                status="Car editor closed / refresh saved designs to update the map";
            } else design_menu(DesignKind::Car,directory.parent_path());
            painting=dragging=false; first_frame=true; continue;
        }
        if (builder_request>=0) {
            preview_renderer.reset(); preview_revision = -1;
            if (edit_building) building_builder(city.buildings[selected_building].shape(),buildings_directory);
            else design_menu(DesignKind::Building,directory.parent_path());
            status="Object Builder closed / refresh saved designs to update the map";
            painting=dragging=false; first_frame=true; continue;
        }
        first_frame = false;
        if (!screenshot.empty() && ++frames>=3) { auto image = LoadImageFromScreen(); ExportImage(image,screenshot.c_str()); UnloadImage(image); return EditorResult::Quit; }
    }
}
}
int choose_race(const City& city, bool editing) {
    EnableCursor(); SetWindowTitle(editing ? "Ambaretto - Edit races" : "Ambaretto - Choose race");
    int selection=city.races.empty() ? -1 : 0, scroll=0; bool interactive=false;
    while (!WindowShouldClose()) {
        const Rectangle panel{(GetScreenWidth()-760)/2.f,(GetScreenHeight()-460)/2.f,760,460};
        const bool active=interactive && IsWindowFocused(); const int count=int(city.races.size());
        if (active && IsKeyPressed(KEY_INSERT)) return -2;
        if (active && count) {
            if (IsKeyPressed(KEY_DOWN)) selection=(selection+1)%count;
            if (IsKeyPressed(KEY_UP)) selection=(selection+count-1)%count;
            scroll=std::clamp(scroll-int(GetMouseWheelMove()),0,std::max(0,count-7));
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_DOWN)) scroll=std::clamp(selection-3,0,std::max(0,count-7));
        }
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(panel,editing ? "Edit / delete races" : "Start a race"); ui::draw_window_close(panel);
        text(city.name,panel.x+24,panel.y+52,20);
        if (!count) text("No races yet. Create a race in the map editor, or go back.",panel.x+24,panel.y+120,17);
        for (int i = scroll; i < std::min(count,scroll+7); ++i) {
            const auto& race=city.races[i];
            if (list_item({panel.x+24,panel.y+90+(i-scroll)*38,712,34},race.name+" / "+std::to_string(race.laps)+" laps",selection==i,true,active,17)) selection=i;
        }
        text("Up / Down: choose   Enter: select   Ins: create   Esc: back",panel.x+24,panel.y+370,15);
        const bool chosen=button({panel.x+24,panel.y+402,224,40},editing ? "Edit race" : "Start race",true,selection>=0,active)
            || (active && selection>=0 && IsKeyPressed(KEY_ENTER));
        const bool create=button({panel.x+264,panel.y+402,224,40},"Create race",false,true,active);
        const bool back=close_clicked(panel,active) || button({panel.x+504,panel.y+402,232,40},"Back",false,true,active) || (active && IsKeyPressed(KEY_ESCAPE));
        EndDrawing(); interactive=true;
        if (back) return -1;
        if (create) return -2;
        if (chosen) return selection;
    }
    return -1;
}
bool edit_current_city(City& city, const std::filesystem::path& directory, bool car_only, bool create_race) {
    EnableCursor(); SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    return editor(city, directory, {}, true, car_only, create_race) == EditorResult::Play;
}
bool city_menu(City& selected,const std::filesystem::path& directory,ControllerMapping& controls,
               GraphicsSettings& graphics,bool edit_selected,const std::string& screenshot,bool preview_editor,MenuCommand initial_settings) {
    EnableCursor(); SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    if (initial_settings!=MenuCommand::None) {
        settings_menu(controls,graphics,screenshot,initial_settings);
        if (!screenshot.empty()) return false;
    }
    bool open_editor=edit_selected || preview_editor;
    if (preview_editor && selected.id.empty()) selected=City::create("New city");
    std::string status,name,terrain_x="48",terrain_z="48",terrain_seed,terrain_water="35";
    bool terrain_river=false,terrain_two_islands=false;
    std::vector<City> cities;
    int selection=-1,scroll=0,frames=0,new_field=0;
    bool accept_input=false,creating=false,generated_terrain=true;
    City terrain_draft;
    Texture2D terrain_preview{};
    std::string terrain_key,terrain_error;
    int preview_x=48,preview_z=48;
    bool terrain_ready=false;
    const auto finish=[&](bool play) { if (terrain_preview.id) UnloadTexture(terrain_preview); return play; };
    MenuBar menu(MenuMode::Cities);
    const auto refresh=[&]() {
        cities=saved_cities(directory,status); selection=cities.empty() ? -1 : 0;
        for (int i=0;i<int(cities.size());++i) if (cities[i].id==selected.id) selection=i;
        scroll=std::max(0,selection);
    };
    const auto new_city=[&]() {
        creating=true; name="New city"; new_field=0; generated_terrain=true; status.clear();
        terrain_x=terrain_z="48"; terrain_seed=std::to_string(GetRandomValue(1,2147483647));
        terrain_water="35"; terrain_river=terrain_two_islands=false;
        terrain_draft=City::create("New city"); terrain_key.clear();
    };
    refresh();
    while (!WindowShouldClose()) {
        if (open_editor) {
            open_editor=false;
            const auto result=editor(selected,directory,screenshot);
            if (result==EditorResult::Play) return finish(true);
            if (result==EditorResult::Quit) return finish(false);
            refresh(); accept_input=false;
        }
        if (!accept_input) SetWindowTitle("Ambaretto - Cities");
        const Rectangle r{(GetScreenWidth()-880)/2.f,70,880,float(GetScreenHeight()-140)};
        const Rectangle view{r.x+24,r.y+104,520,r.height-250};
        const bool has=selection>=0 && selection<int(cities.size()),was_creating=creating;
        MenuState state; state.selection=has;
        const auto command=creating || !accept_input ? MenuCommand::None : menu.update(state);
        const bool active=accept_input && !creating && !menu.blocking() && !menu.interacted() && IsWindowFocused();
        if (command==MenuCommand::Quit || (active && IsKeyPressed(KEY_ESCAPE))) return finish(false);
        if (command==MenuCommand::NewCity || (active && IsKeyPressed(KEY_INSERT))) new_city();
        if (command==MenuCommand::EditCity || (active && has && (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ENTER)))) {
            selected=cities[selection]; open_editor=true; continue;
        }
        BeginDrawing(); ui::draw_desktop(); ui::draw_window(r,"Cities / Create or open"); ui::draw_window_close(r);
        text("Start with a new city or open a saved one.",r.x+24,r.y+63,20,accent);
        const bool can_choose=active && !creating;
        if (!creating) draw_city_list(cities,selection,scroll,view,can_choose);
        if (button({r.x+576,r.y+128,280,46},"Create new city",true,true,can_choose)) new_city();
        if (button({r.x+576,r.y+192,280,46},"Open / edit selected",false,has,can_choose)) {selected=cities[selection]; open_editor=true;}
        const bool duplicate=command==MenuCommand::DuplicateCity || (can_choose && has && duplicate_pressed())
            || button({r.x+576,r.y+256,134,40},"Duplicate",false,has,can_choose,16);
        const bool deleting=command==MenuCommand::DeleteCity || (can_choose && has && IsKeyPressed(KEY_DELETE))
            || button({r.x+722,r.y+256,134,40},"Delete",false,has,can_choose);
        const bool back=close_clicked(r,can_choose) || button({r.x+576,r.y+r.height-64,280,40},"Back",false,true,can_choose);
        text(fit(status,15,540),r.x+24,r.y+r.height-47,15,accent);
        if (creating) {
            const bool modal_input=was_creating && IsWindowFocused();
            ui::draw_desktop();
            const Rectangle r{(GetScreenWidth()-960)/2.f,(GetScreenHeight()-520)/2.f,960,520};
            ui::draw_window(r,"Create a city"); ui::draw_window_close(r);
            if (modal_input && IsKeyPressed(KEY_TAB)) new_field = (new_field+(IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT) ? 6 : 1))%(generated_terrain ? 7 : 1);
            const auto entry = [&](std::string& value,Rectangle box,int id,bool enabled = true) {
                if (enabled && modal_input && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hit(box)) new_field = id;
                if (enabled && new_field==id && modal_input) input(value,box);
                else { ui::draw_panel(box,true,enabled ? ui::highlight : ui::face); text(fit(value,21,box.width-24),box.x+12,box.y+(box.height-21)/2,21,enabled ? ink : muted); }
            };
            text("Name",r.x+24,r.y+48,16); entry(name,{r.x+24,r.y+72,572,44},0);
            if (button({r.x+24,r.y+132,278,30},"Perlin terrain",generated_terrain,true,modal_input,16)) generated_terrain = true;
            if (button({r.x+318,r.y+132,278,30},"Empty water",!generated_terrain,true,modal_input,16)) { generated_terrain = false; new_field = 0; }
            const auto number = [&](std::string& value,Rectangle box,int id,int lo,int hi) {
                entry(value,box,id,generated_terrain);
                for (int direction : {-1,1}) if (button({box.x+box.width+(direction<0 ? 10.f : 50.f),box.y,32,box.height},direction<0 ? "-" : "+",false,generated_terrain,modal_input,16)) {
                    int count=lo; try { count=std::stoi(value); } catch (const std::exception&) {}
                    value=std::to_string(std::clamp(std::clamp(count,lo,hi)+direction,lo,hi)); new_field=id;
                }
            };
            text("Starter tiles X",r.x+24,r.y+190,16); number(terrain_x,{r.x+24,r.y+218,170,36},1,1,City::width);
            text("Starter tiles Z",r.x+318,r.y+190,16); number(terrain_z,{r.x+318,r.y+218,170,36},2,1,City::width);
            text("Seed",r.x+24,r.y+286,17); entry(terrain_seed,{r.x+120,r.y+278,322,36},3,generated_terrain);
            if (button({r.x+452,r.y+278,144,36},"Regenerate",false,generated_terrain,modal_input,16)
                || (modal_input && generated_terrain && (IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_R)))
                terrain_seed = std::to_string(GetRandomValue(1,2147483647));
            text("Flat land with a smooth sand beach at the waterline",r.x+24,r.y+330,15);
            text("Water coverage (%)",r.x+24,r.y+370,15);
            number(terrain_water,{r.x+228,r.y+362,110,32},4,0,100);
            text("0 = land / 100 = sea",r.x+432,r.y+372,12);
            if (button({r.x+24,r.y+408,278,30},terrain_river ? "Central river: On" : "Central river: Off",terrain_river || new_field==5,generated_terrain,modal_input,16)) {
                terrain_river=!terrain_river; new_field=5;
            }
            if (button({r.x+318,r.y+408,278,30},terrain_two_islands ? "Two islands" : "One island",terrain_two_islands || new_field==6,generated_terrain,modal_input,16)) {
                terrain_two_islands=!terrain_two_islands; new_field=6;
            }
            if (modal_input && generated_terrain && IsKeyPressed(KEY_SPACE)) {
                if (new_field==5) terrain_river=!terrain_river;
                if (new_field==6) terrain_two_islands=!terrain_two_islands;
            }
            const auto key=std::to_string(generated_terrain)+"|"+terrain_x+"|"+terrain_z+"|"+terrain_seed+"|"+terrain_water+"|"+std::to_string(terrain_river)+std::to_string(terrain_two_islands);
            if (key!=terrain_key) {
                terrain_key=key; terrain_error.clear(); status.clear(); terrain_ready=true;
                City next; next.id=terrain_draft.id;
                if (generated_terrain) {
                    try {
                        for (const auto& value : {terrain_x,terrain_z,terrain_seed,terrain_water})
                            if (value.empty() || value.find_first_not_of("0123456789")!=std::string::npos) throw std::out_of_range("terrain");
                        const auto seed=std::stoull(terrain_seed);
                        if (seed>std::numeric_limits<std::uint32_t>::max()) throw std::out_of_range("seed");
                        preview_x=std::stoi(terrain_x); preview_z=std::stoi(terrain_z);
                        const TerrainSettings settings{0,std::stoi(terrain_water),terrain_river,terrain_two_islands};
                        terrain_ready=next.generate_terrain(preview_x,preview_z,std::uint32_t(seed),terrain_error,settings);
                        if (terrain_ready) { next.make_freeform(true); terrain_ready=next.validate(terrain_error); }
                    } catch (const std::exception&) {terrain_ready=false; terrain_error="Use whole numbers: size 1-128, water 0-100, seed 0-4294967295.";}
                }
                if (terrain_ready) {
                    terrain_draft=std::move(next);
                    constexpr int size=City::width*4;
                    auto image=GenImageColor(size,size,{28,66,108,255});
                    auto* pixels=static_cast<Color*>(image.data);
                    for (const auto& area:terrain_draft.areas) for (int y=0;y<size;++y) {
                        const float z=(y+.5f)*2*City::extent/size-City::extent;
                        std::vector<float> crossings;
                        for (std::size_t i=1;i<area.boundary.size();++i) {
                            const Vec3 a=area.boundary[i-1],b=area.boundary[i];
                            if ((a.GetZ()>z)!=(b.GetZ()>z))
                                crossings.push_back((a.GetX()+(b.GetX()-a.GetX())*(z-a.GetZ())/(b.GetZ()-a.GetZ())+City::extent)*size/(2*City::extent));
                        }
                        std::sort(crossings.begin(),crossings.end());
                        for (std::size_t i=1;i<crossings.size();i+=2) {
                            const int from=std::clamp(int(std::ceil(crossings[i-1]-.5f)),0,size),to=std::clamp(int(std::ceil(crossings[i]-.5f)),0,size);
                            std::fill(pixels+y*size+from,pixels+y*size+to,area.kind==AreaKind::Land ? Color{84,129,74,255} : Color{28,66,108,255});
                        }
                    }
                    if (terrain_preview.id) UpdateTexture(terrain_preview,image.data);
                    else {terrain_preview=LoadTextureFromImage(image); SetTextureFilter(terrain_preview,TEXTURE_FILTER_BILINEAR);}
                    UnloadImage(image);
                }
            }
            text("Terrain preview",r.x+640,r.y+48,18,accent);
            const Rectangle view{r.x+640,r.y+80,296,296};
            DrawRectangleRec(view,{28,66,108,255});
            if (terrain_ready) {
                DrawTexturePro(terrain_preview,{0,0,float(terrain_preview.width),float(terrain_preview.height)},view,{0,0},0,WHITE);
                if (generated_terrain) {
                    const float tile=view.width/City::width;
                    DrawRectangleLinesEx({view.x+(City::width-preview_x)/2*tile,view.y+(City::width-preview_z)/2*tile,preview_x*tile,preview_z*tile},1,accent);
                }
            } else text("Fix terrain values to preview",view.x+12,view.y+138,13,accent);
            ui::draw_frame(view,true);
            DrawRectangleRec({view.x,r.y+390,14,14},{84,129,74,255}); text("Land",view.x+22,r.y+389,14);
            DrawRectangleRec({view.x+96,r.y+390,14,14},{172,169,122,255}); text("Beach",view.x+118,r.y+389,14);
            DrawRectangleRec({view.x+200,r.y+390,14,14},{28,66,108,255}); text("Water",view.x+222,r.y+389,14);
            const int land=terrain_draft.land_count();
            text(terrain_ready ? "Land: "+std::to_string(land)+(generated_terrain ? " / water: "+std::to_string(int(std::round(100.f*(1-land/float(preview_x*preview_z)))))+"%" : " tiles") : "Preview unavailable",view.x,r.y+417,13);
            text("Level land / box: starter area",view.x,r.y+444,12);
            text(fit(terrain_error.empty() ? status.empty() ? "Ctrl+R: regenerate / Enter: create / Esc: cancel" : status : terrain_error,13,572),r.x+24,r.y+171,13,accent);
            const bool accept=button({r.x+24,r.y+452,278,44},"Create city",true,terrain_ready,modal_input) || (modal_input && terrain_ready && IsKeyPressed(KEY_ENTER));
            if (close_clicked(r,modal_input) || button({r.x+318,r.y+452,278,44},"Cancel",false,true,modal_input) || (modal_input && IsKeyPressed(KEY_ESCAPE))) {
                creating=false; accept_input=false;
            } else if (accept) {
                City next=terrain_draft; next.name=name; next.make_freeform();
                if (next.save(directory,status)) {selected=next; creating=false; open_editor=true; refresh(); status="City saved";}
            }
        }
        menu.draw(state);
        EndDrawing(); accept_input=true;
        if (!creating && terrain_preview.id) {UnloadTexture(terrain_preview); terrain_preview={};}
        if (back) return finish(false);
        if (deleting) {
            const auto id=cities[selection].id;
            if (delete_selected_saved(cities,selection,directory,status) && selected.id==id) selected={};
            accept_input=false; continue;
        }
        if (duplicate) if (auto copy=duplicate_saved(cities[selection],directory,cities,status)) {
            selected=*copy; cities.push_back(std::move(*copy)); selection=int(cities.size())-1; scroll=selection;
        }
        if (!screenshot.empty() && ++frames>=3) {auto image=LoadImageFromScreen(); ExportImage(image,screenshot.c_str()); UnloadImage(image); return finish(false);}
    }
    return finish(false);
}
} // namespace ambaretto
