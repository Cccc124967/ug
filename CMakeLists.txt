cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "ug requires Windows")
endif()
project(ug LANGUAGES CXX)
if(NOT MSVC)
    message(FATAL_ERROR "ug requires MSVC")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
add_library(core_module STATIC
    core_module/src/dev_core.cpp
    core_module/src/file_io.cpp
    core_module/src/git_control.cpp
    core_module/src/path_env.cpp
    core_module/src/project_files.cpp
)
target_include_directories(core_module PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(core_module PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
add_library(console_module STATIC
    console_module/src/console_element.cpp
    console_module/src/single_select_element.cpp
    console_module/src/multi_select_element.cpp
    console_module/src/input_element.cpp
    console_module/src/selector_element.cpp
    console_module/src/file_picker.cpp
    console_module/src/console_session.cpp
)
target_link_libraries(console_module PUBLIC core_module PRIVATE ole32 shell32 uuid user32)
add_library(prompt_module STATIC
    prompt_module/src/prompt_flow.cpp
    prompt_module/src/prompt_execution.cpp
)
target_link_libraries(prompt_module PUBLIC console_module)
add_library(scan_module STATIC
    scan_module/src/build_plan.cpp
    scan_module/src/tool_chain_scan.cpp
    scan_module/src/scan_cache.cpp
    scan_module/src/scan_service.cpp
    scan_module/src/scan_select.cpp
)
target_link_libraries(scan_module PUBLIC core_module PRIVATE advapi32)
add_library(template_module STATIC
    template_module/src/template_release.cpp
)
target_link_libraries(template_module PUBLIC core_module)
add_executable(ug
    app/main.cpp
    app/console_theme.cpp
    app/main_entry.cpp
    app/project_build.cpp
    app/register_command.cpp
    app/registration_record.cpp
    app/scan_command.cpp
    app/template_command.cpp
)
target_link_libraries(ug PRIVATE prompt_module scan_module template_module advapi32 user32)
foreach(target core_module console_module prompt_module scan_module template_module ug)
    target_compile_options(${target} PRIVATE /utf-8 /W4 /permissive- /EHsc)
endforeach()
# 可执行交付物默认放 bin；沙箱/CI 构建可覆盖输出目录，避免污染用户交付物。
set(NX_DEV_BIN_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH "ug 可执行文件输出目录")
set_target_properties(ug PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${NX_DEV_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${NX_DEV_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${NX_DEV_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${NX_DEV_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${NX_DEV_BIN_DIR}"
)
