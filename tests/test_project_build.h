#pragma once
// ============================================================================
// test_project_build.h — project_build 的落地自检（只写 agent_build/，不弹窗口）。
//
// 覆盖：模板源码 / CMakeLists / CMakePresets / .vscode 的落位与内容、生成器与
// 工具集拼接、Windows SDK 版本锁定、覆盖替换、失败时保留旧目录、空工程。
// 全部用 agent_build/ 下的合成夹具（假路径字符串），不读本机真实工具链。
// 由 app/main.cpp 调用，不自己写 main()。
// ============================================================================
#include "../app/project_build.h"
#include "../core_module/dev_core.h"
#include "../core_module/file_io.h"
#include "../core_module/git_control.h"
#include <windows.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace
{
int g_project_test_failures = 0 ;
int g_project_test_checks = 0 ;

/** 记一条断言结果；通过打 INFO，失败打 ERROR 并累计。 */
void project_test_report(bool passed, const std::string& what)
{
    ++g_project_test_checks ;
    if (passed)
    {
        std::cout << "[INFO] project: " << what << '\n' ;
        return ;
    }
    ++g_project_test_failures ;
    std::cerr << "[ERROR] project: " << what << '\n' ;
}

/** 布尔断言。 */
void project_test_check(bool passed, const std::string& what)
{
    project_test_report(passed, what) ;
}

/** 数量相等断言。 */
void project_test_number(std::size_t actual, std::size_t expected, const std::string& what)
{
    const bool passed = actual == expected ;
    project_test_report(passed, what) ;
    if (!passed)
    {
        std::cerr << "        期望: " << expected << "\n" ;
        std::cerr << "        实际: " << actual << "\n" ;
    }
}

/** 文本里是否包含一段内容。 */
bool project_test_contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos ;
}

/** 文本里是否完全没有 CR。 */
bool project_test_has_no_cr(const std::string& text)
{
    return text.find('\r') == std::string::npos ;
}

/** 文本是否是纯 CRLF 行尾：有换行且每个 LF 前面都有 CR。 */
bool project_test_is_crlf_only(const std::string& text)
{
    bool has_newline = false ;
    for (std::size_t index = 0 ; index < text.size() ; ++index)
    {
        if (text[index] != '\n')
        {
            continue ;
        }
        has_newline = true ;
        if (index == 0 || text[index - 1] != '\r')
        {
            return false ;
        }
    }
    return has_newline ;
}

/** 反斜杠换成正斜杠，便于和 CMake 文本比较。 */
std::string project_test_forward_slashes(const std::string& text)
{
    std::string result = text ;
    for (char& letter : result)
    {
        if (letter == '\\')
        {
            letter = '/' ;
        }
    }
    return result ;
}

/** 二进制读取整个文件；失败返回 false。 */
bool project_test_read_file(const std::filesystem::path& path, std::string& content)
{
    std::ifstream stream(path, std::ios::binary) ;
    if (!stream)
    {
        return false ;
    }
    content.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()) ;
    return true ;
}

/** 结果文件清单里是否有某个相对路径。 */
bool project_test_has_file(const std::vector<std::string>& files, const std::string& destination)
{
    for (const std::string& file : files)
    {
        if (file == destination)
        {
            return true ;
        }
    }
    return false ;
}

/** 从给定目录向上找 AGENTS.md 定位工程根。 */
bool project_test_find_workspace(const std::filesystem::path& start, std::filesystem::path& root)
{
    std::error_code error ;
    std::filesystem::path directory = start ;
    for (int level = 0 ; level < 8 && !directory.empty() ; ++level)
    {
        if (std::filesystem::is_regular_file(directory / L"AGENTS.md", error))
        {
            root = directory ;
            return true ;
        }
        const std::filesystem::path parent = directory.parent_path() ;
        if (parent.empty() || parent == directory)
        {
            break ;
        }
        directory = parent ;
    }
    return false ;
}

/** 工程根：优先从当前目录向上找，其次从 exe 所在目录向上找。 */
std::filesystem::path project_test_workspace_root()
{
    std::error_code error ;
    const std::filesystem::path current = std::filesystem::current_path(error) ;
    std::filesystem::path root ;
    if (!error && project_test_find_workspace(current, root))
    {
        return root ;
    }
    std::wstring buffer(32768, L'\0') ;
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size())) ;
    if (length > 0)
    {
        buffer.resize(length) ;
        if (project_test_find_workspace(std::filesystem::path(buffer).parent_path(), root))
        {
            return root ;
        }
    }
    return current ;
}

/** 合成夹具根目录；所有产物都落在这里。 */
std::filesystem::path project_test_fixture()
{
    return project_test_workspace_root() / "agent_build" / "tests" / "project_build_fixture" ;
}

