#include "../scan_control.h"
#include "scan_internal.h"
#include "../../core_module/dev_core.h"
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace scan
{
namespace
{
// ============================================================================
// 文本与路径
// ============================================================================

/** 宽字符转 UTF-8 且不抛异常；非法码位写成替换字符。 */
std::string lossy_utf8(const std::wstring& text)
{
    if (text.empty())
    {
        return {} ;
    }
    const int size = static_cast<int>(text.size()) ;
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr, 0, nullptr, nullptr) ;
    if (count <= 0)
    {
        return {} ;
    }
    std::string result(static_cast<std::size_t>(count), '\0') ;
    WideCharToMultiByte(CP_UTF8, 0, text.data(), size, result.data(), count, nullptr, nullptr) ;
    return result ;
}

/** UTF-8 转宽字符且不抛异常；非法字节写成替换字符。 */
std::wstring lossy_wide(const std::string& text)
{
    if (text.empty())
    {
        return {} ;
    }
    const int size = static_cast<int>(text.size()) ;
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0) ;
    if (count <= 0)
    {
        return {} ;
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0') ;
    MultiByteToWideChar(CP_UTF8, 0, text.data(), size, result.data(), count) ;
    return result ;
}

/** 把 ASCII 字母转成小写，用于不区分大小写的比较。 */
std::string ascii_lower(std::string text)
{
    for (char& letter : text)
    {
        if (letter >= 'A' && letter <= 'Z')
        {
            letter = static_cast<char>(letter - 'A' + 'a') ;
        }
    }
    return text ;
}

/** 生成去重用的路径键；同时抹平大小写、相对段和 junction 别名。 */
std::string path_key(const Path& path)
{
    std::error_code error ;
    const auto absolute = std::filesystem::weakly_canonical(path, error) ;
    const auto target = error ? path.lexically_normal() : absolute ;
    return ascii_lower(path_text(target)) ;
}

/** 判断路径是否为已存在的目录；无权限时返回 false。 */
bool directory_exists(const Path& path)
{
    std::error_code error ;
    return std::filesystem::is_directory(path, error) ;
}

/** 判断路径是否为已存在的常规文件；无权限时返回 false。 */
bool file_exists(const Path& path)
{
    std::error_code error ;
    return std::filesystem::is_regular_file(path, error) ;
}

/** 判断目录下是否有指定名字的子目录。 */
bool has_child_directory(const Path& parent, const std::string& name)
{
    return directory_exists(parent / lossy_wide(name)) ;
}

/** 递归扫描时跳过的系统目录与明显的第三方垃圾目录。 */
bool is_skipped_directory(const Path& directory)
{
    const auto name = ascii_lower(path_text(directory.filename())) ;
    return name == "$recycle.bin" || name == "windows" || name == "system volume information" ||
        name == "recovery" || name == "programdata" || name == "appdata" || name == "node_modules" ||
        name == ".git" || name == "venv" || name == ".venv" || name == "site-packages" || name == "winxs" ;
}

/** 读取环境变量；不存在、为空串或读取失败返回空。 */
Path environment_path(const wchar_t* name)
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
    while (!value.empty() && (value.back() == L'\\' || value.back() == L'/'))
    {
        value.pop_back() ;
    }
    return Path(value) ;
}

/** 去掉路径末尾的目录分隔符；保留盘符根("C:\")。 */
Path trim_trailing_separator(Path path)
{
    while (path.has_relative_path() && (path.filename().empty() || path.filename() == L"."))
    {
        path = path.parent_path() ;
    }
    return path ;
}

/** 去掉文本首尾的 ASCII 空白。 */
std::string trim_ascii(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    {
        text.erase(text.begin()) ;
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    {
        text.pop_back() ;
    }
    return text ;
}

// ============================================================================
// 版本
// ============================================================================

/** 按 MSVC 工具集版本推断大类：14.4x 属于 v143，14.5x 属于 v145。 */
ToolChainVersion version_from_toolset(const std::wstring& text)
{
    if (text.starts_with(L"14.5"))
    {
        return ToolChainVersion::V145 ;
    }
    if (text.starts_with(L"14.4") || text.starts_with(L"14.3"))
    {
        return ToolChainVersion::V143 ;
    }
    if (text.starts_with(L"14.2"))
    {
        return ToolChainVersion::V142 ;
    }
    if (text.starts_with(L"14.1"))
    {
        return ToolChainVersion::V141 ;
    }
    if (text.starts_with(L"14.0"))
    {
        return ToolChainVersion::V140 ;
    }
    return ToolChainVersion::Undefined ;
}

/** 工具链大类版本的显示名。 */
std::string version_name(ToolChainVersion version)
{
    switch (version)
    {
        case ToolChainVersion::V140: return "MSVC v140" ;
        case ToolChainVersion::V141: return "MSVC v141" ;
        case ToolChainVersion::V142: return "MSVC v142" ;
        case ToolChainVersion::V143: return "MSVC v143" ;
        case ToolChainVersion::V145: return "MSVC v145" ;
        default: return "MSVC" ;
    }
}

/** 把版本文本拆成数字段；非数字字符当分隔符。 */
std::vector<long long> version_parts(const std::string& text)
{
    std::vector<long long> parts ;
    long long part = 0 ;
    bool started = false ;
    for (const char letter : text)
    {
        if (letter >= '0' && letter <= '9')
        {
            part = part * 10 + (letter - '0') ;
            started = true ;
        }
        else if (letter == '.')
        {
            parts.push_back(started ? part : 0) ;
            part = 0 ;
            started = false ;
        }
    }
    if (started)
    {
        parts.push_back(part) ;
    }
    return parts ;
}

/** 按数字段比较版本文本：left 大返回正数，小返回负数。 */
int compare_version_text_impl(const std::string& left, const std::string& right)
{
    const auto left_parts = version_parts(left) ;
    const auto right_parts = version_parts(right) ;
    const std::size_t count = std::max(left_parts.size(), right_parts.size()) ;
    for (std::size_t index = 0 ; index < count ; ++index)
    {
        const long long left_value = index < left_parts.size() ? left_parts[index] : 0 ;
        const long long right_value = index < right_parts.size() ? right_parts[index] : 0 ;
        if (left_value != right_value)
        {
            return left_value < right_value ? -1 : 1 ;
        }
    }
    return 0 ;
}

/** 取版本号的主.次两段，如 14.xx.yyyyy -> 14.xx。 */
std::string toolset_major_minor(const std::string& version)
{
    const auto first = version.find('.') ;
    if (first == std::string::npos)
    {
        return version ;
    }
    const auto second = version.find('.', first + 1) ;
    return second == std::string::npos ? version : version.substr(0, second) ;
}

/** 从一段文本里抠出第一个版本号（数字与点）；没有则返回空。 */
std::string first_version_digits(const std::string& text)
{
    std::string version ;
    bool started = false ;
    for (const char letter : text)
    {
        const bool digit = letter >= '0' && letter <= '9' ;
        if (digit || (started && letter == '.'))
        {
            version.push_back(letter) ;
            started = true ;
        }
        else if (started)
        {
            break ;
        }
    }
    return version ;
}

// ============================================================================
// 子进程
// ============================================================================

/** 子进程执行结果；启动失败、超时和正常退出分开记录。 */
struct CommandResult
{
    bool m_started = false ;
    bool m_timed_out = false ;
    DWORD m_exit_code = 0 ;
    std::string m_output ;
    std::string m_error ;
} ;

/** 执行控制台程序并持续排空输出；最多等待 timeout_ms 毫秒。 */
CommandResult run_text_command(const Path& executable, const std::wstring& arguments, DWORD timeout_ms)
{
    CommandResult result ;
    SECURITY_ATTRIBUTES attributes = {} ;
    attributes.nLength = sizeof(attributes) ;
    attributes.bInheritHandle = TRUE ;
    HANDLE read_pipe = nullptr ;
    HANDLE write_pipe = nullptr ;
    if (!CreatePipe(&read_pipe, &write_pipe, &attributes, 0))
    {
        result.m_error = "CreatePipe 失败: " + std::to_string(GetLastError()) ;
        return result ;
    }
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0))
    {
        result.m_error = "SetHandleInformation 失败: " + std::to_string(GetLastError()) ;
        CloseHandle(read_pipe) ;
        CloseHandle(write_pipe) ;
        return result ;
    }
    HANDLE null_input = GetStdHandle(STD_INPUT_HANDLE) ;
    bool close_input = false ;
    if (null_input == nullptr || null_input == INVALID_HANDLE_VALUE)
    {
        null_input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &attributes, OPEN_EXISTING, 0, nullptr) ;
        close_input = null_input != nullptr && null_input != INVALID_HANDLE_VALUE ;
    }
    STARTUPINFOW startup = {} ;
    startup.cb = sizeof(startup) ;
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW ;
    startup.wShowWindow = SW_HIDE ;
    startup.hStdOutput = write_pipe ;
    startup.hStdError = write_pipe ;
    startup.hStdInput = null_input ;
    PROCESS_INFORMATION process = {} ;
    std::wstring command_line = L"\"" + executable.wstring() + L"\" " + arguments ;
    const BOOL started = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) ;
    const DWORD create_error = GetLastError() ;
    CloseHandle(write_pipe) ;
    if (close_input)
    {
        CloseHandle(null_input) ;
    }
    if (!started)
    {
        result.m_error = "CreateProcess 失败: " + std::to_string(create_error) ;
        CloseHandle(read_pipe) ;
        return result ;
    }
    result.m_started = true ;
    CloseHandle(process.hThread) ;
    const ULONGLONG deadline = GetTickCount64() + timeout_ms ;
    constexpr std::size_t output_limit = 64 * 1024 ;
    std::array<char, 4096> buffer = {} ;
    bool process_exited = false ;
    while (true)
    {
        if (!process_exited && GetTickCount64() >= deadline)
        {
            TerminateProcess(process.hProcess, 1) ;
            result.m_timed_out = true ;
            process_exited = true ;
        }
        DWORD available = 0 ;
        if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr))
        {
            break ;
        }
        if (available > 0)
        {
            const DWORD wanted = std::min<DWORD>(available, static_cast<DWORD>(buffer.size())) ;
            DWORD read = 0 ;
            if (!ReadFile(read_pipe, buffer.data(), wanted, &read, nullptr) || read == 0)
            {
                break ;
            }
            if (result.m_output.size() < output_limit)
            {
                result.m_output.append(buffer.data(), read) ;
            }
            continue ;
        }
        if (process_exited)
        {
            break ;
        }
        const DWORD wait = WaitForSingleObject(process.hProcess, 20) ;
        if (wait == WAIT_OBJECT_0)
        {
            process_exited = true ;
            continue ;
        }
        if (wait == WAIT_FAILED)
        {
            result.m_error = "WaitForSingleObject 失败: " + std::to_string(GetLastError()) ;
            break ;
        }
    }
    if (!process_exited)
    {
        TerminateProcess(process.hProcess, 1) ;
        result.m_timed_out = true ;
        WaitForSingleObject(process.hProcess, 500) ;
    }
    DWORD exit_code = 0 ;
    if (GetExitCodeProcess(process.hProcess, &exit_code))
    {
        result.m_exit_code = exit_code ;
    }
    CloseHandle(process.hProcess) ;
    CloseHandle(read_pipe) ;
    return result ;
}

// ============================================================================
// 编译器身份：_MSC_VER 探测与便携工具集识别
// ============================================================================

/** 把 _MSC_VER 映射到工具链大类版本；未知返回 Undefined。 */
ToolChainVersion version_from_msvc_ver_impl(int msvc_ver)
{
    if (msvc_ver >= 1950 && msvc_ver < 1960)
    {
        return ToolChainVersion::V145 ;
    }
    if (msvc_ver >= 1930 && msvc_ver < 1950)
    {
        return ToolChainVersion::V143 ;
    }
    if (msvc_ver >= 1920 && msvc_ver < 1930)
    {
        return ToolChainVersion::V142 ;
    }
    if (msvc_ver >= 1910 && msvc_ver < 1920)
    {
        return ToolChainVersion::V141 ;
    }
    if (msvc_ver >= 1900 && msvc_ver < 1910)
    {
        return ToolChainVersion::V140 ;
    }
    return ToolChainVersion::Undefined ;
}

/** 从 cl 版本文本（如 19.29.30159）推出 _MSC_VER 整数（1929）；推不出返回 0。 */
int msvc_ver_from_compiler_version(const std::string& version)
{
    const std::vector<long long> parts = version_parts(version) ;
    if (parts.size() < 2 || parts[0] != 19)
    {
        return 0 ;
    }
    return static_cast<int>(parts[0] * 100 + parts[1]) ;
}

/** 把 _MSC_FULL_VER（192930159）格式化成 19.29.30159。 */
std::string format_msvc_full_version(int full_version)
{
    if (full_version <= 0)
    {
        return {} ;
    }
    const int major = full_version / 10000000 ;
    const int minor = (full_version / 100000) % 100 ;
    const int build = full_version % 100000 ;
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(build) ;
}

/** 从预处理输出里取 "名字=数字" 形式的宏值；找不到返回 0。 */
int parse_macro_int(const std::string& output, const std::string& name)
{
    const std::string marker = name + "=" ;
    const std::size_t position = output.find(marker) ;
    if (position == std::string::npos)
    {
        return 0 ;
    }
    std::size_t index = position + marker.size() ;
    int value = 0 ;
    bool started = false ;
    while (index < output.size() && output[index] >= '0' && output[index] <= '9')
    {
        if (value > 100000000)
        {
            return 0 ;
        }
        value = value * 10 + (output[index] - '0') ;
        started = true ;
        ++index ;
    }
    return started ? value : 0 ;
}

