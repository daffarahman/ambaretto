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
        const MenuCommand tile_actions[] = {MenuCommand::LandTool,MenuCommand::RoadBend,MenuCommand::RoadDiagonal,
            MenuCommand::GroundTool,MenuCommand::RaiseGround,MenuCommand::LowerGround};
        for (int i = 0; i<int(std::size(tile_actions)); ++i) dispatch(2,i,tile_actions[i]);
        dispatch(3,0,MenuCommand::BuildingTool); dispatch(3,1,MenuCommand::BuildingCreator);
        dispatch(3,4,MenuCommand::SavedBuildings);
        dispatch(4,11,MenuCommand::CarEditor);
        dispatch(4,13,MenuCommand::CharacterCreator); dispatch(4,14,MenuCommand::ChoosePlayerCharacter);
        dispatch(0,5,MenuCommand::RefreshDesigns);
        MenuBar character_creator(MenuMode::CharacterCreator); character_creator.open(); input={}; input.vertical=1; character_creator.update({},input); input={}; input.enter=true;
        require(character_creator.update({},input)==MenuCommand::SaveCharacter,"Character creator Save menu is missing");
        state.car_selection=true; dispatch(4,12,MenuCommand::EditCar); state.car_selection=false;
        editor.open(4); input={}; input.vertical=12; editor.update(state,input); input={}; input.enter=true;
        require(editor.update(state,input)==MenuCommand::None,"Car edit enabled without a selected car");
        MenuBar car_creator(MenuMode::CarCreator); car_creator.open(); input={}; input.vertical=1; car_creator.update({},input); input={}; input.enter=true;
        require(car_creator.update({},input)==MenuCommand::SaveCar,"Car creator Save menu is missing");
        editor.open(3); input = {}; input.vertical = 2; editor.update(state,input); input = {}; input.enter = true;
        require(editor.update(state,input)==MenuCommand::None && editor.blocking(),"Building edit enabled without a selected building");
        state.building_selection = true; require(editor.update(state,input)==MenuCommand::EditBuilding,"Selected building edit was not available");
        dispatch(3,3,MenuCommand::RotateBuilding);
        state.building_selection = false; state.tool = 3; dispatch(3,3,MenuCommand::RotateBuilding);
        state.tool = 0; dispatch(3,3,MenuCommand::None);
        const MenuCommand object_actions[] = {MenuCommand::SpawnTool,MenuCommand::PlaceCar,MenuCommand::PlaceTrainer,MenuCommand::PlaceF18,
            MenuCommand::PlaceBoeing,MenuCommand::RotateObject,MenuCommand::TreesSparse,MenuCommand::TreesMedium,MenuCommand::TreesDense,MenuCommand::BrushSmaller,MenuCommand::BrushLarger};
        for (int i = 0; i<int(std::size(object_actions)); ++i) dispatch(4,i,object_actions[i]);
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
        dispatch_menu(MenuMode::Cities,0,3,MenuCommand::Quit);
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
            EndDrawing();
        }
        Image image = LoadImageFromScreen();
        require(same(GetImageColor(image, 1000, 12), ui::dos_white), "Menu bar lost its white palette");
        require(same(GetImageColor(image, 1000, 580), ui::dos_blue), "Desktop lost its DOS blue palette");
        require(same(GetImageColor(image, 300, 450), ui::dos_white), "Document border was not rendered");
        if (argc > 1) ExportImage(image, argv[1]);
        UnloadImage(image);
        std::cout << "Desktop menus: dispatch, disabled actions, keyboard, mouse, focus and DOS palette passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; CloseWindow(); return 1;
    }
    CloseWindow();
}
