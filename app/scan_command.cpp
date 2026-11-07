#include "scan_command.h"
#include "../core_module/console_style.h"
#include "../scan_module/scan_control.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
/** 有颜色时套 ANSI 序列；没有颜色原样返回。 */
std::string styled(const std::string& text, std::string_view ansi)
{
    return core::paint(text, ansi) ;
}

/** 报告里的分节标题，如 [toolchains] 2。 */
std::string section_header(const std::string& name, std::size_t count)
{
    return styled("[" + name + "] " + std::to_string(count),
        std::string(core::color::bold) + core::color::bright_cyan) ;
}

/** 校核状态文字：通过绿色、不完整红色。 */
std::string state_text(const std::string& text, bool ok)
{
    return styled(text, ok ? core::color::bright_green : core::color::bright_red) ;
}

/** 阶段名的稳定文本，供日志与 JSON 使用。 */
std::string tier_text(scan::ScanTier tier)
{
    switch (tier)
    {
        case scan::ScanTier::ActiveProcess: return "ActiveProcess" ;
        case scan::ScanTier::Environment: return "Environment" ;
        case scan::ScanTier::CommonRoot: return "CommonRoot" ;
        case scan::ScanTier::UserDirectory: return "UserDirectory" ;
        case scan::ScanTier::DiskSweep: return "DiskSweep" ;
    }
    return "Unknown" ;
}

/** 工具链来源类型的稳定文本。 */
std::string toolchain_kind_text(scan::ToolChainKind kind)
{
    switch (kind)
    {
        case scan::ToolChainKind::VisualStudio: return "visual-studio" ;
        case scan::ToolChainKind::Portable: return "portable" ;
        case scan::ToolChainKind::Unknown: return "unknown" ;
    }
    return "unknown" ;
}

/** 工具链大类版本的稳定文本。 */
std::string compiler_version_text(scan::ToolChainVersion version)
{
    switch (version)
    {
        case scan::ToolChainVersion::V140: return "V140" ;
        case scan::ToolChainVersion::V141: return "V141" ;
        case scan::ToolChainVersion::V142: return "V142" ;
        case scan::ToolChainVersion::V143: return "V143" ;
        case scan::ToolChainVersion::V145: return "V145" ;
        case scan::ToolChainVersion::Undefined: return "Undefined" ;
    }
    return "Undefined" ;
}

/** JSON 布尔字面量。 */
const char* json_bool(bool value)
{
    return value ? "true" : "false" ;
}

/** 转义 JSON 字符串里的特殊字符；UTF-8 文本原样保留。 */
std::string json_escape(const std::string& text)
{
    std::string result ;
    result.reserve(text.size() + 8) ;
    for (const char letter : text)
    {
        switch (letter)
        {
            case '\\': result += "\\\\" ; break ;
            case '"': result += "\\\"" ; break ;
            case '\n': result += "\\n" ; break ;
            case '\r': result += "\\r" ; break ;
            case '\t': result += "\\t" ; break ;
            default:
                if (static_cast<unsigned char>(letter) < 0x20)
                {
                    result += "?" ;
                }
                else
                {
                    result.push_back(letter) ;
                }
                break ;
        }
    }
    return result ;
}

/** 读取一个注册表字符串值；读不到返回空。 */
std::string registry_string(const wchar_t* key_path, const wchar_t* value_name)
{
    std::array<wchar_t, 2048> buffer = {} ;
    DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t)) ;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key_path, value_name, RRF_RT_REG_SZ, nullptr,
            buffer.data(), &size) != ERROR_SUCCESS)
    {
        return {} ;
    }
    std::wstring value(buffer.data(), size / sizeof(wchar_t)) ;
    while (!value.empty() && value.back() == L'\0')
    {
        value.pop_back() ;
    }
    return scan::path_text(scan::Path(value)) ;
}

/** 读取环境变量的 UTF-8 文本；不存在返回空。 */
std::string environment_text(const wchar_t* name)
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
    return scan::path_text(scan::Path(value)) ;
}

/** 判断路径是否为已存在的常规文件。 */
bool path_is_file(const scan::Path& path)
{
    std::error_code error ;
    return std::filesystem::is_regular_file(path, error) ;
}