/** 从任意文本里找形如 19.29.30159 的编译器版本号；找不到返回空。 */
std::string find_compiler_version_text(const std::string& text)
{
    for (std::size_t index = 0 ; index + 3 < text.size() ; ++index)
    {
        if (text[index] != '1' || text[index + 1] != '9' || text[index + 2] != '.')
        {
            continue ;
        }
        std::size_t end = index + 3 ;
        std::size_t dots = 1 ;
        bool has_digits = false ;
        while (end < text.size())
        {
            const char letter = text[end] ;
            if (letter >= '0' && letter <= '9')
            {
                has_digits = true ;
                ++end ;
                continue ;
            }
            if (letter == '.' && dots < 2)
            {
                ++dots ;
                ++end ;
                continue ;
            }
            break ;
        }
        if (has_digits && dots == 2)
        {
            return text.substr(index, end - index) ;
        }
    }
    return {} ;
}

/** 探针源文件路径；缓存目录不可用返回空。 */
Path msvc_probe_file()
{
    const Path cache = engine::cache_directory() ;
    if (cache.empty())
    {
        return {} ;
    }
    const Path directory = cache / L"probe" ;
    std::error_code error ;
    std::filesystem::create_directories(directory, error) ;
    if (error)
    {
        return {} ;
    }
    return directory / L"msvc_probe.c" ;
}

/** 跑 cl 预处理探针并返回输出；失败返回 false。 */
bool run_msvc_probe(const Path& cl_path, std::string& output)
{
    const Path probe_file = msvc_probe_file() ;
    if (probe_file.empty())
    {
        return false ;
    }
    {
        std::ofstream stream(probe_file, std::ios::binary) ;
        if (!stream)
        {
            return false ;
        }
        stream << "NX_MSC_VER=_MSC_VER\r\nNX_MSC_FULL_VER=_MSC_FULL_VER\r\n" ;
    }
    const std::wstring arguments = L"/nologo /EP /TC \"" + probe_file.wstring() + L"\"" ;
    const CommandResult command = run_text_command(cl_path, arguments, 3000) ;
    if (!command.m_started || command.m_timed_out || command.m_exit_code != 0)
    {
        return false ;
    }
    output = command.m_output ;
    return true ;
}

/** 探测 cl 版本：优先 _MSC_VER 宏，失败用 /Bv 兜底；成功时回填编译器字段。 */
bool probe_compiler_impl(Compiler& compiler)
{
    if (compiler.m_cl_path.empty() || !file_exists(compiler.m_cl_path))
    {
        return false ;
    }
    if (compiler.m_msvc_ver == 0 || compiler.m_compiler_version.empty())
    {
        std::string output ;
        if (run_msvc_probe(compiler.m_cl_path, output))
        {
            const int msvc_ver = parse_macro_int(output, "NX_MSC_VER") ;
            const int full_version = parse_macro_int(output, "NX_MSC_FULL_VER") ;
            if (msvc_ver > 0)
            {
                compiler.m_msvc_ver = msvc_ver ;
                if (full_version > 0)
                {
                    compiler.m_compiler_version = format_msvc_full_version(full_version) ;
                }
            }
        }
    }
    if (compiler.m_compiler_version.empty())
    {
        const CommandResult command = run_text_command(compiler.m_cl_path, L"/Bv", 3000) ;
        if (command.m_started && !command.m_timed_out && command.m_exit_code == 0)
        {
            compiler.m_compiler_version = find_compiler_version_text(command.m_output) ;
        }
    }
    if (compiler.m_msvc_ver == 0 && !compiler.m_compiler_version.empty())
    {
        compiler.m_msvc_ver = msvc_ver_from_compiler_version(compiler.m_compiler_version) ;
    }
    const ToolChainVersion probed = version_from_msvc_ver_impl(compiler.m_msvc_ver) ;
    if (probed != ToolChainVersion::Undefined)
    {
        compiler.m_version = probed ;
    }
    return compiler.m_msvc_ver != 0 || !compiler.m_compiler_version.empty() ;
}

/** 从 cl 路径反推工具集根：<root>/bin/<host>/<arch>/cl.exe 的那层 root。 */
Path portable_toolset_root_impl(const Path& cl_path)
{
    const Path arch = cl_path.parent_path() ;
    const Path host = arch.parent_path() ;
    const Path bin = host.parent_path() ;
    if (bin.empty() || ascii_lower(path_text(bin.filename())) != "bin")
    {
        return {} ;
    }
    const Path root = bin.parent_path() ;
    if (root.empty() || !directory_exists(root / L"include"))
    {
        return {} ;
    }
    return root ;
}

/** 在工具集根下找 cl.exe：bin/<host>/<arch>/cl.exe，优先 HostX64/x64。 */
Path find_cl_in_toolset(const Path& toolset_root)
{
    std::error_code error ;
    std::filesystem::directory_iterator host_iterator(toolset_root / L"bin", error) ;
    if (error)
    {
        return {} ;
    }
    Path fallback ;
    for (const auto& host_entry : host_iterator)
    {
        std::error_code entry_error ;
        if (!host_entry.is_directory(entry_error))
        {
            continue ;
        }
        std::error_code arch_error ;
        std::filesystem::directory_iterator arch_iterator(host_entry.path(), arch_error) ;
        if (arch_error)
        {
            continue ;
        }
        for (const auto& arch_entry : arch_iterator)
        {
            std::error_code arch_entry_error ;
            if (!arch_entry.is_directory(arch_entry_error))
            {
                continue ;
            }
            const Path cl = arch_entry.path() / L"cl.exe" ;
            if (!file_exists(cl))
            {
                continue ;
            }
            const std::string host_name = ascii_lower(path_text(host_entry.path().filename())) ;
            const std::string arch_name = ascii_lower(path_text(arch_entry.path().filename())) ;
            if (arch_name == "x64" && host_name.find("x64") != std::string::npos)
            {
                return cl ;
            }
            if (fallback.empty())
            {
                fallback = cl ;
            }
        }
    }
    return fallback ;
}

/** 从工具集根向上找 *toolchain*.cmake（兼容 NXDevKit 等便携布局）；找 4 层。 */
Path find_portable_toolchain_file(const Path& toolset_root)
{
    Path current = toolset_root ;
    for (int level = 0 ; level < 4 && !current.empty() ; ++level)
    {
        std::error_code error ;
        std::filesystem::directory_iterator iterator(current, error) ;
        if (!error)
        {
            for (const auto& entry : iterator)
            {
                std::error_code entry_error ;
                if (!entry.is_regular_file(entry_error))
                {
                    continue ;
                }
                const std::string name = ascii_lower(path_text(entry.path().filename())) ;
                if (name.size() > 6 && name.ends_with(".cmake") &&
                    name.find("toolchain") != std::string::npos)
                {
                    return entry.path() ;
                }
            }
        }
        const Path parent = current.parent_path() ;
        if (parent.empty() || parent == current)
        {
            break ;
        }
        current = parent ;
    }
    return {} ;
}

/** 由工具集根构造便携编译器条目；不是工具集根返回 nullopt。 */
std::optional<Compiler> compiler_from_toolset_root(const Path& toolset_root)
{
    const Path cl = find_cl_in_toolset(toolset_root) ;
    if (cl.empty())
    {
        return std::nullopt ;
    }
    Compiler compiler ;
    compiler.m_kind = ToolChainKind::Portable ;
    compiler.m_toolset_version = path_text(toolset_root.filename()) ;
    compiler.m_version = version_from_toolset(toolset_root.filename().wstring()) ;
    compiler.m_cl_path = cl ;
    compiler.m_vcvars_path = find_portable_toolchain_file(toolset_root) ;
    compiler.m_include_dirs = { toolset_root / L"include" } ;
    const std::string arch = ascii_lower(path_text(cl.parent_path().filename())) ;
    const Path lib = toolset_root / L"lib" / lossy_wide(arch) ;
    if (directory_exists(lib))
    {
        compiler.m_lib_dirs = { lib } ;
    }
    compiler.m_name = version_name(compiler.m_version) ;
    compiler.m_is_checked = check_compiler(compiler) ;
    return compiler ;
}

/** 识别目录下的全部便携编译器：目录本身是工具集根、Tools/<版本> 容器或 MSVC 容器。 */
std::vector<Compiler> portable_compilers_from_directory(const Path& directory)
{
    std::vector<Compiler> result ;
    const auto push = [&result](std::optional<Compiler> compiler)
    {
        if (compiler.has_value())
        {
            result.push_back(std::move(*compiler)) ;
        }
    } ;
    push(compiler_from_toolset_root(directory)) ;
    const Path tools = directory / L"Tools" ;
    if (directory_exists(tools))
    {
        std::error_code error ;
        std::filesystem::directory_iterator iterator(tools, error) ;
        if (!error)
        {
            for (const auto& entry : iterator)
            {
                std::error_code entry_error ;
                if (!entry.is_directory(entry_error))
                {
                    continue ;
                }
                push(compiler_from_toolset_root(entry.path())) ;
            }
        }
    }
    const Path msvc = directory / L"MSVC" ;
    if (!msvc.empty() && msvc != directory && directory_exists(msvc))
    {
        auto nested = portable_compilers_from_directory(msvc) ;
        result.insert(result.end(), nested.begin(), nested.end()) ;
    }
    return result ;
}

// ============================================================================
// 注册表
// ============================================================================

/** 展开字符串里的 %VAR% 环境变量；失败时原样返回。 */
std::wstring expand_environment(const std::wstring& text)
{
    const DWORD size = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0) ;
    if (size <= 1)
    {
        return text ;
    }
    std::wstring expanded(size, L'\0') ;
    const DWORD written = ExpandEnvironmentStringsW(text.c_str(), expanded.data(), size) ;
    if (written == 0 || written > size)
    {
        return text ;
    }
    expanded.resize(written - 1) ;
    return expanded ;
}

/** 读取一个注册表键下的全部字符串值；键不存在时返回空。 */
std::vector<std::pair<std::wstring, std::wstring>> registry_string_values(
    const wchar_t* key_path)
{
    std::vector<std::pair<std::wstring, std::wstring>> result ;
    HKEY key = nullptr ;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key_path, 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return result ;
    }
    for (DWORD index = 0 ; ; ++index)
    {
        std::array<wchar_t, 256> name = {} ;
        DWORD name_size = static_cast<DWORD>(name.size()) ;
        std::array<wchar_t, 4096> data = {} ;
        DWORD data_size = static_cast<DWORD>(data.size() * sizeof(wchar_t)) ;
        const LSTATUS status = RegEnumValueW(key, index, name.data(), &name_size, nullptr, nullptr,
            reinterpret_cast<LPBYTE>(data.data()), &data_size) ;
        if (status == ERROR_NO_MORE_ITEMS)
        {
            break ;
        }
        if (status != ERROR_SUCCESS || data_size < sizeof(wchar_t))
        {
            continue ;
        }
        std::wstring value(data.data(), data_size / sizeof(wchar_t)) ;
        while (!value.empty() && value.back() == L'\0')
        {
            value.pop_back() ;
        }
        if (value.empty())
        {
            continue ;
        }
        result.emplace_back(std::wstring(name.data(), name_size), expand_environment(value)) ;
    }
    RegCloseKey(key) ;
    return result ;
}

/** 读取一个注册表字符串值；键或值不存在、类型不对返回空。 */
std::wstring registry_string_value(const wchar_t* key_path, const wchar_t* value_name)
{
    HKEY key = nullptr ;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key_path, 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return {} ;
    }
    std::array<wchar_t, 4096> buffer = {} ;
    DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t)) ;
    DWORD type = 0 ;
    const LSTATUS status = RegQueryValueExW(key, value_name, nullptr, &type,
        reinterpret_cast<LPBYTE>(buffer.data()), &size) ;
    RegCloseKey(key) ;
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t))
    {
        return {} ;
    }
    std::wstring value(buffer.data(), size / sizeof(wchar_t)) ;
    while (!value.empty() && value.back() == L'\0')
    {
        value.pop_back() ;
    }
    return expand_environment(value) ;
}

// ============================================================================
// Visual Studio / MSVC 工具集
// ============================================================================

/** 从版本文本开头抠出主段，如 18.6.11819.183 -> 18。 */
std::string version_major(const std::string& text)
{
    std::string digits ;
    for (const char letter : text)
    {
        if (letter < '0' || letter > '9')
        {
            break ;
        }
        digits.push_back(letter) ;
    }
    return digits ;
}

/** 判断文本是否全是数字。 */
bool digits_only(const std::string& text)
{
    if (text.empty())
    {
        return false ;
    }
    for (const char letter : text)
    {
        if (letter < '0' || letter > '9')
        {
            return false ;
        }
    }
    return true ;
}

/** 从文本里找第一个独立出现、以 2 开头的 4 位年份；没有返回空。 */
std::string year_from_text(const std::string& text)
{
    for (std::size_t index = 0 ; index + 4 <= text.size() ; ++index)
    {
        if (index > 0 && text[index - 1] >= '0' && text[index - 1] <= '9')
        {
            continue ;
        }
        if (index + 4 < text.size() && text[index + 4] >= '0' && text[index + 4] <= '9')
        {
            continue ;
        }
        const std::string candidate = text.substr(index, 4) ;
        if (candidate.front() == '2' && digits_only(candidate))
        {
            return candidate ;
        }
    }
    return {} ;
}

