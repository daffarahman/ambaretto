#include <raylib.h>
#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void push(SDL_Event event) {
    require(SDL_PushEvent(&event), "SDL rejected an input event");
}
}

int main() {
    SetTraceLogLevel(LOG_WARNING);
    try {
        for (int run = 0; run < 2; ++run) {
            SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_WINDOW_RESIZABLE);
            InitWindow(320, 240, "SDL backend checks");
            require(IsWindowReady(), "Raylib did not create its graphics context");
            SetTargetFPS(0);
            const double first_frame_start = GetTime();
            BeginDrawing(); ClearBackground(BLACK); EndDrawing();
            require(GetFrameTime() >= 0 && GetTime() - first_frame_start <= GetFrameTime() + .02,
                "Reinitializing SDL must reset frame timing without waiting for the previous window's elapsed time");
            const auto subsystems = SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD;
            require(SDL_WasInit(subsystems) == subsystems, "Raylib and the game must share initialized SDL subsystems");
            int count = 0;
            auto** windows = SDL_GetWindows(&count);
            auto* window = count == 1 && windows ? windows[0] : nullptr;
            SDL_free(windows);
            require(window, "Raylib must create exactly one window in the linked SDL instance");
            const auto flags = SDL_GetWindowFlags(window);
            require((flags & SDL_WINDOW_HIDDEN) && (flags & SDL_WINDOW_RESIZABLE) && (flags & SDL_WINDOW_OPENGL),
                "SDL must honor the hidden, resizable OpenGL window flags");
            require(SDL_GL_GetCurrentContext(), "Raylib's SDL OpenGL context must be current");
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
            auto* hand = SDL_GetCursor();
            require(hand && hand != SDL_GetDefaultCursor(), "Raylib must create the requested SDL cursor");
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
            require(SDL_GetCursor() == hand, "Repeated cursor requests must reuse the SDL cursor");
            SetMouseCursor(MOUSE_CURSOR_IBEAM);
            require(SDL_GetCursor() && SDL_GetCursor() != hand, "Changing cursor shape must replace the SDL cursor");
            const auto id = SDL_GetWindowID(window);
            PollInputEvents();

            SDL_Event key{};
            key.key.type = SDL_EVENT_KEY_DOWN; key.key.windowID = id;
            key.key.scancode = SDL_SCANCODE_W; key.key.down = true;
            push(key);
            SDL_Event text{};
            text.text.type = SDL_EVENT_TEXT_INPUT; text.text.windowID = id; text.text.text = "A\xC3\xA9\xF0\x9F\x9A\x97";
            push(text);
            SDL_Event motion{};
            motion.motion.type = SDL_EVENT_MOUSE_MOTION; motion.motion.windowID = id;
            motion.motion.x = 40; motion.motion.y = 60;
            push(motion);
            SDL_Event button{};
            button.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN; button.button.windowID = id;
            button.button.button = SDL_BUTTON_RIGHT; button.button.down = true;
            push(button);
            SDL_Event wheel{};
            wheel.wheel.type = SDL_EVENT_MOUSE_WHEEL; wheel.wheel.windowID = id;
            wheel.wheel.x = 1.25f; wheel.wheel.y = -2.5f;
            push(wheel);
            wheel.wheel.x = -.5f; wheel.wheel.y = 1;
            push(wheel);
            PollInputEvents();
            require(IsKeyPressed(KEY_W) && IsKeyDown(KEY_W), "SDL key events did not reach raylib");
            require(GetCharPressed() == 'A' && GetCharPressed() == 0xE9 && GetCharPressed() == 0x1F697 && GetCharPressed() == 0,
                "Every codepoint in an SDL UTF-8 text event must reach raylib");
            require(GetMouseX() == 40 && GetMouseY() == 60, "SDL mouse motion did not reach raylib");
            require(IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && IsMouseButtonDown(MOUSE_BUTTON_RIGHT),
                "SDL right mouse button must map to raylib's right button");
            const auto scroll = GetMouseWheelMoveV();
            require(scroll.x == .75f && scroll.y == -1.5f, "SDL must accumulate fractional mouse wheel packets");

            PollInputEvents();
            require(IsKeyDown(KEY_W) && !IsKeyPressed(KEY_W) && !IsMouseButtonPressed(MOUSE_BUTTON_RIGHT),
                "Held SDL input must not repeat its leading edge");
            require(GetMouseWheelMoveV().x == 0 && GetMouseWheelMoveV().y == 0, "Mouse wheel movement must clear each frame");
            DisableCursor();
            PollInputEvents();
            motion.motion.xrel = 3; motion.motion.yrel = -4;
            push(motion);
            motion.motion.xrel = -1.5f; motion.motion.yrel = 2.25f;
            push(motion);
            PollInputEvents();
            require(GetMouseDelta().x == 1.5f && GetMouseDelta().y == -1.75f,
                "SDL must accumulate every relative mouse packet for camera movement");
            PollInputEvents();
            require(GetMouseDelta().x == 0 && GetMouseDelta().y == 0, "Relative mouse movement must clear each frame");
            EnableCursor();
            key.key.type = SDL_EVENT_KEY_UP; key.key.down = false; push(key);
            button.button.type = SDL_EVENT_MOUSE_BUTTON_UP; button.button.down = false; push(button);
            PollInputEvents();
            require(IsKeyReleased(KEY_W) && !IsKeyDown(KEY_W) && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT),
                "SDL release events must clear raylib input");

            require(SDL_SetWindowSize(window, 360, 260) && SDL_SyncWindow(window), "SDL window resize failed");
            PollInputEvents();
            require(IsWindowResized() && GetScreenWidth() == 360 && GetScreenHeight() == 260,
                "A native SDL window resize must update raylib's viewport");
            SetWindowSize(400, 280);
            int drawable_width = 0, drawable_height = 0;
            require(SDL_GetWindowSizeInPixels(window, &drawable_width, &drawable_height) &&
                GetScreenWidth() == 400 && GetScreenHeight() == 280 &&
                GetRenderWidth() == drawable_width && GetRenderHeight() == drawable_height,
                "Raylib SetWindowSize must update the drawable and viewport before the next frame");
            SDL_Delay(50);
            BeginDrawing(); ClearBackground(BLACK); EndDrawing();
            CloseWindow();
            require(!IsWindowReady() && SDL_WasInit(subsystems) == 0, "Closing raylib must shut down its SDL instance");
        }
        std::cout << "SDL window, context, input, UTF-8, resize and reinitialization checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (IsWindowReady()) CloseWindow();
        return 1;
    }
    return 0;
}
