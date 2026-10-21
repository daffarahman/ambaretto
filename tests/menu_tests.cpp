#include "menu_bar.hpp"
#include "ui_font.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool same(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
}
int main(int argc, char** argv) {
    using namespace ambaretto;
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(1024, 600, "Desktop menu check");
    try {
        ui::FontResource font;
        MenuBar editor(MenuMode::Editor);
        MenuState state; state.play = false;
        MenuInput input; input.toggle = true;
        require(editor.update(state, input) == MenuCommand::None && editor.blocking(), "F10 did not open the menu");
        input = {}; input.enter = true;
        require(editor.update(state, input) == MenuCommand::SaveCity && !editor.blocking(), "File > Save did not dispatch");
        editor.open(); input = {}; input.vertical = 2; editor.update(state, input);
        input = {}; input.enter = true;
        require(editor.update(state, input) == MenuCommand::None && editor.blocking(), "Play was enabled without a spawn");
        state.play = true;
        require(editor.update(state, input) == MenuCommand::PlayCity, "Play did not enable after a spawn");
        editor.open(1);
        require(editor.update(state, input) == MenuCommand::None && editor.blocking(), "Empty undo was enabled");
        state.undo = true;
        require(editor.update(state, input) == MenuCommand::Undo, "Undo did not enable after an edit");
        editor.open(1); input = {}; input.vertical = 1; editor.update(state, input);
        input = {}; input.enter = true;
        require(editor.update(state, input) == MenuCommand::None, "Empty redo was enabled");
        state.redo = true;
        require(editor.update(state, input) == MenuCommand::Redo, "Redo did not dispatch");
        editor.open(1); input = {}; input.vertical = 2; editor.update(state, input);
        input = {}; input.enter = true;
        require(editor.update(state, input) == MenuCommand::None, "Delete was enabled without a selection");
        state.selection = true;
        require(editor.update(state, input) == MenuCommand::DeleteSelection, "Delete selection did not dispatch");
        const auto dispatch = [&](int menu,int item,MenuCommand expected) {
            editor.open(menu); MenuInput move; move.vertical = item; editor.update(state,move);
            MenuInput accept; accept.enter = true;
            require(editor.update(state,accept)==expected,"Editor category dispatched the wrong action");
        };
        dispatch(1,3,MenuCommand::SelectTool); dispatch(1,4,MenuCommand::BulldozeTool);
        const MenuCommand tile_actions[] = {MenuCommand::LandTool,MenuCommand::RoadTool,MenuCommand::GroundTool,MenuCommand::WaterTool};
        for (int i = 0; i<int(std::size(tile_actions)); ++i) dispatch(2,i,tile_actions[i]);
        dispatch(3,0,MenuCommand::BuildingTool); dispatch(3,1,MenuCommand::BuildingCreator);
        dispatch(3,4,MenuCommand::SavedBuildings);
        dispatch(3,5,MenuCommand::PlainBuilding);
        dispatch(5,6,MenuCommand::PanView);
        const auto dispatch_object = [&](int group,int item,MenuCommand expected) {
            editor.open(4); MenuInput move; move.vertical=group; editor.update(state,move);
            MenuInput accept; accept.enter=true;
            require(editor.update(state,accept)==MenuCommand::None && editor.blocking(),"Object group dispatched instead of opening its submenu");
            move={}; move.vertical=item; editor.update(state,move);
            require(editor.update(state,accept)==expected,"Object submenu dispatched the wrong action");
        };
        dispatch_object(2,1,MenuCommand::CarEditor);
        dispatch_object(1,1,MenuCommand::CharacterCreator); dispatch_object(1,2,MenuCommand::ChoosePlayerCharacter);
        dispatch(0,5,MenuCommand::RefreshDesigns);
        dispatch(6,0,MenuCommand::CitySettings);
        dispatch(6,1,MenuCommand::RoadEditor);
        dispatch(7,0,MenuCommand::NewRace); dispatch(7,1,MenuCommand::Races);
        MenuBar character_creator(MenuMode::CharacterCreator); character_creator.open(); input={}; input.vertical=1; character_creator.update({},input); input={}; input.enter=true;
        require(character_creator.update({},input)==MenuCommand::SaveCharacter,"Character creator Save menu is missing");
        state.car_selection=true; dispatch_object(2,2,MenuCommand::EditCar); state.car_selection=false;
        dispatch_object(2,2,MenuCommand::None);
        require(editor.blocking(),"Disabled car edit closed the submenu");
        state.choose_character=false; dispatch_object(1,2,MenuCommand::None); state.choose_character=true;
        MenuBar car_creator(MenuMode::CarCreator); car_creator.open(); input={}; input.vertical=1; car_creator.update({},input); input={}; input.enter=true;
        require(car_creator.update({},input)==MenuCommand::SaveCar,"Car creator Save menu is missing");
        editor.open(3); input = {}; input.vertical = 2; editor.update(state,input); input = {}; input.enter = true;
        require(editor.update(state,input)==MenuCommand::None && editor.blocking(),"Building edit enabled without a selected building");
        state.building_selection = true; require(editor.update(state,input)==MenuCommand::EditBuilding,"Selected building edit was not available");
        dispatch(3,3,MenuCommand::RotateBuilding);
        state.building_selection = false; state.tool = 3; dispatch(3,3,MenuCommand::RotateBuilding);
        state.tool = 0; dispatch(3,3,MenuCommand::None);
        const MenuCommand tree_actions[] = {MenuCommand::TreesSparse,MenuCommand::TreesMedium,MenuCommand::TreesDense,MenuCommand::BrushSmaller,MenuCommand::BrushLarger};
        for (int i = 0; i<int(std::size(tree_actions)); ++i) dispatch_object(0,i,tree_actions[i]);
        dispatch_object(1,0,MenuCommand::SpawnTool); dispatch_object(2,0,MenuCommand::PlaceCar);
        dispatch_object(2,3,MenuCommand::PlaceCar);
        const MenuCommand vehicle_actions[] = {MenuCommand::PlaceTrainer,MenuCommand::PlaceF18,MenuCommand::PlaceBoeing};
        for (int i = 0; i<int(std::size(vehicle_actions)); ++i) dispatch_object(3,i,vehicle_actions[i]);
        dispatch(4,4,MenuCommand::RotateObject);
        editor.open(4); input={}; input.horizontal=1; editor.update(state,input);
        input={}; input.vertical=-1; editor.update(state,input); input={}; input.enter=true;
        require(editor.update(state,input)==MenuCommand::BrushLarger,"Right arrow did not open Trees or Up did not wrap its children");
        editor.open(4); input={}; input.enter=true; editor.update(state,input);
        input={}; input.escape=true; editor.update(state,input);
        require(editor.blocking(),"Escape closed Objects instead of returning from its submenu");
        editor.update(state,input); require(!editor.blocking(),"Second Escape did not close Objects");
        editor.open(4); input={}; input.enter=true; editor.update(state,input);
        input={}; input.horizontal=-1; editor.update(state,input);
        input={}; input.vertical=4; editor.update(state,input); input={}; input.enter=true;
        require(editor.update(state,input)==MenuCommand::RotateObject,"Left arrow did not return to Objects");
        float objects_x=0;
        for (const auto title : {"File","Edit","Tiles","Buildings"}) objects_x+=ui::measure_text(title,19)+32;
        const float submenu_x=objects_x+716<=GetScreenWidth() ? objects_x+356 : objects_x-356;
        editor.open(4); input={}; input.mouse={objects_x+50,48}; input.moved=true; editor.update(state,input);
        input.mouse={submenu_x+50,84}; input.click=true;
        require(editor.update(state,input)==MenuCommand::TreesMedium && !editor.blocking(),"Hover and click did not dispatch a submenu item");
        editor.open(4); input={}; input.mouse={objects_x+50,112}; input.click=true; editor.update(state,input);
        input={}; input.mouse={submenu_x+50,112}; input.click=true;
        require(editor.update(state,input)==MenuCommand::PlaceCar,"Clicking a group did not open the Car submenu");
        editor.open(4); input={}; input.enter=true; editor.update(state,input);
        input={}; input.mouse={990,580}; input.click=true; editor.update(state,input);
        require(!editor.blocking() && editor.interacted(),"Outside click retained the submenu or leaked into the canvas");
        editor.open(4); input={}; input.enter=true; editor.update(state,input);
        input={}; input.focused=false; editor.update(state,input); editor.open(4); input={}; input.enter=true;
        require(editor.update(state,input)==MenuCommand::None && editor.blocking(),"Focus loss retained stale submenu selection");
        MenuBar creator(MenuMode::Creator); creator.open(); input = {}; input.enter = true;
        require(creator.update(state,input)==MenuCommand::NewBuilding,"Creator File > New is missing");
        creator.open(); input = {}; input.vertical = 1; creator.update(state,input); input = {}; input.enter = true;
        require(creator.update(state,input)==MenuCommand::SaveBuilding,"Creator File > Save is missing");
        const auto dispatch_menu=[&](MenuMode mode,int category,int item,MenuCommand expected) {
            MenuBar bar(mode); bar.open(category); MenuInput move; move.vertical=item; bar.update({},move);
            MenuInput accept; accept.enter=true;
            require(bar.update({},accept)==expected,"Menu category dispatched the wrong action");
        };
        dispatch_menu(MenuMode::Creator,0,2,MenuCommand::SavedBuildings);
        dispatch_menu(MenuMode::CarCreator,0,2,MenuCommand::SavedCars);
        dispatch_menu(MenuMode::CharacterCreator,0,2,MenuCommand::SavedCharacters);
        dispatch_menu(MenuMode::Main,0,0,MenuCommand::PlayCity);
        dispatch_menu(MenuMode::Main,0,1,MenuCommand::Editors);
        dispatch_menu(MenuMode::Main,0,2,MenuCommand::Quit);
        dispatch_menu(MenuMode::Main,1,0,MenuCommand::Graphics);
        dispatch_menu(MenuMode::Main,1,1,MenuCommand::Controllers);
        dispatch_menu(MenuMode::Main,2,0,MenuCommand::Controls);
        dispatch_menu(MenuMode::Main,2,1,MenuCommand::About);
        dispatch_menu(MenuMode::Game,0,1,MenuCommand::SaveCity);
        dispatch_menu(MenuMode::Game,0,2,MenuCommand::Restart);
        dispatch_menu(MenuMode::Game,0,3,MenuCommand::StartRace);
        MenuBar race_menu; race_menu.open(); input={}; input.vertical=3; state.race=false; race_menu.update(state,input); input={}; input.enter=true;
        require(race_menu.update(state,input)==MenuCommand::None && race_menu.blocking(),"Start race was enabled on foot");
        state.race=true; require(race_menu.update(state,input)==MenuCommand::StartRace,"Start race did not enable while driving");
        dispatch_menu(MenuMode::Game,1,0,MenuCommand::EditMap);
        dispatch_menu(MenuMode::PlayEditor,0,0,MenuCommand::SaveCity);
        dispatch_menu(MenuMode::PlayEditor,0,2,MenuCommand::PlayCity);
        dispatch_menu(MenuMode::PlayEditor,0,3,MenuCommand::Cities);
        dispatch_menu(MenuMode::PlayEditor,2,0,MenuCommand::LandTool);
        creator.open(1); require(creator.update(state,input)==MenuCommand::Undo,"Creator undo is missing");
        editor.open(); input = {}; input.horizontal = -1; editor.update(state, input);
        input = {}; input.enter = true;
        require(editor.update(state, input) == MenuCommand::Controls, "Left arrow did not wrap to Help");
        editor.open(); input = {}; input.escape = true;
        require(editor.update(state, input) == MenuCommand::None && editor.interacted() && !editor.blocking(), "Escape leaked out of the dropdown");
        editor.open(2); input = {}; input.mouse = {990, 500}; input.click = true;
        require(editor.update(state, input) == MenuCommand::None && editor.interacted() && !editor.blocking(), "Outside click leaked to the canvas");
        editor.open(); input = {}; input.focused = false;
        editor.update(state, input);
        require(!editor.blocking() && editor.interacted(), "Focus loss retained an open menu");
        input = {}; input.mouse = {50, 12}; input.click = true; input.mouse_enabled = false;
        editor.update(state, input);
        require(!editor.blocking(), "Captured game mouse opened a menu");
        input.mouse_enabled = true; editor.update(state, input);
        require(editor.blocking(), "Clicking File did not open its dropdown");
        input = {}; input.mouse = {100, 48}; input.click = input.moved = true;
        require(editor.update(state, input) == MenuCommand::SaveCity, "Clicking a menu item did not dispatch");

        MenuBar cities(MenuMode::Cities); cities.open(); state.selection = false; input = {}; input.vertical=1; cities.update(state,input); input = {}; input.enter = true;
        require(cities.update(state, input) == MenuCommand::None && cities.blocking(), "City commands were enabled without a city");
        state.selection = true;
        require(cities.update(state, input) == MenuCommand::EditCity, "Cities File > Edit did not dispatch");
        dispatch_menu(MenuMode::Cities,0,0,MenuCommand::NewCity);
        cities.open(); input={}; input.vertical=2; cities.update({},input); input={}; input.enter=true;
        require(cities.update({},input)==MenuCommand::None && cities.blocking(),"Duplicate city was enabled without a selection");
        require(cities.update(state,input)==MenuCommand::DuplicateCity,"Cities File > Duplicate did not dispatch");
        cities.open(); input={}; input.vertical=3; cities.update({},input); input={}; input.enter=true;
        require(cities.update({},input)==MenuCommand::None && cities.blocking(),"Delete city was enabled without a selection");
        require(cities.update(state,input)==MenuCommand::DeleteCity,"Cities File > Delete did not dispatch");
        dispatch_menu(MenuMode::Cities,0,4,MenuCommand::Quit);
        cities.open(); input={}; input.vertical=-1; cities.update({},input); input={}; input.enter=true;
        require(cities.update({},input)==MenuCommand::Quit,"Back was not the last Cities File action");
        MenuBar game; game.open(2);
        require(game.update({}, input) == MenuCommand::Map, "Existing gameplay menu changed");
        game.open(2); input = {}; input.vertical = -1; game.update({},input);
        input = {}; input.enter = true;
        require(game.update({},input) == MenuCommand::AimMode, "Global settings remained in the gameplay menu");
        game.open(); input = {}; input.vertical = -1; game.update({},input);
        input = {}; input.enter = true;
        require(game.update({},input) == MenuCommand::Cities, "End Game was not the last File item");
        game.open(); input={}; input.enter=true;
        require(game.update({},input)==MenuCommand::Resume,"File > Resume is missing");
        input = {}; input.escape = true;
        require(game.update({},input) == MenuCommand::Pause && game.interacted() && !game.blocking(), "Escape did not request pause / map");
        game.open();
        require(game.update({},input) == MenuCommand::None && !game.blocking(), "Escape in a menu changed gameplay instead of closing the menu");

        editor.open(2); state.tool = 1;
        for (int frame = 0; frame < 3; ++frame) {
            BeginDrawing(); ui::draw_desktop();
            ui::draw_window({300, 60, 700, 460}, "City document"); editor.draw(state);
            ui::draw_window_close({300, 60, 700, 460});
            ui::draw_button({20,400,120,40});
            ui::draw_button({160,400,120,40},true,true);
            ui::draw_panel({20,460,260,40},true,ui::highlight);
            ui::draw_slider({20,540,120,10},0);
            ui::draw_slider({160,540,120,10},1);
            EndDrawing();
        }
        Image image = LoadImageFromScreen();
        require(same(GetImageColor(image,0,16),ui::face),"Idle menu title did not stay flat");
        const int tiles_x=ui::measure_text("File",19)+ui::measure_text("Edit",19)+64;
        require(same(GetImageColor(image,tiles_x,16),ui::title_blue),"Open menu title lost its flat selection highlight");
        require(same(GetImageColor(image, 1000, 12), ui::face), "Menu bar lost its Windows 95 gray palette");
        require(same(GetImageColor(image, 1000, 580), ui::desktop), "Desktop lost its gray palette");
        require(same(GetImageColor(image, 800, 76), ui::title_blue), "Window title bar was not navy blue");
        require(same(GetImageColor(image, 800, 400), ui::face), "Window body was not gray");
        require(same(GetImageColor(image, 300, 450), ui::highlight), "Raised window lost its light edge");
        require(same(GetImageColor(image, 999, 450), ui::ink), "Raised window lost its dark edge");
        const auto close=ui::window_close({300,60,700,460});
        require(same(GetImageColor(image,int(close.x+3),int(close.y+3)),ui::face),"Close button did not use the gray face");
        require(same(GetImageColor(image,20,420),ui::highlight) && same(GetImageColor(image,139,420),ui::ink),"Raised button bevel was inverted");
        require(same(GetImageColor(image,160,420),ui::shadow) && same(GetImageColor(image,279,420),ui::highlight),"Pressed button did not sink");
        require(same(GetImageColor(image,164,404),ui::ink),"Focused button lost its dotted keyboard cue");
        require(same(GetImageColor(image,20,480),ui::shadow) && same(GetImageColor(image,40,480),ui::highlight),"Text field did not have a sunken white surface");
        require(same(GetImageColor(image,20,540),ui::highlight) && same(GetImageColor(image,279,540),ui::ink),"Slider thumbs escaped their endpoints");
        if (argc > 1) ExportImage(image, argv[1]);
        UnloadImage(image);
        editor.open(4); input={}; input.vertical=2; editor.update(state,input);
        input={}; input.horizontal=1; editor.update(state,input);
        for (int width : {1024,1280}) {
            SetWindowSize(width,600);
            for (int frame=0; frame<3; ++frame) {
                BeginDrawing(); ui::draw_desktop(); ui::draw_window({300,60,700,460},"City document"); editor.draw(state); EndDrawing();
            }
            image=LoadImageFromScreen();
            const float child_x=objects_x+716<=GetScreenWidth() ? objects_x+356 : objects_x-356;
            require(same(GetImageColor(image,int(child_x+5),112),ui::title_blue),"Car submenu was not visible within the screen");
            if (argc > 2 && width==1280) ExportImage(image,argv[2]);
            UnloadImage(image);
        }
        std::cout << "Desktop menus: dispatch, disabled actions, keyboard, mouse, focus and Windows 95 controls passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; CloseWindow(); return 1;
    }
    CloseWindow();
}