/** 从安装路径的目录名里找年份；取最内层的那一段。 */
std::string year_from_path(const Path& root)
{
    std::string year ;
    for (const Path& part : root)
    {
        const std::string text = path_text(part) ;
        if (text.size() == 4 && text.front() == '2' && digits_only(text))
        {
            year = text ;
        }
    }
    return year ;
}

/** 从安装路径的目录名里找 1-2 位的产品主版本；取最内层的那一段。 */
std::string major_from_path(const Path& root)
{
    std::string major ;
    for (const Path& part : root)
    {
        const std::string text = path_text(part) ;
        if (text.size() <= 2 && text.front() != '0' && digits_only(text))
        {
            major = text ;
        }
    }
    return major ;
}

/** 从含 Visual Studio 字样的目录/显示名里抠主版本，如 Visual Studio 14.0 -> 14。 */
std::string major_from_visual_studio_name(const std::string& name)
{
    const auto marker = ascii_lower(name).find("visual studio") ;
    if (marker == std::string::npos)
    {
        return {} ;
    }
    return version_major(first_version_digits(name.substr(marker))) ;
}

/** 组一个 VS 安装实例，并补上能从安装路径推导出的主版本与年份。 */
engine::VisualStudioInstall make_visual_studio_install(const Path& root)
{
    engine::VisualStudioInstall install ;
    install.m_root = trim_trailing_separator(root) ;
    install.m_major = major_from_path(install.m_root) ;
    if (install.m_major.empty())
    {
        install.m_major = major_from_visual_studio_name(path_text(install.m_root.filename())) ;
    }
    install.m_year = year_from_path(install.m_root) ;
    return install ;
}

/** 从注册表 VS7 键收集 VS 安装实例；值名形如 18.0 时顺带带上主版本。 */
std::vector<engine::VisualStudioInstall> registry_visual_studios()
{
    constexpr std::array<const wchar_t*, 2> keys = {
        L"SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7" ,
        L"SOFTWARE\\WOW6432Node\\Microsoft\\VisualStudio\\SxS\\VS7" ,
    } ;
    std::vector<engine::VisualStudioInstall> result ;
    for (const wchar_t* key : keys)
    {
        for (const auto& [name, value] : registry_string_values(key))
        {
            if (value.empty())
            {
                continue ;
            }
            engine::VisualStudioInstall install = make_visual_studio_install(Path(value)) ;
            const std::string major = version_major(lossy_utf8(name)) ;
            if (!major.empty())
            {
                install.m_major = major ;
            }
            result.push_back(std::move(install)) ;
        }
    }
    return result ;
}

/** 从 JSON 文本里取某个字符串字段；转义按 JSON 规则还原。 */
std::string json_string_field(const std::string& text, const std::string& name)
{
    const std::string key = "\"" + name + "\"" ;
    auto position = text.find(key) ;
    if (position == std::string::npos)
    {
        return {} ;
    }
    position += key.size() ;
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0)
    {
        ++position ;
    }
    if (position >= text.size() || text[position] != ':')
    {
        return {} ;
    }
    ++position ;
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0)
    {
        ++position ;
    }
    if (position >= text.size() || text[position] != '"')
    {
        return {} ;
    }
    ++position ;
    std::string value ;
    while (position < text.size())
    {
        const char letter = text[position] ;
        if (letter == '"')
        {
            break ;
        }
        if (letter == '\\' && position + 1 < text.size())
        {
            ++position ;
            const char escaped = text[position] ;
            switch (escaped)
            {
                case 'n': value.push_back('\n') ; break ;
                case 'r': value.push_back('\r') ; break ;
                case 't': value.push_back('\t') ; break ;
                default: value.push_back(escaped) ; break ;
            }
            ++position ;
            continue ;
        }
        value.push_back(letter) ;
        ++position ;
    }
    return value ;
}

/** 逐个切出 JSON 文本里的顶层对象并交给 handler。 */
template <typename Handler>
void for_each_json_object(const std::string& text, Handler&& handler)
{
    int depth = 0 ;
    std::size_t begin = 0 ;
    bool in_string = false ;
    for (std::size_t index = 0 ; index < text.size() ; ++index)
    {
        const char letter = text[index] ;
        if (in_string)
        {
            if (letter == '\\')
            {
                ++index ;
                continue ;
            }
            if (letter == '"')
            {
                in_string = false ;
            }
            continue ;
        }
        if (letter == '"')
        {
            in_string = true ;
            continue ;
        }
        if (letter == '{')
        {
            if (depth == 0)
            {
                begin = index ;
            }
            ++depth ;
            continue ;
        }
        if (letter == '}')
        {
            --depth ;
            if (depth == 0)
            {
                handler(text.substr(begin, index - begin + 1)) ;
            }
        }
    }
}

/** 用 vswhere 枚举带 C++ 工具的 VS 安装实例；没有 vswhere 返回空。 */
std::vector<engine::VisualStudioInstall> vswhere_visual_studios()
{
    std::vector<engine::VisualStudioInstall> result ;
    const Path program_files_x86 = environment_path(L"ProgramFiles(x86)") ;
    if (program_files_x86.empty())
    {
        return result ;
    }
    const Path tool = program_files_x86 / L"Microsoft Visual Studio" / L"Installer" / L"vswhere.exe" ;
    if (!file_exists(tool))
    {
        return result ;
    }
    const CommandResult command = run_text_command(tool,
        L"-all -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 "
        L"-format json", 8000) ;
    if (!command.m_started || command.m_timed_out || command.m_exit_code != 0)
    {
        return result ;
    }
    for_each_json_object(command.m_output, [&result](const std::string& object)
    {
        const std::string root_text = json_string_field(object, "installationPath") ;
        if (root_text.empty())
        {
            return ;
        }
        engine::VisualStudioInstall install = make_visual_studio_install(Path(lossy_wide(root_text))) ;
        install.m_display_name = json_string_field(object, "displayName") ;
        install.m_version = json_string_field(object, "installationVersion") ;
        if (install.m_major.empty())
        {
            install.m_major = version_major(install.m_version) ;
        }
        if (install.m_year.empty())
        {
            install.m_year = year_from_text(install.m_display_name) ;
        }
        result.push_back(std::move(install)) ;
    }) ;
    return result ;
}

/** 常规安装位置下的 VS 安装实例；只枚举 Microsoft Visual Studio/<产品>/<版本> 布局。 */
std::vector<engine::VisualStudioInstall> common_visual_studios()
{
    std::vector<engine::VisualStudioInstall> result ;
    for (const wchar_t* name : { L"ProgramFiles" , L"ProgramFiles(x86)" })
    {
        const Path program_files = environment_path(name) ;
        if (program_files.empty())
        {
            continue ;
        }
        std::error_code error ;
        std::filesystem::directory_iterator products(program_files / L"Microsoft Visual Studio", error) ;
        if (error)
        {
            continue ;
        }
        for (const auto& product : products)
        {
            std::error_code product_error ;
            if (!product.is_directory(product_error))
            {
                continue ;
            }
            std::error_code edition_error ;
            std::filesystem::directory_iterator editions(product.path(), edition_error) ;
            if (edition_error)
            {
                continue ;
            }
            for (const auto& edition : editions)
            {
                std::error_code directory_error ;
                if (!edition.is_directory(directory_error) ||
                    !directory_exists(edition.path() / L"VC"))
                {
                    continue ;
                }
                result.push_back(make_visual_studio_install(edition.path())) ;
            }
        }
    }
    return result ;
}

/** 注册表、vswhere 与常见安装位置的并集；按路径去重并合并证据。 */
std::vector<engine::VisualStudioInstall> installed_visual_studios()
{
    std::vector<engine::VisualStudioInstall> result ;
    std::map<std::string, std::size_t> positions ;
    const auto merge = [&result, &positions](engine::VisualStudioInstall install)
    {
        const std::string key = path_key(install.m_root) ;
        if (key.empty())
        {
            return ;
        }
        const auto found = positions.find(key) ;
        if (found == positions.end())
        {
            positions.emplace(key, result.size()) ;
            result.push_back(std::move(install)) ;
            return ;
        }
        engine::VisualStudioInstall& target = result[found->second] ;
        if (target.m_display_name.empty())
        {
            target.m_display_name = std::move(install.m_display_name) ;
        }
        if (target.m_version.empty())
        {
            target.m_version = std::move(install.m_version) ;
        }
        if (target.m_major.empty())
        {
            target.m_major = std::move(install.m_major) ;
        }
        if (target.m_year.empty())
        {
            target.m_year = std::move(install.m_year) ;
        }
    } ;
    for (auto& install : registry_visual_studios())
    {
        merge(std::move(install)) ;
    }
    for (auto& install : vswhere_visual_studios())
    {
        merge(std::move(install)) ;
    }
    for (auto& install : common_visual_studios())
    {
        merge(std::move(install)) ;
    }
    return result ;
}

/** 读取文本文件的第一行并去掉首尾空白；读不到返回空。 */
std::string read_first_line(const Path& file)
{
    std::ifstream stream(file) ;
    if (!stream)
    {
        return {} ;
    }
    std::string text ;
    std::getline(stream, text) ;
    return trim_ascii(text) ;
}

/** 读取 VS 安装的默认 MSVC 工具集版本；读不到返回空。 */
std::string default_toolset_version(const Path& install_dir)
{
    return read_first_line(install_dir / L"VC" / L"Auxiliary" / L"Build" /
        L"Microsoft.VCToolsVersion.default.txt") ;
}

/** 从平台工具集名推工具链大类版本：v140 → V140 ... v145 → V145；不认识返回 Undefined。 */
ToolChainVersion version_from_platform_toolset(const std::string& toolset)
{
    if (toolset == "v140")
    {
        return ToolChainVersion::V140 ;
    }
    if (toolset == "v141")
    {
        return ToolChainVersion::V141 ;
    }
    if (toolset == "v142")
    {
        return ToolChainVersion::V142 ;
    }
    if (toolset == "v143")
    {
        return ToolChainVersion::V143 ;
    }
    if (toolset == "v145")
    {
        return ToolChainVersion::V145 ;
    }
    return ToolChainVersion::Undefined ;
}

/** 平台工具集兼容的 MSVC 工具集目录前缀；v143 同时覆盖 14.3x 与 14.4x。 */
std::vector<std::string> msvc_toolset_prefixes(ToolChainVersion version)
{
    switch (version)
    {
        case ToolChainVersion::V140: return { "14.0" } ;
        case ToolChainVersion::V141: return { "14.1" } ;
        case ToolChainVersion::V142: return { "14.2" } ;
        case ToolChainVersion::V143: return { "14.3" , "14.4" } ;
        case ToolChainVersion::V145: return { "14.5" } ;
        case ToolChainVersion::Undefined: return {} ;
    }
    return {} ;
}

/** 判断工具集目录名是否与平台工具集兼容。 */
bool toolset_compatible_with(const std::string& toolset_version, ToolChainVersion version)
{
    for (const std::string& prefix : msvc_toolset_prefixes(version))
    {
        if (toolset_version.rfind(prefix, 0) == 0)
        {
            return true ;
        }
    }
    return false ;
}

/** 在 MSVC 目录里找与平台工具集兼容的最高工具集目录；没有返回空。 */
Path highest_compatible_toolset_dir(const Path& msvc_root, ToolChainVersion version)
{
    Path best ;
    std::error_code error ;
    std::filesystem::directory_iterator iterator(msvc_root, error) ;
    if (error)
    {
        return best ;
    }
    for (const auto& entry : iterator)
    {
        std::error_code entry_error ;
        if (!entry.is_directory(entry_error))
        {
            continue ;
        }
        const std::string name = path_text(entry.path().filename()) ;
        if (!toolset_compatible_with(name, version))
        {
            continue ;
        }
        if (best.empty() || compare_version_text_impl(name, path_text(best.filename())) > 0)
        {
            best = entry.path() ;
        }
    }
    return best ;
}

/** 该平台工具集实际使用的 MSVC 工具集版本：优先读默认版本文件，否则取兼容的最高工具集目录。 */
std::string platform_toolset_version(const Path& install_dir, const std::string& toolset)
{
    const std::string declared = read_first_line(install_dir / L"VC" / L"Auxiliary" / L"Build" /
        lossy_wide("Microsoft.VCToolsVersion." + toolset + ".default.txt")) ;
    if (!declared.empty())
    {
        return declared ;
    }
    const Path compatible = highest_compatible_toolset_dir(
        install_dir / L"VC" / L"Tools" / L"MSVC", version_from_platform_toolset(toolset)) ;
    return compatible.empty() ? std::string() : path_text(compatible.filename()) ;
}

/** 一个候选平台工具集及其证据来源。 */
struct PlatformToolsetEntry
{
    /** 平台工具集名，如 v145。 */
    std::string m_name = {} ;
    /** MSBuild 下是否真有这个平台工具集目录。 */
    bool m_directory_exists = false ;
} ;

