#pragma once
// ============================================================================
// test_scan_select.h — 扫描选型规则与选型记忆的自动测试。
//
// 由 app/main.cpp 的 main() 调 test_scan_select()。全部夹具在运行时合成到
// agent_build/tests/fixtures 下，不读取本机真实 VS/NX/Windows SDK。
// ============================================================================
#include "../scan_module/scan_select.h"
#include "../scan_module/src/scan_internal.h"
#include <windows.h>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace test_scan_select_detail
{
/** 本组测试的失败次数。 */
inline int& failures()
{
    static int count = 0 ;
    return count ;
}

/** 断言；失败时打印并计数。 */
inline void check(bool condition, const std::string& label)
{
    if (!condition)
    {
        ++failures() ;
        std::cerr << "[FAIL] test_scan_select: " << label << '\n' ;
    }
}

/** 读取一个环境变量；不存在返回空。 */
inline std::wstring read_environment(const wchar_t* name)
{
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0) ;
    if (size <= 1)
    {
        return {} ;
    }
    std::wstring value(size, L'\0') ;
    const DWORD written = GetEnvironmentVariableW(name, value.data(), size) ;
    if (written == 0 || written >= size)
    {
        return {} ;
    }
    value.resize(written) ;
    return value ;
}

/** 临时改写一个环境变量，析构时还原；空新值表示删除。 */
class EnvironmentGuard
{
    public:
    /** 记录旧值并写入新值。 */
    EnvironmentGuard(const wchar_t* name, const std::wstring& new_value) : m_name(name)
    {
        m_had_old_value = GetEnvironmentVariableW(name, nullptr, 0) > 1 ;
        m_old_value = read_environment(name) ;
        SetEnvironmentVariableW(name, new_value.empty() ? nullptr : new_value.c_str()) ;
    }

    ~EnvironmentGuard()
    {
        SetEnvironmentVariableW(m_name.c_str(), m_had_old_value ? m_old_value.c_str() : nullptr) ;
    }

    EnvironmentGuard(const EnvironmentGuard&) = delete ;
    EnvironmentGuard& operator=(const EnvironmentGuard&) = delete ;

    private:
    std::wstring m_name ;
    std::wstring m_old_value ;
    bool m_had_old_value = false ;
} ;

/** 测试夹具根：从当前目录向上找项目根，落在 agent_build/tests/fixtures 下。 */
inline scan::Path fixture_root()
{
    std::error_code error ;
    scan::Path directory = std::filesystem::current_path(error) ;
    for (int level = 0 ; level < 8 && !directory.empty() ; ++level)
    {
        if (std::filesystem::is_regular_file(directory / L"AGENTS.md", error) &&
            std::filesystem::is_directory(directory / L"agent_build", error))
        {
            return directory / L"agent_build" / L"tests" / L"fixtures" ;
        }
        const scan::Path parent = directory.parent_path() ;
        if (parent.empty() || parent == directory)
        {
            break ;
        }
        directory = parent ;
    }
    return std::filesystem::current_path(error) / L"agent_build" / L"tests" / L"fixtures" ;
}

/** 造一个编译器条目；路径只是夹具文本，不指向本机安装。 */
inline scan::Compiler make_compiler(scan::ToolChainVersion version, const std::string& toolset_version,
    const std::string& cl_text, bool checked)
{
    scan::Compiler compiler ;
    compiler.m_name = "MSVC test" ;
    compiler.m_version = version ;
    compiler.m_toolset_version = toolset_version ;
    compiler.m_cl_path = scan::path_from_text(cl_text) ;
    compiler.m_is_checked = checked ;
    return compiler ;
}

/** 造一个 NX SDK 条目；路径只是夹具文本。 */
inline scan::NXSdk make_nx_sdk(const std::string& version, const std::string& root_text,
    bool checked, bool running)
{
    scan::NXSdk sdk ;
    sdk.m_version = version ;
    sdk.m_root = scan::path_from_text(root_text) ;
    sdk.m_is_checked = checked ;
    sdk.m_is_running = running ;
    return sdk ;
}