/** 合成编译器夹具：全部是假路径字符串，不读本机真实工具链。 */
scan::Compiler project_test_compiler(const std::filesystem::path& fixture)
{
    scan::Compiler compiler ;
    compiler.m_name = "fixture MSVC v145" ;
    // 故意保留"旧口径"的枚举与完整版本：平台工具集只认 m_platform_toolset。
    compiler.m_version = scan::ToolChainVersion::V143 ;
    compiler.m_toolset_version = "14.44.99999" ;
    compiler.m_platform_toolset = "v145" ;
    compiler.m_cl_path = fixture / "fake_toolchain" / "VC" / "bin" / "Hostx64" / "x64" / "cl.exe" ;
    compiler.m_vcvars_path = fixture / "fake_toolchain" / "VC" / "Auxiliary" / "Build" / "vcvars64.bat" ;
    compiler.m_generator = "Visual Studio 99 Test" ;
    compiler.m_is_checked = true ;
    return compiler ;
}

/** 合成 NX SDK 夹具：假头文件根与假库目录。 */
scan::NXSdk project_test_nx_sdk(const std::filesystem::path& fixture)
{
    scan::NXSdk sdk ;
    sdk.m_version = "2007" ;
    sdk.m_version_source = "fixture" ;
    sdk.m_root = fixture / "fake_nx" ;
    sdk.m_uf_header_dir = sdk.m_root / "UGOPEN" ;
    sdk.m_nxopen_header_dir = sdk.m_root / "NXOPEN" ;
    sdk.m_lib_dir = sdk.m_root / "lib" ;
    sdk.m_is_checked = true ;
    return sdk ;
}

/** 合成 Windows SDK 夹具版本。 */
scan::WindowsSdk project_test_windows_sdk()
{
    scan::WindowsSdk sdk ;
    sdk.m_version = "10.9.8888.0" ;
    sdk.m_source = "fixture" ;
    sdk.m_is_checked = true ;
    return sdk ;
}

/** 默认请求：入口 + 四元数库、UTF-8 + LF、合成工具链、不初始化 git。 */
app::ProjectBuildRequest project_test_request(const std::filesystem::path& fixture)
{
    app::ProjectBuildRequest request ;
    request.m_parent_directory = fixture / "out" ;
    request.m_project_name = "DemoPlugin" ;
    request.m_template_modules = { template_release::TemplateModule::EntryCode ,
        template_release::TemplateModule::QuaternionLib } ;
    request.m_coding = ctx::Coding::UTF8 ;
    request.m_line_ending = ctx::LineEnding::LF ;
    request.m_init_git = false ;
    request.m_compiler = project_test_compiler(fixture) ;
    request.m_nx_sdk = project_test_nx_sdk(fixture) ;
    request.m_windows_sdk = project_test_windows_sdk() ;
    request.m_generator = "Visual Studio 99 Test" ;
    return request ;
}