/** 枚举 VS 安装里真实可用的平台工具集：MSBuild 目录优先，其次版本文件，最后按安装主版本映射。 */
std::vector<PlatformToolsetEntry> platform_toolset_entries(const engine::VisualStudioInstall& install)
{
    std::map<std::string, bool> found ;
    const Path msbuild_vc = install.m_root / L"MSBuild" / L"Microsoft" / L"VC" ;
    std::error_code error ;
    std::filesystem::directory_iterator versions(msbuild_vc, error) ;
    if (!error)
    {
        for (const auto& version : versions)
        {
            std::error_code version_error ;
            if (!version.is_directory(version_error))
            {
                continue ;
            }
            const Path toolsets = version.path() / L"Platforms" / L"x64" / L"PlatformToolsets" ;
            std::error_code toolsets_error ;
            std::filesystem::directory_iterator entries(toolsets, toolsets_error) ;
            if (toolsets_error)
            {
                continue ;
            }
            for (const auto& entry : entries)
            {
                std::error_code entry_error ;
                if (!entry.is_directory(entry_error))
                {
                    continue ;
                }
                const std::string name = path_text(entry.path().filename()) ;
                if (!name.empty())
                {
                    found[name] = true ;
                }
            }
        }
    }
    if (found.empty())
    {
        // 退路一：VC\Auxiliary\Build 下 Microsoft.VCToolsVersion.<toolset>.default.txt 的存在性。
        const std::string prefix = "Microsoft.VCToolsVersion." ;
        const std::string suffix = ".default.txt" ;
        std::error_code build_error ;
        std::filesystem::directory_iterator files(install.m_root / L"VC" / L"Auxiliary" / L"Build",
            build_error) ;
        if (!build_error)
        {
            for (const auto& file : files)
            {
                std::error_code file_error ;
                if (!file.is_regular_file(file_error))
                {
                    continue ;
                }
                const std::string name = path_text(file.path().filename()) ;
                if (name.size() <= prefix.size() + suffix.size() ||
                    name.rfind(prefix, 0) != 0 ||
                    name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
                {
                    continue ;
                }
                const std::string toolset = name.substr(prefix.size(),
                    name.size() - prefix.size() - suffix.size()) ;
                if (!toolset.empty())
                {
                    found[toolset] = false ;
                }
            }
        }
    }
    if (found.empty())
    {
        // 退路二：按安装主版本映射到该 VS 的默认平台工具集。
        std::string major = install.m_major ;
        if (major.empty())
        {
            major = major_from_path(install.m_root) ;
        }
        if (major == "16")
        {
            found["v142"] = false ;
        }
        else if (major == "17")
        {
            found["v143"] = false ;
        }
        else if (major == "18")
        {
            found["v145"] = false ;
        }
    }
    std::vector<PlatformToolsetEntry> result ;
    for (const auto& [name, exists] : found)
    {
        PlatformToolsetEntry entry ;
        entry.m_name = name ;
        entry.m_directory_exists = exists ;
        result.push_back(std::move(entry)) ;
    }
    return result ;
}

/** 枚举一个 VS 安装实例下的编译器候选：每个真实可用的平台工具集一条。 */
std::vector<Compiler> compilers_from_install(const engine::VisualStudioInstall& install)
{
    std::vector<Compiler> result ;
    if (install.m_root.empty())
    {
        return result ;
    }
    const Path msvc_root = install.m_root / L"VC" / L"Tools" / L"MSVC" ;
    Path vcvars = install.m_root / L"VC" / L"Auxiliary" / L"Build" / L"vcvarsall.bat" ;
    if (!file_exists(vcvars))
    {
        // 旧版 VS(2015 及更早)把 vcvarsall.bat 放在 VC 根目录。
        vcvars = install.m_root / L"VC" / L"vcvarsall.bat" ;
    }
    const bool has_vcvars = file_exists(vcvars) ;
    const std::string default_version = default_toolset_version(install.m_root) ;
    const std::string generator = engine::generator_from_install(install) ;
    for (const PlatformToolsetEntry& entry : platform_toolset_entries(install))
    {
        Compiler compiler ;
        compiler.m_kind = ToolChainKind::VisualStudio ;
        compiler.m_platform_toolset = entry.m_name ;
        compiler.m_version = version_from_platform_toolset(entry.m_name) ;
        compiler.m_toolset_version = platform_toolset_version(install.m_root, entry.m_name) ;
        if (!compiler.m_toolset_version.empty())
        {
            const Path toolset_root = msvc_root / lossy_wide(compiler.m_toolset_version) ;
            compiler.m_cl_path = toolset_root / L"bin" / L"Hostx64" / L"x64" / L"cl.exe" ;
            compiler.m_include_dirs = { toolset_root / L"include" } ;
            compiler.m_lib_dirs = { toolset_root / L"lib" / L"x64" } ;
        }
        if (has_vcvars)
        {
            compiler.m_vcvars_path = vcvars ;
        }
        if (!default_version.empty() && !compiler.m_toolset_version.empty() &&
            compiler.m_toolset_version != default_version)
        {
            compiler.m_vcvars_argument = "-vcvars_ver=" + toolset_major_minor(compiler.m_toolset_version) ;
        }
        compiler.m_name = version_name(compiler.m_version) ;
        compiler.m_generator = generator ;
        compiler.m_is_checked = entry.m_directory_exists && check_compiler(compiler) ;
        result.push_back(std::move(compiler)) ;
    }
    std::sort(result.begin(), result.end(), [](const Compiler& left, const Compiler& right)
    {
        const int compared = compare_version_text_impl(left.m_toolset_version, right.m_toolset_version) ;
        if (compared != 0)
        {
            return compared > 0 ;
        }
        return left.m_platform_toolset < right.m_platform_toolset ;
    }) ;
    return result ;
}

/** 枚举一个 VS 安装根下的编译器候选；只凭安装路径推导安装证据。 */
std::vector<Compiler> compilers_from_install(const Path& install_dir)
{
    return compilers_from_install(make_visual_studio_install(install_dir)) ;
}

/** 判断路径是否是 kit 形式的 MSVC 根：Tools/<版本> 下能找到 cl.exe。 */
bool looks_like_kit_msvc_root(const Path& directory)
{
    const Path tools = directory / L"Tools" ;
    std::error_code error ;
    std::filesystem::directory_iterator iterator(tools, error) ;
    if (error)
    {
        return false ;
    }
    for (const auto& entry : iterator)
    {
        std::error_code entry_error ;
        if (!entry.is_directory(entry_error))
        {
            continue ;
        }
        if (!find_cl_in_toolset(entry.path()).empty())
        {
            return true ;
        }
    }
    return false ;
}

/** 枚举 kit 布局(MSVC/Tools/<版本>)下的编译器；环境脚本用找到的 *toolchain*.cmake。 */
std::vector<Compiler> compilers_from_kit(const Path& msvc_root)
{
    std::vector<Compiler> result = portable_compilers_from_directory(msvc_root) ;
    std::sort(result.begin(), result.end(), [](const Compiler& left, const Compiler& right)
    {
        return compare_version_text_impl(left.m_toolset_version, right.m_toolset_version) > 0 ;
    }) ;
    return result ;
}

/** 判断目录是否是 VS 安装根。 */
bool looks_like_msvc_root(const Path& directory)
{
    if (file_exists(directory / L"VC" / L"Auxiliary" / L"Build" / L"vcvarsall.bat"))
    {
        return true ;
    }
    if (file_exists(directory / L"VC" / L"vcvarsall.bat"))
    {
        return true ;
    }
    return directory_exists(directory / L"VC" / L"Tools" / L"MSVC") ;
}

/** 从环境变量给出的路径还原 VS 安装根；认不出返回空。 */
Path normalize_vs_root(const Path& value)
{
    Path current = value ;
    for (int level = 0 ; level < 5 && !current.empty() ; ++level)
    {
        if (looks_like_msvc_root(current) || looks_like_kit_msvc_root(current))
        {
            return current ;
        }
        const Path parent = current.parent_path() ;
        if (parent.empty() || parent == current)
        {
            break ;
        }
        current = parent ;
    }
    return {} ;
}

// ============================================================================
// 进程
// ============================================================================

/** 枚举指定进程名的可执行文件路径；找不到返回空。 */
std::vector<Path> running_executables(const std::string& process_name)
{
    std::vector<Path> result ;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) ;
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return result ;
    }
    PROCESSENTRY32W entry = {} ;
    entry.dwSize = sizeof(entry) ;
    BOOL more = Process32FirstW(snapshot, &entry) ;
    while (more)
    {
        if (ascii_lower(path_text(Path(entry.szExeFile))) == ascii_lower(process_name))
        {
            const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID) ;
            if (process != nullptr)
            {
                std::array<wchar_t, 32768> buffer = {} ;
                DWORD size = static_cast<DWORD>(buffer.size()) ;
                if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size))
                {
                    result.emplace_back(std::wstring(buffer.data(), size)) ;
                }
                CloseHandle(process) ;
            }
        }
        more = Process32NextW(snapshot, &entry) ;
    }
    CloseHandle(snapshot) ;
    return result ;
}

/** 判断 path 是否位于 root 目录树内（按规范化路径的路径段比较）。 */
bool path_within(const Path& path, const Path& root)
{
    if (path.empty() || root.empty())
    {
        return false ;
    }
    const std::string path_text_value = ascii_lower(path_key(path)) ;
    const std::string root_text_value = ascii_lower(path_key(root)) ;
    if (path_text_value == root_text_value)
    {
        return true ;
    }
    if (path_text_value.size() <= root_text_value.size() ||
        path_text_value.compare(0, root_text_value.size(), root_text_value) != 0)
    {
        return false ;
    }
    const char boundary = path_text_value[root_text_value.size()] ;
    return boundary == '\\' || boundary == '/' ;
}

// ============================================================================
// NX SDK
// ============================================================================

/** 注册表登记的 NX 安装根与版本。 */
struct NxInstallEntry
{
    /** 安装根，即含 UGII 与 NXBIN 的那一层。 */
    Path m_root = {} ;
    /** 注册表值名里带的版本号。 */
    std::string m_version = {} ;
} ;

/** 判断目录是否像一个 NX 安装根：UGII 必须有，NXBIN 或 UGOPEN 至少有一个。 */
bool looks_like_nx_root(const Path& directory)
{
    return has_child_directory(directory, "UGII") &&
        (has_child_directory(directory, "NXBIN") || has_child_directory(directory, "UGOPEN")) ;
}

/** 从注册表值/环境变量给出的路径还原 NX 安装根；认不出返回空。 */
Path normalize_nx_root(const Path& value)
{
    Path current = value ;
    for (int level = 0 ; level < 4 && !current.empty() ; ++level)
    {
        if (looks_like_nx_root(current))
        {
            return current ;
        }
        const Path parent = current.parent_path() ;
        if (parent.empty() || parent == current)
        {
            break ;
        }
        current = parent ;
    }
    return {} ;
}

/** 读两个注册表位置里的 NX 安装项；读不到返回空。 */
std::vector<NxInstallEntry> registry_nx_installations()
{
    constexpr std::array<const wchar_t*, 2> keys = {
        L"SOFTWARE\\Wow6432Node\\Unigraphics Solutions\\Installed Applications" ,
        L"SOFTWARE\\Unigraphics Solutions\\Installed Applications" ,
    } ;
    std::vector<NxInstallEntry> result ;
    for (const wchar_t* key : keys)
    {
        for (const auto& [name, value] : registry_string_values(key))
        {
            const Path root = normalize_nx_root(Path(value)) ;
            if (root.empty())
            {
                continue ;
            }
            NxInstallEntry entry ;
            entry.m_root = root ;
            entry.m_version = first_version_digits(lossy_utf8(name)) ;
            result.push_back(std::move(entry)) ;
        }
    }
    return result ;
}

/** SDK 布局：uf 头文件根、NXOpen 头文件根与库目录。 */
struct NxSdkLayout
{
    Path m_uf_header = {} ;
    Path m_nxopen_header = {} ;
    Path m_lib = {} ;
} ;

/** 在一个 NX 安装根下验证 SDK 头文件与库；缺关键文件返回空。 */
std::optional<NxSdkLayout> find_sdk_layout(const Path& root)
{
    const std::array<Path, 5> uf_candidates = {
        root / L"UGOPEN" ,
        root / L"UGII" / L"ugopen" ,
        root / L"UGII" ,
        root / L"NXOPEN" / L"include" ,
        root ,
    } ;
    const std::array<Path, 5> nxopen_candidates = {
        root / L"UGOPEN" ,
        root / L"UGII" / L"ugopen" ,
        root / L"UGII" / L"include" ,
        root / L"NXOPEN" / L"include" ,
        root / L"NXOPEN" ,
    } ;
    const std::array<Path, 6> lib_candidates = {
        root / L"UGOPEN" ,
        root / L"UGII" / L"lib" ,
        root / L"UGII" / L"ugopen" ,
        root / L"NXBIN" ,
        root / L"UGII" ,
        root ,
    } ;
    for (const auto& uf : uf_candidates)
    {
        if (!file_exists(uf / L"uf.h"))
        {
            continue ;
        }
        for (const auto& nxopen : nxopen_candidates)
        {
            if (!file_exists(nxopen / L"NXOpen" / L"Session.hxx"))
            {
                continue ;
            }
            for (const auto& lib : lib_candidates)
            {
                if (!file_exists(lib / L"libufun.lib"))
                {
                    continue ;
                }
                NxSdkLayout layout ;
                layout.m_uf_header = uf ;
                layout.m_nxopen_header = nxopen ;
                layout.m_lib = lib ;
                return layout ;
            }
        }
    }
    return std::nullopt ;
}

/** 从目录名里抠出版本号；没有数字时再看一层父目录。 */
std::string nx_version_from_name(const Path& directory)
{
    const std::string own = first_version_digits(path_text(directory.filename())) ;
    if (!own.empty())
    {
        return own ;
    }
    const Path parent = directory.parent_path() ;
    if (!parent.empty() && parent != directory)
    {
        const std::string inherited = first_version_digits(path_text(parent.filename())) ;
        if (!inherited.empty())
        {
            return inherited ;
        }
    }
    return path_text(directory.filename()) ;
}