/** 造一个 Windows SDK 条目；路径只是夹具文本。 */
inline scan::WindowsSdk make_windows_sdk(const std::string& version, const std::string& root_text,
    bool checked)
{
    scan::WindowsSdk sdk ;
    sdk.m_version = version ;
    sdk.m_source = "test" ;
    sdk.m_root = scan::path_from_text(root_text) ;
    sdk.m_is_checked = checked ;
    return sdk ;
}

/** 覆盖自动选编译器：优先级、同类取最高、未校验过滤与 V145 警告。 */
inline void test_pick_compiler()
{
    std::string warning ;
    check(!scan::pick_compiler({}, warning).has_value(), "空列表不选编译器") ;

    const std::vector<scan::Compiler> unchecked = {
        make_compiler(scan::ToolChainVersion::V142, "70.1.1000", "fixture/unchecked/cl.exe", false) ,
    } ;
    check(!scan::pick_compiler(unchecked, warning).has_value(), "未校验编译器不参与自动选型") ;

    const std::vector<scan::Compiler> v145_only = {
        make_compiler(scan::ToolChainVersion::V145, "70.3.3000", "fixture/v145/cl.exe", true) ,
    } ;
    const auto picked_v145 = scan::pick_compiler(v145_only, warning) ;
    check(picked_v145.has_value() && picked_v145->m_version == scan::ToolChainVersion::V145,
        "只有 v145 时选中 v145") ;
    check(!warning.empty(), "选中 v145 必须写警告") ;

    const std::vector<scan::Compiler> mixed = {
        make_compiler(scan::ToolChainVersion::V143, "70.2.2000", "fixture/v143/cl.exe", true) ,
        make_compiler(scan::ToolChainVersion::V142, "70.1.1000", "fixture/v142/cl.exe", true) ,
        make_compiler(scan::ToolChainVersion::V145, "70.3.3000", "fixture/v145b/cl.exe", true) ,
    } ;
    const auto picked_mixed = scan::pick_compiler(mixed, warning) ;
    check(picked_mixed.has_value() && picked_mixed->m_version == scan::ToolChainVersion::V142,
        "v142 优先于 v143 与 v145") ;
    check(warning.empty(), "选中 v142 不写警告") ;

    const std::vector<scan::Compiler> same_kind = {
        make_compiler(scan::ToolChainVersion::V143, "70.2.2000", "fixture/v143-low/cl.exe", true) ,
        make_compiler(scan::ToolChainVersion::V143, "70.2.2001", "fixture/v143-high/cl.exe", true) ,
    } ;
    const auto picked_high = scan::pick_compiler(same_kind, warning) ;
    check(picked_high.has_value() && picked_high->m_toolset_version == "70.2.2001",
        "同类取工具集完整版本最高") ;

    const std::vector<scan::Compiler> old = {
        make_compiler(scan::ToolChainVersion::V141, "70.0.100", "fixture/v141/cl.exe", true) ,
    } ;
    check(!scan::pick_compiler(old, warning).has_value(), "v141 不参与自动选型") ;
}

