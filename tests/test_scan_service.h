#pragma once
// ============================================================================
// test_scan_service.h — 三域并行扫描会话、缓存 v6、平台工具集枚举与手动定位的测试。
//
// 由 app/main.cpp 的 main() 调 test_scan_service()。假 VS/NX/Windows SDK 夹具
// 全部运行时合成在 agent_build/tests/fixtures 下，不读取本机真实安装；缓存
// 目录通过 NX_DEV_CACHE_DIR 指向夹具，不写用户目录。
// ============================================================================
#include "../scan_module/scan_service.h"
#include "../scan_module/src/scan_internal.h"
#include <windows.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace test_scan_service_detail
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
        std::cerr << "[FAIL] test_scan_service: " << label << '\n' ;
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

/** 写一个夹具文件；父目录不存在时先建出来。 */
inline bool write_file(const scan::Path& file, const std::string& content = "fixture\n")
{
    std::error_code error ;
    const scan::Path parent = file.parent_path() ;
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, error) ;
        if (error)
        {
            return false ;
        }
    }
    std::ofstream stream(file, std::ios::binary | std::ios::trunc) ;
    if (!stream)
    {
        return false ;
    }
    stream << content ;
    stream.flush() ;
    return stream.good() ;
}

/** 建一个目录；失败返回 false。 */
inline bool make_directory(const scan::Path& directory)
{
    std::error_code error ;
    std::filesystem::create_directories(directory, error) ;
    return !error ;
}

/** 缓存文件字段转义（与 scan_cache 内部一致），用于手写旧版缓存夹具。 */
inline std::string cache_escape(const std::string& text)
{
    std::string result ;
    result.reserve(text.size()) ;
    for (const char letter : text)
    {
        switch (letter)
        {
            case '\\': result += "\\\\" ; break ;
            case '\t': result += "\\t" ; break ;
            case '\n': result += "\\n" ; break ;
            case '\r': result += "\\r" ; break ;
            default: result.push_back(letter) ; break ;
        }
    }
    return result ;
}

/** 一套合成夹具的路径。 */
struct Fixture
{
    /** 夹具总根。 */
    scan::Path m_root = {} ;
    /** 主假 VS 安装根（只有 v145 平台工具集）。 */
    scan::Path m_visual_studio = {} ;
    /** 主假 VS 的 v145 平台工具集目录。 */
    scan::Path m_platform_toolset_dir = {} ;
    /** 主假 VS 的 cl.exe。 */
    scan::Path m_cl = {} ;
    /** 主假 VS 的 vcvarsall.bat。 */
    scan::Path m_vcvars = {} ;
    /** 主假 VS 里多出来的 14.40 工具集目录（旧逻辑会误判成 v143）。 */
    scan::Path m_decoy_cl = {} ;
    /** 假 NX 安装根。 */
    scan::Path m_nx_root = {} ;
    /** 假 NX SDK 的 UGOPEN 根。 */
    scan::Path m_nx_ugopen = {} ;
    /** 假 Windows Kits 根。 */
    scan::Path m_kits_root = {} ;
    /** 假 Windows SDK 版本。 */
    std::string m_kits_version = {} ;
    /** 假 VS 安装根（v143 + v145 两个平台工具集）。 */
    scan::Path m_dual_visual_studio = {} ;
    /** 双工具集假 VS 的 v143 cl.exe。 */
    scan::Path m_dual_v143_cl = {} ;
    /** 双工具集假 VS 的 v145 cl.exe。 */
    scan::Path m_dual_v145_cl = {} ;
} ;

/** 假 VS 的 MSBuild 平台工具集目录。 */
inline scan::Path platform_toolset_directory(const scan::Path& visual_studio,
    const std::string& platform_toolset)
{
    return visual_studio / L"MSBuild" / L"Microsoft" / L"VC" / L"v990" / L"Platforms" / L"x64" /
        L"PlatformToolsets" / scan::path_from_text(platform_toolset) ;
}

/** 假 VS 的 VC\Auxiliary\Build 目录。 */
inline scan::Path visual_studio_build_directory(const scan::Path& visual_studio)
{
    return visual_studio / L"VC" / L"Auxiliary" / L"Build" ;
}