/** 从 env_print 输出里按 V2007 形式取版本；没有返回空。 */
std::string version_from_env_print_output(const std::string& output)
{
    for (std::size_t index = 0 ; index + 1 < output.size() ; ++index)
    {
        const char letter = output[index] ;
        if ((letter != 'V' && letter != 'v') ||
            std::isdigit(static_cast<unsigned char>(output[index + 1])) == 0)
        {
            continue ;
        }
        std::string digits ;
        std::size_t cursor = index + 1 ;
        while (cursor < output.size() && std::isdigit(static_cast<unsigned char>(output[cursor])) != 0)
        {
            digits.push_back(output[cursor]) ;
            ++cursor ;
        }
        return digits ;
    }
    return {} ;
}

/** 调 NXBIN\env_print -v 取权威版本；只在成功且格式正确时缓存。 */
std::string env_print_version(const Path& nx_root)
{
    // 手动定位可能与后台 NX 扫描线程并发，缓存需要互斥保护。
    static std::map<std::string, std::string> cache ;
    static std::mutex cache_mutex ;
    const std::string key = path_key(nx_root) ;
    {
        const std::lock_guard<std::mutex> guard(cache_mutex) ;
        if (const auto found = cache.find(key) ; found != cache.end())
        {
            return found->second ;
        }
    }
    std::string version ;
    const std::array<Path, 2> tools = {
        nx_root / L"NXBIN" / L"env_print.exe" ,
        nx_root / L"UGII" / L"env_print.exe" ,
    } ;
    for (const auto& tool : tools)
    {
        if (!file_exists(tool))
        {
            continue ;
        }
        const CommandResult command = run_text_command(tool, L"-v", 2000) ;
        if (!command.m_started || command.m_timed_out || command.m_exit_code != 0)
        {
            continue ;
        }
        version = version_from_env_print_output(command.m_output) ;
        if (!version.empty())
        {
            break ;
        }
    }
    if (!version.empty())
    {
        const std::lock_guard<std::mutex> guard(cache_mutex) ;
        cache.emplace(key, version) ;
    }
    return version ;
}

/** 用 NX 安装根构造一个 SDK 条目；版本优先取 env_print。 */
NXSdk make_nx_sdk(const Path& root, const std::string& known_version)
{
    NXSdk sdk ;
    sdk.m_root = root ;
    if (const auto layout = find_sdk_layout(root) ; layout.has_value())
    {
        sdk.m_uf_header_dir = layout->m_uf_header ;
        sdk.m_nxopen_header_dir = layout->m_nxopen_header ;
        sdk.m_lib_dir = layout->m_lib ;
        sdk.m_is_checked = true ;
    }
    else
    {
        // 开发包不完整时仍保留条目，记录能找到的头文件候选，界面可提示"不完整"。
        const std::array<Path, 3> candidates = {
            root / L"UGOPEN" ,
            root / L"UGII" / L"ugopen" ,
            root / L"NXOPEN" / L"include" ,
        } ;
        for (const auto& candidate : candidates)
        {
            if (directory_exists(candidate))
            {
                sdk.m_uf_header_dir = candidate ;
                break ;
            }
        }
    }
    sdk.m_version = env_print_version(root) ;
    if (!sdk.m_version.empty())
    {
        sdk.m_version_source = "env_print" ;
    }
    else if (!known_version.empty())
    {
        sdk.m_version = known_version ;
        sdk.m_version_source = "registry" ;
    }
    else
    {
        sdk.m_version = nx_version_from_name(root) ;
        sdk.m_version_source = "directory" ;
    }
    return sdk ;
}

/** SDK 的关键头文件与库是否齐全。 */
bool sdk_files_ok(const NXSdk& sdk)
{
    if (!sdk.m_uf_header_dir.empty() && !file_exists(sdk.m_uf_header_dir / L"uf.h"))
    {
        return false ;
    }
    if (!sdk.m_nxopen_header_dir.empty() &&
        !file_exists(sdk.m_nxopen_header_dir / L"NXOpen" / L"Session.hxx"))
    {
        return false ;
    }
    if (!sdk.m_lib_dir.empty() && !file_exists(sdk.m_lib_dir / L"libufun.lib"))
    {
        return false ;
    }
    return sdk.m_is_checked ;
}

/** 判断路径是否是 kit 形式的 SDK 根：include/uf.h + include/NXOpen/Session.hxx + lib/libufun.lib。 */
bool looks_like_kit_sdk_root(const Path& directory)
{
    return file_exists(directory / L"include" / L"uf.h") &&
        file_exists(directory / L"include" / L"NXOpen" / L"Session.hxx") &&
        file_exists(directory / L"lib" / L"libufun.lib") ;
}

/** 从 kit 的 NXOpenConfigVersion.cmake 里抠版本；读不到返回空。 */
std::string kit_sdk_version(const Path& sdk_root)
{
    std::ifstream stream(sdk_root / L"cmake" / L"NXOpenConfigVersion.cmake") ;
    if (!stream)
    {
        return {} ;
    }
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>()) ;
    const auto marker = text.find("PACKAGE_VERSION") ;
    if (marker == std::string::npos)
    {
        return {} ;
    }
    const auto quote = text.find('"', marker) ;
    if (quote == std::string::npos)
    {
        return {} ;
    }
    const auto end = text.find('"', quote + 1) ;
    if (end == std::string::npos)
    {
        return {} ;
    }
    return first_version_digits(text.substr(quote + 1, end - quote - 1)) ;
}

/** 用 kit 的 ug_sdk 目录构造一个 SDK 条目。 */
NXSdk make_kit_sdk(const Path& sdk_root)
{
    NXSdk sdk ;
    sdk.m_root = sdk_root ;
    sdk.m_uf_header_dir = sdk_root / L"include" ;
    sdk.m_nxopen_header_dir = sdk_root / L"include" ;
    sdk.m_lib_dir = sdk_root / L"lib" ;
    sdk.m_is_checked = true ;
    sdk.m_version = kit_sdk_version(sdk_root) ;
    if (!sdk.m_version.empty())
    {
        sdk.m_version_source = "cmake" ;
    }
    return sdk ;
}

// ============================================================================
// Windows SDK
// ============================================================================

/** 版本目录名是否形如 10.0.26100.0。 */
bool looks_like_windows_sdk_version(const std::string& text)
{
    bool has_dot = false ;
    for (const char letter : text)
    {
        if (letter == '.')
        {
            has_dot = true ;
            continue ;
        }
        if (letter < '0' || letter > '9')
        {
            return false ;
        }
    }
    return has_dot && !text.empty() ;
}

/** Windows SDK 的关键头文件、库与资源编译器是否齐全。 */
bool windows_sdk_files_ok(const WindowsSdk& sdk)
{
    return file_exists(sdk.m_include_dir / L"um" / L"windows.h") &&
        file_exists(sdk.m_include_dir / L"ucrt" / L"stdio.h") &&
        file_exists(sdk.m_lib_dir / L"um" / L"x64" / L"kernel32.lib") &&
        file_exists(sdk.m_lib_dir / L"ucrt" / L"x64" / L"ucrt.lib") &&
        file_exists(sdk.m_bin_dir / L"rc.exe") ;
}

/** 从一个 kits 根枚举全部版本；没有可用版本时返回空（不算 SDK 根）。 */
std::vector<WindowsSdk> windows_sdks_from_root(const Path& root)
{
    std::vector<WindowsSdk> result ;
    if (root.empty())
    {
        return result ;
    }
    const Path include_root = root / L"Include" ;
    const Path lib_root = root / L"Lib" ;
    if (!directory_exists(include_root) || !directory_exists(lib_root))
    {
        return result ;
    }
    bool any_checked = false ;
    std::error_code error ;
    std::filesystem::directory_iterator iterator(include_root, error) ;
    if (error)
    {
        return result ;
    }
    for (const auto& entry : iterator)
    {
        std::error_code entry_error ;
        if (!entry.is_directory(entry_error))
        {
            continue ;
        }
        const std::string version = path_text(entry.path().filename()) ;
        if (!looks_like_windows_sdk_version(version))
        {
            continue ;
        }
        WindowsSdk sdk ;
        sdk.m_version = version ;
        sdk.m_root = root ;
        sdk.m_include_dir = include_root / lossy_wide(version) ;
        sdk.m_lib_dir = lib_root / lossy_wide(version) ;
        sdk.m_bin_dir = root / L"bin" / lossy_wide(version) / L"x64" ;
        sdk.m_is_checked = windows_sdk_files_ok(sdk) ;
        any_checked = any_checked || sdk.m_is_checked ;
        result.push_back(std::move(sdk)) ;
    }
    if (!any_checked)
    {
        result.clear() ;
        return result ;
    }
    std::sort(result.begin(), result.end(),
        [](const WindowsSdk& left, const WindowsSdk& right)
        {
            return compare_version_text_impl(left.m_version, right.m_version) > 0 ;
        }) ;
    return result ;
}

