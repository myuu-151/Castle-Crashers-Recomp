cmake_minimum_required(VERSION 3.25)

project(CastleCrashers LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# SDL3: window, input, audio and the OpenGL context.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.8
    GIT_SHALLOW TRUE
)

# libtess2: triangulates the filled regions of vector shapes.
FetchContent_Declare(
    libtess2
    GIT_REPOSITORY https://github.com/memononen/libtess2.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-cmake  # sources only; built below
)

# stb_image: loads PNGs for mods.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-cmake  # header-only
)

FetchContent_MakeAvailable(SDL3 libtess2 stb)

file(GLOB TESS_SOURCES ${libtess2_SOURCE_DIR}/Source/*.c)
add_library(tess2 STATIC ${TESS_SOURCES})
target_include_directories(tess2 PUBLIC ${libtess2_SOURCE_DIR}/Include)

add_library(cc_engine STATIC
    engine/swf/movie.cpp
    engine/swf/shape.cpp
    engine/player/clip.cpp
    engine/player/mods.cpp
    engine/render/gl.cpp
    engine/render/renderer.cpp
    engine/render/offscreen.cpp
    engine/common/png.cpp
    engine/common/files.cpp
    engine/input/input.cpp
    engine/menu/menu.cpp
    engine/menu/main_menu.cpp
    engine/menu/pause_menu.cpp
    engine/audio/audio.cpp
    engine/as/interpreter.cpp
    engine/as/names.cpp
    engine/as/object.cpp
    engine/as/objects.cpp
    engine/as/rng.cpp
    engine/as/value.cpp
    engine/player/dump.cpp
    engine/player/game.cpp
    engine/player/movieclip.cpp
    engine/player/natives.cpp
    engine/player/bsp.cpp
    engine/verify.cpp
    engine/common/crash.cpp
    engine/save/storage.cpp
    engine/player/player.cpp
    engine/text/font.cpp
    engine/text/fonts.cpp
    engine/text/layout.cpp
    engine/text/strings.cpp
)
find_package(OpenGL REQUIRED)
target_include_directories(cc_engine PUBLIC engine PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(cc_engine PUBLIC SDL3::SDL3 tess2 OpenGL::GL)
if(MSVC)
    target_compile_options(cc_engine PRIVATE /W4 /wd4100)
endif()
if(WIN32)
    target_link_libraries(cc_engine PUBLIC dbghelp xaudio2 ole32)
endif()


add_executable(castle engine/main.cpp)
if(WIN32)
    target_sources(castle PRIVATE engine/resources/castle.rc)
endif()
target_link_libraries(castle PRIVATE cc_engine)
# Release builds keep debug information, for crash stack traces.
if(MSVC)
    target_compile_options(cc_engine PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_compile_options(castle PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(castle PRIVATE $<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()
add_custom_command(
    TARGET castle
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:castle>
)