/** 全新生成：文件落位、CMakeLists / Presets / .vscode 内容。 */
void project_test_fresh_build(const std::filesystem::path& fixture)
{
    const app::ProjectBuildRequest request = project_test_request(fixture) ;
    const app::ProjectBuildResult result = app::build_project(request) ;
    const std::filesystem::path target = fixture / "out" / "DemoPlugin" ;

    project_test_check(result.m_ok, "fresh: 生成成功") ;
    project_test_check(result.m_project_directory == target, "fresh: 返回项目目录正确") ;
    project_test_check(project_test_contains(result.m_message, "已生成工程"), "fresh: 成功消息可读") ;
    project_test_check(!std::filesystem::exists(fixture / "out" / ".DemoPlugin.nx_dev_tmp"),
        "fresh: 临时目录已清理") ;
    project_test_number(result.m_files.size(), 8, "fresh: 文件清单 8 项") ;

    const std::vector<std::string> expected_files = { "templates/basic_type_defs.h" , "main.cpp" ,
        "templates/vector.hpp" , "templates/matrix.hpp" , "templates/quaternion.hpp" ,
        "CMakeLists.txt" , "CMakePresets.json" , ".vscode/settings.json" } ;
    for (const std::string& destination : expected_files)
    {
        project_test_check(project_test_has_file(result.m_files, destination),
            "fresh: 文件清单含 " + destination) ;
        project_test_check(std::filesystem::is_regular_file(target / core::utf8_to_wide(destination)),
            "fresh: 磁盘上有 " + destination) ;
    }
    project_test_check(!std::filesystem::exists(target / L"templates" / L"output.hpp"),
        "fresh: 没勾守卫宏就不出 output.hpp") ;
    project_test_check(!std::filesystem::exists(target / L"templates" / L"point.hpp"),
        "fresh: 没勾点库就不出 point.hpp") ;

    std::string cmake_text ;
    project_test_check(project_test_read_file(target / L"CMakeLists.txt", cmake_text),
        "fresh: 读得到 CMakeLists.txt") ;
    project_test_check(project_test_contains(cmake_text, "project(\"DemoPlugin\""),
        "fresh: CMakeLists 项目名") ;
    project_test_check(project_test_contains(cmake_text, "\"main.cpp\""),
        "fresh: CMakeLists 源文件是 main.cpp") ;
    project_test_check(project_test_contains(cmake_text, "\"${CMAKE_CURRENT_SOURCE_DIR}/templates\""),
        "fresh: CMakeLists include 根含 templates/") ;
    project_test_check(project_test_contains(cmake_text, project_test_forward_slashes(
        core::path_text(fixture / "fake_nx" / "UGOPEN"))), "fresh: CMakeLists include 根含 NX uf 头根") ;
    project_test_check(project_test_contains(cmake_text, project_test_forward_slashes(
        core::path_text(fixture / "fake_nx" / "NXOPEN"))), "fresh: CMakeLists include 根含 NXOpen 头根") ;
    project_test_check(project_test_contains(cmake_text, project_test_forward_slashes(
        core::path_text(fixture / "fake_nx" / "lib"))), "fresh: CMakeLists 库目录是 NX SDK lib") ;
    project_test_check(project_test_contains(cmake_text, "/utf-8"),
        "fresh: CMakeLists 源码编码是 UTF-8") ;
    project_test_check(project_test_contains(cmake_text, "WINDOWS_EXPORT_ALL_SYMBOLS ON"),
        "fresh: CMakeLists 开启导出全部符号") ;
    project_test_check(project_test_contains(cmake_text, "CMAKE_CXX_STANDARD 20"),
        "fresh: CMakeLists 用 C++20") ;
    project_test_check(project_test_has_no_cr(cmake_text), "fresh: CMakeLists 是 LF") ;

    std::string presets_text ;
    project_test_check(project_test_read_file(target / L"CMakePresets.json", presets_text),
        "fresh: 读得到 CMakePresets.json") ;
    project_test_check(project_test_contains(presets_text, "\"generator\": \"Visual Studio 99 Test\""),
        "fresh: Presets 生成器来自请求") ;
    project_test_check(project_test_contains(presets_text, "\"architecture\": \"x64\""),
        "fresh: Presets 架构是 x64") ;
    project_test_check(project_test_contains(presets_text, "\"toolset\": \"v145\""),
        "fresh: Presets 工具集直接用 m_platform_toolset") ;
    project_test_check(!project_test_contains(presets_text, "v143"),
        "fresh: Presets 不再按 m_version 枚举映射") ;
    project_test_check(!project_test_contains(presets_text, ",version="),
        "fresh: Presets 不再拼 version=") ;
    project_test_check(project_test_contains(presets_text,
        "\"CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION\": \"10.9.8888.0\""),
        "fresh: Presets 锁定 Windows SDK 版本") ;
    project_test_check(project_test_contains(presets_text, "\"name\": \"nx\""),
        "fresh: configure 预设名是 nx") ;
    project_test_check(project_test_contains(presets_text, "\"name\": \"nx-release\""),
        "fresh: build 预设名是 nx-release") ;
    project_test_check(project_test_contains(presets_text, "\"configurePreset\": \"nx\""),
        "fresh: build 预设引用 nx") ;
    project_test_check(project_test_contains(presets_text, "\"configuration\": \"Release\""),
        "fresh: Presets 是 Release") ;
    project_test_check(project_test_contains(presets_text, "\"binaryDir\": \"${sourceDir}/build\""),
        "fresh: Presets 构建目录是 ${sourceDir}/build") ;
    project_test_check(project_test_has_no_cr(presets_text), "fresh: Presets 是 LF") ;

    std::string settings_text ;
    project_test_check(project_test_read_file(target / L".vscode" / L"settings.json", settings_text),
        "fresh: 读得到 .vscode/settings.json") ;
    project_test_check(!project_test_contains(settings_text, "cmake.generator"),
        "fresh: 没探测到 Ninja 时不写 cmake.generator") ;
    project_test_check(project_test_contains(settings_text, "\"files.exclude\": {"),
        "fresh: settings 固定写 files.exclude") ;
    const char* const excluded_entries[] = { "\"build\": true" , "\".gitattributes\": true" ,
        "\".gitignore\": true" , "\".cache\": true" , "\".git\": true" ,
        "\"CMakePresets.json\": true" , "\".vscode\": true" } ;
    for (const char* entry : excluded_entries)
    {
        project_test_check(project_test_contains(settings_text, entry),
            std::string("fresh: files.exclude 隐藏 ") + entry) ;
    }
    project_test_check(project_test_contains(settings_text, "\"files.encoding\": \"utf8\""),
        "fresh: settings 源码编码是 utf8") ;
    project_test_check(project_test_contains(settings_text, "\"files.eol\": \"\\n\""),
        "fresh: settings 行尾是 LF") ;
    project_test_check(project_test_has_no_cr(settings_text), "fresh: settings 是 LF") ;

    std::string main_text ;
    project_test_check(project_test_read_file(target / L"main.cpp", main_text), "fresh: 读得到 main.cpp") ;
    project_test_check(project_test_contains(main_text, "ufusr"), "fresh: main.cpp 有入口函数") ;
    project_test_check(project_test_has_no_cr(main_text), "fresh: main.cpp 是 LF") ;
}