/** 从注册表收集 Windows Kits 根；64/32 位视图与两代 SDK 键都试。 */
std::vector<Path> registry_windows_kits_roots()
{
    struct Entry
    {
        const wchar_t* m_key ;
        const wchar_t* m_value ;
    } ;
    constexpr std::array<Entry, 4> entries = {{
        { L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots" , L"KitsRoot10" } ,
        { L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows Kits\\Installed Roots" , L"KitsRoot10" } ,
        { L"SOFTWARE\\Microsoft\\Microsoft SDKs\\Windows\\v10.0" , L"InstallationFolder" } ,
        { L"SOFTWARE\\WOW6432Node\\Microsoft\\Microsoft SDKs\\Windows\\v10.0" , L"InstallationFolder" } ,
    }} ;
    std::vector<Path> result ;
    std::set<std::string> seen ;
    for (const Entry& entry : entries)
    {
        const Path root = trim_trailing_separator(Path(registry_string_value(entry.m_key, entry.m_value))) ;
        if (root.empty() || !directory_exists(root))
        {
            continue ;
        }
        const std::string key = path_key(root) ;
        if (key.empty() || !seen.insert(key).second)
        {
            continue ;
        }
        result.push_back(root) ;
    }
    return result ;
}

/** 从任意路径向上找 kits 根（含 Include 与 Lib 的那一层）。 */
Path windows_kits_root_from_value(const Path& value)
{
    Path current = trim_trailing_separator(value) ;
    for (int level = 0 ; level < 8 && !current.empty() ; ++level)
    {
        if (directory_exists(current / L"Include") && directory_exists(current / L"Lib"))
        {
            return current ;
        }
        const Path parent = current.parent_path() ;
        if (parent.empty() || parent == current)
        {
            break ;
        }
        current = parent ;
    }
    return {} ;
}

/** 拆开分号分隔的环境变量路径列表；空项忽略。 */
std::vector<Path> environment_path_list(const wchar_t* name)
{
    std::vector<Path> result ;
    const Path value = environment_path(name) ;
    if (value.empty())
    {
        return result ;
    }
    const std::wstring text = value.wstring() ;
    std::size_t begin = 0 ;
    while (begin <= text.size())
    {
        std::size_t end = text.find(L';', begin) ;
        if (end == std::wstring::npos)
        {
            end = text.size() ;
        }
        const std::wstring entry = text.substr(begin, end - begin) ;
        if (!entry.empty())
        {
            result.push_back(Path(entry)) ;
        }
        begin = end + 1 ;
    }
    return result ;
}

/** 从环境变量推导 Windows Kits 根；多级回退，重复根只保留一次。 */
std::vector<Path> environment_windows_kits_roots()
{
    std::vector<Path> result ;
    std::set<std::string> seen ;
    const auto push = [&result, &seen](const Path& value)
    {
        const Path root = windows_kits_root_from_value(value) ;
        if (root.empty())
        {
            return ;
        }
        const std::string key = path_key(root) ;
        if (key.empty() || !seen.insert(key).second)
        {
            return ;
        }
        result.push_back(root) ;
    } ;
    for (const wchar_t* name : { L"WindowsSdkDir" , L"UniversalCRTSdkDir" , L"WindowsSdkVerBinPath" })
    {
        push(environment_path(name)) ;
    }
    for (const auto& entry : environment_path_list(L"INCLUDE"))
    {
        push(entry) ;
    }
    return result ;
}

// ============================================================================
// 收集器
// ============================================================================

/** 收集器：负责去重、报进度以及响应取消。 */
class Collector
{
    public:
    explicit Collector(ScanRequest request) : m_request(std::move(request))
    {
    }

    /** 当前阶段是否已被取消。 */
    bool cancelled() const noexcept
    {
        return m_cancelled ;
    }

    /** 替换进度观察者；用于把单域扫描进度转给外部。 */
    void set_observer(ScanObserver observer)
    {
        m_request.m_observer = std::move(observer) ;
    }

    /** 当前已确认的编译器数。 */
    std::size_t compiler_count() const noexcept
    {
        return m_compilers.size() ;
    }

    /** 当前已确认的 NX SDK 数。 */
    std::size_t sdk_count() const noexcept
    {
        return m_sdks.size() ;
    }

    /** 当前已确认的 Windows SDK 数。 */
    std::size_t windows_sdk_count() const noexcept
    {
        return m_windows_sdks.size() ;
    }

    /** 切换阶段并重置本阶段计数。 */
    void begin(ScanTier tier)
    {
        m_tier = tier ;
        m_visited = 0 ;
    }

    /** 记录一次目录访问；同路径已按同样或更深的剩余深度扫过则跳过。 */
    bool visit(const Path& directory, int remaining_depth)
    {
        const std::string key = path_key(directory) ;
        if (!key.empty())
        {
            const auto found = m_visited_depths.find(key) ;
            if (found != m_visited_depths.end() && found->second >= remaining_depth)
            {
                return false ;
            }
            m_visited_depths[key] = remaining_depth ;
        }
        ++m_visited ;
        ++m_total_visited ;
        m_progress.m_tier = m_tier ;
        m_progress.m_directory = directory ;
        m_progress.m_visited = m_visited ;
        m_progress.m_found = m_compilers.size() + m_sdks.size() + m_windows_sdks.size() ;
        m_progress.m_stage_finished = false ;
        return observe() ;
    }

    /** 累计访问过的目录数；跨阶段不清零。 */
    std::size_t total_visited() const noexcept
    {
        return m_total_visited ;
    }

    /** 报告阶段结束。 */
    void finish()
    {
        m_progress.m_tier = m_tier ;
        m_progress.m_directory.clear() ;
        m_progress.m_visited = m_visited ;
        m_progress.m_found = m_compilers.size() + m_sdks.size() + m_windows_sdks.size() ;
        m_progress.m_stage_finished = true ;
        observe() ;
    }

    /** 添加编译器；按 cl 路径去重。 */
    void add(Compiler compiler)
    {
        const auto key = path_key(compiler.m_cl_path.empty() ? compiler.m_vcvars_path : compiler.m_cl_path) ;
        if (key.empty() || !m_compiler_keys.insert(key).second)
        {
            return ;
        }
        m_compilers.push_back(std::move(compiler)) ;
        if (m_probe_budget > 0)
        {
            --m_probe_budget ;
            probe_compiler_impl(m_compilers.back()) ;
        }
    }

    /** 添加 SDK；按安装根去重并补充运行状态。 */
    void add(NXSdk sdk)
    {
        const Path identity = sdk.m_root.empty() ?
            (sdk.m_uf_header_dir.empty() ? sdk.m_nxopen_header_dir : sdk.m_uf_header_dir) : sdk.m_root ;
        const std::string key = path_key(identity) ;
        if (key.empty() || !m_sdk_keys.insert(key).second)
        {
            return ;
        }
        sdk.m_is_running = sdk_is_running(sdk) ;
        m_sdks.push_back(std::move(sdk)) ;
    }

    /** 添加 Windows SDK；按 kits 根 + 版本去重，来源标记当前阶段。 */
    void add(WindowsSdk sdk)
    {
        const std::string root_key = path_key(sdk.m_root) ;
        const std::string key = root_key + "\t" + ascii_lower(sdk.m_version) ;
        if (root_key.empty() || !m_windows_sdk_keys.insert(key).second)
        {
            return ;
        }
        if (sdk.m_source.empty())
        {
            sdk.m_source = windows_source() ;
        }
        m_windows_sdks.push_back(std::move(sdk)) ;
    }

    /** 重新核对编译器关键文件。 */
    void recheck_compilers()
    {
        for (auto& compiler : m_compilers)
        {
            compiler.m_is_checked = check_compiler(compiler) ;
        }
    }

    /** 记录注册表里登记的 NX 安装根；后续命中时用它补版本。 */
    void set_registry(std::vector<NxInstallEntry> entries)
    {
        m_registry = std::move(entries) ;
    }

    /** 查注册表里该安装根的版本；没登记返回空。 */
    std::string registry_version(const Path& root) const
    {
        const std::string key = path_key(root) ;
        for (const auto& entry : m_registry)
        {
            if (!key.empty() && path_key(entry.m_root) == key)
            {
                return entry.m_version ;
            }
        }
        return {} ;
    }

    /** 递归查找目录下的安装根；命中安装根后不再深入。 */
    void descend(const Path& directory, int depth)
    {
        if (cancelled() || !visit(directory, depth))
        {
            return ;
        }
        if (looks_like_msvc_root(directory))
        {
            for (auto& compiler : compilers_from_install(directory))
            {
                add(std::move(compiler)) ;
            }
            return ;
        }
        if (looks_like_kit_msvc_root(directory))
        {
            for (auto& compiler : compilers_from_kit(directory))
            {
                add(std::move(compiler)) ;
            }
            return ;
        }
        // 任意便携工具集根：<root>/bin/<host>/<arch>/cl.exe
        if (auto portable = compiler_from_toolset_root(directory) ; portable.has_value())
        {
            add(std::move(*portable)) ;
            return ;
        }
        if (looks_like_nx_root(directory))
        {
            add(make_nx_sdk(directory, registry_version(directory))) ;
            return ;
        }
        if (looks_like_kit_sdk_root(directory))
        {
            add(make_kit_sdk(directory)) ;
            return ;
        }
        auto windows_sdks = windows_sdks_from_root(directory) ;
        if (!windows_sdks.empty())
        {
            for (WindowsSdk& sdk : windows_sdks)
            {
                add(std::move(sdk)) ;
            }
            return ;
        }
        if (depth <= 0)
        {
            return ;
        }
        std::error_code error ;
        std::filesystem::directory_iterator iterator(directory, error) ;
        if (error)
        {
            return ;
        }
        for (const auto& entry : iterator)
        {
            if (cancelled())
            {
                return ;
            }
            std::error_code entry_error ;
            if (!entry.is_directory(entry_error) || is_skipped_directory(entry.path()))
            {
                continue ;
            }
            descend(entry.path(), depth - 1) ;
        }
    }

    /** 只在一层里识别编译器与 SDK，不做递归。 */
    void examine(const Path& directory)
    {
        if (cancelled() || !visit(directory, 0))
        {
            return ;
        }
        if (looks_like_msvc_root(directory))
        {
            for (auto& compiler : compilers_from_install(directory))
            {
                add(std::move(compiler)) ;
            }
        }
        if (looks_like_kit_msvc_root(directory))
        {
            for (auto& compiler : compilers_from_kit(directory))
            {
                add(std::move(compiler)) ;
            }
        }
        if (auto portable = compiler_from_toolset_root(directory) ; portable.has_value())
        {
            add(std::move(*portable)) ;
        }
        if (looks_like_nx_root(directory))
        {
            add(make_nx_sdk(directory, registry_version(directory))) ;
        }
        if (looks_like_kit_sdk_root(directory))
        {
            add(make_kit_sdk(directory)) ;
        }
        auto windows_sdks = windows_sdks_from_root(directory) ;
        if (!windows_sdks.empty())
        {
            for (WindowsSdk& sdk : windows_sdks)
            {
                add(std::move(sdk)) ;
            }
        }
    }

    /** 汇总成报告；排序保证不同机器输出顺序稳定。 */
    ScanReport report() const
    {
        ScanReport result ;
        result.m_compilers = m_compilers ;
        std::sort(result.m_compilers.begin(), result.m_compilers.end(),
            [](const Compiler& left, const Compiler& right)
            {
                const int compared = compare_version_text_impl(left.m_toolset_version, right.m_toolset_version) ;
                if (compared != 0)
                {
                    return compared > 0 ;
                }
                return path_text(left.m_cl_path) < path_text(right.m_cl_path) ;
            }) ;
        result.m_sdks = m_sdks ;
        std::sort(result.m_sdks.begin(), result.m_sdks.end(),
            [](const NXSdk& left, const NXSdk& right)
            {
                return path_text(left.m_root) < path_text(right.m_root) ;
            }) ;
        result.m_windows_sdks = m_windows_sdks ;
        std::sort(result.m_windows_sdks.begin(), result.m_windows_sdks.end(),
            [](const WindowsSdk& left, const WindowsSdk& right)
            {
                const int compared = compare_version_text_impl(left.m_version, right.m_version) ;
                if (compared != 0)
                {
                    return compared > 0 ;
                }
                return path_text(left.m_root) < path_text(right.m_root) ;
            }) ;
        result.m_reached = m_tier ;
        result.m_cancelled = m_cancelled ;
        return result ;
    }

    /** 取扫描请求，供阶段实现共享深度等参数。 */
    const ScanRequest& request() const noexcept
    {
        return m_request ;
    }

    private:
    /** 判断 SDK 安装根下是否有 NX 进程在跑。 */
    bool sdk_is_running(const NXSdk& sdk)
    {
        if (sdk.m_root.empty())
        {
            return false ;
        }
        for (const auto& executable : nx_processes())
        {
            if (path_within(executable, sdk.m_root))
            {
                return true ;
            }
        }
        return false ;
    }

    /** 惰性枚举一次正在运行的 NX 进程。 */
    const std::vector<Path>& nx_processes()
    {
        if (!m_processes_loaded)
        {
            m_nx_processes = running_executables("ugraf.exe") ;
            m_processes_loaded = true ;
        }
        return m_nx_processes ;
    }

    /** 调用观察者；没有观察者视为继续。 */
    bool observe()
    {
        if (m_cancelled || !m_request.m_observer)
        {
            return !m_cancelled ;
        }
        if (!m_request.m_observer(m_progress))
        {
            m_cancelled = true ;
            return false ;
        }
        return true ;
    }

    /** 当前扫描阶段对应的 Windows SDK 来源标记。 */
    std::string windows_source() const
    {
        switch (m_tier)
        {
            case ScanTier::ActiveProcess: return "registry" ;
            case ScanTier::Environment: return "env" ;
            case ScanTier::CommonRoot: return "common" ;
            case ScanTier::UserDirectory: return "user" ;
            case ScanTier::DiskSweep: return "sweep" ;
        }
        return "directory" ;
    }

    ScanRequest m_request ;
    std::vector<Compiler> m_compilers ;
    std::vector<NXSdk> m_sdks ;
    std::vector<WindowsSdk> m_windows_sdks ;
    std::set<std::string> m_compiler_keys ;
    std::set<std::string> m_sdk_keys ;
    std::set<std::string> m_windows_sdk_keys ;
    std::map<std::string, int> m_visited_depths ;
    std::vector<NxInstallEntry> m_registry ;
    std::vector<Path> m_nx_processes ;
    /** 单次扫描最多探测多少个编译器的真实版本；全盘兜底时防止过度拖慢。 */
    std::size_t m_probe_budget = 32 ;
    ScanTier m_tier = ScanTier::ActiveProcess ;
    ScanProgress m_progress ;
    std::size_t m_visited = 0 ;
    std::size_t m_total_visited = 0 ;
    bool m_cancelled = false ;
    bool m_processes_loaded = false ;
} ;

// ============================================================================
// 五级扫描：正在运行的程序 → 环境变量 → 常规目录 → 手动目录 → 全盘兜底
// ============================================================================

/** 检查一个 VS 安装实例：按真实平台工具集枚举编译器。 */
void examine_visual_studio(Collector& collector, const engine::VisualStudioInstall& install)
{
    if (install.m_root.empty() || !directory_exists(install.m_root) ||
        !collector.visit(install.m_root, 0))
    {
        return ;
    }
    if (!looks_like_msvc_root(install.m_root))
    {
        return ;
    }
    for (auto& compiler : compilers_from_install(install))
    {
        collector.add(std::move(compiler)) ;
    }
}

/** 第一阶段：正在运行的程序与系统登记表(进程 → 注册表)。 */
void scan_active_process(Collector& collector)
{
    collector.begin(ScanTier::ActiveProcess) ;
    const Path running = find_dir_by_active_process("ugraf.exe") ;
    if (!running.empty())
    {
        collector.examine(running) ;
    }
    auto registry = registry_nx_installations() ;
    collector.set_registry(registry) ;
    for (const auto& entry : registry)
    {
        if (collector.cancelled())
        {
            return ;
        }
        if (directory_exists(entry.m_root))
        {
            collector.examine(entry.m_root) ;
        }
    }
    for (const auto& install : installed_visual_studios())
    {
        if (collector.cancelled())
        {
            return ;
        }
        examine_visual_studio(collector, install) ;
    }
    for (const auto& root : registry_windows_kits_roots())
    {
        if (collector.cancelled())
        {
            return ;
        }
        if (directory_exists(root))
        {
            collector.examine(root) ;
        }
    }
    collector.finish() ;
}

/** 第二阶段：环境变量给出的安装根(NX + VS)。 */
void scan_environment_variables(Collector& collector)
{
    collector.begin(ScanTier::Environment) ;
    for (const wchar_t* name : { L"UGII_BASE_DIR" , L"UGII_ROOT_DIR" })
    {
        const Path root = normalize_nx_root(environment_path(name)) ;
        if (!root.empty())
        {
            collector.examine(root) ;
        }
        if (collector.cancelled())
        {
            return ;
        }
    }
    for (const wchar_t* name : { L"VSINSTALLDIR" , L"VCINSTALLDIR" , L"VCToolsInstallDir" })
    {
        const Path root = normalize_vs_root(environment_path(name)) ;
        if (!root.empty())
        {
            collector.examine(root) ;
        }
        if (collector.cancelled())
        {
            return ;
        }
    }
    for (const auto& root : environment_windows_kits_roots())
    {
        if (collector.cancelled())
        {
            return ;
        }
        if (directory_exists(root))
        {
            collector.examine(root) ;
        }
    }
    collector.finish() ;
}

/** 第三阶段：常规安装目录下浅层递归。 */
void scan_common_roots(Collector& collector)
{
    collector.begin(ScanTier::CommonRoot) ;
    if (!collector.request().m_system_roots)
    {
        collector.finish() ;
        return ;
    }
    const Path program_files = environment_path(L"ProgramFiles") ;
    const Path program_files_x86 = environment_path(L"ProgramFiles(x86)") ;
    std::vector<Path> roots ;
    for (const Path& base : { program_files , program_files_x86 })
    {
        if (base.empty())
        {
            continue ;
        }
        roots.push_back(base / L"Microsoft Visual Studio") ;
        roots.push_back(base / L"Windows Kits") ;
        roots.push_back(base / L"Siemens") ;
        roots.push_back(base / L"UGS") ;
    }
    for (const auto& root : roots)
    {
        if (collector.cancelled())
        {
            return ;
        }
        if (directory_exists(root))
        {
            collector.descend(root, collector.request().m_depth) ;
        }
    }
    collector.finish() ;
}

/** 第四阶段：手动指定的目录(或自动探测到的 devkit)。 */
void scan_user_directory(Collector& collector)
{
    collector.begin(ScanTier::UserDirectory) ;
    const Path& directory = collector.request().m_user_directory ;
    if (!directory.empty() && directory_exists(directory))
    {
        collector.descend(directory, collector.request().m_depth) ;
    }
    collector.finish() ;
}

/** 第五阶段：全盘兜底，跳过系统目录。 */
void scan_disk_sweep(Collector& collector)
{
    collector.begin(ScanTier::DiskSweep) ;
    const DWORD drives = GetLogicalDrives() ;
    for (DWORD index = 0 ; index < 26 && !collector.cancelled() ; ++index)
    {
        if ((drives & (1UL << index)) == 0)
        {
            continue ;
        }
        const wchar_t letter = static_cast<wchar_t>(L'A' + index) ;
        const std::wstring root = std::wstring(1, letter) + L":\\" ;
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED)
        {
            continue ;
        }
        collector.descend(Path(root), collector.request().m_depth) ;
    }
    collector.finish() ;
}

// ============================================================================
// 单域扫描：与五级扫描共用低层识别器，只走与该域相关的回退链
// ============================================================================

/** 常规安装根列表：ProgramFiles 与 ProgramFiles(x86) 下的指定子目录。 */
std::vector<Path> common_install_roots(std::initializer_list<const wchar_t*> names)
{
    std::vector<Path> result ;
    for (const wchar_t* base_name : { L"ProgramFiles" , L"ProgramFiles(x86)" })
    {
        const Path base = environment_path(base_name) ;
        if (base.empty())
        {
            continue ;
        }
        for (const wchar_t* name : names)
        {
            result.push_back(base / name) ;
        }
    }
    return result ;
}

/** 构造单域扫描请求：默认开常规安装根，不含用户目录与全盘。 */
ScanRequest domain_scan_request(int depth)
{
    ScanRequest request ;
    request.m_system_roots = true ;
    request.m_depth = depth ;
    request.m_disk_sweep = false ;
    return request ;
}

/** 单域扫描里该域已确认的候选数。 */
std::size_t domain_found_count(const Collector& collector, ScanDomain domain)
{
    switch (domain)
    {
        case ScanDomain::Compiler: return collector.compiler_count() ;
        case ScanDomain::NxSdk: return collector.sdk_count() ;
        case ScanDomain::WindowsSdk: return collector.windows_sdk_count() ;
    }
    return 0 ;
}

/** 把单域扫描的累计进度转给外部钩子；取消时返回 false 中止扫描。 */
ScanObserver make_domain_observer(const engine::DomainScanHooks& hooks, const Collector& collector,
    ScanDomain domain)
{
    return [&hooks, &collector, domain](const ScanProgress&)
    {
        if (hooks.m_cancelled && hooks.m_cancelled())
        {
            return false ;
        }
        if (!hooks.m_progress)
        {
            return true ;
        }
        return hooks.m_progress(collector.total_visited(), domain_found_count(collector, domain)) ;
    } ;
}

/** 手动定位时按目录统计并回调观察者。 */
struct LocateSearch
{
    /** 观察者；为空时只看不报。 */
    const LocateObserver* m_observer = nullptr ;
    /** 已访问目录数。 */
    std::size_t m_visited = 0 ;
    /** 观察者要求中止。 */
    bool m_stopped = false ;

    /** 访问一个目录；观察者返回 false 时置停止标记。 */
    bool visit(const Path& directory)
    {
        ++m_visited ;
        if (m_observer != nullptr && *m_observer && !(*m_observer)(directory, m_visited))
        {
            m_stopped = true ;
        }
        return !m_stopped ;
    }
} ;

/** 从候选里取第一个已校验的；都未校验则取第一个，便于手动路径显示。 */
std::optional<Compiler> first_checked_compiler(std::vector<Compiler> candidates)
{
    for (Compiler& candidate : candidates)
    {
        if (candidate.m_is_checked)
        {
            return std::move(candidate) ;
        }
    }
    if (candidates.empty())
    {
        return std::nullopt ;
    }
    return std::move(candidates.front()) ;
}

/** 定位编译器时识别一个目录：VS 安装根，或任意 MSVC 工具集/容器布局。 */
std::optional<Compiler> recognize_compiler_directory(const Path& directory)
{
    if (looks_like_msvc_root(directory))
    {
        auto compilers_here = compilers_from_install(directory) ;
        if (!compilers_here.empty())
        {
            std::optional<Compiler> found = first_checked_compiler(std::move(compilers_here)) ;
            if (found.has_value())
            {
                probe_compiler_impl(*found) ;
            }
            return found ;
        }
    }
    auto portable = portable_compilers_from_directory(directory) ;
    if (!portable.empty())
    {
        std::optional<Compiler> found = first_checked_compiler(std::move(portable)) ;
        if (found.has_value())
        {
            probe_compiler_impl(*found) ;
        }
        return found ;
    }
    return std::nullopt ;
}

/** 从 cl.exe 路径向上找 VS 安装根：含 vcvarsall.bat 的那一层；不是 VS 布局返回空。 */
Path visual_studio_root_from_cl(const Path& cl_path)
{
    Path current = cl_path.parent_path() ;
    for (int level = 0 ; level < 10 && !current.empty() ; ++level)
    {
        if (file_exists(current / L"VC" / L"Auxiliary" / L"Build" / L"vcvarsall.bat") ||
            file_exists(current / L"VC" / L"vcvarsall.bat"))
        {
            return current ;
        }
        const Path parent = current.parent_path() ;
        if (parent.empty() || parent == current)
        {
            break ;
        }
        current = parent ;
    }
    return {} ;
}

/** 定位 NX SDK 时识别一个目录。 */
std::optional<NXSdk> recognize_nx_sdk_directory(const Path& directory)
{
    if (looks_like_nx_root(directory))
    {
        return make_nx_sdk(directory, {}) ;
    }
    if (looks_like_kit_sdk_root(directory))
    {
        return make_kit_sdk(directory) ;
    }
    return std::nullopt ;
}

/** 定位 Windows SDK 时识别一个目录。 */
std::optional<WindowsSdk> recognize_windows_sdk_directory(const Path& directory)
{
    auto sdks_here = windows_sdks_from_root(directory) ;
    if (sdks_here.empty())
    {
        return std::nullopt ;
    }
    return std::move(sdks_here.front()) ;
}

/** 递归找第一个命中项；找到立即返回，观察者返回 false 立即中止。 */
template <typename Item>
std::optional<Item> locate_first(const Path& directory, int depth, LocateSearch& search,
    std::optional<Item> (*recognize)(const Path&))
{
    if (search.m_stopped || !directory_exists(directory))
    {
        return std::nullopt ;
    }
    if (!search.visit(directory))
    {
        return std::nullopt ;
    }
    if (auto item = recognize(directory) ; item.has_value())
    {
        return item ;
    }
    if (depth <= 0)
    {
        return std::nullopt ;
    }
    std::error_code error ;
    std::filesystem::directory_iterator iterator(directory, error) ;
    if (error)
    {
        return std::nullopt ;
    }
    for (const auto& entry : iterator)
    {
        std::error_code entry_error ;
        if (!entry.is_directory(entry_error) || is_skipped_directory(entry.path()))
        {
            continue ;
        }
        if (auto item = locate_first<Item>(entry.path(), depth - 1, search, recognize) ; item.has_value())
        {
            return item ;
        }
        if (search.m_stopped)
        {
            return std::nullopt ;
        }
    }
    return std::nullopt ;
}
} // namespace

