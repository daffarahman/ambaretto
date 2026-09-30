cmake_minimum_required(VERSION 3.24)
project(forzaambazon VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(RAYLIB REQUIRED IMPORTED_TARGET raylib)

# Build a pinned, static Jolt library with the same compiler as the game.
include(FetchContent)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS ON CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(USE_AVX OFF CACHE BOOL "" FORCE)
set(USE_AVX2 OFF CACHE BOOL "" FORCE)
set(USE_AVX512 OFF CACHE BOOL "" FORCE)
FetchContent_Declare(Jolt
    URL https://codeload.github.com/jrouwe/JoltPhysics/zip/refs/tags/v5.4.0
    URL_HASH SHA256=a0d6ce05bc1be14a9e596de10abbaf4b6fbbb1237125d14afc8974b542bb2dec
    SOURCE_SUBDIR Build
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(Jolt)

add_library(vehicle_physics src/vehicle.cpp src/environment.cpp src/player.cpp src/traffic.cpp src/third_person_camera.cpp)
target_include_directories(vehicle_physics PUBLIC include)
target_include_directories(vehicle_physics SYSTEM PUBLIC $<TARGET_PROPERTY:Jolt,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(vehicle_physics PUBLIC Jolt)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vehicle_physics PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(forzaambazon src/main.cpp src/environment_renderer.cpp src/car_renderer.cpp src/tuning_panel.cpp)
target_link_libraries(forzaambazon PRIVATE vehicle_physics PkgConfig::RAYLIB)
# Keep runtime assets beside the executable, including after model/texture edits.
add_custom_target(forzaambazon_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:forzaambazon>/assets"
    COMMENT "Copying game assets"
    VERBATIM
)
add_dependencies(forzaambazon forzaambazon_assets)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(forzaambazon PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(physics_tests tests/physics_tests.cpp)
    target_link_libraries(physics_tests PRIVATE vehicle_physics)
    add_test(NAME vehicle_behavior COMMAND physics_tests)
    add_executable(player_tests tests/player_tests.cpp)
    target_link_libraries(player_tests PRIVATE vehicle_physics)
    add_test(NAME player_behavior COMMAND player_tests)
    add_executable(tuning_tests tests/tuning_tests.cpp src/tuning_panel.cpp)
    target_link_libraries(tuning_tests PRIVATE vehicle_physics PkgConfig::RAYLIB)
    add_test(NAME live_tuning COMMAND tuning_tests)
    add_executable(plane_tests tests/plane_tests.cpp)
    target_link_libraries(plane_tests PRIVATE vehicle_physics)
    add_test(NAME airport_and_flight COMMAND plane_tests)
    add_executable(traffic_tests tests/traffic_tests.cpp)
    target_link_libraries(traffic_tests PRIVATE vehicle_physics)
    add_test(NAME traffic_and_theft COMMAND traffic_tests)
endif()
