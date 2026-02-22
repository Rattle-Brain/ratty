cmake_minimum_required(VERSION 3.16)
project(ratty VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
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

# Platform-specific libraries
if(APPLE)
    find_library(UTIL_LIB util REQUIRED)
else()
    set(UTIL_LIB util)
endif()

# Sources
set(RATTY_SOURCES
    src/main.cpp
    src/core/pty.cpp
    src/ui/main_window.cpp
    src/ui/terminal_tab.cpp
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
    src/ui/main_window.h
    src/ui/terminal_tab.h
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