std::string path_text(const Path& path)
{
    return path.empty() ? std::string() : lossy_utf8(path.wstring()) ;
}

Path path_from_text(const std::string& text)
{
    return text.empty() ? Path() : Path(lossy_wide(text)) ;
}

std::string environment_value(const std::string& name)
{
    const Path value = environment_path(lossy_wide(name).c_str()) ;
    return value.empty() ? std::string() : path_text(value) ;
}

std::string nx_runtime_version(const Path& nx_root)
{
    return env_print_version(nx_root) ;
}

NinjaTool detect_ninja()
{
    NinjaTool result ;
    try
    {
        // 候选按优先级排序：PATH 最方便，其次是 VS 自带的 CMake 组件。
        std::vector<Path> candidates ;
        std::vector<std::string> seen_keys ;
        const auto add_candidate = [&candidates, &seen_keys](const Path& candidate)
        {
            if (candidate.empty() || !file_exists(candidate))
            {
                return ;
            }
            const std::string key = path_key(candidate) ;
            if (std::find(seen_keys.begin(), seen_keys.end(), key) != seen_keys.end())
            {
                return ;
            }
            seen_keys.push_back(key) ;
            candidates.push_back(candidate) ;
        } ;
        std::array<wchar_t, 32768> path_buffer = {} ;
        const DWORD path_length = SearchPathW(nullptr, L"ninja.exe", nullptr,
            static_cast<DWORD>(path_buffer.size()), path_buffer.data(), nullptr) ;
        if (path_length > 0 && path_length < path_buffer.size())
        {
            add_candidate(Path(std::wstring(path_buffer.data(), path_length))) ;
        }
        for (const engine::VisualStudioInstall& install : installed_visual_studios())
        {
            add_candidate(install.m_root / L"Common7" / L"IDE" / L"CommonExtensions" /
                L"Microsoft" / L"CMake" / L"Ninja" / L"ninja.exe") ;
        }
        for (const Path& candidate : candidates)
        {
            const CommandResult command = run_text_command(candidate, L"--version", 3000) ;
            if (!command.m_started || command.m_timed_out || command.m_exit_code != 0)
            {
                continue ;
            }
            const std::string version = trim_ascii(command.m_output) ;
            if (version.empty())
            {
                continue ;
            }
            result.m_found = true ;
            result.m_executable = candidate ;
            result.m_version = version ;
            return result ;
        }
    }
    catch (...)
    {
        // 探测失败按没有 Ninja 处理；它只影响 settings 里的一行，不该拖垮向导。
    }
    return result ;
}

bool scan_msvc(const Path& target_dir, int depth)
{
    Collector collector(ScanRequest{}) ;
    if (!directory_exists(target_dir))
    {
        return false ;
    }
    collector.begin(ScanTier::UserDirectory) ;
    collector.descend(target_dir, depth) ;
    compilers = collector.report().m_compilers ;
    return !compilers.empty() ;
}

bool check_compiler(const Compiler& compiler)
{
    if (compiler.m_cl_path.empty() || !file_exists(compiler.m_cl_path))
    {
        return false ;
    }
    if (!compiler.m_vcvars_path.empty() && !file_exists(compiler.m_vcvars_path))
    {
        return false ;
    }
    // VS 安装必须有 vcvarsall；便携工具集允许完全没有环境脚本（由生成式 toolchain 兜底）。
    if (compiler.m_kind == ToolChainKind::VisualStudio && compiler.m_vcvars_path.empty())
    {
        return false ;
    }
    return true ;
}

std::vector<NXSdk> scan_nx_sdks(const Path& target_path, int depth)
{
    Collector collector(ScanRequest{}) ;
    if (!directory_exists(target_path))
    {
        return {} ;
    }
    collector.begin(ScanTier::UserDirectory) ;
    collector.descend(target_path, depth) ;
    return collector.report().m_sdks ;
}

std::vector<WindowsSdk> scan_windows_sdks(const Path& target_path, int depth)
{
    Collector collector(ScanRequest{}) ;
    if (!directory_exists(target_path))
    {
        return {} ;
    }
    collector.begin(ScanTier::UserDirectory) ;
    collector.descend(target_path, depth) ;
    return collector.report().m_windows_sdks ;
}

Path find_dir_by_active_process(const std::string& process_name, const std::vector<std::string>& flags)
{
    for (const auto& executable : running_executables(process_name))
    {
        Path directory = executable.parent_path() ;
        for (int level = 0 ; level < 6 && !directory.empty() ; ++level)
        {
            for (const auto& flag : flags)
            {
                if (has_child_directory(directory, flag))
                {
                    return directory ;
                }
            }
            const Path parent = directory.parent_path() ;
            if (parent == directory)
            {
                break ;
            }
            directory = parent ;
        }
    }
    return {} ;
}

bool synthesize_check(const Compiler& compiler, const NXSdk& sdk)
{
    if (!compiler.m_is_checked)
    {
        return false ;
    }
    return !sdk.m_version.empty() && sdk_files_ok(sdk) ;
}

ScanReport scan_environment(const ScanRequest& request)
{
    Collector collector(request) ;
    scan_active_process(collector) ;
    if (!collector.cancelled())
    {
        scan_environment_variables(collector) ;
    }
    if (!collector.cancelled())
    {
        scan_common_roots(collector) ;
    }
    if (!collector.cancelled() && !request.m_user_directory.empty())
    {
        scan_user_directory(collector) ;
    }
    if (!collector.cancelled() && request.m_disk_sweep)
    {
        scan_disk_sweep(collector) ;
    }
    collector.recheck_compilers() ;
    return collector.report() ;
}

