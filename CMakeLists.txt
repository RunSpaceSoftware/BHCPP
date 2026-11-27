cmake_minimum_required(VERSION 3.20)

project(BulletHellCPP VERSION 1.0.0 LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# raylib is used as a static library. Point RAYLIB_ROOT at a raylib source
# checkout, or override RAYLIB_LIBRARY directly for a different build.
set(RAYLIB_ROOT "C:/raylib/raylib" CACHE PATH "raylib source checkout")
set(RAYLIB_LIBRARY "${RAYLIB_ROOT}/src/libraylib.a" CACHE FILEPATH "static raylib library")
set(RAYLIB_INCLUDE_DIR "${RAYLIB_ROOT}/src" CACHE PATH "raylib headers")

if(NOT EXISTS "${RAYLIB_INCLUDE_DIR}/raylib.h" OR NOT EXISTS "${RAYLIB_LIBRARY}")
    message(FATAL_ERROR
        "raylib not found. Set RAYLIB_ROOT, or set RAYLIB_LIBRARY and "
        "RAYLIB_INCLUDE_DIR explicitly (expected ${RAYLIB_LIBRARY}).")
endif()

if(NOT MINGW)
    message(FATAL_ERROR "This build is configured for MinGW-w64 (gcc).")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(THIRD_PARTY_LUA "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua")
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${THIRD_PARTY_LUA}/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")

set(ENGINE_SOURCES
    src/bullet.c
    src/patterns.c
    src/patterns_io.c
    src/pattern_script.c
    src/player_stats.c
    src/pause_menu.c
    src/platform_compat.c
    src/audio.c
    src/engine/engine.c
    src/engine/input.c
    src/engine/console.c
    src/engine/assets.c
    src/engine/scene.c
    src/engine/level.c
    src/engine/ui.c
    src/game/game.c
    ${LUA_SOURCES}
)

add_library(bhcpp SHARED ${ENGINE_SOURCES})
target_compile_definitions(bhcpp PRIVATE PLATFORM_DESKTOP)
target_include_directories(bhcpp
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/libs"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${RAYLIB_INCLUDE_DIR}"
        "${RAYLIB_INCLUDE_DIR}/external"
    PRIVATE
        "${THIRD_PARTY_LUA}"
)
target_compile_options(bhcpp PRIVATE -Wall -Wextra)

# raylib is linked into the engine DLL only. --whole-archive is required
# because the executables import raylib symbols through this DLL, and the
# wrappers skip GLFW's DirectInput joystick path, whose lazy init can leave
# a GLFW error that raylib reports as a failed graphics device (PLATFORM).
target_link_options(bhcpp PRIVATE
    "-Wl,--whole-archive" "${RAYLIB_LIBRARY}" "-Wl,--no-whole-archive"
    "-Wl,--wrap=glfwSetJoystickCallback"
    "-Wl,--wrap=glfwJoystickPresent"
    "-Wl,--export-all-symbols"
)
target_link_libraries(bhcpp PRIVATE opengl32 gdi32 winmm)

set_target_properties(bhcpp PROPERTIES PREFIX "" OUTPUT_NAME bhcpp)

# Windows icon and version metadata. windres produces a COFF object that
# every executable links next to its own sources.
set(RESOURCE_OBJECT "${CMAKE_BINARY_DIR}/game.rc.data")
find_program(WINDRES_EXECUTABLE NAMES windres REQUIRED)
add_custom_command(
    OUTPUT "${RESOURCE_OBJECT}"
    COMMAND "${WINDRES_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/game.rc" -O coff -o "${RESOURCE_OBJECT}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/game.rc"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico"
    VERBATIM
)
add_custom_target(game_resource DEPENDS "${RESOURCE_OBJECT}")
set_source_files_properties("${RESOURCE_OBJECT}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)

function(add_bhcpp_executable target)
    add_executable(${target} ${ARGN})
    target_compile_definitions(${target} PRIVATE PLATFORM_DESKTOP)
    target_compile_options(${target} PRIVATE -Wall)
    target_include_directories(${target} PRIVATE "${THIRD_PARTY_LUA}")
    target_link_libraries(${target} PRIVATE bhcpp)
    target_sources(${target} PRIVATE "${RESOURCE_OBJECT}")
    add_dependencies(${target} game_resource)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endfunction()

add_bhcpp_executable(game src/main.c)
set_target_properties(game PROPERTIES WIN32_EXECUTABLE TRUE)

add_bhcpp_executable(editor tools/editor.c)
add_bhcpp_executable(shape_test tools/shape_test.c)
add_bhcpp_executable(pattern_editor tools/pattern_editor.c)
# The legacy tool deliberately truncates long status messages into its 512-byte
# status bar; GCC 16 warns about that intended snprintf truncation.
target_compile_options(pattern_editor PRIVATE -Wno-format-truncation)

file(GLOB RES_TOP CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.wav"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.ogg"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.otf"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/LICENSE")
file(GLOB RES_SCRIPTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/scripts/*.lua")
file(GLOB RES_SCENES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/scenes/*.scene")

add_custom_target(copy_resources ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/resources/scripts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/resources/scenes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RES_TOP} "${CMAKE_BINARY_DIR}/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RES_SCRIPTS} "${CMAKE_BINARY_DIR}/resources/scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RES_SCENES} "${CMAKE_BINARY_DIR}/resources/scenes"
    VERBATIM)

add_custom_target(check
    COMMAND $<TARGET_FILE:shape_test>
    COMMAND $<TARGET_FILE:editor> --check
    COMMAND $<TARGET_FILE:editor> --layout
    COMMAND $<TARGET_FILE:pattern_editor> --check "${CMAKE_CURRENT_SOURCE_DIR}/resources/patterns.txt"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM)
add_dependencies(check game editor shape_test pattern_editor copy_resources)

add_dependencies(game copy_resources)
add_dependencies(editor copy_resources)
add_dependencies(shape_test copy_resources)
add_dependencies(pattern_editor copy_resources)