/** 覆盖自动选 NX SDK：运行中优先，其次有版本优先，再按版本降序；未校验过滤。 */
inline void test_pick_nx_sdk()
{
    check(!scan::pick_nx_sdk({}).has_value(), "空列表不选 NX SDK") ;

    const std::vector<scan::NXSdk> incomplete = {
        make_nx_sdk("2400", "fixture/nx/incomplete", false, true) ,
    } ;
    check(!scan::pick_nx_sdk(incomplete).has_value(), "未校验 NX SDK 不参与自动选型") ;

    const std::vector<scan::NXSdk> running = {
        make_nx_sdk("2007", "fixture/nx/running", true, true) ,
        make_nx_sdk("2400", "fixture/nx/newer", true, false) ,
    } ;
    const auto picked_running = scan::pick_nx_sdk(running) ;
    check(picked_running.has_value() && picked_running->m_version == "2007", "运行中的 NX SDK 优先") ;

    const std::vector<scan::NXSdk> versions = {
        make_nx_sdk("2007", "fixture/nx/old", true, false) ,
        make_nx_sdk("2400", "fixture/nx/new", true, false) ,
        make_nx_sdk("2200", "fixture/nx/mid", true, false) ,
    } ;
    const auto picked_newest = scan::pick_nx_sdk(versions) ;
    check(picked_newest.has_value() && picked_newest->m_version == "2400", "没在运行时取版本最高") ;

    // 版本探测失败但完整可用的 SDK 也参与选型，只是排在有版本之后。
    const std::vector<scan::NXSdk> unknown_versions = {
        make_nx_sdk("", "fixture/nx/unknown-a", true, false) ,
        make_nx_sdk("", "fixture/nx/unknown-b", true, false) ,
    } ;
    check(scan::pick_nx_sdk(unknown_versions).has_value(), "版本未知但可用的 NX SDK 仍可被选中") ;

    const std::vector<scan::NXSdk> known_before_unknown = {
        make_nx_sdk("", "fixture/nx/unknown", true, false) ,
        make_nx_sdk("2200", "fixture/nx/known", true, false) ,
    } ;
    const auto picked_known = scan::pick_nx_sdk(known_before_unknown) ;
    check(picked_known.has_value() && picked_known->m_version == "2200", "有版本优先于版本未知") ;

    const std::vector<scan::NXSdk> running_unknown = {
        make_nx_sdk("", "fixture/nx/running-unknown", true, true) ,
        make_nx_sdk("2400", "fixture/nx/idle-known", true, false) ,
    } ;
    const auto picked_running_unknown = scan::pick_nx_sdk(running_unknown) ;
    check(picked_running_unknown.has_value() &&
        scan::path_text(picked_running_unknown->m_root) == "fixture/nx/running-unknown",
        "运行中优先于有版本") ;
}

/** 覆盖自动选 Windows SDK：校验通过里取版本最高。 */
inline void test_pick_windows_sdk()
{
    check(!scan::pick_windows_sdk({}).has_value(), "空列表不选 Windows SDK") ;

    const std::vector<scan::WindowsSdk> sdks = {
        make_windows_sdk("10.0.26100.0", "fixture/kits/a", true) ,
        make_windows_sdk("10.0.99999.0", "fixture/kits/b", true) ,
        make_windows_sdk("10.0.99999.0", "fixture/kits/c", false) ,
    } ;
    const auto picked = scan::pick_windows_sdk(sdks) ;
    check(picked.has_value() && picked->m_version == "10.0.99999.0", "Windows SDK 取版本最高") ;

    const std::vector<scan::WindowsSdk> unknown_versions = {
        make_windows_sdk("", "fixture/kits/unknown-a", true) ,
        make_windows_sdk("", "fixture/kits/unknown-b", true) ,
    } ;
    check(!scan::pick_windows_sdk(unknown_versions).has_value(),
        "全部缺版本时返回空，由调用方按不锁版本处理") ;
}