/** 一个假 VS 安装的公共骨架（vcvars + 指定平台工具集目录）。 */
inline scan::Path make_visual_studio_skeleton(const scan::Path& root,
    const std::vector<std::string>& platform_toolsets)
{
    const scan::Path visual_studio = root / L"Microsoft Visual Studio" / L"99" / L"Community" ;
    write_file(visual_studio_build_directory(visual_studio) / L"vcvarsall.bat") ;
    for (const std::string& platform_toolset : platform_toolsets)
    {
        make_directory(platform_toolset_directory(visual_studio, platform_toolset)) ;
    }
    return visual_studio ;
}

/** 在 agent_build/tests/fixtures 下现造假 VS、假 NX 与假 Windows SDK。 */
inline Fixture build_fixture()
{
    Fixture fixture ;
    fixture.m_root = fixture_root() / L"scan_service" ;
    std::error_code error ;
    std::filesystem::remove_all(fixture.m_root, error) ;
    fixture.m_visual_studio = make_visual_studio_skeleton(fixture.m_root, { "v145" }) ;
    const scan::Path msvc_root = fixture.m_visual_studio / L"VC" / L"Tools" / L"MSVC" ;
    fixture.m_cl = msvc_root / L"14.50.99999" / L"bin" / L"Hostx64" / L"x64" / L"cl.exe" ;
    fixture.m_decoy_cl = msvc_root / L"14.40.0" / L"bin" / L"Hostx64" / L"x64" / L"cl.exe" ;
    fixture.m_vcvars = visual_studio_build_directory(fixture.m_visual_studio) / L"vcvarsall.bat" ;
    fixture.m_platform_toolset_dir = platform_toolset_directory(fixture.m_visual_studio, "v145") ;
    write_file(fixture.m_cl) ;
    write_file(fixture.m_decoy_cl) ;
    write_file(visual_studio_build_directory(fixture.m_visual_studio) /
        L"Microsoft.VCToolsVersion.v145.default.txt", "14.50.99999\n") ;
    write_file(visual_studio_build_directory(fixture.m_visual_studio) /
        L"Microsoft.VCToolsVersion.default.txt", "14.50.99999\n") ;

    // 双平台工具集的假 VS：v143 有默认版本文件，v145 没有（走兼容最高工具集目录）。
    fixture.m_dual_visual_studio =
        make_visual_studio_skeleton(fixture.m_root / L"dual", { "v143", "v145" }) ;
    const scan::Path dual_msvc_root = fixture.m_dual_visual_studio / L"VC" / L"Tools" / L"MSVC" ;
    fixture.m_dual_v143_cl = dual_msvc_root / L"14.40.0" / L"bin" / L"Hostx64" / L"x64" / L"cl.exe" ;
    fixture.m_dual_v145_cl = dual_msvc_root / L"14.50.99999" / L"bin" / L"Hostx64" / L"x64" / L"cl.exe" ;
    write_file(fixture.m_dual_v143_cl) ;
    write_file(fixture.m_dual_v145_cl) ;
    write_file(visual_studio_build_directory(fixture.m_dual_visual_studio) /
        L"Microsoft.VCToolsVersion.v143.default.txt", "14.40.0\n") ;

    fixture.m_nx_root = fixture.m_root / L"NX" ;
    fixture.m_nx_ugopen = fixture.m_nx_root / L"UGOPEN" ;
    fixture.m_kits_root = fixture.m_root / L"Windows Kits" / L"10" ;
    fixture.m_kits_version = "10.99.99999.0" ;
    write_file(fixture.m_nx_ugopen / L"uf.h") ;
    write_file(fixture.m_nx_ugopen / L"NXOpen" / L"Session.hxx") ;
    write_file(fixture.m_nx_ugopen / L"libufun.lib") ;
    make_directory(fixture.m_nx_root / L"UGII") ;
    make_directory(fixture.m_nx_root / L"NXBIN") ;
    const scan::Path include_root = fixture.m_kits_root / L"Include" / L"10.99.99999.0" ;
    const scan::Path lib_root = fixture.m_kits_root / L"Lib" / L"10.99.99999.0" ;
    write_file(include_root / L"um" / L"windows.h") ;
    write_file(include_root / L"ucrt" / L"stdio.h") ;
    write_file(lib_root / L"um" / L"x64" / L"kernel32.lib") ;
    write_file(lib_root / L"ucrt" / L"x64" / L"ucrt.lib") ;
    write_file(fixture.m_kits_root / L"bin" / L"10.99.99999.0" / L"x64" / L"rc.exe") ;
    return fixture ;
}

