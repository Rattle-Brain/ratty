cmake_minimum_required(VERSION 3.16)

project(ratty VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt's meta-object, resource and UI compilers.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Export a compilation database for clangd / editor tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Link-time optimization for release builds. It matters here because the hot
# paths are small functions spread across translation units -- Screen::print and
# the cell accessors the renderer calls per cell -- which the compiler can only
# inline once it can see across object files. Probed rather than assumed, and
# left off for debug builds where the link time is not worth paying.
include(CheckIPOSupported)
check_ipo_supported(RESULT RATTY_HAS_LTO OUTPUT RATTY_LTO_ERROR)
if(RATTY_HAS_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
else()
    message(STATUS "Link-time optimization unavailable: ${RATTY_LTO_ERROR}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(Freetype REQUIRED)
# Configuration is YAML; see src/config/config.h for why.
find_package(yaml-cpp REQUIRED)

if(APPLE)
    find_library(UTIL_LIB util REQUIRED)
    # Foundation, for the press-and-hold preference in src/platform.
    find_library(FOUNDATION_LIB Foundation REQUIRED)
    # OpenGL is deprecated but fully functional on macOS; silence the noise.
    add_compile_definitions(GL_SILENCE_DEPRECATION)
else()
    set(UTIL_LIB util)
endif()

# --- Sources ----------------------------------------------------------------
# core/  : terminal model - no Qt widgets, no OpenGL
# render/: font rasterization and GPU drawing
# ui/    : Qt widgets and event handling
# config/: settings

set(RATTY_SOURCES
    src/main.cpp

    src/core/history.cpp
    src/core/mouse.cpp
    src/core/palette.cpp
    src/core/pty.cpp
    src/core/screen.cpp
    src/core/terminal_emulator.cpp
    src/core/terminal_session.cpp
    src/core/vt_parser.cpp

    src/render/box_drawing.cpp
    src/render/font_manager.cpp
    src/render/gl_renderer.cpp
    src/render/glyph_atlas.cpp
    src/render/terminal_renderer.cpp

    src/ui/input_handler.cpp
    src/ui/main_window.cpp
    src/ui/split_container.cpp
    src/ui/terminal_canvas.cpp
    src/ui/tab_bar.cpp
    src/ui/terminal_widget.cpp

    src/config/chrome.cpp
    src/config/theme.cpp
    src/config/config.cpp
)

# Platform shims. Exactly one implementation is compiled; see src/platform.
if(APPLE)
    # Objective-C++, for the one AppKit preference Qt cannot express.
    list(APPEND RATTY_SOURCES src/platform/platform_mac.mm)
else()
    list(APPEND RATTY_SOURCES src/platform/platform_generic.cpp)
endif()

# Listed so IDEs and AUTOMOC see them.
set(RATTY_HEADERS
    src/platform/platform.h

    src/core/cell.h
    src/core/history.h
    src/core/unicode.h
    src/core/cursor.h
    src/core/mouse.h
    src/core/palette.h
    src/core/pty.h
    src/core/screen.h
    src/core/terminal_emulator.h
    src/core/terminal_session.h
    src/core/utf8.h
    src/core/vt_parser.h

    src/render/box_drawing.h
    src/render/font_manager.h
    src/render/gl_renderer.h
    src/render/glyph_atlas.h
    src/render/terminal_renderer.h

    src/ui/input_handler.h
    src/ui/main_window.h
    src/ui/split_container.h
    src/ui/terminal_canvas.h
    src/ui/tab_bar.h
    src/ui/terminal_widget.h

    src/config/chrome.h
    src/config/theme.h
    src/config/config.h
)

set(RATTY_RESOURCES
    resources/shaders.qrc
    resources/config.qrc
    resources/fonts.qrc
    resources/themes.qrc
    resources/keybindings.qrc
)

# Everything except main.cpp, so the test suites can link the same code the
# application does instead of re-listing sources. Absolute paths, because the
# list is consumed from tests/ where relative paths would resolve differently.
set(RATTY_SOURCES_NO_MAIN "")
foreach(source IN LISTS RATTY_SOURCES RATTY_HEADERS)
    if(NOT source STREQUAL "src/main.cpp")
        list(APPEND RATTY_SOURCES_NO_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    endif()
endforeach()

# Resources are kept out of that list on purpose. A .qrc compiled into a static
# library registers itself from a global initializer, which the linker discards
# because nothing references it -- the resources then silently do not exist.
# Every executable therefore compiles the .qrc files itself.
set(RATTY_RESOURCES_ABS "")
foreach(resource IN LISTS RATTY_RESOURCES)
    list(APPEND RATTY_RESOURCES_ABS "${CMAKE_CURRENT_SOURCE_DIR}/${resource}")
endforeach()

add_executable(ratty
    ${RATTY_SOURCES}
    ${RATTY_HEADERS}
    ${RATTY_RESOURCES}
)

target_include_directories(ratty PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FREETYPE_INCLUDE_DIRS}
)

# The version is stated once, above, and reaches the code from here. It used to
# be written out again as a string literal in main.cpp, which is the kind of
# duplication that is only ever noticed once the two have already disagreed.
target_compile_definitions(ratty PRIVATE RATTY_VERSION="${PROJECT_VERSION}")

target_link_libraries(ratty PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Freetype::Freetype
    yaml-cpp::yaml-cpp
    ${UTIL_LIB}
    ${FOUNDATION_LIB}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ratty PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS ratty RUNTIME DESTINATION bin)

# Tests are opt-in: -DRATTY_BUILD_TESTS=ON
option(RATTY_BUILD_TESTS "Build the test suites" OFF)
if(RATTY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