/** 探测到 Ninja 时：settings 写 cmake.generator=Ninja，CMakePresets 仍用 VS 生成器。 */
void project_test_ninja_settings(const std::filesystem::path& fixture)
{
    app::ProjectBuildRequest request = project_test_request(fixture) ;
    request.m_project_name = "NinjaPlugin" ;
    request.m_ninja_available = true ;
    const app::ProjectBuildResult result = app::build_project(request) ;
    const std::filesystem::path target = fixture / "out" / "NinjaPlugin" ;

    project_test_check(result.m_ok, "ninja: 生成成功") ;

    std::string settings_text ;
    project_test_check(project_test_read_file(target / L".vscode" / L"settings.json", settings_text),
        "ninja: 读得到 .vscode/settings.json") ;
    project_test_check(project_test_contains(settings_text, "\"cmake.generator\": \"Ninja\""),
        "ninja: settings 写 cmake.generator=Ninja") ;
    project_test_check(!project_test_contains(settings_text, "Visual Studio 99 Test"),
        "ninja: settings 不再写 VS 生成器") ;
    project_test_check(project_test_contains(settings_text, "\"files.exclude\": {"),
        "ninja: settings 仍写 files.exclude") ;

    std::string presets_text ;
    project_test_check(project_test_read_file(target / L"CMakePresets.json", presets_text),
        "ninja: 读得到 CMakePresets.json") ;
    project_test_check(project_test_contains(presets_text, "\"generator\": \"Visual Studio 99 Test\""),
        "ninja: CMakePresets 仍用 VS 生成器") ;
}