/** 覆盖生成器名推导：只认动态证据，证据不足留空。 */
inline void test_generator()
{
    scan::Compiler compiler = make_compiler(scan::ToolChainVersion::V143, "70.2.2000",
        "fixture/gen/cl.exe", true) ;
    compiler.m_generator = "Visual Studio 99 2099" ;
    check(scan::cmake_generator_name(compiler) == "Visual Studio 99 2099", "生成器名原样返回") ;
    compiler.m_generator.clear() ;
    check(scan::cmake_generator_name(compiler).empty(), "没有生成器证据时留空") ;

    scan::engine::VisualStudioInstall install ;
    install.m_root = scan::path_from_text("fixture/Microsoft Visual Studio/2099/Community") ;
    install.m_version = "99.8.7" ;
    install.m_display_name = "Visual Studio Community 2099" ;
    check(scan::engine::generator_from_install(install) == "Visual Studio 99 2099",
        "从安装版本与显示名年份拼生成器") ;

    install = scan::engine::VisualStudioInstall{} ;
    install.m_root = scan::path_from_text("fixture/Microsoft Visual Studio/2077/Community") ;
    check(scan::engine::generator_from_install(install).empty(), "缺主版本时留空") ;

    install = scan::engine::VisualStudioInstall{} ;
    install.m_root = scan::path_from_text("fixture/Microsoft Visual Studio/88/Community") ;
    install.m_display_name = "Visual Studio Community 2077" ;
    check(scan::engine::generator_from_install(install) == "Visual Studio 88 2077",
        "主版本可从安装目录名推导") ;

    install = scan::engine::VisualStudioInstall{} ;
    install.m_root = scan::path_from_text("fixture/Community") ;
    check(scan::engine::generator_from_install(install).empty(), "无任何版本证据时留空") ;

    // 按安装根匹配补生成器：纯函数，用合成安装列表验证，不读本机安装。
    scan::engine::VisualStudioInstall known ;
    known.m_root = scan::path_from_text("fixture/Microsoft Visual Studio/2099/Community") ;
    known.m_version = "99.8.7" ;
    known.m_display_name = "Visual Studio Community 2099" ;
    const std::vector<scan::engine::VisualStudioInstall> installs = { known } ;
    check(scan::engine::generator_for_install(known.m_root, installs) == "Visual Studio 99 2099",
        "按安装根匹配补生成器") ;
    check(scan::engine::generator_for_install(
        scan::path_from_text("FIXTURE/MICROSOFT VISUAL STUDIO/2099/COMMUNITY"), installs) ==
        "Visual Studio 99 2099", "安装根匹配忽略大小写") ;
    check(scan::engine::generator_for_install(scan::path_from_text("fixture/other"), installs).empty(),
        "安装根匹配不到时留空") ;
    check(scan::engine::generator_for_install(known.m_root, {}).empty(), "空安装列表时留空") ;
}