/** 用夹具路径造一份三域都有效、可互相校验的扫描报告。 */
inline scan::ScanReport make_report(const Fixture& fixture)
{
    scan::ScanReport report ;
    scan::Compiler compiler ;
    compiler.m_name = "MSVC v145" ;
    compiler.m_version = scan::ToolChainVersion::V145 ;
    compiler.m_toolset_version = "14.50.99999" ;
    compiler.m_cl_path = fixture.m_cl ;
    compiler.m_vcvars_path = fixture.m_vcvars ;
    compiler.m_vcvars_argument = "" ;
    compiler.m_is_checked = true ;
    compiler.m_generator = "Visual Studio 99 2099" ;
    compiler.m_platform_toolset = "v145" ;
    report.m_compilers.push_back(compiler) ;

    scan::NXSdk sdk ;
    sdk.m_version = "2007" ;
    sdk.m_version_source = "test" ;
    sdk.m_root = fixture.m_nx_root ;
    sdk.m_uf_header_dir = fixture.m_nx_ugopen ;
    sdk.m_nxopen_header_dir = fixture.m_nx_ugopen ;
    sdk.m_lib_dir = fixture.m_nx_ugopen ;
    sdk.m_is_checked = true ;
    sdk.m_is_running = true ;
    report.m_sdks.push_back(sdk) ;

    scan::WindowsSdk windows_sdk ;
    windows_sdk.m_version = fixture.m_kits_version ;
    windows_sdk.m_source = "test" ;
    windows_sdk.m_root = fixture.m_kits_root ;
    windows_sdk.m_include_dir = fixture.m_kits_root / L"Include" / L"10.99.99999.0" ;
    windows_sdk.m_lib_dir = fixture.m_kits_root / L"Lib" / L"10.99.99999.0" ;
    windows_sdk.m_bin_dir = fixture.m_kits_root / L"bin" / L"10.99.99999.0" / L"x64" ;
    windows_sdk.m_is_checked = true ;
    report.m_windows_sdks.push_back(windows_sdk) ;
    return report ;
}

/** 覆盖平台工具集枚举：只装 v145 时不出 v143 候选，双工具集分类正确。 */
inline void test_platform_toolsets(const Fixture& fixture)
{
    const std::vector<scan::Compiler> candidates = scan::engine::compiler_candidates(
        scan::engine::visual_studio_from_path(fixture.m_visual_studio)) ;
    check(candidates.size() == 1, "只有 v145 平台工具集时只产出一个候选") ;
    if (candidates.size() == 1)
    {
        const scan::Compiler& candidate = candidates[0] ;
        check(candidate.m_platform_toolset == "v145", "候选平台工具集是 v145") ;
        check(candidate.m_version == scan::ToolChainVersion::V145, "v145 平台工具集映射到大类 V145") ;
        check(candidate.m_toolset_version == "14.50.99999", "平台工具集默认版本文件读取正确") ;
        check(candidate.m_is_checked, "平台工具集目录存在且关键文件齐全时算已校验") ;
        check(scan::path_text(candidate.m_cl_path) == scan::path_text(fixture.m_cl),
            "候选 cl 路径指向 v145 工具集") ;
        check(candidate.m_generator.empty(), "只有安装路径证据时生成器留空") ;
    }
    for (const scan::Compiler& candidate : candidates)
    {
        check(candidate.m_platform_toolset != "v143" &&
            candidate.m_toolset_version != "14.40.0",
            "VC\\Tools\\MSVC 下的 14.40 目录不能当成 v143 候选") ;
    }

    // v143 + v145 同时安装：两个候选，分类与版本来源都正确。
    scan::engine::VisualStudioInstall dual =
        scan::engine::visual_studio_from_path(fixture.m_dual_visual_studio) ;
    dual.m_major = "99" ;
    dual.m_year = "2099" ;
    const std::vector<scan::Compiler> dual_candidates = scan::engine::compiler_candidates(dual) ;
    check(dual_candidates.size() == 2, "v143+v145 同时安装时两个候选") ;
    const scan::Compiler* v143 = nullptr ;
    const scan::Compiler* v145 = nullptr ;
    for (const scan::Compiler& candidate : dual_candidates)
    {
        if (candidate.m_platform_toolset == "v143")
        {
            v143 = &candidate ;
        }
        if (candidate.m_platform_toolset == "v145")
        {
            v145 = &candidate ;
        }
    }
    check(v143 != nullptr && v143->m_version == scan::ToolChainVersion::V143, "v143 候选分类正确") ;
    check(v143 != nullptr && v143->m_toolset_version == "14.40.0",
        "v143 候选优先读 .v143.default.txt 的完整版本") ;
    check(v145 != nullptr && v145->m_version == scan::ToolChainVersion::V145, "v145 候选分类正确") ;
    check(v145 != nullptr && v145->m_toolset_version == "14.50.99999",
        "没有版本文件时取兼容的最高 MSVC 工具集目录") ;
    check(v143 != nullptr && v143->m_is_checked, "v143 候选已校验") ;
    check(v145 != nullptr && v145->m_is_checked, "v145 候选已校验") ;
    check(v143 != nullptr && v143->m_generator == "Visual Studio 99 2099",
        "候选带出安装实例的生成器名") ;
    check(v145 != nullptr && v145->m_generator == "Visual Studio 99 2099",
        "候选带出安装实例的生成器名") ;

    // 旧缓存条目补平台工具集：对得上真实平台工具集的能补，对不上的补不出。
    scan::Compiler legacy ;
    legacy.m_name = "MSVC v145" ;
    legacy.m_version = scan::ToolChainVersion::V145 ;
    legacy.m_toolset_version = "14.50.99999" ;
    legacy.m_cl_path = fixture.m_cl ;
    legacy.m_vcvars_path = fixture.m_vcvars ;
    check(scan::engine::platform_toolset_for_compiler(legacy) == "v145",
        "旧缓存条目按工具集版本补出平台工具集") ;
    scan::Compiler stale = legacy ;
    stale.m_version = scan::ToolChainVersion::V143 ;
    stale.m_toolset_version = "14.40.0" ;
    stale.m_cl_path = fixture.m_decoy_cl ;
    check(scan::engine::platform_toolset_for_compiler(stale).empty(),
        "对不上本机真实平台工具集的旧条目补不出平台工具集") ;
}