/** 构建方案：自动生成 toolchain、现成 toolchain、NMake 回退三种落盘形态。 */
void project_test_build_plans(const std::filesystem::path& fixture)
{
    const std::filesystem::path toolset = fixture / "portable" / "14.29.99999" ;
    const std::filesystem::path toolset_bin = toolset / L"bin" / L"HostX64" / L"x64" ;
    std::string error ;
    project_test_check(core::ensure_directory(toolset_bin, error) &&
        core::ensure_directory(toolset / L"include", error) &&
        core::ensure_directory(toolset / L"lib" / L"x64", error), "plan: 创建便携工具集夹具") ;
    for (const wchar_t* name : { L"cl.exe" , L"link.exe" , L"lib.exe" , L"nmake.exe" })
    {
        project_test_check(core::write_text_file(toolset_bin / name, "", error),
            "plan: 创建 " + core::wide_to_utf8(name)) ;
    }
    const std::filesystem::path windows_sdk_root = fixture / "fake_winsdk" ;
    const std::filesystem::path win_include = windows_sdk_root / L"Include" / L"10.9.8888.0" ;
    const std::filesystem::path win_lib = windows_sdk_root / L"Lib" / L"10.9.8888.0" ;
    const std::filesystem::path win_bin = windows_sdk_root / L"bin" / L"10.9.8888.0" / L"x64" ;
    project_test_check(core::ensure_directory(win_include / L"um", error) &&
        core::ensure_directory(win_lib / L"um" / L"x64", error) &&
        core::ensure_directory(win_bin, error), "plan: 创建 Windows SDK 夹具") ;
    project_test_check(core::write_text_file(win_bin / L"rc.exe", "", error) &&
        core::write_text_file(win_bin / L"mt.exe", "", error), "plan: 创建 rc/mt 夹具") ;

    scan::Compiler compiler ;
    compiler.m_name = "MSVC v142 (fixture)" ;
    compiler.m_version = scan::ToolChainVersion::V142 ;
    compiler.m_toolset_version = "14.29.99999" ;
    compiler.m_cl_path = toolset_bin / L"cl.exe" ;
    compiler.m_include_dirs = { toolset / L"include" } ;
    compiler.m_lib_dirs = { toolset / L"lib" / L"x64" } ;
    compiler.m_is_checked = true ;
    compiler.m_kind = scan::ToolChainKind::Portable ;

    app::ProjectBuildRequest request = project_test_request(fixture) ;
    request.m_project_name = "PortablePlugin" ;
    request.m_compiler = compiler ;
    request.m_generator.clear() ;
    scan::WindowsSdk windows_sdk = project_test_windows_sdk() ;
    windows_sdk.m_include_dir = win_include ;
    windows_sdk.m_lib_dir = win_lib ;
    windows_sdk.m_bin_dir = win_bin ;
    request.m_windows_sdk = windows_sdk ;
    request.m_build_plan = scan::resolve_build_plan(compiler, true) ;
    project_test_check(request.m_build_plan.m_ok &&
        request.m_build_plan.m_kind == scan::BuildPlanKind::GeneratedToolchain,
        "plan: 便携编译器推出自动 toolchain 方案") ;

    const app::ProjectBuildResult result = app::build_project(request) ;
    const std::filesystem::path target = fixture / "out" / "PortablePlugin" ;
    project_test_check(result.m_ok, "plan: 自动 toolchain 工程生成成功") ;
    project_test_check(project_test_has_file(result.m_files, "cmake/nx_dev_toolchain.cmake"),
        "plan: 文件清单含自动 toolchain") ;
    std::string toolchain_text ;
    project_test_check(project_test_read_file(target / L"cmake" / L"nx_dev_toolchain.cmake",
        toolchain_text), "plan: 读得到自动 toolchain") ;
    project_test_check(project_test_contains(toolchain_text, "set(CMAKE_CXX_COMPILER"),
        "plan: toolchain 钉死编译器") ;
    project_test_check(project_test_contains(toolchain_text, "/I\\\""),
        "plan: toolchain 带头文件路径") ;
    project_test_check(project_test_contains(toolchain_text, "/LIBPATH:\\\""),
        "plan: toolchain 带库搜索路径") ;

    std::string presets_text ;
    project_test_check(project_test_read_file(target / L"CMakePresets.json", presets_text),
        "plan: 读得到自动方案的 Presets") ;
    project_test_check(project_test_contains(presets_text, "\"generator\": \"Ninja\""),
        "plan: 自动方案生成器是 Ninja") ;
    project_test_check(project_test_contains(presets_text,
        "\"toolchainFile\": \"${sourceDir}/cmake/nx_dev_toolchain.cmake\""),
        "plan: Presets 引用自动 toolchain") ;
    project_test_check(!project_test_contains(presets_text, "\"architecture\""),
        "plan: 单配置不写 architecture") ;
    project_test_check(!project_test_contains(presets_text, "\"toolset\""),
        "plan: 单配置不写 toolset") ;
    project_test_check(project_test_contains(presets_text, "\"CMAKE_BUILD_TYPE\": \"Release\""),
        "plan: 单配置写 CMAKE_BUILD_TYPE") ;

    std::string settings_text ;
    project_test_check(project_test_read_file(target / L".vscode" / L"settings.json", settings_text) &&
        project_test_contains(settings_text, "\"cmake.generator\": \"Ninja\""),
        "plan: settings 写 cmake.generator=Ninja") ;

    app::ProjectBuildRequest toolchain_request = request ;
    toolchain_request.m_project_name = "ToolchainPlugin" ;
    toolchain_request.m_compiler.m_vcvars_path = fixture / "devkit" / L"nx_toolchain.cmake" ;
    toolchain_request.m_build_plan = scan::resolve_build_plan(toolchain_request.m_compiler, true) ;
    project_test_check(toolchain_request.m_build_plan.m_kind == scan::BuildPlanKind::ToolchainFile,
        "plan: 自带 toolchain 文件方案类型正确") ;
    const app::ProjectBuildResult toolchain_result = app::build_project(toolchain_request) ;
    project_test_check(toolchain_result.m_ok, "plan: 自带 toolchain 工程生成成功") ;
    project_test_check(!project_test_has_file(toolchain_result.m_files, "cmake/nx_dev_toolchain.cmake"),
        "plan: 自带 toolchain 时不生成新文件") ;
    std::string toolchain_presets ;
    project_test_check(project_test_read_file(fixture / "out" / "ToolchainPlugin" / L"CMakePresets.json",
        toolchain_presets), "plan: 读得到自带 toolchain 的 Presets") ;
    project_test_check(project_test_contains(toolchain_presets, "nx_toolchain.cmake"),
        "plan: Presets 引用自带 toolchain") ;
    std::string toolchain_cmake_lists ;
    project_test_check(project_test_read_file(fixture / "out" / "ToolchainPlugin" / L"CMakeLists.txt",
        toolchain_cmake_lists), "plan: 读得到自带 toolchain 的 CMakeLists") ;
    project_test_check(!project_test_contains(toolchain_cmake_lists, "/source-charset") &&
        !project_test_contains(toolchain_cmake_lists, "/utf-8"),
        "plan: 自带 toolchain 时不再叠加源码字符集参数") ;

    // Ninja 不可用 → 静默回退 NMake，并把 nmake 路径写进预置。
    app::ProjectBuildRequest nmake_request = request ;
    nmake_request.m_project_name = "NMakePlugin" ;
    nmake_request.m_build_plan = scan::resolve_build_plan(compiler, false) ;
    project_test_check(nmake_request.m_build_plan.m_ok &&
        nmake_request.m_build_plan.m_generator == "NMake Makefiles",
        "plan: Ninja 不可用时静默回退 NMake") ;
    project_test_check(!nmake_request.m_build_plan.m_make_program.empty(),
        "plan: NMake 方案带 nmake 路径") ;
    const app::ProjectBuildResult nmake_result = app::build_project(nmake_request) ;
    project_test_check(nmake_result.m_ok, "plan: NMake 回退工程生成成功") ;
    std::string nmake_presets ;
    project_test_check(project_test_read_file(fixture / "out" / "NMakePlugin" / L"CMakePresets.json",
        nmake_presets), "plan: 读得到 NMake 方案的 Presets") ;
    project_test_check(project_test_contains(nmake_presets, "\"generator\": \"NMake Makefiles\""),
        "plan: NMake 方案生成器正确") ;
    project_test_check(project_test_contains(nmake_presets, "\"CMAKE_MAKE_PROGRAM\""),
        "plan: NMake 方案写 CMAKE_MAKE_PROGRAM") ;
    project_test_check(!project_test_contains(nmake_presets, "\"configuration\""),
        "plan: 单配置 buildPreset 不写 configuration") ;
}

