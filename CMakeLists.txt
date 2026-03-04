cmake_minimum_required(VERSION 3.16)

# Set Clang 20 as the default compiler if not already specified
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "/opt/homebrew/opt/llvm@20/bin/clang++")
endif()

project(ratty VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt6
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    OpenGL
    OpenGLWidgets
)

# Font rendering
find_package(Freetype REQUIRED)

# Platform-specific libraries and definitions
if(APPLE)
    find_library(UTIL_LIB util REQUIRED)
    # Silence OpenGL deprecation warnings on macOS (OpenGL 3.3 is still supported)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
else()
    set(UTIL_LIB util)
endif()

# Sources
set(RATTY_SOURCES
    src/main.cpp
    src/core/pty.cpp
    src/core/terminal_emulator.cpp
    src/ui/main_window.cpp
    src/ui/split_container.cpp
    src/ui/terminal_widget.cpp
    src/ui/input_handler.cpp
    src/render/font_manager.cpp
    src/render/glyph_atlas.cpp
    src/render/gl_renderer.cpp
    src/config/config.cpp
)

# Headers (for IDE support)
set(RATTY_HEADERS
    src/core/pty.h
    src/core/terminal_emulator.h
    src/ui/main_window.h
    src/ui/split_container.h
    src/ui/terminal_widget.h
    src/ui/input_handler.h
    src/render/font_manager.h
    src/render/glyph_atlas.h
    src/render/gl_renderer.h
    src/config/config.h
)

add_executable(ratty
    ${RATTY_SOURCES}
    ${RATTY_HEADERS}
)

target_include_directories(ratty PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${FREETYPE_INCLUDE_DIRS}
)

target_link_libraries(ratty PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Freetype::Freetype
    ${UTIL_LIB}
)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ratty PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# Add shader resources
target_sources(ratty PRIVATE
    resources/shaders.qrc
)