/** 覆盖缓存 v6 写入与读回：字段齐全，平台工具集不丢。 */
inline void test_cache_roundtrip(const Fixture& fixture, const scan::ScanReport& report)
{
    const EnvironmentGuard guard(L"NX_DEV_CACHE_DIR", (fixture.m_root / L"cache_dir").wstring()) ;
    check(scan::save_cache(report), "v6 缓存写入成功") ;
    check(std::filesystem::is_regular_file(scan::cache_path()), "v6 缓存文件存在") ;
    std::ifstream stream(scan::cache_path(), std::ios::binary) ;
    std::string header ;
    std::getline(stream, header) ;
    check(header == "# nx_dev scan cache v6", "缓存文件头是 v6") ;

    const scan::ScanReport loaded = scan::load_cache() ;
    check(loaded.m_compilers.size() == 1, "v6 缓存读回一条编译器") ;
    if (loaded.m_compilers.size() == 1)
    {
        const scan::Compiler& compiler = loaded.m_compilers[0] ;
        check(compiler.m_name == report.m_compilers[0].m_name, "v6 缓存：编译器名字一致") ;
        check(compiler.m_version == scan::ToolChainVersion::V145, "v6 缓存：编译器大类版本一致") ;
        check(compiler.m_toolset_version == "14.50.99999", "v6 缓存：工具集版本一致") ;
        check(scan::path_text(compiler.m_cl_path) == scan::path_text(report.m_compilers[0].m_cl_path),
            "v6 缓存：cl 路径一致") ;
        check(compiler.m_is_checked, "v6 缓存：校验状态一致") ;
        check(compiler.m_generator == "Visual Studio 99 2099", "v6 缓存：生成器字段往返一致") ;
        check(compiler.m_platform_toolset == "v145", "v6 缓存：平台工具集字段往返一致") ;
        check(compiler.m_msvc_ver == report.m_compilers[0].m_msvc_ver, "v6 缓存：_MSC_VER 往返一致") ;
        check(compiler.m_compiler_version == report.m_compilers[0].m_compiler_version,
            "v6 缓存：cl 完整版本往返一致") ;
        check(compiler.m_kind == report.m_compilers[0].m_kind, "v6 缓存：工具链来源类型往返一致") ;
    }
    check(loaded.m_sdks.size() == 1, "v6 缓存读回一条 NX SDK") ;
    if (loaded.m_sdks.size() == 1)
    {
        check(loaded.m_sdks[0].m_version == "2007", "v6 缓存：NX 版本一致") ;
        check(loaded.m_sdks[0].m_is_running, "v6 缓存：NX 运行状态一致") ;
        check(scan::path_text(loaded.m_sdks[0].m_lib_dir) == scan::path_text(fixture.m_nx_ugopen),
            "v6 缓存：NX 库目录一致") ;
    }
    check(loaded.m_windows_sdks.size() == 1, "v6 缓存读回一条 Windows SDK") ;
    if (loaded.m_windows_sdks.size() == 1)
    {
        check(loaded.m_windows_sdks[0].m_version == fixture.m_kits_version,
            "v6 缓存：Windows SDK 版本一致") ;
        check(scan::path_text(loaded.m_windows_sdks[0].m_bin_dir) ==
            scan::path_text(fixture.m_kits_root / L"bin" / L"10.99.99999.0" / L"x64"),
            "v6 缓存：Windows SDK bin 目录一致") ;
    }
}

