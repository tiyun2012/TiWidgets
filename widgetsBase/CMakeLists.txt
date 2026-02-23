cmake_minimum_required(VERSION 3.20)
project(widgets_base LANGUAGES CXX)

# Header-only interface for the custom docking framework (no ImGui dependency).
add_library(widgets_base INTERFACE)
target_include_directories(widgets_base INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_compile_features(widgets_base INTERFACE cxx_std_17)

# Engine-agnostic docking framework skeleton.
add_library(dock_framework
    dock_framework.cpp
    dock_framework.h
    dock_theme.h
    dock_layout.h
    dock_drag.h
    core_types.h
    icon_module.h
    icons/IconsFontAwesome6.h
)
target_compile_features(dock_framework PUBLIC cxx_std_17)
target_include_directories(dock_framework PUBLIC ${CMAKE_CURRENT_LIST_DIR})

add_library(dock_components
    window_manager.cpp
    dock_splitter.cpp
    dock_renderer.cpp
    dock_renderer.h
)
target_link_libraries(dock_components PUBLIC dock_framework)
target_compile_features(dock_components PUBLIC cxx_std_17)
target_include_directories(dock_components PUBLIC ${CMAKE_CURRENT_LIST_DIR})

option(WB_BUILD_STANDALONE "Build docking framework sandbox" ON)
if(WB_BUILD_STANDALONE)
    add_executable(dock_framework_sandbox dock_framework_sandbox.cpp)
    target_link_libraries(dock_framework_sandbox PRIVATE dock_framework dock_components)

    add_executable(dock_framework_demo simple_demo.cpp)
    target_link_libraries(dock_framework_demo PRIVATE dock_framework dock_components)

    add_executable(interactive_demo interactive_demo.cpp)
    target_link_libraries(interactive_demo PRIVATE dock_framework dock_components)

    add_executable(game_loop_demo platform_agnostic_demo.cpp)
    target_link_libraries(game_loop_demo PRIVATE dock_framework dock_components)

    add_executable(dock_layout_constraints_demo dock_layout_constraints_demo.cpp)
    target_link_libraries(dock_layout_constraints_demo PRIVATE dock_framework dock_components)
endif()

# Optional DirectX 12 backend demo (placeholder). Off by default.
# DirectX 12 demo is defined at the root CMakeLists to avoid duplicate targets.
