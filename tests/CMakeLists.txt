# Test suites. Each is a plain executable that returns non-zero on failure, so
# they work with ctest and equally well when run by hand.
#
# The sources are compiled into a static library once and shared, so a test does
# not silently drift from what the application actually builds.

add_library(ratty_lib STATIC ${RATTY_SOURCES_NO_MAIN})
target_include_directories(ratty_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${FREETYPE_INCLUDE_DIRS}
)
target_link_libraries(ratty_lib PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    Freetype::Freetype yaml-cpp::yaml-cpp ${UTIL_LIB}
)

function(ratty_add_test name)
    # ${RATTY_RESOURCES_ABS} is compiled per-executable; see the note in the
    # top-level CMakeLists.txt.
    add_executable(${name} ${name}.cpp check.h ${RATTY_RESOURCES_ABS})
    target_link_libraries(${name} PRIVATE ratty_lib)
    add_test(NAME ${name} COMMAND ${name})
    # The widget tests need a platform plugin but no real display or GPU.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

ratty_add_test(test_terminal)
ratty_add_test(test_input)
ratty_add_test(test_splits)
ratty_add_test(test_render)
ratty_add_test(test_config)
ratty_add_test(test_tabbar)