/** 覆盖 v1/v2/v3/v4 旧缓存兼容读；读完把 v6 缓存恢复回去。 */
inline void test_cache_compatibility(const Fixture& fixture, const scan::ScanReport& report)
{
    const EnvironmentGuard guard(L"NX_DEV_CACHE_DIR", (fixture.m_root / L"cache_dir").wstring()) ;
    const scan::Path file = scan::cache_path() ;

    std::string v4 = "# nx_dev scan cache v4\n" ;
    v4 += "compiler\tMSVC v145\t4\t14.50.99999\tfixture/v4/cl.exe\tfixture/v4/vcvarsall.bat\t\t1\t"
        "Visual Studio 99 2099\n" ;
    v4 += "sdk\t2007\tregistry\tfixture/v4/nx\tfixture/v4/nx/UGOPEN\tfixture/v4/nx/UGOPEN\t"
        "fixture/v4/nx/UGOPEN\t1\t0\n" ;
    v4 += "winsdk\t10.0.99999.0\tregistry\tfixture/v4/kits\tfixture/v4/kits/Include/10.0.99999.0\t"
        "fixture/v4/kits/Lib/10.0.99999.0\tfixture/v4/kits/bin/10.0.99999.0/x64\t1\n" ;
    check(write_file(file, v4), "写入 v4 缓存夹具") ;
    {
        const scan::ScanReport loaded = scan::load_cache() ;
        check(loaded.m_compilers.size() == 1 &&
            loaded.m_compilers[0].m_generator == "Visual Studio 99 2099", "v4 缓存：生成器可读") ;
        check(loaded.m_compilers.size() == 1 && loaded.m_compilers[0].m_platform_toolset.empty(),
            "v4 缓存：平台工具集字段留空") ;
        check(loaded.m_sdks.size() == 1 && loaded.m_windows_sdks.size() == 1, "v4 缓存：SDK 段可读") ;
    }

    std::string v3 = "# nx_dev scan cache v3\n" ;
    v3 += "compiler\tMSVC v143\t3\t14.40.7\tfixture/old/cl.exe\tfixture/old/vcvarsall.bat\t\t1\n" ;
    v3 += "sdk\t2007\tregistry\tfixture/old/nx\tfixture/old/nx/UGOPEN\tfixture/old/nx/UGOPEN\t"
        "fixture/old/nx/UGOPEN\t1\t0\n" ;
    v3 += "winsdk\t10.0.99999.0\tregistry\tfixture/old/kits\tfixture/old/kits/Include/10.0.99999.0\t"
        "fixture/old/kits/Lib/10.0.99999.0\tfixture/old/kits/bin/10.0.99999.0/x64\t1\n" ;
    check(write_file(file, v3), "写入 v3 缓存夹具") ;
    {
        const scan::ScanReport loaded = scan::load_cache() ;
        check(loaded.m_compilers.size() == 1 && loaded.m_compilers[0].m_toolset_version == "14.40.7",
            "v3 缓存：编译器可读") ;
        check(loaded.m_compilers.size() == 1 && loaded.m_compilers[0].m_generator.empty() &&
            loaded.m_compilers[0].m_platform_toolset.empty(), "v3 缓存：生成器与平台工具集留空") ;
        check(loaded.m_sdks.size() == 1 && loaded.m_sdks[0].m_version == "2007", "v3 缓存：NX SDK 可读") ;
        check(loaded.m_windows_sdks.size() == 1, "v3 缓存：Windows SDK 可读") ;
    }

    std::string v2 = "# nx_dev scan cache v2\n" ;
    v2 += "compiler\tMSVC v142\t2\t14.20.0\tfixture/old2/cl.exe\tfixture/old2/vcvarsall.bat\t\t1\n" ;
    v2 += "sdk\t2200\tregistry\tfixture/old2/nx\tfixture/old2/nx/UGOPEN\tfixture/old2/nx/UGOPEN\t"
        "fixture/old2/nx/UGOPEN\t1\t0\n" ;
    check(write_file(file, v2), "写入 v2 缓存夹具") ;
    {
        const scan::ScanReport loaded = scan::load_cache() ;
        check(loaded.m_compilers.size() == 1 &&
            loaded.m_compilers[0].m_version == scan::ToolChainVersion::V142, "v2 缓存：编译器可读") ;
        check(loaded.m_sdks.size() == 1 && loaded.m_sdks[0].m_version == "2200", "v2 缓存：NX SDK 可读") ;
        check(loaded.m_windows_sdks.empty(), "v2 缓存：没有 Windows SDK 段") ;
    }

    std::string v1 = "# nx_dev scan cache v1\n" ;
    v1 += "compiler\tMSVC v140\t0\tfixture/legacy/cl.exe\tfixture/legacy/vcvarsall.bat\t1\n" ;
    v1 += "sdk\t2007\tfixture/legacy/uf\tfixture/legacy/nxopen\tfixture/legacy/lib\t0\n" ;
    check(write_file(file, v1), "写入 v1 缓存夹具") ;
    {
        const scan::ScanReport loaded = scan::load_cache() ;
        check(loaded.m_compilers.size() == 1 &&
            loaded.m_compilers[0].m_version == scan::ToolChainVersion::V140, "v1 缓存：编译器可读") ;
        check(loaded.m_sdks.size() == 1 &&
            scan::path_text(loaded.m_sdks[0].m_lib_dir) == "fixture/legacy/lib", "v1 缓存：NX SDK 可读") ;
        check(loaded.m_windows_sdks.empty(), "v1 缓存：没有 Windows SDK 段") ;
    }
    check(scan::save_cache(report), "缓存兼容测试后恢复 v6 缓存") ;
}