/** 获取当前 exe 的绝对路径；失败返回空。 */
scan::Path executable_path()
{
    std::wstring buffer(32768, L'\0') ;
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size())) ;
    if (length == 0)
    {
        return {} ;
    }
    buffer.resize(length) ;
    return scan::Path(buffer) ;
}

/** 从 exe 位置或 NX_DEVKIT_DIR 找 devkit 根（含 nx_toolchain.cmake）；找不到返回空。 */
std::optional<scan::Path> detect_devkit_root()
{
    const std::string environment = environment_text(L"NX_DEVKIT_DIR") ;
    if (!environment.empty())
    {
        const scan::Path root = scan::path_from_text(environment) ;
        if (path_is_file(root / L"nx_toolchain.cmake"))
        {
            return root ;
        }
    }
    scan::Path directory = executable_path() ;
    if (directory.empty())
    {
        return std::nullopt ;
    }
    directory = directory.parent_path() ;
    for (int level = 0 ; level < 4 && !directory.empty() ; ++level)
    {
        if (path_is_file(directory / L"nx_toolchain.cmake"))
        {
            return directory ;
        }
        const scan::Path parent = directory.parent_path() ;
        if (parent.empty() || parent == directory)
        {
            break ;
        }
        directory = parent ;
    }
    return std::nullopt ;
}

/** 本机机器信息；用于跨机器对比扫描结果。 */
struct MachineInfo
{
    std::string m_name ;
    std::string m_windows ;
    std::string m_architecture ;
} ;

