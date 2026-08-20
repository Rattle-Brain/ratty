cmake_minimum_required(VERSION 3.16)

project(ratty VERSION 0.2.0 LANGUAGES CXX)

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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(Freetype REQUIRED)
# Configuration is YAML; see src/config/config.h for why.
find_package(yaml-cpp REQUIRED)

if(APPLE)
    find_library(UTIL_LIB util REQUIRED)
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
    src/ui/tab_bar.cpp
    src/ui/terminal_widget.cpp

    src/config/chrome.cpp
    src/config/theme.cpp
    src/config/config.cpp
)

# Listed so IDEs and AUTOMOC see them.
set(RATTY_HEADERS
    src/core/cell.h
    src/core/unicode.h
    src/core/cursor.h
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
    src/ui/tab_bar.h
    src/ui/terminal_widget.h

    src/config/chrome.h
    src/config/theme.h
    src/config/config.h
)

set(RATTY_RESOURCES
    resources/shaders.qrc
    resources/config.qrc
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

target_link_libraries(ratty PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Freetype::Freetype
    yaml-cpp::yaml-cpp
    ${UTIL_LIB}
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