/** 覆盖组合选型：必需项缺失写中文原因，Windows SDK 可选。 */
inline void test_pick_toolchain()
{
    scan::ScanReport report ;
    scan::Compiler compiler = make_compiler(scan::ToolChainVersion::V142, "70.1.1000",
        "fixture/toolchain/cl.exe", true) ;
    compiler.m_generator = "Visual Studio 88 2077" ;
    report.m_compilers.push_back(compiler) ;
    report.m_sdks.push_back(make_nx_sdk("2007", "fixture/toolchain/nx", true, false)) ;
    report.m_windows_sdks.push_back(make_windows_sdk("10.0.99999.0", "fixture/toolchain/kits", true)) ;

    const scan::ToolchainSelection complete = scan::pick_toolchain(report) ;
    check(complete.m_ok, "齐全时组合选型成功") ;
    check(complete.m_generator == "Visual Studio 88 2077", "组合选型带出生成器名") ;
    check(complete.m_windows_sdk.m_version == "10.0.99999.0", "组合选型带出 Windows SDK") ;
    check(complete.m_message.empty(), "成功时不写失败原因") ;

    // 没有 VS 生成器但有 cl：自动生成 toolchain + Ninja，不再是失败。
    scan::ScanReport no_generator_report = report ;
    no_generator_report.m_compilers[0].m_generator.clear() ;
    no_generator_report.m_compilers[0].m_cl_path = scan::path_from_text("fixture/toolchain/cl.exe") ;
    const scan::ToolchainSelection ninja_plan = scan::pick_toolchain(no_generator_report, true) ;
    check(ninja_plan.m_ok, "缺 VS 生成器但有 cl：Ninja 自动 toolchain 方案成功") ;
    check(ninja_plan.m_generator == "Ninja", "自动 toolchain 方案生成器是 Ninja") ;
    check(ninja_plan.m_build_plan.m_kind == scan::BuildPlanKind::GeneratedToolchain,
        "自动 toolchain 方案类型正确") ;
    check(ninja_plan.m_build_plan.m_single_config, "自动 toolchain 方案是单配置生成器") ;

    // Ninja 不可用且没有 cl 路径：直接失败，不猜。
    scan::ScanReport no_cl_report = report ;
    no_cl_report.m_compilers[0].m_cl_path.clear() ;
    const scan::ToolchainSelection no_cl = scan::pick_toolchain(no_cl_report, false) ;
    check(!no_cl.m_ok, "缺 cl 路径时组合选型失败") ;
    check(!no_cl.m_message.empty(), "缺 cl 路径时写失败原因") ;

    // 自带 toolchain 文件的便携工具链：直接引用该文件 + Ninja。
    scan::ScanReport toolchain_report = report ;
    toolchain_report.m_compilers[0].m_generator.clear() ;
    toolchain_report.m_compilers[0].m_cl_path = scan::path_from_text("fixture/devkit/MSVC/cl.exe") ;
    toolchain_report.m_compilers[0].m_vcvars_path = scan::path_from_text("fixture/devkit/nx_toolchain.cmake") ;
    const scan::ToolchainSelection toolchain_plan = scan::pick_toolchain(toolchain_report, true) ;
    check(toolchain_plan.m_ok, "自带 toolchain 文件时选型成功") ;
    check(toolchain_plan.m_build_plan.m_kind == scan::BuildPlanKind::ToolchainFile,
        "自带 toolchain 文件时方案类型正确") ;
    check(scan::path_text(toolchain_plan.m_build_plan.m_toolchain_file) ==
        scan::path_text(scan::path_from_text("fixture/devkit/nx_toolchain.cmake")),
        "方案带出 toolchain 文件路径") ;

    scan::ScanReport no_sdk_report = report ;
    no_sdk_report.m_sdks.clear() ;
    const scan::ToolchainSelection no_sdk = scan::pick_toolchain(no_sdk_report) ;
    check(!no_sdk.m_ok, "缺 NX SDK 时组合选型失败") ;
    check(!no_sdk.m_message.empty(), "缺 NX SDK 时写失败原因") ;

    scan::ScanReport no_compiler_report = report ;
    no_compiler_report.m_compilers.clear() ;
    const scan::ToolchainSelection no_compiler = scan::pick_toolchain(no_compiler_report) ;
    check(!no_compiler.m_ok, "缺编译器时组合选型失败") ;
    check(!no_compiler.m_message.empty(), "缺编译器时写失败原因") ;

    scan::ScanReport minimal_report = report ;
    minimal_report.m_windows_sdks.clear() ;
    const scan::ToolchainSelection minimal = scan::pick_toolchain(minimal_report) ;
    check(minimal.m_ok, "Windows SDK 缺失不阻断组合选型") ;
}

/** 覆盖手动菜单排序规则。 */
inline void test_sorting()
{
    std::vector<scan::Compiler> candidates = {
        make_compiler(scan::ToolChainVersion::V145, "70.3.3000", "fixture/sort/v145.exe", true) ,
        make_compiler(scan::ToolChainVersion::V141, "70.0.100", "fixture/sort/v141.exe", true) ,
        make_compiler(scan::ToolChainVersion::V143, "70.2.2000", "fixture/sort/v143-low.exe", true) ,
        make_compiler(scan::ToolChainVersion::V142, "70.1.1000", "fixture/sort/v142.exe", true) ,
        make_compiler(scan::ToolChainVersion::V143, "70.2.2001", "fixture/sort/v143-high.exe", true) ,
    } ;
    scan::sort_compilers(candidates) ;
    check(candidates.size() == 5 && candidates[0].m_version == scan::ToolChainVersion::V142,
        "编译器菜单排序：v142 最前") ;
    check(candidates.size() == 5 && candidates[1].m_toolset_version == "70.2.2001" &&
        candidates[2].m_toolset_version == "70.2.2000", "编译器菜单排序：v143 按工具集降序") ;
    check(candidates.size() == 5 && candidates[3].m_version == scan::ToolChainVersion::V145,
        "编译器菜单排序：v145 在 v143 之后") ;
    check(candidates.size() == 5 && candidates[4].m_version == scan::ToolChainVersion::V141,
        "编译器菜单排序：未参与自动选型的版本排最后") ;

    std::vector<scan::NXSdk> sdks = {
        make_nx_sdk("2007", "fixture/sort/nx-old", true, false) ,
        make_nx_sdk("2400", "fixture/sort/nx-new", true, true) ,
        make_nx_sdk("2200", "fixture/sort/nx-mid", true, false) ,
    } ;
    scan::sort_nx_sdks(sdks) ;
    check(sdks.size() == 3 && sdks[0].m_version == "2400" && sdks[0].m_is_running,
        "NX SDK 菜单排序：运行中优先") ;
    check(sdks.size() == 3 && sdks[1].m_version == "2200" && sdks[2].m_version == "2007",
        "NX SDK 菜单排序：其余按版本降序") ;

    std::vector<scan::WindowsSdk> windows_sdks = {
        make_windows_sdk("10.0.1.0", "fixture/sort/kits-a", true) ,
        make_windows_sdk("10.0.10.0", "fixture/sort/kits-b", true) ,
        make_windows_sdk("10.0.2.0", "fixture/sort/kits-c", true) ,
    } ;
    scan::sort_windows_sdks(windows_sdks) ;
    check(windows_sdks.size() == 3 && windows_sdks[0].m_version == "10.0.10.0" &&
        windows_sdks[1].m_version == "10.0.2.0" && windows_sdks[2].m_version == "10.0.1.0",
        "Windows SDK 菜单排序：版本号按数字段降序") ;
}

