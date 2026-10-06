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
        for (int tool = 0; tool < 10; ++tool) {
            editor.open(2); input = {}; input.vertical = tool; editor.update(state, input);
            input = {}; input.enter = true;
            require(int(editor.update(state, input)) == int(MenuCommand::SelectTool) + tool, "Build menu chose the wrong tool");
        }
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

        MenuBar cities(MenuMode::Cities); cities.open(1); state.selection = false; input = {}; input.enter = true;
        require(cities.update(state, input) == MenuCommand::None && cities.blocking(), "City commands were enabled without a city");
        state.selection = true;
        require(cities.update(state, input) == MenuCommand::EditCity, "City > Edit did not dispatch");
        cities.open(2);
        require(cities.update(state,input) == MenuCommand::Graphics, "Graphics was not available in the main menu");
        cities.open(2); input = {}; input.vertical = 1; cities.update(state,input);
        input = {}; input.enter = true;
        require(cities.update(state,input) == MenuCommand::Controllers, "Controller mapping was not available in the main menu");
        MenuBar game; game.open(2);
        require(game.update({}, input) == MenuCommand::Map, "Existing gameplay menu changed");
        game.open(2); input = {}; input.vertical = -1; game.update({},input);
        input = {}; input.enter = true;
        require(game.update({},input) == MenuCommand::AimMode, "Global settings remained in the gameplay menu");
        game.open(); input = {}; input.vertical = -1; game.update({},input);
        input = {}; input.enter = true;
        require(game.update({},input) == MenuCommand::Cities, "End Game was not the last File item");
        input = {}; input.escape = true;
        require(game.update({},input) == MenuCommand::Pause && game.interacted() && !game.blocking(), "Escape did not release the gameplay cursor");
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
