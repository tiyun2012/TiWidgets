cmake_minimum_required(VERSION 3.20)
project(dx12_sample LANGUAGES CXX)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep all outputs in one predictable tree for easier run/check workflows.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfg_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_BINARY_DIR}/bin/${cfg}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_BINARY_DIR}/lib/${cfg}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_BINARY_DIR}/lib/${cfg}")
endforeach()

# Ensure we target Windows 10+ APIs for DirectX 12
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)

add_subdirectory(widgetsBase)

add_executable(dx12_sample main.cpp)

target_link_libraries(dx12_sample PRIVATE d3d12 dxgi dxguid)

# ---- DX12 demo (Windows only, optional) ----
option(WB_BUILD_DX12_DEMO "Build DX12 docking demo" OFF)
if (WIN32 AND WB_BUILD_DX12_DEMO)
    add_executable(dx12_demo
        widgetsBase/dx12_demo.cpp
        widgetsBase/dx12_canvas.cpp
    )
    target_link_libraries(dx12_demo PRIVATE
        dock_framework
        dock_components
        d3d12
        dxgi
        d3dcompiler
        dwmapi
        dbghelp
    )
    target_compile_features(dx12_demo PRIVATE cxx_std_17)
    target_include_directories(dx12_demo PRIVATE ${CMAKE_CURRENT_LIST_DIR}/widgetsBase)
    target_compile_definitions(dx12_demo PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if (BUILD_TESTING)
    if (TARGET dock_framework_demo)
        add_test(NAME dock_framework_demo_smoke COMMAND $<TARGET_FILE:dock_framework_demo>)
        set_tests_properties(dock_framework_demo_smoke PROPERTIES PASS_REGULAR_EXPRESSION "Demo Complete")
    endif()

    if (TARGET dock_layout_constraints_demo)
        add_test(NAME dock_layout_constraints_demo COMMAND $<TARGET_FILE:dock_layout_constraints_demo>)
        set_tests_properties(dock_layout_constraints_demo PROPERTIES PASS_REGULAR_EXPRESSION "ALL CHECKS PASSED")
    endif()

    if (TARGET dx12_demo)
        add_test(
            NAME dx12_event_automation
            COMMAND ${CMAKE_COMMAND} -E env DF_AUTOMATE_EVENTS=1 DF_EVENT_CONSOLE=0 DF_EVENT_VERBOSE=0 $<TARGET_FILE:dx12_demo>
        )
        set_tests_properties(dx12_event_automation PROPERTIES TIMEOUT 30)

        add_test(
            NAME dx12_event_widget_stress
            COMMAND ${CMAKE_COMMAND} -E env DF_AUTOMATE_EVENTS=1 DF_AUTOMATION_SCENARIO=widget_drag_stress DF_EVENT_CONSOLE=0 DF_EVENT_VERBOSE=0 $<TARGET_FILE:dx12_demo>
        )
        set_tests_properties(dx12_event_widget_stress PROPERTIES TIMEOUT 30)

        add_test(
            NAME dx12_event_splitter_stress
            COMMAND ${CMAKE_COMMAND} -E env DF_AUTOMATE_EVENTS=1 DF_AUTOMATION_SCENARIO=splitter_stress DF_EVENT_CONSOLE=0 DF_EVENT_VERBOSE=0 $<TARGET_FILE:dx12_demo>
        )
        set_tests_properties(dx12_event_splitter_stress PROPERTIES TIMEOUT 30)

        add_test(
            NAME dx12_event_resize_stress
            COMMAND ${CMAKE_COMMAND} -E env DF_AUTOMATE_EVENTS=1 DF_AUTOMATION_SCENARIO=resize_stress DF_EVENT_CONSOLE=0 DF_EVENT_VERBOSE=0 $<TARGET_FILE:dx12_demo>
        )
        set_tests_properties(dx12_event_resize_stress PROPERTIES TIMEOUT 45)

        add_test(
            NAME dx12_event_resize_crash_stress
            COMMAND ${CMAKE_COMMAND} -E env DF_AUTOMATE_EVENTS=1 DF_AUTOMATION_SCENARIO=resize_crash_stress DF_EVENT_CONSOLE=0 DF_EVENT_VERBOSE=0 DF_RESIZE_DEBUG=1 $<TARGET_FILE:dx12_demo>
        )
        set_tests_properties(dx12_event_resize_crash_stress PROPERTIES TIMEOUT 60)

        add_test(
            NAME dx12_event_recursive_constraints_stress
            COMMAND ${CMAKE_COMMAND} -E env DF_AUTOMATE_EVENTS=1 DF_AUTOMATION_SCENARIO=recursive_constraints_stress DF_AUTOMATION_FUZZ_ITERS=24 DF_AUTOMATION_SEED=424242 DF_EVENT_CONSOLE=0 DF_EVENT_VERBOSE=0 $<TARGET_FILE:dx12_demo>
        )
        set_tests_properties(dx12_event_recursive_constraints_stress PROPERTIES TIMEOUT 60)

        add_test(
            NAME dx12_event_close_all
            COMMAND ${CMAKE_COMMAND} -E env DF_AUTOMATE_EVENTS=1 DF_AUTOMATION_SCENARIO=close_all DF_EVENT_CONSOLE=0 DF_EVENT_VERBOSE=0 $<TARGET_FILE:dx12_demo>
        )
        set_tests_properties(dx12_event_close_all PROPERTIES TIMEOUT 30)

        add_test(
            NAME dx12_event_global_float_sync
            COMMAND ${CMAKE_COMMAND} -E env DF_AUTOMATE_EVENTS=1 DF_AUTOMATION_SCENARIO=global_float_sync DF_EVENT_CONSOLE=0 DF_EVENT_VERBOSE=0 $<TARGET_FILE:dx12_demo>
        )
        set_tests_properties(dx12_event_global_float_sync PROPERTIES TIMEOUT 30)

        add_test(
            NAME dx12_event_host_transfer_stress
            COMMAND ${CMAKE_COMMAND} -E env DF_AUTOMATE_EVENTS=1 DF_AUTOMATION_SCENARIO=host_transfer_stress DF_EVENT_CONSOLE=0 DF_EVENT_VERBOSE=0 $<TARGET_FILE:dx12_demo>
        )
        set_tests_properties(dx12_event_host_transfer_stress PROPERTIES TIMEOUT 30)
    endif()
endif()
