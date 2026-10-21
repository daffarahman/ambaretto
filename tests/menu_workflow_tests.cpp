// Exercise the real frame loops with raylib's automation events, including the game loop.
#define main ambaretto_main
#include "../src/main.cpp"
#undef main
#include <functional>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include "saved_copy.hpp"
#include "graphics_panel.hpp"

namespace {
std::vector<std::function<void()>> steps;
std::size_t frame=0;
unsigned physics_steps=0,window_count=0;
unsigned model_loads=0,image_loads=0,shader_loads=0,mesh_uploads=0,render_targets=0,preview_draws=0;
unsigned triangle_picks=0;
int largest_preview=0;
double frame_begin=0;
bool measure_preview=false;
std::vector<double> preview_frames;
ambaretto::PhysicsWorld* active_world=nullptr;
Camera3D scene_camera{};
std::string title;
std::string typed_text;
const char* stage="start";
bool should_close=false,focused=true;
struct WorkflowFailure { std::string message; };
void require(bool ok,const char* message) {if (!ok) throw WorkflowFailure{message};}
void event(unsigned type,int a=0,int b=0) {PlayAutomationEvent({0,type,{a,b,0,0}});}
void idle(int count=2) {while (count-->0) steps.push_back([]{});}
void click(int x,int y) {steps.push_back([=]{event(7,x,y); event(6,MOUSE_BUTTON_LEFT);}); steps.push_back([]{event(5,MOUSE_BUTTON_LEFT);}); idle();}
void city_click(ambaretto::Vec3 focus,ambaretto::Vec3 point) {
    steps.push_back([=]{
        Camera3D camera{{focus.GetX(),800,focus.GetZ()+.01f},{focus.GetX(),0,focus.GetZ()},{0,0,-1},400,CAMERA_ORTHOGRAPHIC};
        const auto p=GetWorldToScreen({point.GetX(),point.GetY(),point.GetZ()},camera);
        event(7,int(p.x),int(p.y)); event(6,MOUSE_BUTTON_LEFT);
    });
    steps.push_back([]{event(5,MOUSE_BUTTON_LEFT);}); idle();
}
void map_click(ambaretto::Vec3 point) {
    steps.push_back([=]{const auto p=GetWorldToScreen({point.GetX(),point.GetY(),point.GetZ()},scene_camera); event(7,int(p.x),int(p.y)); event(6,MOUSE_BUTTON_LEFT);});
    steps.push_back([]{event(5,MOUSE_BUTTON_LEFT);}); idle();
}
void map_drag(ambaretto::Vec3 from,ambaretto::Vec3 to) {
    steps.push_back([=]{const auto p=GetWorldToScreen({from.GetX(),from.GetY(),from.GetZ()},scene_camera);event(7,int(p.x),int(p.y));event(6,MOUSE_BUTTON_LEFT);});
    for(int i=1;i<=5;++i)steps.push_back([=]{const auto point=from+(to-from)*(i/5.f);const auto p=GetWorldToScreen({point.GetX(),point.GetY(),point.GetZ()},scene_camera);event(7,int(p.x),int(p.y));});
    steps.push_back([]{event(5,MOUSE_BUTTON_LEFT);});idle();
}
void key(int code) {steps.push_back([=]{event(2,code);}); steps.push_back([=]{event(1,code);}); idle();}
void wheel(int amount) {steps.push_back([=]{event(7,100,380); event(8,0,amount);}); idle();}
void replace_text(const std::string& value) {
    steps.push_back([]{event(2,KEY_LEFT_CONTROL); event(2,KEY_A);});
    steps.push_back([]{event(1,KEY_A); event(1,KEY_LEFT_CONTROL);});
    steps.push_back([=]{typed_text=value;}); idle();
}
void number_text(const std::string& value) {replace_text(value); key(KEY_ENTER);}
Vector2 building_screen(ambaretto::Vec3 size,Vector3 point) {
        const float radius=size.Length(),yaw=2.44f,pitch=.5f; const Vector3 target{0,size.GetY()/2,0};
        Camera3D camera{{std::sin(yaw)*std::cos(pitch)*radius*2,target.y+std::sin(pitch)*radius*2,std::cos(yaw)*std::cos(pitch)*radius*2},target,{0,1,0},radius*1.45f,CAMERA_ORTHOGRAPHIC};
        const auto p=GetWorldToScreenEx(point,camera,GetScreenWidth()-438,GetScreenHeight()-244);
        return {p.x+414,p.y+84};
}
void building_mouse(ambaretto::Vec3 size,Vector3 point) {const auto p=building_screen(size,point); event(7,int(p.x),int(p.y));}
void building_wall(ambaretto::Vec3 size,Vector3 point) {
    steps.push_back([=]{
        building_mouse(size,point); event(6,MOUSE_BUTTON_LEFT);
    });
    steps.push_back([]{event(5,MOUSE_BUTTON_LEFT);}); idle();
}
void building_drag(ambaretto::Vec3 size,Vector3 from,Vector3 to,bool outside=false) {
    steps.push_back([=]{building_mouse(size,from); event(6,MOUSE_BUTTON_LEFT);});
    for (int i=1;i<=4;++i) steps.push_back([=]{building_mouse(size,Vector3Lerp(from,to,i/4.f));});
    if (outside) steps.push_back([]{event(7,1000,40);});
    steps.push_back([]{event(5,MOUSE_BUTTON_LEFT);}); idle();
}
void shortcut(int code) {steps.push_back([=]{event(2,KEY_LEFT_CONTROL); event(2,code);}); steps.push_back([=]{event(1,code); event(1,KEY_LEFT_CONTROL);}); idle();}
void capture(const char* name) {auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/name).string().c_str()); UnloadImage(image);}
void require_button(Rectangle r,bool pressed=false) {
    const auto image=LoadImageFromScreen();
    const auto left=GetImageColor(image,int(r.x),int(r.y+r.height/2)),right=GetImageColor(image,int(r.x+r.width-1),int(r.y+r.height/2));
    UnloadImage(image);
    const auto matches=[](Color a,Color b){return a.r==b.r && a.g==b.g && a.b==b.b;};
    if (!matches(left,pressed ? ambaretto::ui::shadow : ambaretto::ui::highlight)
        || !matches(right,pressed ? ambaretto::ui::highlight : ambaretto::ui::ink))
        throw WorkflowFailure{std::string(pressed ? "Pressed" : "Idle")+" button at "+std::to_string(int(r.x))+","+std::to_string(int(r.y))
            +" has incorrect bevel (left "+std::to_string(left.r)+", right "+std::to_string(right.r)+")"};
}
void require_preview(Rectangle r) {
    const auto image=LoadImageFromScreen(); int pixels=0;
    for (int y=int(r.y+12);y<r.y+r.height-12;y+=3) for (int x=int(r.x+12);x<r.x+r.width-12;x+=3) {
        const auto c=GetImageColor(image,x,y);
        if (!(c.r==18 && c.g==30 && c.b==49) && !(c.r>230 && c.g>230 && c.b>230)) ++pixels;
    }
    UnloadImage(image); require(pixels>100,"Saved item preview is empty or unavailable");
}
std::string terrain_pixels() {
    const auto image=LoadImageFromScreen(); std::string pixels;
    for (int z=0;z<ambaretto::City::width;++z) for (int x=0;x<ambaretto::City::width;++x) {
        const auto c=GetImageColor(image,int(672+(x+.5f)*296/ambaretto::City::width),int(120+(z+.5f)*296/ambaretto::City::width));
        pixels+=c.r>=84 && c.r<=172 && c.g>=129 && c.g<=169 && c.b>=74 && c.b<=122 ? '1' : c.r==28 && c.g==66 && c.b==108 ? '0' : '?';
    }
    UnloadImage(image); return pixels;
}
void require_terrain_preview(const ambaretto::City& city) {
    const auto pixels=terrain_pixels(); int matched=0;
    auto outline=city;outline.make_freeform(true);
    for (std::size_t i=0;i<pixels.size();++i) if (pixels[i]!='?') {
        // Filtering a curved shore changes boundary pixels; compare solid land and water.
        if (outline.shore_distance(ambaretto::City::center({int(i%ambaretto::City::width),int(i/ambaretto::City::width)}))<ambaretto::City::block*.6f) continue;
        require(pixels[i]=='0'+int(city.tiles[i]),("Terrain image differs from the generated map at "+std::to_string(i%ambaretto::City::width)+","+std::to_string(i/ambaretto::City::width)).c_str()); ++matched;
    }
    require(matched>15000,"Terrain preview is missing or obscured");
}
bool same_land(const ambaretto::City& a,const ambaretto::City& b) {
    for(int z=0;z<ambaretto::City::width;++z)for(int x=0;x<ambaretto::City::width;++x)
        if(a.land({x,z})!=b.land({x,z}))return false;
    return true;
}
void duplicate_key() {steps.push_back([]{event(2,KEY_LEFT_CONTROL); event(2,KEY_D);}); steps.push_back([]{event(1,KEY_D); event(1,KEY_LEFT_CONTROL);}); idle();}
void reset() {steps.clear(); frame=0; title.clear(); should_close=false; focused=true; frame_begin=0; measure_preview=false; preview_frames.clear(); active_world=nullptr;}
void window() {reset(); InitWindow(1024,600,"Menu workflow check"); SetExitKey(KEY_NULL); SetTargetFPS(120);}
bool gray_desktop() {const auto image=LoadImageFromScreen(); const Color p=GetImageColor(image,20,140); UnloadImage(image); return p.r==ambaretto::ui::desktop.r && p.g==ambaretto::ui::desktop.g && p.b==ambaretto::ui::desktop.b;}
bool saved_popup() {const auto image=LoadImageFromScreen(); const Color p=GetImageColor(image,152,300); UnloadImage(image); return p.r==255 && p.g==255 && p.b==255;}
void open_saved() {key(KEY_F10); key(KEY_DOWN); key(KEY_DOWN); key(KEY_ENTER);}
void open_city_settings() {key(KEY_F10); key(KEY_LEFT); key(KEY_LEFT); key(KEY_LEFT); key(KEY_ENTER);}
void preview(const char* name) {steps.push_back([=]{require(saved_popup(),"Saved selector did not open as a popup"); require_preview({176,152,672,206}); capture(name);});}
std::string contents(const std::filesystem::path& path) {std::ifstream file(path); return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
}
extern "C" void __real_InitWindow(int,int,const char*);
extern "C" void __wrap_InitWindow(int w,int h,const char* name) {++window_count; SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_WINDOW_RESIZABLE); __real_InitWindow(w,h,name);}
extern "C" bool __wrap_IsWindowFocused() {return focused;}
extern "C" bool __real_WindowShouldClose();
extern "C" bool __wrap_WindowShouldClose() {return should_close || __real_WindowShouldClose();}
extern "C" void __real_SetWindowTitle(const char*);
extern "C" void __wrap_SetWindowTitle(const char* name) {title=name; __real_SetWindowTitle(name);}
extern "C" void __real__ZN9ambaretto12PhysicsWorld4stepEf(ambaretto::PhysicsWorld*,float);
extern "C" void __wrap__ZN9ambaretto12PhysicsWorld4stepEf(ambaretto::PhysicsWorld* world,float dt) {active_world=world; ++physics_steps; __real__ZN9ambaretto12PhysicsWorld4stepEf(world,dt);}
extern "C" void __real_EndDrawing();
extern "C" int __real_GetCharPressed();
extern "C" int __wrap_GetCharPressed() {if (typed_text.empty()) return __real_GetCharPressed(); const int c=typed_text.front(); typed_text.erase(typed_text.begin()); return c;}
extern "C" Model __real_LoadModel(const char*);
extern "C" Model __wrap_LoadModel(const char* path) {++model_loads; return __real_LoadModel(path);}
extern "C" Image __real_LoadImage(const char*);
extern "C" Image __wrap_LoadImage(const char* path) {++image_loads; return __real_LoadImage(path);}
extern "C" Shader __real_LoadShaderFromMemory(const char*,const char*);
extern "C" Shader __wrap_LoadShaderFromMemory(const char* vertex,const char* fragment) {++shader_loads; return __real_LoadShaderFromMemory(vertex,fragment);}
extern "C" void __real_UploadMesh(Mesh*,bool);
extern "C" void __wrap_UploadMesh(Mesh* mesh,bool dynamic) {++mesh_uploads; __real_UploadMesh(mesh,dynamic);}
extern "C" RenderTexture2D __real_LoadRenderTexture(int,int);
extern "C" RenderTexture2D __wrap_LoadRenderTexture(int width,int height) {++render_targets; largest_preview=std::max({largest_preview,width,height}); return __real_LoadRenderTexture(width,height);}
extern "C" void __real_BeginTextureMode(RenderTexture2D);
extern "C" void __wrap_BeginTextureMode(RenderTexture2D target) {++preview_draws; __real_BeginTextureMode(target);}
extern "C" void __real_BeginMode3D(Camera3D);
extern "C" void __wrap_BeginMode3D(Camera3D camera) {scene_camera=camera; __real_BeginMode3D(camera);}
extern "C" RayCollision __real_GetRayCollisionTriangle(Ray,Vector3,Vector3,Vector3);
extern "C" RayCollision __wrap_GetRayCollisionTriangle(Ray ray,Vector3 a,Vector3 b,Vector3 c) {++triangle_picks;return __real_GetRayCollisionTriangle(ray,a,b,c);}
extern "C" void __wrap_EndDrawing() {
    if (measure_preview && frame_begin>0) preview_frames.push_back((GetTime()-frame_begin)*1000);
    __real_EndDrawing();
    if (frame<steps.size()) steps[frame++]();
    else if (++frame==steps.size()+3) {auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/"menu-workflow-last.png").string().c_str()); UnloadImage(image);}
    else if (frame>steps.size()+1200) throw WorkflowFailure{std::string("UI workflow timed out: ")+stage};
    frame_begin=GetTime();
}
int main() {
    using namespace ambaretto;
    SetTraceLogLevel(LOG_WARNING);
        const auto root=std::filesystem::path(GetApplicationDirectory())/("menu-workflow-test-"+City::create("test").id);
    std::string error;
    try {
        std::filesystem::create_directories(root);
        CharacterDesign player; player.name="Menu test player";
        const auto assets=std::filesystem::path(GetApplicationDirectory())/"assets/character";
        for (int i=0;i<9;++i) {
            const auto models=car_models(assets/character_slot_folders[i],error);
            require(!models.empty(),"Character component fixtures missing"); player.parts[i].model=models.front();
        }
        require(player.save(root/"characters",error),"Player fixture save failed");
        stage="button press and release"; window(); {
            ui::FontResource font; PhysicsWorld world(false); Car car(world); TuningPanel tuning;
            GraphicsPanel graphics; graphics.open({}); ControllerMapping controls; MenuBar menu;
            const auto check_press=[&](Rectangle button,const std::function<void()>& draw) {
                event(7,int(button.x+button.width/2),int(button.y+button.height/2));
                for (bool pressed : {false,true,false}) {
                    event(pressed ? 6 : 5,MOUSE_BUTTON_LEFT);
                    for (int i=0;i<3;++i) {BeginDrawing(); ui::draw_desktop(); draw(); EndDrawing();}
                    require_button(button,pressed);
                }
            };
            menu.show(MenuCommand::Controllers);
            check_press({60,162,216,28},[&]{menu.draw(controls,{});});
            check_press({60,218,416,28},[&]{menu.draw(controls,{});});
            auto r=graphics.bounds(1024,600);
            check_press({r.x+120,r.y+70,94,28},[&]{graphics.draw(120,{});});
            check_press({r.x+18,r.y+108,r.width-36,std::min(42.f,(r.height-252)/9)-2},[&]{graphics.draw(120,{});});
            r=tuning.bounds(1024,600);
            check_press({r.x+18,r.y+66,110,30},[&]{tuning.draw(car,1024,600);});
            check_press({r.x+12,r.y+108,360,22},[&]{tuning.draw(car,1024,600);});
        } CloseWindow();
        // Open/edit, cancel the unsaved prompt, then save and close through that prompt.
        stage="character open / draft save"; window(); {
            ui::FontResource font;
            click(760,280); click(215,159); click(450,553); click(740,394);
            steps.push_back([&]{CharacterDesign saved; require(CharacterDesign::load(root/"characters/character-Menu test player.character",saved,error) && saved.type==CharacterType::Player,"Cancel changed the saved character");});
            click(450,553); click(280,394);
            design_menu(DesignKind::Character,root);
            CharacterDesign saved; require(CharacterDesign::load(root/"characters/character-Menu test player.character",saved,error) && saved.type==CharacterType::NPC,"Unsaved prompt Save did not persist the draft");
        } CloseWindow();
        // New car stays blank even when a library exists; a failed Save cannot close its draft.
        stage="new car / invalid save"; window(); {
            ui::FontResource font;
            idle(); click(760,345); duplicate_key();
            steps.push_back([&]{require(!std::filesystem::exists(root/"cars"),"Empty design list enabled duplication");});
            click(760,220); click(215,159); click(450,545); click(280,394); idle(3); click(740,394);
            click(450,545); click(500,394);
            design_menu(DesignKind::Car,root);
            require(!std::filesystem::exists(root/"cars/car-New car.car"),"Invalid new car was saved");
        } CloseWindow();
        // Navigate with mouse and keyboard, returning to the list in the same window.
        stage="main menu navigation"; window(); {
            ui::FontResource font; City selected; ControllerMapping controls; GraphicsSettings graphics;
            const auto initial_windows=window_count;
            idle();
            steps.push_back([]{for (int i=0;i<4;++i) require_button({(GetScreenWidth()-620)/2.f+24,(GetScreenHeight()-388)/2.f+82+i*56.f,572,42});});
            click(300,265);
            steps.push_back([]{require(title=="Ambaretto - Create / edit","Create / edit did not open the editor list");});
            click(300,290);
            steps.push_back([]{require(title=="Ambaretto - Cars","Menu click did not open Cars");});
            key(KEY_ESCAPE);
            steps.push_back([]{require(title=="Ambaretto - Create / edit","Cars did not return to the editor list");});
            key(KEY_ESCAPE);
            key(KEY_DOWN); key(KEY_DOWN); key(KEY_ENTER);
            steps.push_back([]{require(title=="Ambaretto - Settings","Menu list did not open Settings");});
            key(KEY_ENTER);
            steps.push_back([]{
                GraphicsPanel panel; const auto r=panel.bounds(GetScreenWidth(),GetScreenHeight()); const auto image=LoadImageFromScreen();
                for (int y=int(r.y+r.height-24);y<int(r.y+r.height-8);++y) for (int x=int(r.x+r.width+8);x<image.width;++x) {
                    const auto c=GetImageColor(image,x,y); require(!(c.r==255 && c.g==255 && c.b==255),"Graphics keyboard help overflowed the minimum window panel");
                }
                UnloadImage(image); capture("graphics-help-workflow.png");
            });
            key(KEY_ESCAPE);
            steps.push_back([]{require(title=="Ambaretto - Settings","Graphics Escape closed the Settings parent");});
            key(KEY_DOWN); key(KEY_ENTER);
            steps.push_back([]{capture("controller-help-workflow.png");});
            key(KEY_ESCAPE);
            key(KEY_ESCAPE);
            steps.push_back([]{require(title=="Ambaretto - Main menu","Settings did not return to the main menu");});
            // Escape closes File before it can leave the main menu.
            key(KEY_F10); key(KEY_ESCAPE);
            steps.push_back([]{require(title=="Ambaretto - Main menu","Dropdown Escape leaked to Quit");});
            key(KEY_F10); key(KEY_RIGHT); key(KEY_ENTER);
            steps.push_back([]{require(title=="Ambaretto - Settings","Main-menu Graphics did not open settings");});
            key(KEY_ESCAPE);
            steps.push_back([]{require(title=="Ambaretto - Main menu","Cancelling Graphics did not return to the main menu");});
            key(KEY_F10); key(KEY_RIGHT); key(KEY_RIGHT); key(KEY_ENTER); key(KEY_ESCAPE);
            steps.push_back([]{require(title=="Ambaretto - Main menu","Closing Help left the main menu");});
            key(KEY_DOWN); key(KEY_ENTER);
            require(!main_menu(selected,root/"cities",controls,graphics),"Quit did not leave the main menu");
            require(window_count==initial_windows,"Menu navigation opened another window");
        } CloseWindow();
        // Saved libraries open in popups, without replacing the component editing controls.
        BuildingMesh building; building.name="Menu test building";
        require(building.save(root/"buildings",error),"Building fixture save failed");
        stage="building shape composition"; window(); {
            ui::FontResource font; auto parts=building; parts.name="Parts workflow";
            require(parts.set_footprint(4,4,error,true),error.c_str());
            idle(); click(140,258); wheel(-5); click(220,332); number_text("16"); click(100,430);
            steps.push_back([]{auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/"building-parts-workflow.png").string().c_str()); UnloadImage(image);});
            click(250,495);
            const auto result=building_builder(parts,root/"editor-workflows");
            require(result && result->faces.size()==12 && result->part_vertices(1).size()==8,"Parts controls did not insert and save a shape");
            for (int vertex : result->part_vertices(1)) require(result->points()[vertex].GetX()>13,"Shape position field did not apply");
        } CloseWindow();
        stage="building UV controls"; window(); {
            ui::FontResource font; auto edited=building; edited.name="UV workflow"; edited.texture="brick01.png";
            idle(); click(275,258); building_wall(edited.size,{0,6,-5.6f}); click(180,340);
            wheel(-5); click(220,398); number_text("30");
            steps.push_back([]{auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/"building-uv-workflow.png").string().c_str()); UnloadImage(image);});
            click(250,495); const auto result=building_builder(edited,root/"editor-workflows");
            require(result && result->faces[0].projection==BuildingProjection::Planar && result->faces[0].rotation==30,"UV selection or rotation controls failed");
            require(result->faces[1].projection==BuildingProjection::Manual,"UV controls changed an unselected face");
        } CloseWindow();
        stage="building decal placement and repeat"; window(); {
            ui::FontResource font; auto edited=building; edited.name="Decal workflow";
            idle(); click(350,258); click(100,340); building_wall(edited.size,{-4,6,-5.6f}); key(KEY_ESCAPE);
            wheel(-10); click(180,384); click(100,430);
            steps.push_back([]{auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/"building-decals-workflow.png").string().c_str()); UnloadImage(image);});
            click(250,495); const auto result=building_builder(edited,root/"editor-workflows");
            require(result && result->decals.size()==6 && result->decals[0].projected,"Click placement or repeat controls failed");
            require(result->decals[0].face==0 && result->decals[0].width*result->decals[0].meters[0]==1,"Decal wall alignment or metric width failed");
        } CloseWindow();
        stage="building decal selection and drag"; window(); {
            ui::FontResource font; auto edited=building; edited.name="Decal drag workflow";
            const char* texture=FileExists((std::string(GetApplicationDirectory())+"assets/textures/window01.png").c_str()) ? "window01.png" : "brick01.png";
            require(edited.place_decal(0,Vec3(-3,5,-5.6f),texture,1,1.5f,error)==0
                && edited.place_decal(0,Vec3(0,6,-5.6f),texture,1,1.5f,error)==1,error.c_str());
            idle(); building_wall(edited.size,{0,6,-5.6f});
            steps.push_back([&]{
                const auto p=building_screen(edited.size,{0,6,-5.6f}); const auto image=LoadImageFromScreen(); int yellow=0;
                for (int y=int(p.y)-12;y<=int(p.y)+12;++y) for (int x=int(p.x)-12;x<=int(p.x)+12;++x) {
                    const auto c=GetImageColor(image,x,y); yellow+=c.r>230 && c.g>220 && c.b<40;
                }
                UnloadImage(image); require(yellow>10,"Clicking a decal did not visibly highlight its footprint"); capture("building-decal-selected.png");
            });
            shortcut(KEY_S);
            steps.push_back([&]{BuildingMesh saved;
                require(BuildingMesh::load(root/"editor-workflows/building-Decal drag workflow.building",saved,error)
                    && saved.decals[1].u==edited.decals[1].u && saved.decals[1].v==edited.decals[1].v,"Selection click changed decal coordinates");});
            building_drag(edited.size,{.2f,6.2f,-5.6f},{1.7f,7.2f,-5.6f});
            shortcut(KEY_Z); shortcut(KEY_S);
            steps.push_back([&]{BuildingMesh saved; Vec3 center;
                require(BuildingMesh::load(root/"editor-workflows/building-Decal drag workflow.building",saved,error)
                    && saved.decal_center(1,center) && (center-Vec3(0,6,-5.6f)).Length()<.01f,"Drag did not undo in one action");});
            shortcut(KEY_Y); key(KEY_G);
            steps.push_back([&]{building_mouse(edited.size,{3,9,-5.6f});}); idle(); key(KEY_ESCAPE);
            building_drag(edited.size,{1.5f,7,-5.6f},{2.5f,8,-5.6f},true);
            wheel(-8); click(100,345); key(KEY_ESCAPE);
            steps.push_back([]{capture("building-decal-dragged.png");});
            click(250,495); const auto result=building_builder(edited,root/"editor-workflows",{},3); Vec3 center;
            require(result && result->decals.size()==2 && result->decal_center(1,center) && (center-Vec3(1.5f,7,-5.6f)).Length()<.08f,
                "Live drag, redo, cancel, off-wall release or duplicate cancel failed");
            require(result->decal_center(0,center) && (center-Vec3(-3,5,-5.6f)).Length()<.001f,"Dragging changed the unselected decal");
        } CloseWindow();
        stage="building popup"; window(); {
            ui::FontResource font;
            idle(); click(350,258);
            steps.push_back([]{require(!saved_popup(),"Removed building Open button still opened the selector");});
            open_saved(); preview("saved-buildings-popup.png"); key(KEY_ENTER); click(250,495);
            const auto result=building_builder(building,root/"buildings");
            require(result && result->name==building.name,"Building popup did not open the saved design");
        } CloseWindow();
        CarDesign car; car.name="Menu test car";
        const auto bodies=car_models(std::filesystem::path(GetApplicationDirectory())/"assets/cars",error),wheels=car_models(std::filesystem::path(GetApplicationDirectory())/"assets/wheels",error);
        require(!bodies.empty() && !wheels.empty(),"Car component fixtures missing"); car.body=bodies.front(); car.wheel=wheels.front();
        require(car.save(root/"cars",error),"Car fixture save failed");
        stage="car popup"; window(); {
            ui::FontResource font;
            idle(); click(215,159); click(350,58);
            steps.push_back([]{require(!saved_popup(),"Removed car Open button still opened the selector");});
            open_saved(); preview("saved-cars-popup.png"); key(KEY_ENTER); click(740,394); click(250,545);
            const auto result=car_builder(car,root/"cars");
            require(result && result->name==car.name && result->type==CarType::Police,"Cancelling Open replaced the car draft");
        } CloseWindow();
        auto officer=player; officer.name="Popup saved officer"; officer.type=CharacterType::Police;
        require(officer.save(root/"characters",error),"Officer fixture save failed");
        stage="character popup"; window(); {
            ui::FontResource font;
            idle(); click(300,58);
            steps.push_back([]{require(!saved_popup(),"Removed character Open button still opened the selector");});
            open_saved(); preview("saved-characters-popup.png"); key(KEY_DOWN); key(KEY_ENTER); click(250,553);
            const auto result=character_builder(player,root/"characters");
            require(result && result->type==CharacterType::Police,"Character popup did not open the selected officer");
        } CloseWindow();
        City city=City::create("Menu pause test");
        require(city.add_land({60,60},{68,68},error) && city.set_spawn({64,64},error),"Game fixture failed");
        city.player_character=player; require(city.save(root/"cities",error),"Game fixture save failed");
        stage="map icon controls"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics;
            City edited=City::create("Map controls"); const auto directory=root/"control-cities";
            const auto object=City::center({66,64}),empty=City::center({65,66});
            require(edited.add_land({56,56},{74,74},error) && edited.set_spawn(CityCell{64,64},error)
                && edited.add_building({{66,64},1,12},error) && edited.add_building({{68,64},1,12},error)
                 ,"Map controls fixture failed");
            edited.make_freeform();require(edited.save(directory,error),error.c_str());
            const auto original=contents(saved_path(edited,directory)); const auto terrain=edited.tiles;
            Camera3D initial{}; Vector3 before{};
            idle(); steps.push_back([&]{initial=scene_camera; require_button({16,56,40,40},true); require_button({60,56,40,40}); capture("city-controls.png");});
            click(80,356); click(80,260); click(36,304);
            steps.push_back([&]{require(title=="Ambaretto - City editor" && contents(saved_path(edited,directory))==original,
                "Disabled Play, Delete or Undo changed the map or left the editor");});
            steps.push_back([]{focused=false;}); click(36,120);
            steps.push_back([&]{require(Vector3Distance(scene_camera.position,initial.position)<.01f,"Unfocused toolbar rotated the camera"); focused=true;});
            click(36,120); steps.push_back([&]{require(Vector3Distance(scene_camera.position,initial.position)>500,"Rotate left icon did nothing");});
            click(80,120); steps.push_back([&]{require(Vector3Distance(scene_camera.position,initial.position)<.01f,"Rotate right icon did not reverse rotation");});
            click(36,164); steps.push_back([]{require(std::abs(scene_camera.fovy-340)<.01f,"Zoom in icon did nothing");});
            click(80,164); steps.push_back([]{require(std::abs(scene_camera.fovy-400)<.01f,"Zoom out icon did not reverse zoom");});
            click(36,208); click(80,208);
            steps.push_back([]{require(scene_camera.up.z==-1,"Top view icon did nothing"); require_button({60,188,40,40},true);});
            map_click(empty); click(80,76); map_click(empty);
            steps.push_back([&]{require(std::abs(scene_camera.target.x-empty.GetX())<1 && std::abs(scene_camera.target.z-empty.GetZ())<1,
                "Hand click did not center the map");});
            map_click(object);
            steps.push_back([&]{require(std::abs(scene_camera.target.x-object.GetX())<1 && std::abs(scene_camera.target.z-object.GetZ())<1,
                "Repeated hand clicks did not center the map"); before=scene_camera.target; event(7,512,300); event(6,MOUSE_BUTTON_LEFT);});
            steps.push_back([]{event(7,562,330);}); steps.push_back([]{event(5,MOUSE_BUTTON_LEFT);}); idle();
            steps.push_back([&]{require(std::abs(scene_camera.target.x-before.x+50*400.f/600)<.1f
                && std::abs(scene_camera.target.z-before.z+30*400.f/600)<.1f,"Hand drag jumped or centered instead of panning");
                require(edited.tiles==terrain && edited.buildings.size()==2 && contents(saved_path(edited,directory))==original,
                    "Selection or camera controls modified the map"); before=scene_camera.target; event(7,512,300); event(6,MOUSE_BUTTON_LEFT);});
            steps.push_back([]{event(7,80,400);}); steps.push_back([]{event(7,512,300);}); steps.push_back([]{event(5,MOUSE_BUTTON_LEFT);}); idle();
            steps.push_back([&]{require(Vector3Distance(scene_camera.target,before)<.01f,"Hand drag resumed after leaving the canvas");});
            click(36,76); map_click(object); click(80,260);
            steps.push_back([&]{require(edited.buildings.size()==1 && edited.tiles==terrain,"Delete icon did not remove only the selected building");});
            key(KEY_F10); click(36,304);
            steps.push_back([&]{require(edited.buildings.size()==1,"Closing a menu leaked a click into Undo");});
            click(36,304); steps.push_back([&]{require(edited.buildings.size()==2,"Undo icon did not restore the deleted building");});
            click(80,304); steps.push_back([&]{require(edited.buildings.size()==1,"Redo icon did not repeat the deletion");});
            click(36,304); click(36,260); map_click(object);
            steps.push_back([&]{require(edited.buildings.size()==1 && edited.tiles==terrain,"Bulldoze icon did not remove the clicked building");});
            click(36,356);
            steps.push_back([&]{City saved; require(City::load(saved_path(edited,directory),saved,error) && saved.buildings.size()==1,"Save icon did not persist the map");});
            click(36,304); steps.push_back([&]{edited.player_character=player;}); click(80,356);
            require(city_menu(edited,directory,controls,graphics,true),"Enabled Play icon did not start the city");
            City saved; require(City::load(saved_path(edited,directory),saved,error) && saved.buildings.size()==2 && saved.player_character,
                "Play icon did not save edits before starting");
        } CloseWindow();
        stage="city pedestrian density settings"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics;
            idle(); open_city_settings();
            click(280,228); number_text("25");
            steps.push_back([&]{require(city.pedestrian_density==25,"City settings did not apply typed density");});
            shortcut(KEY_Z); steps.push_back([&]{require(city.pedestrian_density==100,"Density did not undo");});
            shortcut(KEY_Y); steps.push_back([&]{require(city.pedestrian_density==25,"Density did not redo");});
            open_city_settings(); click(226,274); click(350,438);
            steps.push_back([&]{require(city.pedestrian_density==0,"Density slider cannot disable walking NPCs");});
            open_city_settings(); click(798,274); click(350,438);
            steps.push_back([&]{require(city.pedestrian_density==100,"Density slider cannot select full population");});
            open_city_settings(); click(280,228); number_text("101");
            steps.push_back([&]{require(city.pedestrian_density==100,"Invalid density changed the city"); capture("city-density-invalid.png");});
            key(KEY_ESCAPE);
            open_city_settings(); click(280,228); number_text("25");
            open_city_settings(); steps.push_back([]{capture("city-density-settings.png");}); key(KEY_ESCAPE);
            key(KEY_ESCAPE); click(760,485);
            require(!city_menu(city,root/"cities",controls,graphics,true),"Density settings started Play");
            City loaded;
            require(City::load(root/"cities"/(city.id+".city"),loaded,error) && loaded.pedestrian_density==25 && loaded.player_character,
                "City density or embedded player did not persist after leaving the editor");
        } CloseWindow();
        stage="global road editor"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics;
            const RoadStyle original=city.road_style;
            const auto open_roads=[] {key(KEY_F10); key(KEY_LEFT); key(KEY_LEFT); key(KEY_LEFT); key(KEY_DOWN); key(KEY_ENTER);};
            idle(); open_roads(); idle(3);
            steps.push_back([]{capture("road-editor-preview.png");});
            key(KEY_V); steps.push_back([]{capture("road-editor-top-preview.png");}); key(KEY_V);
            click(130,260); key(KEY_ESCAPE);
            steps.push_back([&]{require(city.road_style==original,"Cancel applied the road preview to the city");});
            open_roads(); click(130,260); click(750,547);
            steps.push_back([&]{require(!city.road_style.sidewalks,"Road editor did not disable sidewalks globally");});
            shortcut(KEY_Z); steps.push_back([&]{require(city.road_style==original,"Global road style did not undo");});
            shortcut(KEY_Y); steps.push_back([&]{require(!city.road_style.sidewalks,"Global road style did not redo");});
            open_roads(); click(130,200); key(KEY_DOWN); key(KEY_DOWN); key(KEY_ENTER); idle(3);
            click(750,547);
            steps.push_back([&]{require(city.road_style.road_texture=="brick01.png","Road texture picker did not apply the selected texture");});
            open_roads(); click(130,260); click(130,375); for (int i=0;i<7;++i) key(KEY_UP); key(KEY_ENTER); idle(3);
            steps.push_back([]{capture("road-editor-custom-preview.png");});
            click(750,547);
            steps.push_back([&]{require(city.road_style.sidewalks && city.road_style.sidewalk_texture=="brick01.png","Sidewalk texture did not apply globally");});
            key(KEY_ESCAPE); click(760,485);
            require(!city_menu(city,root/"cities",controls,graphics,true),"Road editor unexpectedly started Play");
            City loaded;
            require(City::load(root/"cities"/(city.id+".city"),loaded,error) && loaded.road_style==city.road_style,
                "Road style did not persist after leaving the editor");
        } CloseWindow();
        stage="free player and vehicle placement"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics;
            City placed=City::create("Placement workflow"); placed.player_character=player;
            const auto directory=root/"placement-cities";
            BuildingMesh small; small.name="Placement building"; small.size=Vec3(4,12,4);
            require(placed.add_land({60,60},{68,68},error) && placed.set_spawn(CityCell{67,67},error)
                && placed.add_building({{64,64},1,12,small},error) && placed.save(directory,error),"Placement fixture failed");
            const Vec3 focus=*placed.spawn,point=City::center({64,64})+Vec3(-4,0,-3);
            idle(); key(KEY_V); key(KEY_FIVE); city_click(focus,point);
            steps.push_back([&]{require(placed.spawn && City::cell(placed.spawn->GetX(),placed.spawn->GetZ())==CityCell{64,64}
                && std::abs(placed.spawn->GetX()-City::center({64,64}).GetX())>3,"Player placement snapped or rejected the building tile");});
            key(KEY_F10); for (int i=0;i<4;++i) key(KEY_RIGHT); for (int i=0;i<3;++i) key(KEY_DOWN); key(KEY_RIGHT); key(KEY_ENTER);
            city_click(focus,point);
            steps.push_back([&]{require(placed.vehicles.size()==1,"Player or building blocked vehicle placement");});
            key(KEY_FIVE); city_click(focus,point); key(KEY_ONE); city_click(focus,point);
            steps.push_back([]{capture("free-placement.png");}); key(KEY_DELETE);
            steps.push_back([&]{require(placed.vehicles.empty() && placed.buildings.size()==1 && placed.spawn,"Building tile prevented selecting the vehicle");});
            shortcut(KEY_Z); steps.push_back([&]{require(placed.vehicles.size()==1,"Overlapping vehicle deletion did not undo");});
            key(KEY_ESCAPE); click(760,485);
            require(!city_menu(placed,directory,controls,graphics,true),"Placement editor started Play");
            City loaded;
            require(City::load(directory/(placed.id+".city"),loaded,error) && loaded.spawn==placed.spawn && loaded.vehicles.size()==1
                && (loaded.vehicles[0].position-*loaded.spawn).Length()<.01f,"Overlapping player and vehicle did not persist");
        } CloseWindow();
        stage="freeform pen tools and continuous building rotation"; window(); {
            ui::FontResource font;ControllerMapping controls;GraphicsSettings graphics;
            City coast=City::create("Freeform pen workflow");coast.make_freeform();
            const auto directory=root/"freeform-cities";
            const auto point=[](float x,float z){return Vec3(x,City::level,z);};
            idle();key(KEY_V);key(KEY_TWO);
            for(auto p:{point(-120,-60),point(120,-60),point(120,100),point(-120,100),point(-120,-60)})map_click(p);
            steps.push_back([&]{require(coast.areas.size()==1 && coast.land_at(point(0,0)),"Land pen did not close and fill");});
            key(KEY_ZERO);
            for(auto p:{point(-30,-30),point(0,-30),point(0,10),point(-30,10)})map_click(p);
            key(KEY_ENTER);
            steps.push_back([&]{require(coast.areas.size()==2 && !coast.land_at(point(-15,-10)),"Water pen did not create a lake");});
            key(KEY_NINE);key(KEY_ENTER);
            for(auto p:{point(-100,-45),point(-50,-45),point(-70,-5)})map_click(p);
            key(KEY_ENTER);
            steps.push_back([&]{require(coast.areas.size()==3 && coast.areas.back().kind==AreaKind::Texture,"Texture pen did not fill the outline");});
            key(KEY_THREE);map_drag(point(-100,30),point(-70,70));map_drag(point(100,30),point(130,-10));key(KEY_ENTER);
            steps.push_back([&]{require(coast.paths.size()==1 && coast.paths[0].path.nodes[0].tangent.Length()>30,"Road pen lost its Bezier handles");});
            key(KEY_FOUR);map_click(point(65.25f,-20.75f));
            steps.push_back([&]{require(coast.buildings.size()==1 && std::abs(coast.buildings[0].position->GetX()-City::center(coast.buildings[0].cell).GetX())>1,"Building placement snapped to the grid");});
            key(KEY_ONE);map_click(point(65.25f,-20.75f));
            unsigned rotation_uploads=0;
            steps.push_back([&]{rotation_uploads=mesh_uploads;event(2,KEY_R);});idle(12);
            steps.push_back([&]{require(mesh_uploads==rotation_uploads,"Rotating a selected building rebuilt GPU meshes");event(1,KEY_R);});idle();
            steps.push_back([&]{require(coast.buildings[0].yaw>0 && coast.buildings[0].yaw<180,("Holding R did not rotate continuously: "+std::to_string(coast.buildings[0].yaw)).c_str());});
            shortcut(KEY_Z);steps.push_back([&]{require(coast.buildings[0].yaw==0,"One Undo did not restore the whole rotation gesture");});
            shortcut(KEY_Y);key(KEY_EIGHT);map_click(point(80,75));
            steps.push_back([&]{require(!coast.trees.empty(),"Tree brush stopped painting");});
            key(KEY_ONE);map_click(point(-100,30));map_drag(point(-100,30),point(-95,25));key(KEY_ENTER);
            steps.push_back([&]{require(coast.paths.size()==1 && std::abs(coast.paths[0].path.nodes[0].position.GetX()+95)<1,"Existing road node could not be moved");capture("freeform-pen-editor.png");});
            key(KEY_ONE);map_click(point(-95,25));map_click(point(125,90));key(KEY_ENTER);
            key(KEY_ONE);map_click(point(-95,25));map_drag(point(-95,25),point(-90,20));
            steps.push_back([&]{require(!coast.paths[0].path.closed,"Dragging the first road node unexpectedly closed it");});
            map_click(point(-90,20));
            steps.push_back([&]{require(coast.paths[0].path.closed && coast.paths[0].path.nodes.size()==3,"Existing road could not reconnect to its first node");});
            shortcut(KEY_Z);steps.push_back([&]{require(!coast.paths[0].path.closed,"Closing a road did not undo");});
            shortcut(KEY_Y);steps.push_back([&]{require(coast.paths[0].path.closed,"Closing a road did not redo");});
            key(KEY_THREE);for(auto p:{point(15,-45),point(70,-45),point(70,0),point(15,-45)})map_click(p);
            steps.push_back([&]{require(coast.paths.size()==2 && coast.paths[1].path.closed,"New road could not close into a loop");});
            steps.push_back([]{triangle_picks=0;measure_preview=true;});idle(24);
            steps.push_back([]{
                measure_preview=false;require(triangle_picks==0,"Idle editor still scans the full terrain for picking");
                std::sort(preview_frames.begin(),preview_frames.end());
                std::cout<<"Warm city editor CPU frame median/p95: "<<preview_frames[preview_frames.size()/2]<<" / "<<preview_frames[(preview_frames.size()-1)*95/100]<<" ms; terrain triangle picks: "<<triangle_picks<<'\n';
                capture("freeform-road-loops.png");
            });
            shortcut(KEY_S);
            steps.push_back([&]{City loaded;require(City::load(directory/(coast.id+".city"),loaded,error) && loaded.freeform && loaded.areas.size()==3 && loaded.paths.size()==2 && loaded.paths[0].path.closed && loaded.paths[1].path.closed && loaded.buildings[0].yaw>0,"Freeform editor changes did not save");});
            key(KEY_ESCAPE);click(760,485);
            require(!city_menu(coast,directory,controls,graphics,true),"Pen tools started Play");
        } CloseWindow();
        stage="road draft history and node snapping"; window(); {
            ui::FontResource font;ControllerMapping controls;GraphicsSettings graphics;
            City roads=City::create("Junction workflow");roads.make_freeform();
            const auto point=[](float x,float z){return Vec3(x,City::level,z);};
            idle();key(KEY_V);key(KEY_THREE);
            map_click(point(-100,30));map_click(point(0,30));map_click(point(100,30));shortcut(KEY_Z);key(KEY_ENTER);
            steps.push_back([&]{require(roads.paths.size()==1 && roads.paths[0].path.nodes.size()==2,"Ctrl+Z did not remove the last draft node");});
            shortcut(KEY_Z);steps.push_back([&]{require(roads.paths.empty(),"Finished road did not undo");});
            shortcut(KEY_Y);steps.push_back([&]{require(roads.paths.size()==1,"Finished road did not redo");});
            key(KEY_THREE);map_click(point(-40,-70));map_click(point(-40,31));shortcut(KEY_Z);shortcut(KEY_Y);key(KEY_ENTER);
            steps.push_back([&]{require(roads.paths.size()==2 && std::abs(roads.paths[1].path.nodes.back().position.GetZ()-30)<.01f,"Draft redo lost the snapped T-junction");});
            key(KEY_ONE);map_click(point(-40,-20));map_drag(point(-40,30),point(-98,31));shortcut(KEY_Z);key(KEY_ENTER);
            steps.push_back([&]{require(std::abs(roads.paths[1].path.nodes.back().position.GetX()+40)<1,"Node drag did not undo as one gesture");});
            key(KEY_ONE);map_click(point(-40,-20));map_drag(point(-40,30),point(-98,31));shortcut(KEY_Z);shortcut(KEY_Y);key(KEY_ENTER);
            steps.push_back([&]{require(roads.paths[1].path.nodes.back().position==roads.paths[0].path.nodes.front().position,"Dragging a node did not snap exactly to another road node");capture("road-node-connections.png");});
            shortcut(KEY_Z);steps.push_back([&]{require(std::abs(roads.paths[1].path.nodes.back().position.GetX()+40)<1,"Applied road edit did not undo");});
            key(KEY_ONE);map_click(point(-40,-20));shortcut(KEY_Y);
            steps.push_back([&]{require(roads.paths[1].path.nodes.back().position==roads.paths[0].path.nodes.front().position,"Selecting an unchanged road blocked city redo");});
            key(KEY_THREE);map_drag(point(40,-50),point(60,-30));map_click(point(100,0));shortcut(KEY_Z);shortcut(KEY_Z);shortcut(KEY_Y);shortcut(KEY_Y);key(KEY_ENTER);
            steps.push_back([&]{require(roads.paths.size()==3 && roads.paths[2].path.nodes.size()==2 && roads.paths[2].path.nodes.front().tangent.Length()>20,"Draft undo/redo did not preserve a curve gesture");});
            key(KEY_ESCAPE);click(760,485);
            require(!city_menu(roads,root/"junction-cities",controls,graphics,true),"Road editing started Play");
        } CloseWindow();
        stage="map building popup"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics;
            idle(); key(KEY_F10); for (int i=0;i<3;++i) key(KEY_RIGHT); for (int i=0;i<4;++i) key(KEY_DOWN); key(KEY_ENTER);
            preview("map-building-popup.png"); key(KEY_ENTER);
            steps.push_back([&]{require(city.buildings.empty(),"Building selection leaked a placement into the map");});
            key(KEY_F10); for (int i=0;i<3;++i) key(KEY_RIGHT); for (int i=0;i<5;++i) key(KEY_DOWN); key(KEY_ENTER);
            key(KEY_V); city_click(*city.spawn,City::center({66,66}));
            steps.push_back([&]{require(city.buildings.size()==1 && !city.buildings[0].mesh,"Plain block menu kept the saved building design");});
            shortcut(KEY_Z);
            key(KEY_ESCAPE); click(760,485);
            require(!city_menu(city,root/"cities",controls,graphics,true),"Building selector started a game");
        } CloseWindow();
        // Deletion defaults to Cancel, then removes only the confirmed saved design.
        const auto deletion_root=root/"deletion";
        require(building.save(deletion_root/"buildings",error) && car.save(deletion_root/"cars",error)
            && player.save(deletion_root/"characters",error),"Deletion fixtures failed");
        for (const auto kind : {DesignKind::Building,DesignKind::Car,DesignKind::Character}) {
            stage="design deletion"; window(); {
                ui::FontResource font;
                const auto path=kind==DesignKind::Building ? saved_path(building,deletion_root/"buildings")
                    : kind==DesignKind::Car ? saved_path(car,deletion_root/"cars") : saved_path(player,deletion_root/"characters");
                const auto original=contents(path);
                idle(); key(KEY_DELETE); key(KEY_ENTER);
                steps.push_back([&]{require(contents(path)==original,"Default Enter confirmed deletion");});
                key(KEY_DELETE); key(KEY_ESCAPE);
                steps.push_back([&]{require(contents(path)==original,"Escape confirmed deletion");});
                click(860,345);
                steps.push_back([]{capture("delete-design-confirmation.png");});
                if (kind==DesignKind::Character) {key(KEY_LEFT); key(KEY_ENTER);} else click(280,394);
                steps.push_back([&]{require(!std::filesystem::exists(path)
                    && contents(path.parent_path()/"deleted"/(path.filename().string()+"-1")/path.filename())==original,"Confirmed deletion did not keep a backup");});
                key(KEY_DELETE); duplicate_key(); key(KEY_ESCAPE);
                design_menu(kind,deletion_root);
            } CloseWindow();
        }
        stage="saved popup deletion"; window(); {
            ui::FontResource font; const auto directory=deletion_root/"popup";
            require(building.save(directory,error),"Popup deletion fixture failed");
            idle(); open_saved(); key(KEY_DELETE); click(280,394); key(KEY_ENTER);
            steps.push_back([&]{require(!std::filesystem::exists(saved_path(building,directory)),"Empty saved popup opened or recreated the deleted design");});
            key(KEY_ESCAPE); key(KEY_ESCAPE);
            require(!building_builder(building,directory),"Deleting a library design saved the open draft");
        } CloseWindow();
        stage="city deletion"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics;
            City selected=city; const auto directory=deletion_root/"cities";
            require(city.save(directory,error),"City deletion fixture failed");
            const auto path=saved_path(city,directory),progress=directory/(city.id+".play");
            const auto original=contents(path); std::ofstream(progress)<<"Keep progress";
            idle(); key(KEY_DELETE); key(KEY_ENTER);
            steps.push_back([&]{require(contents(path)==original && selected.id==city.id,"Cancel changed the selected city");});
            key(KEY_DELETE); click(280,394);
            steps.push_back([&]{const auto archive=directory/"deleted"/(path.filename().string()+"-1");
                require(selected.id.empty() && !std::filesystem::exists(path) && !std::filesystem::exists(progress)
                    && contents(archive/path.filename())==original && contents(archive/progress.filename())=="Keep progress","City deletion lost its checkpoint or retained the active city");});
            key(KEY_DELETE); key(KEY_ESCAPE);
            require(!city_menu(selected,directory,controls,graphics),"City deletion started Play");
        } CloseWindow();
        // Cities stay a list and never construct 3D previews, including in Play.
        const auto play_directory=root/"play/cities";
        auto playable=city; playable.id="0000000000000001"; playable.name="B playable city";
        auto unavailable=City::create("A unavailable city"); unavailable.id="0000000000000002";
        unavailable.make_freeform();
        require(playable.save(play_directory,error) && unavailable.save(play_directory,error),"Play fixtures failed");
        const auto playable_path=play_directory/(playable.id+".city"),unavailable_path=play_directory/(unavailable.id+".city");
        const auto original_playable=contents(playable_path),original_unavailable=contents(unavailable_path);
        stage="play-only city selector"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics; City selected=unavailable;
            const auto targets=render_targets,models=model_loads,uploads=mesh_uploads,shaders=shader_loads;
            idle(); key(KEY_F10); key(KEY_ENTER);
            steps.push_back([]{require(saved_popup(),"Play city list did not open as a popup"); capture("play-city-selector.png");});
            click(220,170); key(KEY_E); key(KEY_INSERT); key(KEY_DELETE); key(KEY_F2); duplicate_key(); key(KEY_ENTER);
            steps.push_back([]{require(title=="Ambaretto - Play" && saved_popup(),"Unavailable city started playing or opened an editor");});
            key(KEY_RIGHT); key(KEY_ENTER);
            require(main_menu(selected,play_directory,controls,graphics) && selected.id==playable.id,"Choosing a playable city did not start Play");
            require(render_targets==targets && model_loads==models && mesh_uploads==uploads && shader_loads==shaders,"Play city list constructed 3D resources");
            require(contents(playable_path)==original_playable && contents(unavailable_path)==original_unavailable,"Play changed a saved city");
        } CloseWindow();
        stage="city create / edit chooser"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics; City selected=unavailable;
            idle(); key(KEY_F2);
            steps.push_back([]{require(title=="Ambaretto - Cities","Removed shortcuts opened city management");});
            key(KEY_ENTER);
            steps.push_back([]{require(title=="Ambaretto - City editor","City chooser Enter did not open the editor");});
            key(KEY_ESCAPE); click(760,220); click(600,185); click(300,514);
            steps.push_back([]{require(title=="Ambaretto - City editor","Create city did not open the new map editor");});
            key(KEY_ESCAPE); click(760,485);
            require(!city_menu(selected,play_directory,controls,graphics),"City chooser started a game");
            require(selected.name=="New city" && !selected.spawn && !selected.player_character && selected.land_count()==0,"New empty city was not created");
            require(contents(playable_path)==original_playable && contents(unavailable_path)==original_unavailable,"City chooser changed the existing maps");
        } CloseWindow();
        stage="city list scrolling / paging";
        const auto list_directory=root/"list-cities";
        std::vector<City> list_cities(12,playable);
        for (int i=0;i<12;++i) {
            auto& value=list_cities[i]; value.id=std::string(15,'0')+"3456789abcde"[i]; value.name=std::string(1,char('A'+i))+" list city";
            require(value.save(list_directory,error),"City list fixture failed");
        }
        window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics; auto selected=std::make_unique<City>(list_cities.front());
            const auto targets=render_targets,models=model_loads,uploads=mesh_uploads,shaders=shader_loads;
            idle(); key(KEY_END); key(KEY_ESCAPE);
            require(!city_menu(*selected,list_directory,controls,graphics),"City list started Play");
            reset(); idle(); key(KEY_ENTER); key(KEY_END); key(KEY_PAGE_UP); key(KEY_ENTER);
            require(main_menu(*selected,list_directory,controls,graphics) && selected->id==list_cities[6].id,"City list End/Page Up chose the wrong row");
            *selected=list_cities.front(); reset(); idle(); key(KEY_ENTER);
            steps.push_back([]{event(7,220,210); event(8,0,-3);}); idle(); click(220,170); key(KEY_ENTER);
            require(main_menu(*selected,list_directory,controls,graphics) && selected->id==list_cities[7].id,"City list wheel scrolling or row selection failed");
            require(render_targets==targets && model_loads==models && mesh_uploads==uploads && shader_loads==shaders,"Browsing cities allocated 3D resources");
        } CloseWindow();
        stage="terrain preview / regenerate / create"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics; City selected=unavailable;
            const auto directory=root/"preview-cities"; City expected=City::create("Expected preview");
            require(expected.generate_terrain(48,32,1234,error,{0,35}),"Preview fixture failed");
            std::string first,regenerated;
            idle(); click(760,220); click(410,276); replace_text("32"); click(300,336); replace_text("1234");
            steps.push_back([&]{require_terrain_preview(expected); first=terrain_pixels(); capture("terrain-preview.png");});
            click(550,336);
            steps.push_back([&]{regenerated=terrain_pixels(); require(first!=regenerated,"Regenerate did not update the preview"); capture("terrain-regenerated.png");});
            shortcut(KEY_R);
            steps.push_back([&]{require(regenerated!=terrain_pixels(),"Keyboard regenerate did not update the preview");});
            click(300,336); replace_text("4294967296"); key(KEY_ENTER);
            steps.push_back([&]{require(selected.id==unavailable.id && !std::filesystem::exists(directory),"Invalid seed saved a city"); capture("terrain-preview-invalid.png");});
            click(300,336); replace_text("1234"); click(120,276); replace_text("0"); key(KEY_ENTER);
            steps.push_back([&]{require(selected.id==unavailable.id && !std::filesystem::exists(directory),"Invalid dimensions saved a city");});
            key(KEY_ESCAPE);
            steps.push_back([&]{require(selected.id==unavailable.id && !std::filesystem::exists(directory),"Preview or Cancel changed the existing city");});
            click(760,220); click(410,276); replace_text("32"); click(300,336); replace_text("1234");
            steps.push_back([&]{require_terrain_preview(expected);}); click(300,514);
            steps.push_back([&]{require(title=="Ambaretto - City editor" && selected.freeform && same_land(selected,expected),"Create did not use the displayed terrain");capture("smooth-terrain-editor.png");});
            key(KEY_ESCAPE); click(760,485);
            require(!city_menu(selected,directory,controls,graphics),"Terrain preview started Play");
            City loaded;
            require(City::load(directory/(selected.id+".city"),loaded,error) && loaded.freeform && same_land(loaded,expected),
                "Saved terrain differs from the preview");
        } CloseWindow();
        stage="flat terrain / water / river / islands"; window(); {
            ui::FontResource font;ControllerMapping controls;GraphicsSettings graphics;City selected=unavailable;
            const auto directory=root/"terrain-settings";
            City flat=City::create("Flat"),water=flat,river=flat,islands=flat;
            require(flat.generate_terrain(48,48,1234,error,{0,20}) && water.generate_terrain(48,48,1234,error,{0,60})
                && river.generate_terrain(48,48,1234,error,{0,60,true}) && islands.generate_terrain(48,48,1234,error,{0,60,true,true}),"Terrain settings fixtures failed");
            idle();click(760,220);click(300,336);replace_text("1234");click(300,418);replace_text("20");
            steps.push_back([&]{require_terrain_preview(flat);});
            click(300,418);replace_text("60");steps.push_back([&]{require_terrain_preview(water);});
            click(160,462);steps.push_back([&]{require_terrain_preview(river);});
            key(KEY_SPACE);steps.push_back([&]{require_terrain_preview(water);});key(KEY_SPACE);key(KEY_TAB);key(KEY_SPACE);
            steps.push_back([&]{require_terrain_preview(islands);});
            click(300,418);replace_text("101");key(KEY_ENTER);
            steps.push_back([&]{require(selected.id==unavailable.id && !std::filesystem::exists(directory),"Invalid water coverage saved a map");});
            click(300,418);replace_text("60");click(300,514);
            steps.push_back([&]{require(title=="Ambaretto - City editor" && selected.freeform && same_land(selected,islands),"Create ignored flat terrain settings");});
            key(KEY_ESCAPE);click(760,485);
            require(!city_menu(selected,directory,controls,graphics),"Terrain settings editor started Play");
            City loaded;require(City::load(directory/(selected.id+".city"),loaded,error) && loaded.freeform && same_land(loaded,islands),"Generated freeform islands did not save");
        } CloseWindow();
        // Each creation menu duplicates to disk, selects the copy, and leaves the source alone.
        for (auto kind:{DesignKind::Building,DesignKind::Car,DesignKind::Character}) {
            stage="design menu duplicate"; window(); {
                ui::FontResource font;
                unsigned loaded_models=0;
                std::string selected_label;
                const auto label_pixels=[] {
                    const auto image=LoadImageFromScreen(); std::string pixels;
                    for (int y=392;y<414;++y) for (int x=108;x<604;++x) {
                        const auto c=GetImageColor(image,x,y); pixels+=char(c.r); pixels+=char(c.g); pixels+=char(c.b);
                    }
                    UnloadImage(image); return pixels;
                };
                const auto original_path=kind==DesignKind::Building ? saved_path(building,root/"buildings")
                    : kind==DesignKind::Car ? saved_path(car,root/"cars") : saved_path(player,root/"characters");
                const auto original=contents(original_path);
                const auto copy_path=kind==DesignKind::Building ? root/"buildings/building-Menu test building copy.building"
                    : kind==DesignKind::Car ? root/"cars/car-Menu test car copy.car" : root/"characters/character-Menu test player copy.character";
                idle(); steps.push_back([&]{loaded_models=model_loads;}); click(760,345);
                steps.push_back([=]{require(std::filesystem::exists(copy_path),"Duplicate menu button did not save a copy");});
                steps.push_back([&]{require(model_loads==loaded_models,"Duplicating a design reloaded its shared models");});
                steps.push_back([&]{require_preview({96,174,520,210}); selected_label=label_pixels();});
                key(KEY_RIGHT);
                steps.push_back([&]{require(label_pixels()!=selected_label,"Right did not cycle away from the duplicated design");});
                click(170,435);
                steps.push_back([&]{require(label_pixels()==selected_label,"Previous did not wrap back to the duplicated design");});
                if (kind==DesignKind::Building) steps.push_back([]{auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/"duplicate-design-menu.png").string().c_str()); UnloadImage(image);});
                key(KEY_ESCAPE);
                design_menu(kind,root);
                require(contents(original_path)==original,"Duplicating from a menu changed the original design");
            } CloseWindow();
        }
        stage="building popup duplicate"; window(); {
            ui::FontResource font; const auto path=saved_path(building,root/"buildings"); const auto original=contents(path);
            idle(); open_saved(); duplicate_key(); preview("duplicate-building-popup.png"); key(KEY_ENTER); click(250,495);
            const auto result=building_builder(building,root/"buildings");
            require(result && result->name==building.name+" copy 2" && contents(path)==original,"Building popup did not open a separate duplicate");
        } CloseWindow();
        stage="building preview cache / eviction / refresh"; window(); {
            ui::FontResource font; const auto cache_root=root/"preview-cache";
            for (int i=0;i<6;++i) {auto value=building; value.name=std::string(1,char('A'+i))+" cached building"; require(value.save(cache_root/"buildings",error),"Preview cache fixture failed");}
            unsigned uploads=0,images=0,targets=0,draws=0,shaders=0;
            idle(); key(KEY_RIGHT);
            steps.push_back([&]{uploads=mesh_uploads; images=image_loads; targets=render_targets; shaders=shader_loads; measure_preview=true;});
            for (int i=0;i<8;++i) {key(KEY_LEFT); key(KEY_RIGHT);}
            steps.push_back([&]{
                measure_preview=false;
                require(mesh_uploads==uploads && image_loads==images && render_targets==targets && shader_loads==shaders,"Warm preview switches rebuilt GPU resources or decoded images");
                std::sort(preview_frames.begin(),preview_frames.end());
                require(!preview_frames.empty(),"Preview frame measurement was empty");
                std::cout<<"Warm building preview CPU frame median/p95: "<<preview_frames[preview_frames.size()/2]<<" / "<<preview_frames[(preview_frames.size()-1)*95/100]<<" ms (excludes presentation/frame limiter)\n";
            });
            steps.push_back([&]{focused=false; draws=preview_draws;}); idle(8);
            steps.push_back([&]{require(preview_draws==draws,"Unfocused preview kept redrawing"); focused=true;});
            key(KEY_LEFT);
            for (int i=0;i<4;++i) key(KEY_RIGHT);
            steps.push_back([&]{uploads=mesh_uploads;});
            for (int i=0;i<4;++i) key(KEY_LEFT);
            steps.push_back([&]{require(mesh_uploads>uploads,"Old building preview was retained beyond the cache limit"); uploads=mesh_uploads;});
            click(760,402);
            steps.push_back([&]{require(mesh_uploads>uploads,"Refresh reused stale building geometry"); largest_preview=0; SetWindowSize(2048,1200);}); idle(5);
            steps.push_back([&]{require(largest_preview>0 && largest_preview<=720,"Large window allocated an oversized preview target"); capture("cached-building-preview.png");});
            key(KEY_ESCAPE);
            design_menu(DesignKind::Building,cache_root);
            std::cout<<"Preview cache: 16 warm switches, zero mesh uploads/image loads/shader loads/target allocations; eviction, refresh, suspension and 720 px cap passed\n";
        } CloseWindow();
        stage="car popup duplicate"; window(); {
            ui::FontResource font; const auto path=saved_path(car,root/"cars"); const auto original=contents(path);
            idle(); open_saved(); click(390,495); key(KEY_ENTER); click(75,159); click(250,545);
            const auto result=car_builder(car,root/"cars");
            require(result && result->name==car.name+" copy 2" && result->type==CarType::Civilian && contents(path)==original,"Car duplicate edits changed the source");
        } CloseWindow();
        stage="character popup duplicate"; window(); {
            ui::FontResource font; const auto path=saved_path(player,root/"characters"); const auto original=contents(path);
            idle(); open_saved(); duplicate_key(); key(KEY_ENTER); click(75,159); click(250,553);
            const auto result=character_builder(player,root/"characters");
            require(result && result->name==player.name+" copy 2" && result->type==CharacterType::Player && contents(path)==original,"Character duplicate edits changed the source");
        } CloseWindow();
        stage="city duplicate"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics; City selected=city;
            const auto original_path=saved_path(city,root/"cities"); const auto original=contents(original_path);
            idle(); key(KEY_F10); key(KEY_DOWN); key(KEY_DOWN); key(KEY_ENTER);
            steps.push_back([&]{require(selected.id!=city.id && selected.name==city.name+" copy" && std::filesystem::exists(saved_path(selected,root/"cities")),"Cities File > Duplicate did not save a new city");});
            key(KEY_UP); click(760,345);
            steps.push_back([&]{require(selected.name==city.name+" copy 2","City button reused the previous copy name"); auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/"duplicate-city-menu.png").string().c_str()); UnloadImage(image);});
            key(KEY_ENTER);
            steps.push_back([]{require(title=="Ambaretto - City editor","Duplicated city did not open in the editor");});
            key(KEY_ESCAPE); click(760,485);
            require(!city_menu(selected,root/"cities",controls,graphics),"Duplicate city started Play");
            require(contents(original_path)==original,"Duplicate city changed its original");
        } CloseWindow();
        stage="placement selector duplicates"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics;
            City selected=city; selected.id=City::create("test").id; selected.name="Placement duplicate test";
            require(selected.save(root/"cities",error),"Placement selector fixture failed");
            idle(); key(KEY_F10); for (int i=0;i<4;++i) key(KEY_RIGHT); key(KEY_DOWN); key(KEY_DOWN); key(KEY_RIGHT); key(KEY_ENTER);
            duplicate_key();
            steps.push_back([&]{require(std::filesystem::exists(root/"cars/car-Menu test car copy 3.car"),"Car placement selector did not duplicate"); auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/"duplicate-car-selector.png").string().c_str()); UnloadImage(image);});
            click(760,490);
            key(KEY_F10); for (int i=0;i<4;++i) key(KEY_RIGHT); key(KEY_DOWN); key(KEY_RIGHT); key(KEY_DOWN); key(KEY_DOWN); key(KEY_ENTER);
            duplicate_key();
            steps.push_back([]{auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/"duplicate-player-selector.png").string().c_str()); UnloadImage(image);});
            click(210,490); key(KEY_ESCAPE); click(760,485);
            require(!city_menu(selected,root/"cities",controls,graphics,true),"Placement duplicate started a game");
            require(selected.player_character && selected.player_character->name==player.name+" copy 3" && selected.vehicles.empty(),"Placement selectors lost the duplicate or leaked a placement");
        } CloseWindow();
        stage="empty Play list"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics; City selected;
            idle(); key(KEY_ENTER); key(KEY_ENTER); duplicate_key(); key(KEY_ESCAPE); key(KEY_ESCAPE);
            require(!main_menu(selected,root/"empty/cities",controls,graphics),"Empty Play list started a game");
            require(!std::filesystem::exists(root/"empty/cities"),"Play duplicated or edited an empty library");
        } CloseWindow();
        stage="car carousel / rotation / wraparound"; window(); {
            ui::FontResource font; const auto library=saved_cars(root/"cars",error);
            require(library.size()>1,"Carousel needs multiple saved cars");
            const auto preview_pixels=[] {
                const auto image=LoadImageFromScreen(); std::string pixels;
                for (int y=150;y<360;y+=4) for (int x=312;x<712;x+=4) {
                    const auto c=GetImageColor(image,x,y); pixels+=char(c.r); pixels+=char(c.g); pixels+=char(c.b);
                }
                UnloadImage(image); return pixels;
            };
            std::string first_frame;
            idle(3); steps.push_back([&]{first_frame=preview_pixels();}); idle(24);
            steps.push_back([&]{require(first_frame!=preview_pixels(),"Selected car preview did not rotate"); capture("rotating-car-selector.png");});
            for (std::size_t i=0;i<library.size();++i) click(850,470);
            key(KEY_RIGHT); key(KEY_ENTER);
            auto selected=choose_play_car(root/"cars");
            require(selected && selected->name==library[1].name,"Next did not wrap or Right did not cycle to the displayed car");
            reset(); idle(); click(130,470); key(KEY_ENTER);
            selected=choose_play_car(root/"cars");
            require(selected && selected->name==library.back().name,"Previous did not wrap to the last car");
        } CloseWindow();
        stage="car carousel / empty and unavailable cars"; window(); {
            ui::FontResource font;
            idle(); click(850,470); key(KEY_LEFT); key(KEY_ENTER); key(KEY_ESCAPE);
            require(!choose_play_car(root/"no-cars"),"Empty carousel selected a car");
            auto unavailable=car; unavailable.body="missing-preview-body.glb";
            require(unavailable.save(root/"unavailable-cars",error),"Unavailable car fixture failed");
            reset(); idle(); click(850,470); key(KEY_RIGHT); key(KEY_ENTER); idle(3);
            unsigned draws=0;
            steps.push_back([&]{require(title=="Ambaretto - Choose your car","Unavailable car started driving"); draws=preview_draws;}); idle(8);
            steps.push_back([&]{require(preview_draws==draws,"Unavailable car preview kept redrawing");}); key(KEY_ESCAPE);
            require(!choose_play_car(root/"unavailable-cars"),"Carousel accepted an unavailable car");
        } CloseWindow();
        City race_city=City::create("Race workflow");
        require(race_city.add_land({56,56},{73,73},error),error.c_str());
        for (auto ends : {std::make_pair(CityCell{58,58},CityCell{70,58}),std::make_pair(CityCell{70,58},CityCell{70,70}),
            std::make_pair(CityCell{70,70},CityCell{58,70}),std::make_pair(CityCell{58,70},CityCell{58,58})})
            require(race_city.add_road(City::road_stroke(ends.first,ends.second),error),error.c_str());
        require(race_city.set_spawn(CityCell{60,58},error),error.c_str()); race_city.player_character=player;
        stage="race editor creation"; window(); {
            ui::FontResource font;
            idle(3); replace_text("Test circuit"); click(400,294); key(KEY_ENTER); idle(3); key(KEY_V);
            city_click(*race_city.spawn,City::center({60,58})); city_click(*race_city.spawn,City::center({68,58}));
            city_click(*race_city.spawn,City::center({70,68})); city_click(*race_city.spawn,City::center({60,70}));
            steps.push_back([]{capture("race-editor-course.png");}); click(480,136); key(KEY_ESCAPE);
            require(edit_current_city(race_city,root/"cities",true,true),"Race editor did not return to Play");
            require(race_city.races.size()==1 && race_city.races[0].name=="Test circuit" && race_city.races[0].laps==4
                && race_city.races[0].checkpoints.size()==4,"Race editor did not save name, laps or road clicks");
        } CloseWindow();
        stage="race editor edit and delete"; window(); {
            ui::FontResource font;
            idle(3); key(KEY_F10); key(KEY_LEFT); key(KEY_LEFT); key(KEY_DOWN); key(KEY_ENTER); idle(3); key(KEY_ENTER);
            replace_text("Edited circuit"); key(KEY_ENTER); idle(3); key(KEY_V);
            city_click(*race_city.spawn,race_city.races[0].checkpoints[1]); city_click(*race_city.spawn,City::center({67,58}));
            city_click(*race_city.spawn,race_city.races[0].checkpoints[2]); click(770,136); click(480,136); key(KEY_ESCAPE);
            require(edit_current_city(race_city,root/"cities",true),"Race editing did not return to Play");
            require(race_city.races[0].name=="Edited circuit","Race name edit did not persist");
            require(race_city.races[0].checkpoints.size()==3 && (race_city.races[0].checkpoints[1]-City::center({67,58})).Length()<1,"Moving and deleting a checkpoint did not persist");
            event(1,KEY_ESCAPE);
            reset(); idle(3); key(KEY_F10); key(KEY_LEFT); key(KEY_LEFT); key(KEY_DOWN); key(KEY_ENTER); idle(3); key(KEY_ENTER);
            click(500,410); idle(3); key(KEY_ESCAPE);
            require(edit_current_city(race_city,root/"cities",true),"Deleting a race did not return to Play");
            require(race_city.races.empty(),"Race deletion did not persist");
        } CloseWindow();
        RaceMap race_map; race_map.name="Test circuit"; race_map.laps=1;
        stage="empty race picker"; window(); {
            ui::FontResource font; idle(3); steps.push_back([]{capture("empty-race-picker.png");}); key(KEY_ESCAPE);
            require(choose_race(race_city)==-1,"Empty race list did not return with Back"); event(1,KEY_ESCAPE);
            reset(); idle(3); key(KEY_INSERT); require(choose_race(race_city)==-2,"Empty race list did not offer creation");
        } CloseWindow();
        for (auto cell : {CityCell{60,58},CityCell{68,58},CityCell{70,68},CityCell{60,70}}) {
            Vec3 point; require(snap_race_point(race_city,City::center(cell),point),"Race fixture checkpoint failed"); race_map.checkpoints.push_back(point);
        }
        race_city.races.push_back(race_map); require(race_city.save(root/"cities",error),error.c_str());
        // Pause freezes actual Jolt steps; resume continues; End Game returns to the menu.
        stage="game pause / resume / main menu"; reset(); unsigned paused_steps=0;
        CityVehicle parked(CityVehicleKind::Car,*city.spawn+Vec3(2.8f,0,0)); parked.car=car;
        require(city.add_vehicle(parked,error) && city.save(root/"cities",error),"Car HUD fixture failed");
        const auto dial_visible=[](int x,bool clock) {
            const auto image=LoadImageFromScreen(); const int y=GetScreenHeight()-110;
            const Color hub=GetImageColor(image,x,y), tick=GetImageColor(image,x+(clock?76:54),y+(clock?0:54));
            UnloadImage(image);
            return hub.r==24 && hub.g==24 && hub.b==24 && tick.r==RAYWHITE.r && tick.g==RAYWHITE.g && tick.b==RAYWHITE.b;
        };
        idle(5); steps.push_back([]{capture("play-modes.png");}); key(KEY_ENTER); idle(5); steps.push_back([&]{
            require(dial_visible(450,true) && !dial_visible(450,false) && !dial_visible(662,true),"On-foot HUD must show only the clock beside the map");
            const auto image=LoadImageFromScreen(); const Color corner=GetImageColor(image,GetScreenWidth()-20,GetScreenHeight()-30);
            UnloadImage(image);
            require(corner.r!=ui::face.r || corner.g!=ui::face.g || corner.b!=ui::face.b,"Bottom-right location panel remains visible");
            capture("on-foot-clock-workflow.png");
        });
        key(KEY_E); idle(5);
        steps.push_back([&]{
            require(dial_visible(450,false) && dial_visible(662,true) && !dial_visible(450,true),"Car HUD must show the clock to the speedometer's right");
            capture("car-speedometer-workflow.png");
        });
        key(KEY_E); idle(5);
        steps.push_back([&]{require(dial_visible(450,true) && !dial_visible(450,false) && !dial_visible(662,true),"Exiting the car did not return the clock beside the map");});
        idle(3); key(KEY_ESCAPE);
        steps.push_back([&]{require(gray_desktop(),"Escape did not show the paused map"); paused_steps=physics_steps;}); idle(20);
        steps.push_back([&]{require(physics_steps==paused_steps,"Physics advanced while paused");});
        key(KEY_F10); key(KEY_ENTER); idle(5);
        steps.push_back([&]{require(!gray_desktop() && physics_steps>paused_steps,"File > Resume did not resume gameplay");});
        key(KEY_ESCAPE); steps.push_back([&]{require(gray_desktop(),"Second Escape did not pause"); paused_steps=physics_steps;}); idle(5);
        steps.push_back([&]{require(physics_steps==paused_steps,"Second pause advanced physics");});
        key(KEY_ESCAPE); idle(5);
        steps.push_back([&]{require(!gray_desktop() && physics_steps>paused_steps,"Escape did not resume gameplay");});
        // Edit the current map from inside a car, save, and return to the same play session.
        PlaySession before_edit, saved_progress;
        key(KEY_E); shortcut(KEY_S);
        steps.push_back([&]{require(PlaySession::load(root/"cities"/(city.id+".play"),before_edit,error)
            && before_edit.mode==EntryVehicle::Car,"Play Ctrl+S did not save the current car session");});
        key(KEY_F10); key(KEY_RIGHT); key(KEY_ENTER); idle(5);
        steps.push_back([]{require(title=="Ambaretto - City editor","Edit current map did not open the editor from Play");});
        key(KEY_V); key(KEY_FOUR); city_click(*city.spawn,City::center({67,67})); shortcut(KEY_S);
        steps.push_back([&]{City edited; require(City::load(root/"cities"/(city.id+".city"),edited,error)
            && edited.buildings.size()==1,"In-play map edit did not save the building"); capture("in-play-map-editor.png");});
        key(KEY_ESCAPE); idle(5); shortcut(KEY_S);
        steps.push_back([&]{capture("returned-from-map-editor.png");
            require(title=="Ambaretto - "+city.name,"Returning from the editor did not reopen Play");
            require(dial_visible(450,false),"Returning from the editor did not show the car HUD");
            require(PlaySession::load(root/"cities"/(city.id+".play"),saved_progress,error),"Returning from the editor could not save progress");
            require(saved_progress.mode==EntryVehicle::Car,"Returning from the editor lost the current car");
            require((saved_progress.position-before_edit.position).Length()<1,"Returning from the editor lost the player position");});
        key(KEY_F10); key(KEY_DOWN); key(KEY_DOWN); key(KEY_ENTER); idle(5); shortcut(KEY_S);
        steps.push_back([&]{require(dial_visible(450,true) && !dial_visible(450,false)
            && PlaySession::load(root/"cities"/(city.id+".play"),saved_progress,error)
            && saved_progress.mode==EntryVehicle::None && (saved_progress.position-*city.spawn).Length()<.2f
            && std::abs(saved_progress.minutes-city.start_minutes)<5,"Start from beginning did not reset spawn and clock");
            City edited; require(City::load(root/"cities"/(city.id+".city"),edited,error) && edited.buildings.size()==1,"Restart erased map edits");});
        key(KEY_E); shortcut(KEY_S);
        steps.push_back([&]{require(PlaySession::load(root/"cities"/(city.id+".play"),saved_progress,error)
            && saved_progress.mode==EntryVehicle::Car,"Continue fixture did not save the car");});
        key(KEY_F10); key(KEY_UP); key(KEY_ENTER); idle(3);
        steps.push_back([]{require(title=="Ambaretto - Main menu" && gray_desktop(),"End Game did not return to the main menu"); should_close=true;});
        std::string executable="menu_workflow_tests",flag="--city",path=(root/"cities"/(city.id+".city")).string();
        char* args[]={executable.data(),flag.data(),path.data()};
        require(ambaretto_main(3,args)==0,"Game workflow failed");
        stage="continue saved game"; reset(); idle(5);
        steps.push_back([]{require(title=="Ambaretto - Continue or start again","Saved game did not offer Continue / Start from beginning"); capture("continue-or-start-again.png");});
        key(KEY_ENTER); idle(5); shortcut(KEY_S);
        steps.push_back([&]{PlaySession continued; require(dial_visible(450,false)
            && PlaySession::load(root/"cities"/(city.id+".play"),continued,error)
            && continued.mode==EntryVehicle::Car && (continued.position-saved_progress.position).Length()<1,"Continue did not restore saved gameplay");});
        key(KEY_F10); key(KEY_UP); key(KEY_ENTER); idle(3); steps.push_back([]{should_close=true;});
        require(ambaretto_main(3,args)==0,"Continue workflow failed");
        stage="start again from saved game"; reset(); idle(5); key(KEY_RIGHT); key(KEY_ENTER); idle(5); key(KEY_ENTER); idle(5); shortcut(KEY_S);
        steps.push_back([&]{PlaySession fresh; require(dial_visible(450,true) && !dial_visible(450,false)
            && PlaySession::load(root/"cities"/(city.id+".play"),fresh,error) && fresh.mode==EntryVehicle::None
            && (fresh.position-*city.spawn).Length()<.2f && std::abs(fresh.minutes-city.start_minutes)<5,"Start again picker did not reset the saved session"); should_close=true;});
        require(ambaretto_main(3,args)==0,"Start again workflow failed");
        auto driving_city=city; driving_city.id="0000000000000003"; driving_city.name="Car only city"; driving_city.player_character.reset();
        require(driving_city.save(root/"cities",error),"Car only city fixture failed");
        path=(root/"cities"/(driving_city.id+".city")).string(); args[2]=path.data();
        stage="Car only selection / locked exit / restart / edit"; reset(); idle(5);
        steps.push_back([]{require(title=="Ambaretto - Choose play mode","Spawn-only city did not offer play modes"); capture("play-mode-selector.png");});
        key(KEY_UP); key(KEY_ENTER);
        steps.push_back([]{require(title=="Ambaretto - Choose play mode","Free roam accepted a city without a Player character");});
        key(KEY_DOWN); key(KEY_ENTER); idle(3);
        steps.push_back([]{require(title=="Ambaretto - Choose your car","Car only did not open the car picker"); capture("play-car-selector.png");});
        key(KEY_ESCAPE); idle(3);
        steps.push_back([]{require(title=="Ambaretto - Choose play mode","Cancelling the car picker did not return to mode selection");});
        key(KEY_ENTER); key(KEY_ENTER); idle(5); shortcut(KEY_S);
        const auto car_only_path=root/"cities"/(driving_city.id+".play");
        PlaySession chosen_car;
        steps.push_back([&]{require(dial_visible(450,false) && PlaySession::load(car_only_path,chosen_car,error)
            && chosen_car.play_mode==PlayMode::CarOnly && chosen_car.mode==EntryVehicle::Car && chosen_car.car_design,
            "Car only did not start and save in the selected car"); capture("car-only-gameplay.png");});
        key(KEY_E); idle(3); shortcut(KEY_S);
        steps.push_back([&]{PlaySession locked; require(PlaySession::load(car_only_path,locked,error)
            && locked.mode==EntryVehicle::Car && locked.play_mode==PlayMode::CarOnly && dial_visible(450,false),"Exit key escaped Car only");});
        Car* runtime_car=nullptr;
        steps.push_back([&]{GroundHit hit; require(active_world && active_world->cast_ray(chosen_car.position+Vec3(0,8,0),-Vec3::sAxisY(),12,hit)
            && hit.car,"Cannot find the selected runtime car"); runtime_car=hit.car; runtime_car->take_damage(100);});
        idle(5); steps.push_back([&]{require(runtime_car->destroyed(),"Destroyed car skipped the countdown"); capture("car-only-respawn-countdown.png");});
        key(KEY_E); shortcut(KEY_S);
        steps.push_back([&]{PlaySession destroyed; require(PlaySession::load(car_only_path,destroyed,error)
            && destroyed.play_mode==PlayMode::CarOnly && destroyed.mode==EntryVehicle::Car && destroyed.vehicle_health==100,
            "Saving during the countdown lost the chosen car");});
        idle(440);
        steps.push_back([&]{require(!runtime_car->destroyed() && runtime_car->health()==100 && dial_visible(450,false),"Countdown failed to return the player to the same car"); capture("car-only-respawned.png");});
        key(KEY_F10); key(KEY_RIGHT); key(KEY_ENTER); idle(5); key(KEY_ESCAPE); idle(5); shortcut(KEY_S);
        steps.push_back([&]{PlaySession returned; require(title=="Ambaretto - "+driving_city.name
            && PlaySession::load(car_only_path,returned,error) && returned.play_mode==PlayMode::CarOnly
            && returned.car_design->name==chosen_car.car_design->name,"Map editing lost Car only or required a character");});
        key(KEY_F10); key(KEY_DOWN); key(KEY_DOWN); key(KEY_ENTER); idle(5); shortcut(KEY_S);
        steps.push_back([&]{PlaySession restarted; require(PlaySession::load(car_only_path,restarted,error)
            && restarted.play_mode==PlayMode::CarOnly && restarted.mode==EntryVehicle::Car && restarted.car_design->name==chosen_car.car_design->name,
            "Restart lost the selected car or Car only mode");});
        key(KEY_F10); key(KEY_UP); key(KEY_ENTER); idle(3); steps.push_back([]{should_close=true;});
        require(ambaretto_main(3,args)==0,"Car only game workflow failed");
        stage="continue Car only"; reset(); idle(5); key(KEY_ENTER); idle(5); key(KEY_E); shortcut(KEY_S);
        steps.push_back([&]{PlaySession continued; require(PlaySession::load(car_only_path,continued,error)
            && continued.play_mode==PlayMode::CarOnly && continued.mode==EntryVehicle::Car && continued.car_design->name==chosen_car.car_design->name,
            "Continue lost Car only mode or allowed exiting"); should_close=true;});
        require(ambaretto_main(3,args)==0,"Car only continuation workflow failed");
        stage="race mode and completion choices"; reset();
        auto race_design=car; race_design.name="AAA race car"; race_design.type=CarType::Civilian;
        require(race_design.save(root/"cars",error),"Civilian race car fixture failed");
        path=(root/"cities"/(race_city.id+".city")).string(); args[2]=path.data();
        idle(5); key(KEY_DOWN); key(KEY_DOWN); key(KEY_ENTER); idle(3);
        steps.push_back([]{require(title=="Ambaretto - Choose race","Race mode did not open the course list"); capture("race-picker.png");});
        key(KEY_ENTER); key(KEY_ENTER); idle(5);
        steps.push_back([&]{require(title=="Ambaretto - "+race_city.name,"Race mode did not start the game"); capture("race-gameplay.png");});
        Car* racing_player=nullptr;
        steps.push_back([&]{GroundHit hit; require(active_world && active_world->cast_ray(race_city.races[0].checkpoints[0]+Vec3(0,8,0),-Vec3::sAxisY(),12,hit) && hit.car,"Cannot find race player"); racing_player=hit.car;});
        idle(420); steps.push_back([&]{racing_player->take_damage(100);}); idle(5);
        steps.push_back([]{require(title=="Ambaretto - Race finished","Race mode did not show completion choices"); capture("race-finish-choices.png");});
        key(KEY_DOWN); key(KEY_ENTER); idle(5); shortcut(KEY_S);
        steps.push_back([&]{PlaySession resumed; require(PlaySession::load(root/"cities"/(race_city.id+".play"),resumed,error) && resumed.play_mode==PlayMode::CarOnly,"Race result did not continue in Car only");});
        key(KEY_ESCAPE); key(KEY_F10); for (int i = 0; i < 3; ++i) key(KEY_DOWN); key(KEY_ENTER); idle(3); key(KEY_ENTER); idle(420);
        steps.push_back([&]{racing_player->take_damage(100);}); idle(5); shortcut(KEY_S);
        steps.push_back([&]{PlaySession resumed; require(title=="Ambaretto - "+race_city.name && PlaySession::load(root/"cities"/(race_city.id+".play"),resumed,error) && resumed.play_mode==PlayMode::CarOnly,"In-game race did not return to the original Car only mode"); should_close=true;});
        require(ambaretto_main(3,args)==0,"Race gameplay workflow failed");
        stage="Free roam race returns to Free roam"; reset();
        City roaming_race=race_city; roaming_race.id=City::create("fresh").id;
        require(roaming_race.set_spawn(race_map.checkpoints[0]+Vec3(0,0,City::block),error),error.c_str());
        CityVehicle race_car(CityVehicleKind::Car,*roaming_race.spawn+Vec3(2.8f,0,0)); race_car.car=race_design;
        require(roaming_race.add_vehicle(race_car,error) && roaming_race.save(root/"cities",error),error.c_str());
        path=(root/"cities"/(roaming_race.id+".city")).string(); args[2]=path.data();
        idle(5); key(KEY_ENTER); idle(5); key(KEY_E); idle(5);
        key(KEY_ESCAPE); key(KEY_F10); for (int i = 0; i < 3; ++i) key(KEY_DOWN); key(KEY_ENTER); idle(3); key(KEY_ENTER); idle(420);
        steps.push_back([&]{GroundHit hit; require(active_world && active_world->cast_ray(race_map.checkpoints[0]+Vec3(0,8,0),-Vec3::sAxisY(),12,hit) && hit.car,"Free roam car did not enter race"); hit.car->take_damage(100);});
        idle(5); shortcut(KEY_S);
        steps.push_back([&]{PlaySession resumed; require(title=="Ambaretto - "+roaming_race.name && PlaySession::load(root/"cities"/(roaming_race.id+".play"),resumed,error)
            && resumed.play_mode==PlayMode::FreeRoam && resumed.mode==EntryVehicle::Car,"In-game race did not return to Free roam in the player's car"); should_close=true;});
        require(ambaretto_main(3,args)==0,"Free roam race workflow failed");
        std::cout<<"Menus: creation, Save/Discard/Cancel, pause/resume, map editing, saved play modes and races passed\n";
    } catch (const WorkflowFailure& e) {
        std::cerr<<e.message<<'\n'; if (IsWindowReady()) {capture("menu-workflow-failure.png");CloseWindow();} return 1;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n'; if (IsWindowReady()) CloseWindow(); return 1;
    }
}
