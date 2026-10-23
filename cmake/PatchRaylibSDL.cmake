# Preserve the input and hidden-window behavior used by the game with raylib 6.0's SDL backend.
set(raylib_sdl_source "${raylib_SOURCE_DIR}/src/platforms/rcore_desktop_sdl.c")
file(READ "${raylib_sdl_source}" raylib_sdl_original)
set(raylib_sdl_patched "${raylib_sdl_original}")
string(REPLACE [=[
    SDL_SetWindowSize(platform.window, width, height);

    CORE.Window.screen.width = width;
    CORE.Window.screen.height = height;
]=] [=[
#if defined(USING_VERSION_SDL3)
    if (!SDL_SetWindowSize(platform.window, width, height)) return;
    SDL_SyncWindow(platform.window);
    SDL_GetWindowSize(platform.window, &CORE.Window.screen.width, &CORE.Window.screen.height);
    if (SDL_GetWindowSizeInPixels(platform.window, &width, &height) && (width > 0) && (height > 0))
    {
        SetupViewport(width, height);
        CORE.Window.currentFbo = CORE.Window.render;
        CORE.Window.resizedLastFrame = true;
    }
#else
    SDL_SetWindowSize(platform.window, width, height);
    CORE.Window.screen.width = width;
    CORE.Window.screen.height = height;
#endif
]=] raylib_sdl_patched "${raylib_sdl_patched}")
string(REPLACE
    "CORE.Time.base = (double)SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency();\n\n"
    "CORE.Time.base = (double)SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency();\n    CORE.Time.previous = GetTime();\n\n"
    raylib_sdl_patched "${raylib_sdl_patched}")
string(REPLACE
    "//if (!FLAG_IS_SET(CORE.Window.flags, FLAG_WINDOW_HIDDEN)) FLAG_SET(flags, SDL_WINDOW_HIDDEN);"
    "if (FLAG_IS_SET(CORE.Window.flags, FLAG_WINDOW_HIDDEN)) FLAG_SET(flags, SDL_WINDOW_HIDDEN);"
    raylib_sdl_patched "${raylib_sdl_patched}")
string(REPLACE
    "CORE.Input.Mouse.currentPosition.x = (float)event.motion.xrel;"
    "CORE.Input.Mouse.currentPosition.x += (float)event.motion.xrel;"
    raylib_sdl_patched "${raylib_sdl_patched}")
string(REPLACE
    "CORE.Input.Mouse.currentPosition.y = (float)event.motion.yrel;"
    "CORE.Input.Mouse.currentPosition.y += (float)event.motion.yrel;"
    raylib_sdl_patched "${raylib_sdl_patched}")
string(REPLACE
    "CORE.Input.Mouse.currentWheelMove.x = (float)event.wheel.x;"
    "CORE.Input.Mouse.currentWheelMove.x += (float)event.wheel.x;"
    raylib_sdl_patched "${raylib_sdl_patched}")
string(REPLACE
    "CORE.Input.Mouse.currentWheelMove.y = (float)event.wheel.y;"
    "CORE.Input.Mouse.currentWheelMove.y += (float)event.wheel.y;"
    raylib_sdl_patched "${raylib_sdl_patched}")
string(REPLACE [=[
                #if defined(USING_VERSION_SDL3)
                    size_t textLen = strlen(event.text.text);
                    unsigned int codepoint = (unsigned int)SDL_StepUTF8(&event.text.text, &textLen);
                #else
                    int codepointSize = 0;
                    int codepoint = GetCodepointNextSDL(event.text.text, &codepointSize);
                #endif

                    CORE.Input.Keyboard.charPressedQueue[CORE.Input.Keyboard.charPressedQueueCount] = codepoint;
                    CORE.Input.Keyboard.charPressedQueueCount++;
]=] [=[
                #if defined(USING_VERSION_SDL3)
                    const char *text = event.text.text;
                    size_t textLen = strlen(text);
                    while ((textLen > 0) && (CORE.Input.Keyboard.charPressedQueueCount < MAX_CHAR_PRESSED_QUEUE))
                    {
                        CORE.Input.Keyboard.charPressedQueue[CORE.Input.Keyboard.charPressedQueueCount] = (int)SDL_StepUTF8(&text, &textLen);
                        CORE.Input.Keyboard.charPressedQueueCount++;
                    }
                #else
                    int codepointSize = 0;
                    int codepoint = GetCodepointNextSDL(event.text.text, &codepointSize);
                    CORE.Input.Keyboard.charPressedQueue[CORE.Input.Keyboard.charPressedQueueCount] = codepoint;
                    CORE.Input.Keyboard.charPressedQueueCount++;
                #endif
]=] raylib_sdl_patched "${raylib_sdl_patched}")
string(REPLACE [=[
    platform.cursor = SDL_CreateSystemCursor(CursorsLUT[cursor]);
    SDL_SetCursor(platform.cursor);

    CORE.Input.Mouse.cursor = cursor;
]=] [=[
    if (platform.cursor && (CORE.Input.Mouse.cursor == cursor)) return;
    SDL_Cursor *nextCursor = SDL_CreateSystemCursor(CursorsLUT[cursor]);
    if (nextCursor == NULL) return;
#if defined(USING_VERSION_SDL3)
    if (!SDL_SetCursor(nextCursor))
    {
        SDL_FreeCursor(nextCursor);
        return;
    }
#else
    SDL_SetCursor(nextCursor);
#endif
    SDL_FreeCursor(platform.cursor);
    platform.cursor = nextCursor;
    CORE.Input.Mouse.cursor = cursor;
]=] raylib_sdl_patched "${raylib_sdl_patched}")
string(REPLACE
    "SDL_FreeCursor(platform.cursor); // Free cursor\n    if (platform.glContext != NULL)"
    "SDL_FreeCursor(platform.cursor); // Free cursor\n    platform.cursor = NULL;\n    if (platform.glContext != NULL)"
    raylib_sdl_patched "${raylib_sdl_patched}")
if(NOT raylib_sdl_original STREQUAL raylib_sdl_patched)
    file(WRITE "${raylib_sdl_source}" "${raylib_sdl_patched}")
endif()