/** 覆盖缓存目录回退 LOCALAPPDATA → TEMP → 空，以及不可用时只读不写。 */
inline void test_cache_directory_fallback(const Fixture& fixture, const scan::ScanReport& report)
{
    {
        const EnvironmentGuard guard(L"NX_DEV_CACHE_DIR", (fixture.m_root / L"cache_dir").wstring()) ;
        check(scan::cache_path().parent_path() == fixture.m_root / L"cache_dir",
            "缓存目录：NX_DEV_CACHE_DIR 优先") ;
    }
    {
        const EnvironmentGuard override_guard(L"NX_DEV_CACHE_DIR", L"") ;
        const scan::Path appdata = fixture.m_root / L"appdata" ;
        make_directory(appdata) ;
        const EnvironmentGuard local_guard(L"LOCALAPPDATA", appdata.wstring()) ;
        const EnvironmentGuard temp_guard(L"TEMP", (fixture.m_root / L"temp").wstring()) ;
        check(scan::cache_path().parent_path() == appdata / L"nx_dev", "缓存目录：LOCALAPPDATA 优先") ;
    }
    {
        const EnvironmentGuard override_guard(L"NX_DEV_CACHE_DIR", L"") ;
        const scan::Path local_file = fixture.m_root / L"local_file" ;
        check(write_file(local_file), "造一个假的 LOCALAPPDATA 普通文件") ;
        const scan::Path temp = fixture.m_root / L"temp" ;
        make_directory(temp) ;
        const EnvironmentGuard local_guard(L"LOCALAPPDATA", local_file.wstring()) ;
        const EnvironmentGuard temp_guard(L"TEMP", temp.wstring()) ;
        check(scan::cache_path().parent_path() == temp / L"nx_dev",
            "缓存目录：LOCALAPPDATA 不可用时退到 TEMP") ;
    }
    {
        const EnvironmentGuard override_guard(L"NX_DEV_CACHE_DIR", L"") ;
        const EnvironmentGuard local_guard(L"LOCALAPPDATA", L"") ;
        const EnvironmentGuard temp_guard(L"TEMP", L"") ;
        const scan::Path path = scan::cache_path() ;
        check(!path.empty() && path.filename() == scan::Path(L"scan_cache.tsv"),
            "缓存目录：都不可用时给兜底路径") ;
        check(!scan::save_cache(report), "缓存目录不可用时不写缓存") ;
        check(scan::load_cache().m_compilers.empty(), "缓存目录不可用时不读缓存") ;
    }
}