/** 收集本机机器信息。 */
MachineInfo collect_machine()
{
    MachineInfo info ;
    wchar_t name[256] = {} ;
    DWORD name_size = static_cast<DWORD>(std::size(name)) ;
    info.m_name = GetComputerNameW(name, &name_size) ? scan::path_text(scan::Path(name)) : "unknown" ;
    const std::string product = registry_string(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName") ;
    const std::string release = registry_string(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion") ;
    const std::string build = registry_string(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber") ;
    if (!product.empty())
    {
        info.m_windows = product ;
        if (!release.empty())
        {
            info.m_windows += " " + release ;
        }
        if (!build.empty())
        {
            info.m_windows += " (build " + build + ")" ;
        }
    }
    else
    {
        info.m_windows = "Windows" ;
    }
    info.m_architecture = environment_text(L"PROCESSOR_ARCHITECTURE") ;
    if (info.m_architecture.empty())
    {
        info.m_architecture = "unknown" ;
    }
    return info ;
}

/** 扫描范围的一行说明。 */
std::string scope_text(const scan::ScanRequest& request, bool auto_devkit)
{
    std::string text = "system roots=" ;
    text += request.m_system_roots ? "on" : "off" ;
    text += " depth=" ;
    text += std::to_string(request.m_depth) ;
    text += " disk sweep=" ;
    text += request.m_disk_sweep ? "on" : "off" ;
    text += " user dir=" ;
    if (request.m_user_directory.empty())
    {
        text += "(none)" ;
    }
    else
    {
        text += scan::path_text(request.m_user_directory) ;
        if (auto_devkit)
        {
            text += " (auto devkit)" ;
        }
    }
    return text ;
}

/** 打印人读报告。 */
void print_text_report(const scan::ScanReport& report, const scan::NinjaTool& ninja,
    const scan::ScanRequest& request, bool auto_devkit, long long elapsed_ms)
{
    const MachineInfo machine = collect_machine() ;
    std::cout << styled("nx_dev scan report", std::string(core::color::bold) + core::color::bright_cyan) << '\n' ;
    std::cout << styled("machine   : ", core::color::bright_black) << machine.m_name << "  " << machine.m_windows << "  "
              << machine.m_architecture << '\n' ;
    std::cout << styled("scope     : ", core::color::bright_black) << scope_text(request, auto_devkit) << '\n' ;
    std::cout << styled("elapsed   : ", core::color::bright_black) << styled(std::to_string(elapsed_ms) + " ms",
        core::color::bright_white) << "\n\n" ;

    std::cout << section_header("toolchains", report.m_compilers.size()) << '\n' ;
    for (std::size_t index = 0 ; index < report.m_compilers.size() ; ++index)
    {
        const scan::Compiler& compiler = report.m_compilers[index] ;
        std::cout << "  " << styled(std::to_string(index + 1) + ") ", core::color::bright_black)
                  << styled(compiler.m_name, core::color::bright_white)
                  << " | toolset " << (compiler.m_toolset_version.empty() ? "(unknown)" : compiler.m_toolset_version)
                  << " | " << state_text(compiler.m_is_checked ? "checked" : "NOT CHECKED", compiler.m_is_checked)
                  << '\n' ;
        const bool kit_toolchain = compiler.m_vcvars_path.extension() == L".cmake" ;
        std::cout << "     cl      : "
                  << (compiler.m_cl_path.empty() ? "(none)" : scan::path_text(compiler.m_cl_path)) << '\n' ;
        std::cout << "     env     : "
                  << (compiler.m_vcvars_path.empty() ? "(none)" : scan::path_text(compiler.m_vcvars_path)) << '\n' ;
        std::cout << "     platform: "
                  << (compiler.m_platform_toolset.empty() ? "(unknown)" : compiler.m_platform_toolset) << '\n' ;
        std::cout << "     generator: "
                  << (compiler.m_generator.empty() ? "(none)" : compiler.m_generator) << '\n' ;
        std::cout << "     cl ver  : "
                  << (compiler.m_compiler_version.empty() ? "(unknown)" : compiler.m_compiler_version)
                  << " | _MSC_VER "
                  << (compiler.m_msvc_ver > 0 ? std::to_string(compiler.m_msvc_ver) : "(unknown)")
                  << " | kind " << toolchain_kind_text(compiler.m_kind) << '\n' ;
        if (kit_toolchain)
        {
            std::cout << "     select  : (pinned by devkit toolchain)\n" ;
        }
        else
        {
            std::cout << "     select  : "
                      << (compiler.m_vcvars_argument.empty() ? "(default toolset)" : compiler.m_vcvars_argument) << '\n' ;
        }
    }
    std::cout << '\n' ;

    std::cout << section_header("nx sdk", report.m_sdks.size()) << '\n' ;
    for (std::size_t index = 0 ; index < report.m_sdks.size() ; ++index)
    {
        const scan::NXSdk& sdk = report.m_sdks[index] ;
        std::cout << "  " << styled(std::to_string(index + 1) + ") ", core::color::bright_black)
                  << styled(sdk.m_version.empty() ? "NX (version unknown)" : "NX " + sdk.m_version,
                      core::color::bright_white)
                  << " | " << state_text(sdk.m_is_checked ? "checked" : "INCOMPLETE", sdk.m_is_checked)
                  << " | running=" << (sdk.m_is_running ? "yes" : "no")
                  << " | version from " << (sdk.m_version_source.empty() ? "unknown" : sdk.m_version_source) << '\n' ;
        std::cout << "     root    : " << (sdk.m_root.empty() ? "(none)" : scan::path_text(sdk.m_root)) << '\n' ;
        std::cout << "     uf      : "
                  << (sdk.m_uf_header_dir.empty() ? "(none)" : scan::path_text(sdk.m_uf_header_dir)) << '\n' ;
        std::cout << "     nxopen  : "
                  << (sdk.m_nxopen_header_dir.empty() ? "(none)" : scan::path_text(sdk.m_nxopen_header_dir)) << '\n' ;
        std::cout << "     lib     : "
                  << (sdk.m_lib_dir.empty() ? "(none)" : scan::path_text(sdk.m_lib_dir)) << '\n' ;
    }
    std::cout << '\n' ;

    std::cout << section_header("windows sdk", report.m_windows_sdks.size()) << '\n' ;
    for (std::size_t index = 0 ; index < report.m_windows_sdks.size() ; ++index)
    {
        const scan::WindowsSdk& sdk = report.m_windows_sdks[index] ;
        std::cout << "  " << styled(std::to_string(index + 1) + ") ", core::color::bright_black)
                  << styled("Windows SDK " + (sdk.m_version.empty() ? std::string("(version unknown)") : sdk.m_version),
                      core::color::bright_white)
                  << " | " << state_text(sdk.m_is_checked ? "checked" : "INCOMPLETE", sdk.m_is_checked)
                  << " | from " << (sdk.m_source.empty() ? "unknown" : sdk.m_source) << '\n' ;
        std::cout << "     root    : " << (sdk.m_root.empty() ? "(none)" : scan::path_text(sdk.m_root)) << '\n' ;
        std::cout << "     include : "
                  << (sdk.m_include_dir.empty() ? "(none)" : scan::path_text(sdk.m_include_dir)) << '\n' ;
        std::cout << "     lib     : " << (sdk.m_lib_dir.empty() ? "(none)" : scan::path_text(sdk.m_lib_dir)) << '\n' ;
        std::cout << "     bin     : " << (sdk.m_bin_dir.empty() ? "(none)" : scan::path_text(sdk.m_bin_dir)) << '\n' ;
    }
    std::cout << '\n' ;

    std::cout << section_header("ninja", ninja.m_found ? 1 : 0) << '\n' ;
    if (ninja.m_found)
    {
        std::cout << "  " << styled("ninja " + ninja.m_version, core::color::bright_white)
                  << " | " << state_text("found", true) << '\n' ;
        std::cout << "     exe     : " << scan::path_text(ninja.m_executable) << '\n' ;
    }
    else
    {
        std::cout << "  " << state_text("not found", false)
                  << "（.vscode/settings.json 不写 cmake.generator）\n" ;
    }
    std::cout << '\n' ;

    std::cout << styled("[summary]", std::string(core::color::bold) + core::color::bright_cyan)
              << " toolchains=" << styled(std::to_string(report.m_compilers.size()), core::color::bright_white)
              << " sdk=" << styled(std::to_string(report.m_sdks.size()), core::color::bright_white)
              << " winsdk=" << styled(std::to_string(report.m_windows_sdks.size()), core::color::bright_white)
              << " ninja=" << styled(ninja.m_found ? ninja.m_version : std::string("none"),
                  core::color::bright_white)
              << " reached=" << tier_text(report.m_reached)
              << " cancelled=" << (report.m_cancelled ? "yes" : "no") << '\n' ;
}

/** 打印机器可读 JSON，便于跨机器对比。 */
void print_json_report(const scan::ScanReport& report, const scan::NinjaTool& ninja,
    const scan::ScanRequest& request, bool auto_devkit, long long elapsed_ms)
{
    const MachineInfo machine = collect_machine() ;
    std::cout << "{\n" ;
    std::cout << "  \"tool\": \"nx_dev scan\",\n" ;
    std::cout << "  \"machine\": {\"name\": \"" << json_escape(machine.m_name)
              << "\", \"windows\": \"" << json_escape(machine.m_windows)
              << "\", \"arch\": \"" << json_escape(machine.m_architecture) << "\"},\n" ;
    std::cout << "  \"scope\": {\"system_roots\": " << json_bool(request.m_system_roots)
              << ", \"depth\": " << request.m_depth
              << ", \"disk_sweep\": " << json_bool(request.m_disk_sweep)
              << ", \"user_directory\": \"" << json_escape(scan::path_text(request.m_user_directory))
              << "\", \"user_directory_auto_devkit\": " << json_bool(auto_devkit)
              << "},\n" ;
    std::cout << "  \"elapsed_ms\": " << elapsed_ms << ",\n" ;
    std::cout << "  \"reached\": \"" << tier_text(report.m_reached) << "\",\n" ;
    std::cout << "  \"cancelled\": " << json_bool(report.m_cancelled) << ",\n" ;

    std::cout << "  \"toolchains\": [" ;
    for (std::size_t index = 0 ; index < report.m_compilers.size() ; ++index)
    {
        const scan::Compiler& compiler = report.m_compilers[index] ;
        std::cout << (index == 0 ? "\n" : ",\n") ;
        std::cout << "    {\"name\": \"" << json_escape(compiler.m_name)
                  << "\", \"version\": \"" << compiler_version_text(compiler.m_version)
                  << "\", \"toolset\": \"" << json_escape(compiler.m_toolset_version)
                  << "\", \"cl\": \"" << json_escape(scan::path_text(compiler.m_cl_path))
                  << "\", \"vcvars\": \"" << json_escape(scan::path_text(compiler.m_vcvars_path))
                  << "\", \"vcvars_args\": \"" << json_escape(compiler.m_vcvars_argument)
                  << "\", \"platform_toolset\": \"" << json_escape(compiler.m_platform_toolset)
                  << "\", \"generator\": \"" << json_escape(compiler.m_generator)
                  << "\", \"kind\": \"" << toolchain_kind_text(compiler.m_kind)
                  << "\", \"msvc_ver\": " << compiler.m_msvc_ver
                  << ", \"compiler_version\": \"" << json_escape(compiler.m_compiler_version)
                  << "\", \"checked\": " << json_bool(compiler.m_is_checked) << "}" ;
    }
    std::cout << (report.m_compilers.empty() ? "" : "\n") << "  ],\n" ;

    std::cout << "  \"sdks\": [" ;
    for (std::size_t index = 0 ; index < report.m_sdks.size() ; ++index)
    {
        const scan::NXSdk& sdk = report.m_sdks[index] ;
        std::cout << (index == 0 ? "\n" : ",\n") ;
        std::cout << "    {\"version\": \"" << json_escape(sdk.m_version)
                  << "\", \"version_source\": \"" << json_escape(sdk.m_version_source)
                  << "\", \"root\": \"" << json_escape(scan::path_text(sdk.m_root))
                  << "\", \"uf_include\": \"" << json_escape(scan::path_text(sdk.m_uf_header_dir))
                  << "\", \"nxopen_include\": \"" << json_escape(scan::path_text(sdk.m_nxopen_header_dir))
                  << "\", \"lib\": \"" << json_escape(scan::path_text(sdk.m_lib_dir))
                  << "\", \"checked\": " << json_bool(sdk.m_is_checked)
                  << ", \"running\": " << json_bool(sdk.m_is_running) << "}" ;
    }
    std::cout << (report.m_sdks.empty() ? "" : "\n") << "  ],\n" ;

    std::cout << "  \"windows_sdks\": [" ;
    for (std::size_t index = 0 ; index < report.m_windows_sdks.size() ; ++index)
    {
        const scan::WindowsSdk& sdk = report.m_windows_sdks[index] ;
        std::cout << (index == 0 ? "\n" : ",\n") ;
        std::cout << "    {\"version\": \"" << json_escape(sdk.m_version)
                  << "\", \"source\": \"" << json_escape(sdk.m_source)
                  << "\", \"root\": \"" << json_escape(scan::path_text(sdk.m_root))
                  << "\", \"include\": \"" << json_escape(scan::path_text(sdk.m_include_dir))
                  << "\", \"lib\": \"" << json_escape(scan::path_text(sdk.m_lib_dir))
                  << "\", \"bin\": \"" << json_escape(scan::path_text(sdk.m_bin_dir))
                  << "\", \"checked\": " << json_bool(sdk.m_is_checked) << "}" ;
    }
    std::cout << (report.m_windows_sdks.empty() ? "" : "\n") << "  ],\n" ;
    std::cout << "  \"ninja\": {\"found\": " << json_bool(ninja.m_found)
              << ", \"version\": \"" << json_escape(ninja.m_version)
              << "\", \"path\": \"" << json_escape(scan::path_text(ninja.m_executable))
              << "\"}\n" ;
    std::cout << "}\n" ;
}

/** 参数错误的用法说明。 */
void print_usage(std::ostream& stream)
{
    stream << "用法: nx_dev --chain-list [目录] [--deep] [--depth N] [--json]\n"
              "  扫描本机已安装的 MSVC 工具链、NX SDK 与 Windows SDK；只读，不弹窗。\n"
              "  目录    额外扫描的用户目录；不给时自动扫描 exe 所在 devkit（若有）\n"
              "  --deep  开启全盘兜底扫描（很慢，默认关闭）\n"
              "  --depth 递归深度（默认 5，范围 0-32）\n"
              "  --json  输出机器可读 JSON，便于跨机器对比\n" ;
}

/** 临时把控制台输出代码页切到 UTF-8，析构时还原；重定向输出时无害。 */
class ConsoleCodePageGuard
{
    public:
    ConsoleCodePageGuard() : m_saved(GetConsoleOutputCP())
    {
        if (m_saved != 0)
        {
            SetConsoleOutputCP(CP_UTF8) ;
        }
    }

    ~ConsoleCodePageGuard()
    {
        if (m_saved != 0)
        {
            SetConsoleOutputCP(m_saved) ;
        }
    }

    ConsoleCodePageGuard(const ConsoleCodePageGuard&) = delete ;
    ConsoleCodePageGuard& operator=(const ConsoleCodePageGuard&) = delete ;

    private:
    UINT m_saved ;
} ;
} // namespace

namespace app
{
int run_scan_command(const std::vector<std::string>& arguments)
{
    const ConsoleCodePageGuard code_page_guard ;
    (void) code_page_guard ;
    bool json = false ;
    bool deep = false ;
    int depth = 5 ;
    std::optional<scan::Path> user_directory ;
    for (std::size_t index = 0 ; index < arguments.size() ; ++index)
    {
        const std::string& argument = arguments[index] ;
        if (argument == "--json")
        {
            json = true ;
            continue ;
        }
        if (argument == "--deep")
        {
            deep = true ;
            continue ;
        }
        if (argument == "--depth")
        {
            if (index + 1 >= arguments.size())
            {
                std::cerr << "nx_dev --chain-list: --depth 缺少参数\n" ;
                print_usage(std::cerr) ;
                return 1 ;
            }
            ++index ;
            try
            {
                depth = std::stoi(arguments[index]) ;
            }
            catch (...)
            {
                std::cerr << "nx_dev --chain-list: --depth 需要整数: " << arguments[index] << '\n' ;
                return 1 ;
            }
            depth = std::clamp(depth, 0, 32) ;
            continue ;
        }
        if (argument.size() > 1 && argument.front() == '-')
        {
            std::cerr << "nx_dev --chain-list: 未知参数 " << argument << '\n' ;
            print_usage(std::cerr) ;
            return 1 ;
        }
        if (user_directory.has_value())
        {
            std::cerr << "nx_dev --chain-list: 最多只能给一个目录\n" ;
            print_usage(std::cerr) ;
            return 1 ;
        }
        user_directory = scan::path_from_text(argument) ;
    }

    if (user_directory.has_value())
    {
        std::error_code error ;
        if (!std::filesystem::is_directory(*user_directory, error))
        {
            std::cerr << "nx_dev --chain-list: 目录不存在，已跳过: "
                      << scan::path_text(*user_directory) << '\n' ;
        }
    }

    std::optional<scan::Path> devkit_root ;
    if (!user_directory.has_value())
    {
        devkit_root = detect_devkit_root() ;
    }

    scan::ScanRequest request ;
    request.m_system_roots = true ;
    request.m_depth = depth ;
    request.m_disk_sweep = deep ;
    if (user_directory.has_value())
    {
        request.m_user_directory = *user_directory ;
    }
    else if (devkit_root.has_value())
    {
        request.m_user_directory = *devkit_root ;
    }
    const bool auto_devkit = devkit_root.has_value() ;
    request.m_observer = [](const scan::ScanProgress& progress)
    {
        if (progress.m_stage_finished)
        {
            std::cerr << "[scan] " << tier_text(progress.m_tier)
                      << ": visited=" << progress.m_visited
                      << " found=" << progress.m_found << '\n' ;
        }
        return true ;
    } ;

    const auto started = std::chrono::steady_clock::now() ;
    const scan::ScanReport report = scan::scan_environment(request) ;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count() ;
    // Ninja 不进扫描缓存：本身只跑一次 ninja --version，顺带探测最省事。
    const scan::NinjaTool ninja = scan::detect_ninja() ;
    if (json)
    {
        print_json_report(report, ninja, request, auto_devkit, elapsed) ;
    }
    else
    {
        print_text_report(report, ninja, request, auto_devkit, elapsed) ;
    }
    return 0 ;
}
} // namespace app
