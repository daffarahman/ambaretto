// Exercise the real frame loops with raylib's automation events, including the game loop.
#define main ambaretto_main
#include "../src/main.cpp"
#undef main
#include "desktop.hpp"
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
std::vector<std::function<void()>> steps;
std::size_t frame=0;
unsigned physics_steps=0;
std::string title;
const char* stage="start";
bool should_close=false;
void require(bool ok,const char* message) {if (!ok) throw std::runtime_error(message);}
void event(unsigned type,int a=0,int b=0) {PlayAutomationEvent({0,type,{a,b,0,0}});}
void idle(int count=2) {while (count-->0) steps.push_back([]{});}
void click(int x,int y) {steps.push_back([=]{event(7,x,y); event(6,MOUSE_BUTTON_LEFT);}); steps.push_back([]{event(5,MOUSE_BUTTON_LEFT);}); idle();}
void key(int code) {steps.push_back([=]{event(2,code);}); steps.push_back([=]{event(1,code);}); idle();}
void reset() {steps.clear(); frame=0; title.clear(); should_close=false;}
void window() {reset(); InitWindow(1024,600,"Desktop workflow check"); SetExitKey(KEY_NULL); SetTargetFPS(120);}
bool blue_background() {const auto image=LoadImageFromScreen(); const Color p=GetImageColor(image,20,140); UnloadImage(image); return p.r==0 && p.g==0 && p.b==170;}
}
extern "C" void __real_InitWindow(int,int,const char*);
extern "C" void __wrap_InitWindow(int w,int h,const char* name) {SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_WINDOW_RESIZABLE); __real_InitWindow(w,h,name);}
extern "C" bool __wrap_IsWindowFocused() {return true;}
extern "C" bool __real_WindowShouldClose();
extern "C" bool __wrap_WindowShouldClose() {return should_close || __real_WindowShouldClose();}
extern "C" void __real_SetWindowTitle(const char*);
extern "C" void __wrap_SetWindowTitle(const char* name) {title=name; __real_SetWindowTitle(name);}
extern "C" void __real__ZN9ambaretto12PhysicsWorld4stepEf(ambaretto::PhysicsWorld*,float);
extern "C" void __wrap__ZN9ambaretto12PhysicsWorld4stepEf(ambaretto::PhysicsWorld* world,float dt) {++physics_steps; __real__ZN9ambaretto12PhysicsWorld4stepEf(world,dt);}
extern "C" void __real_EndDrawing();
extern "C" void __wrap_EndDrawing() {
    __real_EndDrawing();
    if (frame<steps.size()) steps[frame++]();
    else if (++frame==steps.size()+3) {auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/"desktop-workflow-last.png").string().c_str()); UnloadImage(image);}
    else if (frame>steps.size()+1200) throw std::runtime_error(std::string("UI workflow timed out: ")+stage);
}
int main() {
    using namespace ambaretto;
    SetTraceLogLevel(LOG_WARNING);
    const auto root=std::filesystem::path(GetApplicationDirectory())/"desktop-workflow-test";
    const auto session=root/"session";
    std::string error;
    try {
        std::filesystem::create_directories(root);
        CharacterDesign player; player.name="Desktop test player";
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
            steps.push_back([&]{CharacterDesign saved; require(CharacterDesign::load(root/"characters/character-Desktop test player.character",saved,error) && saved.type==CharacterType::Player,"Cancel changed the saved character");});
            click(450,553); click(280,394);
            design_app(DesktopApp::Characters,root);
            CharacterDesign saved; require(CharacterDesign::load(root/"characters/character-Desktop test player.character",saved,error) && saved.type==CharacterType::NPC,"Unsaved prompt Save did not persist the draft");
        } CloseWindow();
        // New car stays blank even when a library exists; a failed Save cannot close its draft.
        stage="new car / invalid save"; window(); {
            ui::FontResource font;
            click(760,220); click(215,159); click(450,545); click(280,394); idle(3); click(740,394);
            click(450,545); click(500,394);
            design_app(DesktopApp::Cars,root);
            require(!std::filesystem::exists(root/"cars/car-New car.car"),"Invalid new car was saved");
        } CloseWindow();
        // Parent close requests use the same prompt and survive Cancel safely.
        stage="parent close / cancel"; window(); {
            ui::FontResource font; set_app_session(session);
            click(215,159); steps.push_back([&]{std::ofstream(session.string()+".close")<<"close";}); idle(); click(740,394);
            steps.push_back([&]{require(!std::filesystem::exists(session.string()+".close"),"Cancel kept the parent close request pending");});
            click(450,553); click(500,394);
            character_builder(player,root/"characters"); set_app_session({});
        } CloseWindow();
        // Native app processes can coexist and all close cooperatively before a game.
        stage="native app shutdown"; window(); {
            ui::FontResource font;
            require(launch_app(DesktopApp::Cars,error) && launch_app(DesktopApp::Buildings,error) && launch_app(DesktopApp::Characters,error) && launch_app(DesktopApp::Settings,error),"Separate app launch failed");
            require(close_apps(),"App windows failed to close");
            require(close_apps(),"Closed windows remained tracked");
        } CloseWindow();
        // Pause freezes actual Jolt steps; resume continues; End Game returns to desktop.
        City city=City::create("Desktop pause test");
        require(city.add_land({60,60},{68,68},error) && city.set_spawn({64,64},error),"Game fixture failed");
        city.player_character=player; require(city.save(root/"cities",error),"Game fixture save failed");
        stage="city handoff / validation"; window(); {
            ui::FontResource font;
            BuildingMesh building; building.name="Desktop IPC "+city.id;
            require(building.save(root/"buildings",error),"Draft fixture save failed");
            require(launch_app(DesktopApp::Buildings,error,root/"buildings"/("building-"+building.name+".building")),"Draft app launch failed");
            std::filesystem::path request;
            for (const auto& file : std::filesystem::directory_iterator(std::filesystem::path(GetApplicationDirectory())/".desktop")) {
                if (file.path().extension()!=".building") continue;
                BuildingMesh copied;
                if (BuildingMesh::load(file.path(),copied,error) && copied.name==building.name) {request=file.path(); request.replace_extension(".play"); break;}
            }
            require(!request.empty(),"Independent draft copy missing");
            std::ofstream(request)<<"../../outside";
            City handoff;
            steps.push_back([&]{require(handoff.id.empty() && !std::filesystem::exists(request),"Invalid handoff escaped city ID validation"); std::ofstream(request)<<city.id;});
            require(desktop_menu(handoff,root/"cities") && handoff.id==city.id,"Saved city handoff failed");
            require(!std::filesystem::exists(request),"Play request was not consumed");
        } CloseWindow();
        stage="game pause / resume / desktop"; reset(); unsigned paused_steps=0;
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
        steps.push_back([]{require(title=="Ambaretto - Desktop" && blue_background(),"End Game did not return to the desktop"); should_close=true;});
        std::string executable="desktop_workflow_tests",flag="--city",path=(root/"cities"/(city.id+".city")).string();
        char* args[]={executable.data(),flag.data(),path.data()};
        require(ambaretto_main(3,args)==0,"Game workflow failed");
        std::cout<<"Desktop: create/open, draft Save/Discard/Cancel, native windows, cooperative shutdown, validated city handoff, Escape pause/resume, End Game passed\n";
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n'; if (IsWindowReady()) CloseWindow(); return 1;
    }
}
