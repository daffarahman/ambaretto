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
std::string title;
std::string typed_text;
const char* stage="start";
bool should_close=false;
void require(bool ok,const char* message) {if (!ok) throw std::runtime_error(message);}
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
std::string terrain_pixels() {
    const auto image=LoadImageFromScreen(); std::string pixels;
    for (int z=0;z<ambaretto::City::width;++z) for (int x=0;x<ambaretto::City::width;++x) {
        const auto c=GetImageColor(image,int(672+(x+.5f)*296/ambaretto::City::width),int(120+(z+.5f)*296/ambaretto::City::width));
        pixels+=c.r==84 && c.g==129 && c.b==74 ? '1' : c.r==28 && c.g==66 && c.b==108 ? '0' : '?';
    }
    UnloadImage(image); return pixels;
}
void require_terrain_preview(const ambaretto::City& city) {
    const auto pixels=terrain_pixels(); int matched=0;
    for (std::size_t i=0;i<pixels.size();++i) if (pixels[i]!='?') {
        require(pixels[i]=='0'+int(city.tiles[i]),"Terrain image differs from the generated map"); ++matched;
    }
    require(matched>15000,"Terrain preview is missing or obscured");
}
void duplicate_key() {steps.push_back([]{event(2,KEY_LEFT_CONTROL); event(2,KEY_D);}); steps.push_back([]{event(1,KEY_D); event(1,KEY_LEFT_CONTROL);}); idle();}
void reset() {steps.clear(); frame=0; title.clear(); should_close=false;}
void window() {reset(); InitWindow(1024,600,"Menu workflow check"); SetExitKey(KEY_NULL); SetTargetFPS(120);}
bool blue_background() {const auto image=LoadImageFromScreen(); const Color p=GetImageColor(image,20,140); UnloadImage(image); return p.r==0 && p.g==0 && p.b==170;}
bool saved_popup() {const auto image=LoadImageFromScreen(); const Color p=GetImageColor(image,152,300); UnloadImage(image); return p.r==255 && p.g==255 && p.b==255;}
void open_saved() {key(KEY_F10); key(KEY_DOWN); key(KEY_DOWN); key(KEY_ENTER);}
void open_city_settings() {key(KEY_F10); for (int i=0;i<6;++i) key(KEY_RIGHT); key(KEY_ENTER);}
void preview(const char* name) {steps.push_back([=]{require(saved_popup(),"Saved selector did not open as a popup"); auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/name).string().c_str()); UnloadImage(image);});}
std::string contents(const std::filesystem::path& path) {std::ifstream file(path); return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
}
extern "C" void __real_InitWindow(int,int,const char*);
extern "C" void __wrap_InitWindow(int w,int h,const char* name) {++window_count; SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_WINDOW_RESIZABLE); __real_InitWindow(w,h,name);}
extern "C" bool __wrap_IsWindowFocused() {return true;}
extern "C" bool __real_WindowShouldClose();
extern "C" bool __wrap_WindowShouldClose() {return should_close || __real_WindowShouldClose();}
extern "C" void __real_SetWindowTitle(const char*);
extern "C" void __wrap_SetWindowTitle(const char* name) {title=name; __real_SetWindowTitle(name);}
extern "C" void __real__ZN9ambaretto12PhysicsWorld4stepEf(ambaretto::PhysicsWorld*,float);
extern "C" void __wrap__ZN9ambaretto12PhysicsWorld4stepEf(ambaretto::PhysicsWorld* world,float dt) {++physics_steps; __real__ZN9ambaretto12PhysicsWorld4stepEf(world,dt);}
extern "C" void __real_EndDrawing();
extern "C" int __real_GetCharPressed();
extern "C" int __wrap_GetCharPressed() {if (typed_text.empty()) return __real_GetCharPressed(); const int c=typed_text.front(); typed_text.erase(typed_text.begin()); return c;}
extern "C" void __wrap_EndDrawing() {
    __real_EndDrawing();
    if (frame<steps.size()) steps[frame++]();
    else if (++frame==steps.size()+3) {auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/"menu-workflow-last.png").string().c_str()); UnloadImage(image);}
    else if (frame>steps.size()+1200) throw std::runtime_error(std::string("UI workflow timed out: ")+stage);
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
            idle(); click(300,265);
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
            key(KEY_F10); for (int i=0;i<4;++i) key(KEY_RIGHT); key(KEY_DOWN); key(KEY_DOWN); key(KEY_ENTER);
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
        stage="dry beach ground painting"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics;
            City coast=City::create("Beach painting workflow"); const CityCell owner{64,64};
            const auto directory=root/"beach-cities";
            require(coast.add_land(owner,owner,error) && coast.set_spawn(owner,error) && coast.save(directory,error),"Beach painting fixture failed");
            const Vec3 focus=*coast.spawn;
            Vec3 beach=City::center(owner)+Vec3(City::half_block+2,0,0);
            const Environment fixture(coast); beach.SetY(fixture.terrain_height(beach.GetX(),beach.GetZ()));
            require(beach.GetY()>Environment::water_level && beach.GetY()<City::level
                && !coast.land(City::cell(beach.GetX(),beach.GetZ())),"Fixture must click dry beach below the flat island");
            idle(); key(KEY_V); key(KEY_NINE); key(KEY_ENTER); city_click(focus,beach);
            steps.push_back([&]{require(coast.ground_textures.size()==1 && coast.ground_textures.at(City::index(owner))=="soil.png"
                && coast.land_count()==1,"Ground tool did not paint the dry beach's owning land tile");});
            shortcut(KEY_Z); steps.push_back([&]{require(coast.ground_textures.empty(),"Beach painting did not undo");});
            shortcut(KEY_Y); steps.push_back([&]{require(coast.ground_textures.size()==1 && coast.ground_textures.at(City::index(owner))=="soil.png","Beach painting did not redo");});
            city_click(focus,beach+Vec3(40,0,0));
            steps.push_back([&]{require(coast.ground_textures.size()==1 && coast.land_count()==1,"Offshore Ground click changed the island"); capture("city-beach-painting.png");});
            shortcut(KEY_S);
            steps.push_back([&]{City loaded; require(City::load(directory/(coast.id+".city"),loaded,error)
                && loaded.ground_textures==coast.ground_textures,"Beach painting did not save");});
            key(KEY_ESCAPE); click(760,485);
            require(!city_menu(coast,directory,controls,graphics,true),"Beach painting editor started Play");
        } CloseWindow();
        stage="map building popup"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics;
            idle(); click(100,130); preview("map-building-popup.png"); key(KEY_ENTER);
            steps.push_back([&]{require(city.buildings.empty(),"Building selection leaked a placement into the map");});
            key(KEY_ESCAPE); click(760,485);
            require(!city_menu(city,root/"cities",controls,graphics,true),"Building selector started a game");
        } CloseWindow();
        // Play has no edit commands and rejects missing player data, then starts by clicking a city.
        const auto play_directory=root/"play/cities";
        auto playable=city; playable.id="0000000000000001"; playable.name="B playable city";
        auto unavailable=City::create("A unavailable city"); unavailable.id="0000000000000002";
        require(playable.save(play_directory,error) && unavailable.save(play_directory,error),"Play fixtures failed");
        const auto playable_path=play_directory/(playable.id+".city"),unavailable_path=play_directory/(unavailable.id+".city");
        const auto original_playable=contents(playable_path),original_unavailable=contents(unavailable_path);
        stage="play-only city selector"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics; City selected=unavailable;
            idle(); key(KEY_F10); key(KEY_ENTER); preview("play-city-selector.png");
            click(220,170); key(KEY_E); key(KEY_INSERT); key(KEY_DELETE); key(KEY_F2); duplicate_key(); key(KEY_ENTER);
            steps.push_back([]{require(title=="Ambaretto - Play" && saved_popup(),"Unavailable city started playing or opened an editor");});
            click(220,207);
            require(main_menu(selected,play_directory,controls,graphics) && selected.id==playable.id,"Clicking a playable city did not start Play");
            require(contents(playable_path)==original_playable && contents(unavailable_path)==original_unavailable,"Play changed a saved city");
        } CloseWindow();
        stage="city create / edit chooser"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics; City selected=unavailable;
            idle(); key(KEY_F2); key(KEY_DELETE);
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
        stage="terrain preview / regenerate / create"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics; City selected=unavailable;
            const auto directory=root/"preview-cities"; City expected=City::create("Expected preview");
            require(expected.generate_terrain(48,32,1234,error),"Preview fixture failed");
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
            steps.push_back([&]{require(title=="Ambaretto - City editor" && selected.tiles==expected.tiles,"Create did not use the displayed terrain");});
            key(KEY_ESCAPE); click(760,485);
            require(!city_menu(selected,directory,controls,graphics),"Terrain preview started Play");
            City loaded;
            require(City::load(directory/(selected.id+".city"),loaded,error) && loaded.tiles==expected.tiles && loaded.elevation==expected.elevation,
                "Saved terrain differs from the preview");
        } CloseWindow();
        // Each creation menu duplicates to disk, selects the copy, and leaves the source alone.
        for (auto kind:{DesignKind::Building,DesignKind::Car,DesignKind::Character}) {
            stage="design menu duplicate"; window(); {
                ui::FontResource font;
                const auto original_path=kind==DesignKind::Building ? saved_path(building,root/"buildings")
                    : kind==DesignKind::Car ? saved_path(car,root/"cars") : saved_path(player,root/"characters");
                const auto original=contents(original_path);
                const auto copy_path=kind==DesignKind::Building ? root/"buildings/building-Menu test building copy.building"
                    : kind==DesignKind::Car ? root/"cars/car-Menu test car copy.car" : root/"characters/character-Menu test player copy.character";
                idle(); click(760,345);
                steps.push_back([=]{require(std::filesystem::exists(copy_path),"Duplicate menu button did not save a copy");});
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
            idle(); key(KEY_F10); for (int i=0;i<4;++i) key(KEY_RIGHT); key(KEY_DOWN); key(KEY_ENTER);
            duplicate_key();
            steps.push_back([&]{require(std::filesystem::exists(root/"cars/car-Menu test car copy 3.car"),"Car placement selector did not duplicate"); auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/"duplicate-car-selector.png").string().c_str()); UnloadImage(image);});
            click(760,440);
            key(KEY_F10); for (int i=0;i<4;++i) key(KEY_RIGHT); for (int i=0;i<14;++i) key(KEY_DOWN); key(KEY_ENTER);
            duplicate_key();
            steps.push_back([]{auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/"duplicate-player-selector.png").string().c_str()); UnloadImage(image);});
            click(210,475); key(KEY_ESCAPE); click(760,485);
            require(!city_menu(selected,root/"cities",controls,graphics,true),"Placement duplicate started a game");
            require(selected.player_character && selected.player_character->name==player.name+" copy 3" && selected.vehicles.empty(),"Placement selectors lost the duplicate or leaked a placement");
        } CloseWindow();
        stage="empty Play list"; window(); {
            ui::FontResource font; ControllerMapping controls; GraphicsSettings graphics; City selected;
            idle(); key(KEY_ENTER); key(KEY_ENTER); duplicate_key(); key(KEY_ESCAPE); key(KEY_ESCAPE);
            require(!main_menu(selected,root/"empty/cities",controls,graphics),"Empty Play list started a game");
            require(!std::filesystem::exists(root/"empty/cities"),"Play duplicated or edited an empty library");
        } CloseWindow();
        // Pause freezes actual Jolt steps; resume continues; End Game returns to the menu.
        stage="game pause / resume / main menu"; reset(); unsigned paused_steps=0;
        idle(3); key(KEY_ESCAPE);
        steps.push_back([&]{require(blue_background(),"Escape did not show the paused map"); paused_steps=physics_steps;}); idle(20);
        steps.push_back([&]{require(physics_steps==paused_steps,"Physics advanced while paused");});
        key(KEY_F10); key(KEY_ENTER); idle(5);
        steps.push_back([&]{require(!blue_background() && physics_steps>paused_steps,"File > Resume did not resume gameplay");});
        key(KEY_ESCAPE); steps.push_back([&]{require(blue_background(),"Second Escape did not pause"); paused_steps=physics_steps;}); idle(5);
        steps.push_back([&]{require(physics_steps==paused_steps,"Second pause advanced physics");});
        key(KEY_ESCAPE); idle(5);
        steps.push_back([&]{require(!blue_background() && physics_steps>paused_steps,"Escape did not resume gameplay");});
        key(KEY_F10); key(KEY_DOWN); key(KEY_ENTER); idle(3);
        steps.push_back([]{require(title=="Ambaretto - Main menu" && blue_background(),"End Game did not return to the main menu"); should_close=true;});
        std::string executable="menu_workflow_tests",flag="--city",path=(root/"cities"/(city.id+".city")).string();
        char* args[]={executable.data(),flag.data(),path.data()};
        require(ambaretto_main(3,args)==0,"Game workflow failed");
        std::cout<<"Menus: create/open, draft Save/Discard/Cancel, single-window navigation, Escape pause/resume, End Game passed\n";
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n'; if (IsWindowReady()) CloseWindow(); return 1;
    }
}