/** 目标已存在时：不覆盖就失败并保留旧目录，覆盖就整体替换。 */
void project_test_overwrite(const std::filesystem::path& fixture)
{
    const std::filesystem::path target = fixture / "out" / "OverwritePlugin" ;
    std::error_code filesystem_error ;
    std::filesystem::create_directories(target, filesystem_error) ;
    std::string write_error ;
    project_test_check(core::write_text_file(target / L"stale.txt", "stale", write_error),
        "overwrite: 预置旧目录内容") ;

    app::ProjectBuildRequest request = project_test_request(fixture) ;
    request.m_project_name = "OverwritePlugin" ;
    const app::ProjectBuildResult refused = app::build_project(request) ;
    project_test_check(!refused.m_ok, "overwrite: 未勾选覆盖时失败") ;
    project_test_check(project_test_contains(refused.m_message, "已存在"),
        "overwrite: 失败原因说明目标已存在") ;
    project_test_check(refused.m_files.empty(), "overwrite: 失败时无文件清单") ;
    project_test_check(std::filesystem::exists(target / L"stale.txt"), "overwrite: 失败时旧目录原封不动") ;
    project_test_check(!std::filesystem::exists(fixture / "out" / ".OverwritePlugin.nx_dev_tmp"),
        "overwrite: 失败时不留临时目录") ;

    request.m_overwrite = true ;
    const app::ProjectBuildResult replaced = app::build_project(request) ;
    project_test_check(replaced.m_ok, "overwrite: 勾选覆盖后成功") ;
    project_test_check(!std::filesystem::exists(target / L"stale.txt"), "overwrite: 旧内容被整体替换") ;
    project_test_check(std::filesystem::exists(target / L"main.cpp"), "overwrite: 新工程文件已就位") ;
    project_test_check(!std::filesystem::exists(fixture / "out" / ".OverwritePlugin.nx_dev_tmp"),
        "overwrite: 替换后不留临时目录") ;
    project_test_check(!std::filesystem::exists(fixture / "out" / ".OverwritePlugin.nx_dev_old"),
        "overwrite: 替换成功后备份目录已删除") ;
    project_test_check(!project_test_contains(replaced.m_message, "警告"),
        "overwrite: 正常替换没有警告") ;

    // 模拟上次替换中途崩溃留下的备份：新一次替换要先清掉它。
    const std::filesystem::path stale_target = fixture / "out" / "StaleBackupPlugin" ;
    const std::filesystem::path stale_backup = fixture / "out" / ".StaleBackupPlugin.nx_dev_old" ;
    std::filesystem::create_directories(stale_target / L"oldproject", filesystem_error) ;
    std::filesystem::create_directories(stale_backup / L"crash_leftover", filesystem_error) ;
    project_test_check(core::write_text_file(stale_target / L"stale.txt", "stale", write_error) &&
        core::write_text_file(stale_backup / L"junk.txt", "junk", write_error),
        "overwrite: 预置残留备份目录") ;
    app::ProjectBuildRequest stale_request = project_test_request(fixture) ;
    stale_request.m_project_name = "StaleBackupPlugin" ;
    stale_request.m_overwrite = true ;
    const app::ProjectBuildResult stale_result = app::build_project(stale_request) ;
    project_test_check(stale_result.m_ok, "overwrite: 有残留备份时替换照常成功") ;
    project_test_check(!std::filesystem::exists(stale_backup), "overwrite: 残留备份被清掉") ;
    project_test_check(std::filesystem::exists(stale_target / L"main.cpp"),
        "overwrite: 残留备份场景下新工程已就位") ;
}