/** 覆盖选型记忆读写与菜单下标定位；只写 agent_build 下的夹具目录。 */
inline void test_selection_memory()
{
    const scan::Path directory = fixture_root() / L"scan_select" ;
    std::error_code error ;
    std::filesystem::remove_all(directory, error) ;
    std::filesystem::create_directories(directory, error) ;
    check(!error, "创建选型记忆夹具目录") ;

    const test_scan_select_detail::EnvironmentGuard cache_guard(L"NX_DEV_CACHE_DIR",
        directory.wstring()) ;
    check(!scan::cache_path().empty(), "覆盖缓存目录后取到缓存路径") ;

    const scan::SelectionMemory empty_memory = scan::load_selection_memory() ;
    check(empty_memory.m_compiler_key.empty() && empty_memory.m_nx_sdk_key.empty() &&
        empty_memory.m_windows_sdk_key.empty(), "没有记忆文件时读回空") ;

    scan::SelectionMemory memory ;
    memory.m_compiler_key = "fixture/scan_select/VC/v143/cl.exe" ;
    memory.m_nx_sdk_key = "fixture/scan_select/NX" ;
    memory.m_windows_sdk_key = "10.0.99999.0" ;
    check(scan::save_selection_memory(memory), "保存选型记忆") ;
    const scan::SelectionMemory loaded = scan::load_selection_memory() ;
    check(loaded.m_compiler_key == memory.m_compiler_key, "选型记忆：编译器键往返一致") ;
    check(loaded.m_nx_sdk_key == memory.m_nx_sdk_key, "选型记忆：NX SDK 键往返一致") ;
    check(loaded.m_windows_sdk_key == memory.m_windows_sdk_key, "选型记忆：Windows SDK 键往返一致") ;

    const std::vector<scan::Compiler> candidates = {
        make_compiler(scan::ToolChainVersion::V143, "70.2.2000", "fixture/scan_select/other/cl.exe", true) ,
        make_compiler(scan::ToolChainVersion::V143, "70.2.2000", "fixture/scan_select/VC/v143/cl.exe", true) ,
    } ;
    scan::SelectionMemory compiler_memory ;
    compiler_memory.m_compiler_key = "FIXTURE/SCAN_SELECT/VC/V143/CL.EXE" ;
    check(scan::find_compiler_index(candidates, compiler_memory) == 1, "菜单定位：编译器下标忽略大小写") ;
    compiler_memory.m_compiler_key = "fixture/no-such/cl.exe" ;
    check(scan::find_compiler_index(candidates, compiler_memory) == 0, "菜单定位：编译器找不到回到 0") ;

    const std::vector<scan::NXSdk> sdks = {
        make_nx_sdk("2007", "fixture/scan_select/nx-a", true, false) ,
        make_nx_sdk("2007", "fixture/scan_select/NX", true, false) ,
    } ;
    scan::SelectionMemory sdk_memory ;
    sdk_memory.m_nx_sdk_key = "fixture/scan_select/nx" ;
    check(scan::find_nx_sdk_index(sdks, sdk_memory) == 1, "菜单定位：NX SDK 下标") ;

    const std::vector<scan::WindowsSdk> windows_sdks = {
        make_windows_sdk("10.0.1.0", "fixture/scan_select/kits-a", true) ,
        make_windows_sdk("10.0.99999.0", "fixture/scan_select/kits-b", true) ,
    } ;
    scan::SelectionMemory windows_memory ;
    windows_memory.m_windows_sdk_key = "10.0.99999.0" ;
    check(scan::find_windows_sdk_index(windows_sdks, windows_memory) == 1,
        "菜单定位：Windows SDK 下标") ;
}
/** 覆盖 _MSC_VER → 工具链版本的映射。 */
inline void test_msvc_ver_mapping()
{
    check(scan::engine::version_from_msvc_ver(1900) == scan::ToolChainVersion::V140,
        "_MSC_VER 1900 -> v140") ;
    check(scan::engine::version_from_msvc_ver(1916) == scan::ToolChainVersion::V141,
        "_MSC_VER 1916 -> v141") ;
    check(scan::engine::version_from_msvc_ver(1929) == scan::ToolChainVersion::V142,
        "_MSC_VER 1929 -> v142") ;
    check(scan::engine::version_from_msvc_ver(1944) == scan::ToolChainVersion::V143,
        "_MSC_VER 1944 -> v143") ;
    check(scan::engine::version_from_msvc_ver(1951) == scan::ToolChainVersion::V145,
        "_MSC_VER 1951 -> v145") ;
    check(scan::engine::version_from_msvc_ver(1850) == scan::ToolChainVersion::Undefined,
        "未知 _MSC_VER -> Undefined") ;
    check(scan::engine::version_from_msvc_ver(0) == scan::ToolChainVersion::Undefined,
        "空 _MSC_VER -> Undefined") ;
}