/** 覆盖旧缓存（无平台工具集字段）命中后按安装证据补平台工具集，并丢掉对不上的条目。 */
inline void test_legacy_cache_session(const Fixture& fixture)
{
    const EnvironmentGuard guard(L"NX_DEV_CACHE_DIR", (fixture.m_root / L"cache_dir").wstring()) ;
    const std::string vcvars_text = scan::path_text(fixture.m_vcvars) ;
    const std::string uf_text = scan::path_text(fixture.m_nx_ugopen) ;
    const std::string nx_text = scan::path_text(fixture.m_nx_root) ;
    const std::string kits_text = scan::path_text(fixture.m_kits_root) ;
    std::string cache = "# nx_dev scan cache v4\n" ;
    cache += "compiler\tMSVC v145\t4\t14.50.99999\t" + cache_escape(scan::path_text(fixture.m_cl)) +
        "\t" + cache_escape(vcvars_text) + "\t\t1\tVisual Studio 99 2099\n" ;
    cache += "compiler\tMSVC v143\t3\t14.40.0\t" + cache_escape(scan::path_text(fixture.m_decoy_cl)) +
        "\t" + cache_escape(vcvars_text) + "\t\t1\t\n" ;
    cache += "sdk\t2007\ttest\t" + cache_escape(nx_text) + "\t" + cache_escape(uf_text) + "\t" +
        cache_escape(uf_text) + "\t" + cache_escape(uf_text) + "\t1\t1\n" ;
    cache += "winsdk\t" + fixture.m_kits_version + "\ttest\t" + cache_escape(kits_text) + "\t" +
        cache_escape(scan::path_text(fixture.m_kits_root / L"Include" / L"10.99.99999.0")) + "\t" +
        cache_escape(scan::path_text(fixture.m_kits_root / L"Lib" / L"10.99.99999.0")) + "\t" +
        cache_escape(scan::path_text(fixture.m_kits_root / L"bin" / L"10.99.99999.0" / L"x64")) +
        "\t1\n" ;
    check(write_file(scan::cache_path(), cache), "写入旧版 v4 缓存夹具") ;

    scan::ScanSession session ;
    session.wait() ;
    const scan::ScanState state = session.state() ;
    check(state.all_finished(), "旧缓存会话：三个域全部结束") ;
    check(state.m_domains[0].m_from_cache, "旧缓存会话：编译器域命中缓存") ;
    const scan::ScanReport results = session.results() ;
    check(results.m_compilers.size() == 1, "旧缓存会话：对不上平台工具集的条目被丢弃") ;
    if (results.m_compilers.size() == 1)
    {
        check(results.m_compilers[0].m_platform_toolset == "v145",
            "旧缓存会话：按安装证据补出平台工具集") ;
        check(results.m_compilers[0].m_generator == "Visual Studio 99 2099",
            "旧缓存会话：保留缓存里的生成器") ;
    }
    check(results.m_sdks.size() == 1 && results.m_sdks[0].m_version == "2007",
        "旧缓存会话：NX SDK 命中缓存") ;
    check(results.m_windows_sdks.size() == 1, "旧缓存会话：Windows SDK 命中缓存") ;
}

/** 覆盖 ScanSession：有效缓存时三域并行命中、结果可写回、取消能收敛。 */
inline void test_scan_session(const Fixture& fixture, const scan::ScanReport& report)
{
    const EnvironmentGuard guard(L"NX_DEV_CACHE_DIR", (fixture.m_root / L"cache_dir").wstring()) ;
    check(scan::save_cache(report), "会话测试前写入有效缓存") ;
    scan::ScanSession session ;
    session.wait() ;
    const scan::ScanState state = session.state() ;
    check(state.all_finished(), "会话：三个域全部结束") ;
    check(state.m_domains[0].m_from_cache && state.m_domains[1].m_from_cache &&
        state.m_domains[2].m_from_cache, "会话：三个域都命中缓存") ;
    check(!state.m_domains[0].m_cancelled && !state.m_domains[1].m_cancelled &&
        !state.m_domains[2].m_cancelled, "会话：缓存命中不算取消") ;
    const scan::ScanReport results = session.results() ;
    check(results.m_compilers.size() == 1 &&
        results.m_compilers[0].m_generator == "Visual Studio 99 2099", "会话：缓存编译器带出生成器") ;
    check(results.m_compilers.size() == 1 &&
        results.m_compilers[0].m_platform_toolset == "v145", "会话：缓存编译器带出平台工具集") ;
    check(results.m_sdks.size() == 1 && results.m_sdks[0].m_version == "2007", "会话：缓存 NX SDK 带出") ;
    check(results.m_windows_sdks.size() == 1, "会话：缓存 Windows SDK 带出") ;
    check(session.save(), "会话：结果可写回缓存") ;

    scan::ScanSession cancelled_session ;
    cancelled_session.cancel() ;
    cancelled_session.wait() ;
    check(cancelled_session.state().all_finished(), "会话：取消后仍然收敛") ;
}