/** 替换阶段失败（旧目录里有被占用文件，备份改名失败）：旧目录内容必须原封不动。 */
void project_test_failure_keeps_old(const std::filesystem::path& fixture)
{
    const std::filesystem::path target = fixture / "out" / "LockedPlugin" ;
    const std::filesystem::path backup = fixture / "out" / ".LockedPlugin.nx_dev_old" ;
    const std::filesystem::path temp = fixture / "out" / ".LockedPlugin.nx_dev_tmp" ;
    std::error_code filesystem_error ;
    std::filesystem::create_directories(target / L"oldproject", filesystem_error) ;
    std::string write_error ;
    project_test_check(core::write_text_file(target / L"keep.txt", "keep", write_error) &&
        core::write_text_file(target / L"second.txt", "second", write_error) &&
        core::write_text_file(target / L"oldproject" / L"nested.txt", "nested", write_error),
        "failure: 预置旧目录内容") ;

    // 不共享的打开方式让"旧目录改名备份"失败，模拟替换阶段旧工程被占用。
    const std::filesystem::path marker = target / L"keep.txt" ;
    HANDLE locked = CreateFileW(marker.wstring().c_str(), GENERIC_READ, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) ;
    project_test_check(locked != INVALID_HANDLE_VALUE, "failure: 能锁住旧目录里的文件") ;
    if (locked == INVALID_HANDLE_VALUE)
    {
        return ;
    }

    app::ProjectBuildRequest request = project_test_request(fixture) ;
    request.m_project_name = "LockedPlugin" ;
    request.m_overwrite = true ;
    const app::ProjectBuildResult result = app::build_project(request) ;
    CloseHandle(locked) ;

    std::string keep_text ;
    std::string second_text ;
    std::string nested_text ;
    project_test_check(!result.m_ok, "failure: 备份改名失败时整体失败") ;
    project_test_check(project_test_contains(result.m_message, "旧目录未改动"),
        "failure: 失败原因说明旧目录未改动") ;
    project_test_check(result.m_files.empty(), "failure: 失败时无文件清单") ;
    project_test_check(std::filesystem::is_directory(target), "failure: 旧目标目录还在") ;
    project_test_check(project_test_read_file(marker, keep_text) && keep_text == "keep",
        "failure: 旧文件 keep.txt 内容原样") ;
    project_test_check(project_test_read_file(target / L"second.txt", second_text) &&
        second_text == "second", "failure: 旧文件 second.txt 内容原样") ;
    project_test_check(project_test_read_file(target / L"oldproject" / L"nested.txt", nested_text) &&
        nested_text == "nested", "failure: 旧子目录里的文件内容原样") ;
    project_test_check(!std::filesystem::exists(target / L"main.cpp"),
        "failure: 失败时不写入新工程文件") ;
    project_test_check(!std::filesystem::exists(temp), "failure: 失败后清理临时目录") ;
    project_test_check(!std::filesystem::exists(backup), "failure: 失败后不残留备份目录") ;
}

/** 空工程：不写源码与 CMakeLists；没有生成器就不写 CMakePresets。 */
void project_test_empty_project(const std::filesystem::path& fixture)
{
    app::ProjectBuildRequest request = project_test_request(fixture) ;
    request.m_project_name = "EmptyProject" ;
    request.m_template_modules.clear() ;
    request.m_generator.clear() ;
    request.m_compiler.m_generator.clear() ;
    const app::ProjectBuildResult result = app::build_project(request) ;
    const std::filesystem::path target = fixture / "out" / "EmptyProject" ;

    project_test_check(result.m_ok, "empty: 空工程生成成功") ;
    project_test_number(result.m_files.size(), 1, "empty: 只有 settings 一个文件") ;
    project_test_check(!std::filesystem::exists(target / L"main.cpp"), "empty: 不写源码") ;
    project_test_check(!std::filesystem::exists(target / L"CMakeLists.txt"), "empty: 不写 CMakeLists") ;
    project_test_check(!std::filesystem::exists(target / L"CMakePresets.json"),
        "empty: 没有生成器就不写 CMakePresets") ;
    std::string settings_text ;
    project_test_check(project_test_read_file(target / L".vscode" / L"settings.json", settings_text),
        "empty: 仍写 .vscode/settings.json") ;
    project_test_check(!project_test_contains(settings_text, "cmake.generator"),
        "empty: 生成器为空时不写 cmake.generator") ;
    project_test_check(project_test_contains(settings_text, "\"files.exclude\": {"),
        "empty: settings 仍写 files.exclude") ;

    app::ProjectBuildRequest preset_request = project_test_request(fixture) ;
    preset_request.m_project_name = "EmptyWithPresets" ;
    preset_request.m_template_modules.clear() ;
    // 平台工具集未知：Presets 里应省略 toolset 字段，而不是瞎猜一个。
    preset_request.m_compiler.m_platform_toolset.clear() ;
    const app::ProjectBuildResult preset_result = app::build_project(preset_request) ;
    project_test_check(preset_result.m_ok, "empty: 有生成器时空工程也成功") ;
    const std::filesystem::path preset_path = fixture / "out" / "EmptyWithPresets" / L"CMakePresets.json" ;
    project_test_check(std::filesystem::exists(preset_path), "empty: 有生成器就写 CMakePresets") ;
    std::string preset_text ;
    project_test_check(project_test_read_file(preset_path, preset_text), "empty: 读得到 CMakePresets.json") ;
    project_test_check(!project_test_contains(preset_text, "\"toolset\""),
        "empty: 平台工具集为空时 Presets 不写 toolset") ;
}

