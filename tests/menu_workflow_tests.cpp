// Exercise the real frame loops with raylib's automation events, including the game loop.
#define main ambaretto_main
#include "../src/main.cpp"
#undef main
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
std::vector<std::function<void()>> steps;
std::size_t frame=0;
unsigned physics_steps=0,window_count=0;
std::string title;
const char* stage="start";
bool should_close=false;
void require(bool ok,const char* message) {if (!ok) throw std::runtime_error(message);}
void event(unsigned type,int a=0,int b=0) {PlayAutomationEvent({0,type,{a,b,0,0}});}
void idle(int count=2) {while (count-->0) steps.push_back([]{});}
void click(int x,int y) {steps.push_back([=]{event(7,x,y); event(6,MOUSE_BUTTON_LEFT);}); steps.push_back([]{event(5,MOUSE_BUTTON_LEFT);}); idle();}
void key(int code) {steps.push_back([=]{event(2,code);}); steps.push_back([=]{event(1,code);}); idle();}
void reset() {steps.clear(); frame=0; title.clear(); should_close=false;}
void window() {reset(); InitWindow(1024,600,"Menu workflow check"); SetExitKey(KEY_NULL); SetTargetFPS(120);}
bool blue_background() {const auto image=LoadImageFromScreen(); const Color p=GetImageColor(image,20,140); UnloadImage(image); return p.r==0 && p.g==0 && p.b==170;}
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
extern "C" void __wrap_EndDrawing() {
    __real_EndDrawing();
    if (frame<steps.size()) steps[frame++]();
    else if (++frame==steps.size()+3) {auto image=LoadImageFromScreen(); ExportImage(image,(std::filesystem::path(GetApplicationDirectory())/"menu-workflow-last.png").string().c_str()); UnloadImage(image);}
    else if (frame>steps.size()+1200) throw std::runtime_error(std::string("UI workflow timed out: ")+stage);
}
int main() {
    using namespace ambaretto;
    SetTraceLogLevel(LOG_WARNING);
    const auto root=std::filesystem::path(GetApplicationDirectory())/"menu-workflow-test";
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
            steps.push_back([]{require(title=="Ambaretto - Cars","Menu click did not open Cars");});
            key(KEY_ESCAPE);
            steps.push_back([]{require(title=="Ambaretto - Main menu","Cars did not return to the main menu");});
            key(KEY_DOWN); key(KEY_DOWN); key(KEY_ENTER);
            steps.push_back([]{require(title=="Ambaretto - Settings","Menu list did not open Settings");});
            key(KEY_ESCAPE);
            steps.push_back([]{require(title=="Ambaretto - Main menu","Settings did not return to the main menu");});
            key(KEY_DOWN); key(KEY_ENTER);
            require(!main_menu(selected,root/"cities",controls,graphics),"Quit did not leave the main menu");
            require(window_count==initial_windows,"Menu navigation opened another window");
        } CloseWindow();
        // Pause freezes actual Jolt steps; resume continues; End Game returns to the menu.
        City city=City::create("Menu pause test");
        require(city.add_land({60,60},{68,68},error) && city.set_spawn({64,64},error),"Game fixture failed");
        city.player_character=player; require(city.save(root/"cities",error),"Game fixture save failed");
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