/** 覆盖手动定位：只在给定目录内找第一项、带出平台工具集、观察者可中止。 */
inline void test_locate(const Fixture& fixture)
{
    const scan::Path hint = fixture.m_root ;
    std::size_t visited = 0 ;
    std::vector<scan::Path> observed ;
    const auto located = scan::locate_compiler(hint, 6,
        [&visited, &observed](const scan::Path& directory, std::size_t count)
        {
            (void) count ;
            ++visited ;
            observed.push_back(directory) ;
            return true ;
        }) ;
    check(located.has_value(), "手动定位：找到夹具里的编译器") ;
    check(located.has_value() &&
        scan::path_text(located->m_cl_path).find("14.50.99999") != std::string::npos,
        "手动定位：命中的是夹具 cl.exe") ;
    check(located.has_value() && located->m_platform_toolset == "v145",
        "手动定位：带出真实平台工具集") ;
    check(located.has_value() && located->m_generator.empty(),
        "手动定位：夹具安装不在本机 VS 证据里，生成器留空") ;
    check(visited >= 1, "手动定位：每个目录都回调观察者") ;
    bool inside = true ;
    const std::string root_text = scan::path_text(hint) ;
    for (const scan::Path& directory : observed)
    {
        const std::string directory_text = scan::path_text(directory) ;
        if (directory_text.rfind(root_text, 0) != 0)
        {
            inside = false ;
        }
    }
    check(inside, "手动定位：只在给定目录内搜索") ;

    const scan::LocateObserver aborting_observer = [](const scan::Path&, std::size_t)
    {
        return false ;
    } ;
    check(!scan::locate_compiler(hint, 6, aborting_observer).has_value(),
        "手动定位：观察者返回 false 立即中止") ;

    const auto sdk = scan::locate_nx_sdk(hint, 6) ;
    check(sdk.has_value() && scan::path_text(sdk->m_root) == scan::path_text(fixture.m_nx_root),
        "手动定位：找到夹具里的 NX SDK 根") ;

    const auto windows_sdk = scan::locate_windows_sdk(hint, 6) ;
    check(windows_sdk.has_value() &&
        scan::path_text(windows_sdk->m_root) == scan::path_text(fixture.m_kits_root),
        "手动定位：找到夹具里的 Windows SDK 根") ;
    check(windows_sdk.has_value() && windows_sdk->m_version == fixture.m_kits_version &&
        windows_sdk->m_is_checked, "手动定位：Windows SDK 版本与校验状态正确") ;

    check(!scan::locate_compiler(scan::Path(), 5).has_value(), "手动定位：空目录返回空") ;
}
} // namespace test_scan_service_detail

/** 扫描会话、缓存、平台工具集与手动定位自动测试；全部通过返回 0。 */
inline int test_scan_service()
{
    test_scan_service_detail::failures() = 0 ;
    const test_scan_service_detail::Fixture fixture = test_scan_service_detail::build_fixture() ;
    test_scan_service_detail::check(std::filesystem::is_regular_file(fixture.m_cl),
        "合成假 VS 夹具") ;
    test_scan_service_detail::check(std::filesystem::is_directory(fixture.m_platform_toolset_dir),
        "合成假平台工具集夹具") ;
    const scan::ScanReport report = test_scan_service_detail::make_report(fixture) ;
    test_scan_service_detail::test_platform_toolsets(fixture) ;
    test_scan_service_detail::test_cache_roundtrip(fixture, report) ;
    test_scan_service_detail::test_cache_compatibility(fixture, report) ;
    test_scan_service_detail::test_cache_directory_fallback(fixture, report) ;
    test_scan_service_detail::test_legacy_cache_session(fixture) ;
    test_scan_service_detail::test_scan_session(fixture, report) ;
    test_scan_service_detail::test_locate(fixture) ;
    const int failed = test_scan_service_detail::failures() ;
    if (failed == 0)
    {
        std::cout << "[ OK ] test_scan_service\n" ;
        return 0 ;
    }
    std::cerr << "[FAIL] test_scan_service: " << failed << " 项失败\n" ;
    return 1 ;
}