// ============================================================================
// 手动定位：在近似目录下按域关键字找第一个
// ============================================================================

std::optional<Compiler> locate_compiler(const Path& hint_directory, int depth,
    const LocateObserver& observer)
{
    LocateSearch search ;
    search.m_observer = &observer ;
    std::optional<Compiler> compiler = locate_first(hint_directory, std::max(depth, 0), search,
        recognize_compiler_directory) ;
    if (compiler.has_value() && compiler->m_generator.empty())
    {
        // 手动定位只按目录关键字找到安装根；生成器名再用本机注册表/vswhere 证据补全。
        const Path install_root = visual_studio_root_from_cl(compiler->m_cl_path) ;
        if (!install_root.empty())
        {
            compiler->m_generator = engine::generator_for_install(install_root,
                installed_visual_studios()) ;
        }
    }
    return compiler ;
}

std::optional<NXSdk> locate_nx_sdk(const Path& hint_directory, int depth,
    const LocateObserver& observer)
{
    LocateSearch search ;
    search.m_observer = &observer ;
    return locate_first(hint_directory, std::max(depth, 0), search, recognize_nx_sdk_directory) ;
}

std::optional<WindowsSdk> locate_windows_sdk(const Path& hint_directory, int depth,
    const LocateObserver& observer)
{
    LocateSearch search ;
    search.m_observer = &observer ;
    return locate_first(hint_directory, std::max(depth, 0), search, recognize_windows_sdk_directory) ;
}

// ============================================================================
// 引擎：供 scan_service 与 scan_select 使用的内部实现
// ============================================================================

namespace engine
{
int compare_version_text(const std::string& left, const std::string& right)
{
    return compare_version_text_impl(left, right) ;
}

bool probe_compiler(Compiler& compiler)
{
    return probe_compiler_impl(compiler) ;
}

ToolChainVersion version_from_msvc_ver(int msvc_ver)
{
    return version_from_msvc_ver_impl(msvc_ver) ;
}

Path portable_toolset_root(const Path& cl_path)
{
    return portable_toolset_root_impl(cl_path) ;
}

VisualStudioInstall visual_studio_from_path(const Path& root)
{
    return make_visual_studio_install(root) ;
}

std::vector<Compiler> compiler_candidates(const VisualStudioInstall& install)
{
    return compilers_from_install(install) ;
}

std::string generator_from_install(const VisualStudioInstall& install)
{
    std::string major = install.m_major ;
    if (major.empty())
    {
        major = version_major(install.m_version) ;
    }
    if (major.empty())
    {
        major = major_from_path(install.m_root) ;
    }
    if (major.empty())
    {
        major = major_from_visual_studio_name(install.m_display_name) ;
    }
    std::string year = install.m_year ;
    if (year.empty())
    {
        year = year_from_text(install.m_display_name) ;
    }
    if (year.empty())
    {
        year = year_from_path(install.m_root) ;
    }
    if (major.empty() || year.empty() || major == year)
    {
        return {} ;
    }
    return "Visual Studio " + major + " " + year ;
}

std::string generator_for_install(const Path& compiler_root,
    const std::vector<VisualStudioInstall>& installs)
{
    const std::string key = path_key(compiler_root) ;
    if (key.empty())
    {
        return {} ;
    }
    for (const VisualStudioInstall& install : installs)
    {
        if (path_key(install.m_root) == key)
        {
            return generator_from_install(install) ;
        }
    }
    return {} ;
}

bool cached_compiler_ok(const Compiler& compiler)
{
    return check_compiler(compiler) ;
}

std::string generator_for_compiler(const Compiler& compiler)
{
    if (!compiler.m_generator.empty())
    {
        return compiler.m_generator ;
    }
    if (compiler.m_cl_path.empty())
    {
        return {} ;
    }
    const Path install_root = visual_studio_root_from_cl(compiler.m_cl_path) ;
    if (install_root.empty())
    {
        return {} ;
    }
    return generator_for_install(install_root, installed_visual_studios()) ;
}

std::string platform_toolset_for_compiler(const Compiler& compiler)
{
    if (!compiler.m_platform_toolset.empty())
    {
        return compiler.m_platform_toolset ;
    }
    std::vector<VisualStudioInstall> installs = installed_visual_studios() ;
    const VisualStudioInstall* matched = nullptr ;
    const Path install_root = visual_studio_root_from_cl(compiler.m_cl_path) ;
    if (!install_root.empty())
    {
        const std::string root_key = path_key(install_root) ;
        for (const VisualStudioInstall& install : installs)
        {
            if (path_key(install.m_root) == root_key)
            {
                matched = &install ;
                break ;
            }
        }
        if (matched == nullptr)
        {
            // 本机证据里没有这个安装（夹具或手动目录）时，只凭安装路径推导一份实例。
            installs.push_back(visual_studio_from_path(install_root)) ;
            matched = &installs.back() ;
        }
    }
    if (matched == nullptr && !compiler.m_generator.empty())
    {
        for (const VisualStudioInstall& install : installs)
        {
            if (generator_from_install(install) == compiler.m_generator)
            {
                matched = &install ;
                break ;
            }
        }
    }
    if (matched == nullptr)
    {
        return {} ;
    }
    const std::vector<Compiler> candidates = compilers_from_install(*matched) ;
    for (const Compiler& candidate : candidates)
    {
        if (!compiler.m_toolset_version.empty() &&
            candidate.m_toolset_version == compiler.m_toolset_version)
        {
            return candidate.m_platform_toolset ;
        }
    }
    for (const Compiler& candidate : candidates)
    {
        if (!compiler.m_cl_path.empty() &&
            path_text(candidate.m_cl_path) == path_text(compiler.m_cl_path))
        {
            return candidate.m_platform_toolset ;
        }
    }
    return {} ;
}

bool cached_nx_sdk_ok(const NXSdk& sdk)
{
    return sdk_files_ok(sdk) ;
}

bool cached_windows_sdk_ok(const WindowsSdk& sdk)
{
    return windows_sdk_files_ok(sdk) ;
}

std::vector<Compiler> scan_compiler_roots(const std::vector<Path>& roots, int depth,
    const DomainScanHooks& hooks)
{
    Collector collector(domain_scan_request(depth)) ;
    collector.set_observer(make_domain_observer(hooks, collector, ScanDomain::Compiler)) ;
    collector.begin(ScanTier::CommonRoot) ;
    for (const Path& root : roots)
    {
        if (collector.cancelled())
        {
            break ;
        }
        if (directory_exists(root))
        {
            collector.descend(root, depth) ;
        }
    }
    collector.finish() ;
    collector.recheck_compilers() ;
    return collector.report().m_compilers ;
}

std::vector<NXSdk> scan_nx_sdk_roots(const std::vector<Path>& roots, int depth,
    const DomainScanHooks& hooks)
{
    Collector collector(domain_scan_request(depth)) ;
    collector.set_observer(make_domain_observer(hooks, collector, ScanDomain::NxSdk)) ;
    collector.begin(ScanTier::CommonRoot) ;
    for (const Path& root : roots)
    {
        if (collector.cancelled())
        {
            break ;
        }
        if (directory_exists(root))
        {
            collector.descend(root, depth) ;
        }
    }
    collector.finish() ;
    return collector.report().m_sdks ;
}

std::vector<WindowsSdk> scan_windows_sdk_roots(const std::vector<Path>& roots, int depth,
    const DomainScanHooks& hooks)
{
    Collector collector(domain_scan_request(depth)) ;
    collector.set_observer(make_domain_observer(hooks, collector, ScanDomain::WindowsSdk)) ;
    collector.begin(ScanTier::CommonRoot) ;
    for (const Path& root : roots)
    {
        if (collector.cancelled())
        {
            break ;
        }
        if (directory_exists(root))
        {
            collector.descend(root, depth) ;
        }
    }
    collector.finish() ;
    return collector.report().m_windows_sdks ;
}

std::vector<Compiler> scan_compiler_domain(int depth, const DomainScanHooks& hooks, const std::vector<Path>& extra_roots)
{
    Collector collector(domain_scan_request(depth)) ;
    collector.set_observer(make_domain_observer(hooks, collector, ScanDomain::Compiler)) ;
    // 近层：正在运行的程序、注册表与 vswhere 登记的 VS 安装实例。
    collector.begin(ScanTier::ActiveProcess) ;
    for (const auto& install : installed_visual_studios())
    {
        if (collector.cancelled())
        {
            break ;
        }
        examine_visual_studio(collector, install) ;
    }
    collector.finish() ;
    // 中层：环境变量给出的 VS 安装根。
    if (!collector.cancelled())
    {
        collector.begin(ScanTier::Environment) ;
        for (const wchar_t* name : { L"VSINSTALLDIR" , L"VCINSTALLDIR" , L"VCToolsInstallDir" })
        {
            const Path root = normalize_vs_root(environment_path(name)) ;
            if (!root.empty())
            {
                collector.examine(root) ;
            }
            if (collector.cancelled())
            {
                break ;
            }
        }
        collector.finish() ;
    }
    // 远景：常规安装根下递归到 VS 安装根。
    if (!collector.cancelled())
    {
        collector.begin(ScanTier::CommonRoot) ;
        for (const Path& root : extra_roots)
        {
            if (collector.cancelled())
            {
                break ;
            }
            if (directory_exists(root))
            {
                collector.descend(root, depth) ;
            }
        }
        for (const auto& root : common_install_roots({ L"Microsoft Visual Studio" , L"Microsoft Visual Studio 14.0" }))
        {
            if (collector.cancelled())
            {
                break ;
            }
            if (directory_exists(root))
            {
                collector.descend(root, depth) ;
            }
        }
        collector.finish() ;
    }
    collector.recheck_compilers() ;
    return collector.report().m_compilers ;
}

std::vector<NXSdk> scan_nx_sdk_domain(int depth, const DomainScanHooks& hooks, const std::vector<Path>& extra_roots)
{
    Collector collector(domain_scan_request(depth)) ;
    collector.set_observer(make_domain_observer(hooks, collector, ScanDomain::NxSdk)) ;
    // 近层：正在运行的 NX 进程与注册表登记的安装根。
    collector.begin(ScanTier::ActiveProcess) ;
    const Path running = find_dir_by_active_process("ugraf.exe") ;
    if (!running.empty())
    {
        collector.examine(running) ;
    }
    auto registry = registry_nx_installations() ;
    collector.set_registry(registry) ;
    for (const auto& entry : registry)
    {
        if (collector.cancelled())
        {
            break ;
        }
        if (directory_exists(entry.m_root))
        {
            collector.examine(entry.m_root) ;
        }
    }
    collector.finish() ;
    // 中层：UGII 环境变量。
    if (!collector.cancelled())
    {
        collector.begin(ScanTier::Environment) ;
        for (const wchar_t* name : { L"UGII_BASE_DIR" , L"UGII_ROOT_DIR" })
        {
            const Path root = normalize_nx_root(environment_path(name)) ;
            if (!root.empty())
            {
                collector.examine(root) ;
            }
            if (collector.cancelled())
            {
                break ;
            }
        }
        collector.finish() ;
    }
    // 远景：Siemens / UGS 常规安装根。
    if (!collector.cancelled())
    {
        collector.begin(ScanTier::CommonRoot) ;
        for (const Path& root : extra_roots)
        {
            if (collector.cancelled())
            {
                break ;
            }
            if (directory_exists(root))
            {
                collector.descend(root, depth) ;
            }
        }
        for (const auto& root : common_install_roots({ L"Siemens" , L"UGS" }))
        {
            if (collector.cancelled())
            {
                break ;
            }
            if (directory_exists(root))
            {
                collector.descend(root, depth) ;
            }
        }
        collector.finish() ;
    }
    return collector.report().m_sdks ;
}

std::vector<WindowsSdk> scan_windows_sdk_domain(int depth, const DomainScanHooks& hooks, const std::vector<Path>& extra_roots)
{
    Collector collector(domain_scan_request(depth)) ;
    collector.set_observer(make_domain_observer(hooks, collector, ScanDomain::WindowsSdk)) ;
    // 近层：注册表登记的 Windows Kits 根。
    collector.begin(ScanTier::ActiveProcess) ;
    for (const auto& root : registry_windows_kits_roots())
    {
        if (collector.cancelled())
        {
            break ;
        }
        if (directory_exists(root))
        {
            collector.examine(root) ;
        }
    }
    collector.finish() ;
    // 中层：环境变量给出的 kits 根。
    if (!collector.cancelled())
    {
        collector.begin(ScanTier::Environment) ;
        for (const auto& root : environment_windows_kits_roots())
        {
            if (collector.cancelled())
            {
                break ;
            }
            if (directory_exists(root))
            {
                collector.examine(root) ;
            }
        }
        collector.finish() ;
    }
    // 远景：常规安装根下的 Windows Kits。
    if (!collector.cancelled())
    {
        collector.begin(ScanTier::CommonRoot) ;
        for (const Path& root : extra_roots)
        {
            if (collector.cancelled())
            {
                break ;
            }
            if (directory_exists(root))
            {
                collector.descend(root, depth) ;
            }
        }
        for (const auto& root : common_install_roots({ L"Windows Kits" }))
        {
            if (collector.cancelled())
            {
                break ;
            }
            if (directory_exists(root))
            {
                collector.descend(root, depth) ;
            }
        }
        collector.finish() ;
    }
    return collector.report().m_windows_sdks ;
}
} // namespace engine
} // namespace scan