/** GB2312 + CRLF：源码转码与行尾；生成器回退到编译器扫描结果。 */
void project_test_coding_line_ending(const std::filesystem::path& fixture)
{
    app::ProjectBuildRequest request = project_test_request(fixture) ;
    request.m_project_name = "GbPlugin" ;
    request.m_template_modules = { template_release::TemplateModule::EntryCode ,
        template_release::TemplateModule::MatrixLib } ;
    request.m_coding = ctx::Coding::GB2312 ;
    request.m_line_ending = ctx::LineEnding::CRLF ;
    request.m_generator.clear() ;
    request.m_compiler.m_generator = "Visual Studio 98 Fallback" ;
    const app::ProjectBuildResult result = app::build_project(request) ;
    const std::filesystem::path target = fixture / "out" / "GbPlugin" ;

    project_test_check(result.m_ok, "coding: GB2312 + CRLF 工程生成成功") ;
    project_test_number(result.m_files.size(), 7, "coding: 文件清单 7 项") ;

    std::string cmake_text ;
    project_test_check(project_test_read_file(target / L"CMakeLists.txt", cmake_text),
        "coding: 读得到 CMakeLists.txt") ;
    project_test_check(project_test_contains(cmake_text, "/source-charset:.936"),
        "coding: CMakeLists 用 GB2312 源码字符集") ;
    project_test_check(project_test_has_no_cr(cmake_text), "coding: CMakeLists 始终是 LF") ;

    std::string presets_text ;
    project_test_check(project_test_read_file(target / L"CMakePresets.json", presets_text),
        "coding: 读得到 CMakePresets.json") ;
    project_test_check(project_test_contains(presets_text, "\"generator\": \"Visual Studio 98 Fallback\""),
        "coding: 生成器回退到编译器扫描结果") ;

    std::string settings_text ;
    project_test_check(project_test_read_file(target / L".vscode" / L"settings.json", settings_text),
        "coding: 读得到 .vscode/settings.json") ;
    project_test_check(!project_test_contains(settings_text, "cmake.generator"),
        "coding: settings 不写 VS 生成器（Ninja 未探测到）") ;
    project_test_check(project_test_contains(settings_text, "\"files.exclude\": {"),
        "coding: settings 仍写 files.exclude") ;
    project_test_check(project_test_contains(settings_text, "\"files.encoding\": \"gb2312\""),
        "coding: settings 源码编码是 gb2312") ;
    project_test_check(project_test_contains(settings_text, "\"files.eol\": \"\\r\\n\""),
        "coding: settings 行尾是 CRLF") ;
    project_test_check(project_test_is_crlf_only(settings_text), "coding: settings 是 CRLF") ;

    std::string main_text ;
    project_test_check(project_test_read_file(target / L"main.cpp", main_text), "coding: 读得到 main.cpp") ;
    project_test_check(project_test_is_crlf_only(main_text), "coding: main.cpp 是 CRLF") ;
    project_test_check(project_test_contains(main_text, std::string("\xBB\xB6\xD3\xAD")),
        "coding: main.cpp 是 GB2312 字节") ;
    project_test_check(!project_test_contains(main_text, std::string("\xE6\xAC\xA2")),
        "coding: main.cpp 不再是 UTF-8 字节") ;
}

/** git 接口签名：空目录直接失败，不会真的去调本机 git。 */
void project_test_git_signature()
{
    const core::GitInitResult result =
        core::initialize_git_repository(std::filesystem::path(), ctx::LineEnding::LF) ;
    project_test_check(!result.m_ok, "git: 空目录初始化失败") ;
    project_test_check(!result.m_message.empty(), "git: 失败有原因") ;
}

/** 清掉夹具目录；没有也算成功。 */
void project_test_clean_fixture(const std::filesystem::path& fixture)
{
    std::error_code error ;
    std::filesystem::remove_all(fixture, error) ;
}
} // namespace

/** project_build 的自动自检；返回失败条数（0 = 全部通过）。 */
inline int test_project_build()
{
    g_project_test_failures = 0 ;
    g_project_test_checks = 0 ;

    const std::filesystem::path fixture = project_test_fixture() ;
    project_test_clean_fixture(fixture) ;

    project_test_fresh_build(fixture) ;
    project_test_ninja_settings(fixture) ;
    project_test_build_plans(fixture) ;
    project_test_overwrite(fixture) ;
    project_test_failure_keeps_old(fixture) ;
    project_test_empty_project(fixture) ;
    project_test_coding_line_ending(fixture) ;
    project_test_git_signature() ;

    project_test_clean_fixture(fixture) ;

    if (g_project_test_failures == 0)
    {
        std::cout << "[INFO] project_build 自检通过: " << g_project_test_checks
                  << " 项断言全部通过\n" ;
    }
    else
    {
        std::cerr << "[ERROR] project_build 自检失败: " << g_project_test_failures << " / "
                  << g_project_test_checks << " 项\n" ;
    }
    return g_project_test_failures ;
}