/** Ninja 探测只检查结果自洽：找到时版本与路径有效，没找到时字段为空。 */
inline void test_ninja_probe()
{
    const scan::NinjaTool ninja = scan::detect_ninja() ;
    if (ninja.m_found)
    {
        check(!ninja.m_version.empty(), "Ninja 探测：找到时版本非空") ;
        check(!ninja.m_executable.empty() &&
            std::filesystem::is_regular_file(ninja.m_executable), "Ninja 探测：找到时路径存在") ;
        return ;
    }
    check(ninja.m_version.empty(), "Ninja 探测：没找到时版本为空") ;
    check(ninja.m_executable.empty(), "Ninja 探测：没找到时路径为空") ;
}
} // namespace test_scan_select_detail

/** 扫描选型与选型记忆自动测试；全部通过返回 0。 */
inline int test_scan_select()
{
    test_scan_select_detail::failures() = 0 ;
    test_scan_select_detail::test_pick_compiler() ;
    test_scan_select_detail::test_pick_nx_sdk() ;
    test_scan_select_detail::test_pick_windows_sdk() ;
    test_scan_select_detail::test_generator() ;
    test_scan_select_detail::test_msvc_ver_mapping() ;
    test_scan_select_detail::test_ninja_probe() ;
    test_scan_select_detail::test_pick_toolchain() ;
    test_scan_select_detail::test_sorting() ;
    test_scan_select_detail::test_selection_memory() ;
    const int failed = test_scan_select_detail::failures() ;
    if (failed == 0)
    {
        std::cout << "[ OK ] test_scan_select\n" ;
        return 0 ;
    }
    std::cerr << "[FAIL] test_scan_select: " << failed << " 项失败\n" ;
    return 1 ;
}
