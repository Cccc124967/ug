#include "../scan_control.h"
#include "scan_internal.h"
#include <windows.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace scan
{
namespace
{
/** 当前缓存文件头：v6 带编译器真实版本与来源类型。 */
constexpr const char* cache_header = "# nx_dev scan cache v6" ;

/** v5 缓存文件头：有平台工具集、没有编译器真实版本。 */
constexpr const char* v5_cache_header = "# nx_dev scan cache v5" ;

/** v4 缓存文件头：有生成器、没有平台工具集字段。 */
constexpr const char* v4_cache_header = "# nx_dev scan cache v4" ;

/** v3 缓存文件头：没有 CMake 生成器字段。 */
constexpr const char* v3_cache_header = "# nx_dev scan cache v3" ;

/** v2 缓存文件头：没有 Windows SDK 段。 */
constexpr const char* v2_cache_header = "# nx_dev scan cache v2" ;

/** 旧版缓存文件头：字段更少。 */
constexpr const char* legacy_cache_header = "# nx_dev scan cache v1" ;

/** 缓存文件名。 */
constexpr const wchar_t* cache_file_name = L"scan_cache.tsv" ;

/** 把路径里的分隔符写成一行能存回去的形式。 */
std::string escape(const std::string& text)
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

/** 逐字符还原转义。 */
std::string unescape(const std::string& text)
{
    std::string result ;
    result.reserve(text.size()) ;
    for (std::size_t index = 0 ; index < text.size() ; ++index)
    {
        if (text[index] != '\\' || index + 1 >= text.size())
        {
            result.push_back(text[index]) ;
            continue ;
        }
        const char next = text[++index] ;
        switch (next)
        {
            case 't': result.push_back('\t') ; break ;
            case 'n': result.push_back('\n') ; break ;
            case 'r': result.push_back('\r') ; break ;
            default: result.push_back(next) ; break ;
        }
    }
    return result ;
}

/** 按制表符切分一行并去掉转义。 */
std::vector<std::string> split_fields(const std::string& line)
{
    std::vector<std::string> fields ;
    std::string current ;
    for (const char letter : line)
    {
        if (letter == '\t')
        {
            fields.push_back(unescape(current)) ;
            current.clear() ;
        }
        else
        {
            current.push_back(letter) ;
        }
    }
    fields.push_back(unescape(current)) ;
    return fields ;
}

/** 把布尔值写成 0/1。 */
const char* bool_text(bool value)
{
    return value ? "1" : "0" ;
}

/** 读取 0/1 形式的布尔值。 */
bool parse_bool(const std::string& text)
{
    return text == "1" ;
}

/** 读取整数；非法值当作 0。 */
int parse_int(const std::string& text)
{
    try
    {
        return std::stoi(text) ;
    }
    catch (...)
    {
        return 0 ;
    }
}

/** 读取版本号；非法值当作 Undefined。 */
ToolChainVersion parse_version(const std::string& text)
{
    try
    {
        const int value = std::stoi(text) ;
        if (value < static_cast<int>(ToolChainVersion::V140) || value > static_cast<int>(ToolChainVersion::V145))
        {
            return ToolChainVersion::Undefined ;
        }
        return static_cast<ToolChainVersion>(value) ;
    }
    catch (...)
    {
        return ToolChainVersion::Undefined ;
    }
}

/** 工具链来源类型写成 0/1/2。 */
int kind_to_number(ToolChainKind kind)
{
    switch (kind)
    {
        case ToolChainKind::VisualStudio: return 1 ;
        case ToolChainKind::Portable: return 2 ;
        case ToolChainKind::Unknown: return 0 ;
    }
    return 0 ;
}

/** 读取工具链来源类型；非法值当作 Unknown。 */
ToolChainKind parse_kind(const std::string& text)
{
    try
    {
        switch (std::stoi(text))
        {
            case 1: return ToolChainKind::VisualStudio ;
            case 2: return ToolChainKind::Portable ;
            default: return ToolChainKind::Unknown ;
        }
    }
    catch (...)
    {
        return ToolChainKind::Unknown ;
    }
}

/** 解析一行编译器缓存；字段数与标签对不上返回空。 */
std::optional<Compiler> parse_compiler_fields(const std::vector<std::string>& fields)
{
    if (fields.empty() || fields[0] != "compiler")
    {
        return std::nullopt ;
    }
    Compiler compiler ;
    if (fields.size() == 6)
    {
        // v1：没有工具集版本、生成器与平台工具集。
        compiler.m_name = fields[1] ;
        compiler.m_version = parse_version(fields[2]) ;
        compiler.m_cl_path = path_from_text(fields[3]) ;
        compiler.m_vcvars_path = path_from_text(fields[4]) ;
        compiler.m_is_checked = parse_bool(fields[5]) ;
        return compiler ;
    }
    if (fields.size() != 8 && fields.size() != 9 && fields.size() != 10 && fields.size() != 13)
    {
        return std::nullopt ;
    }
    compiler.m_name = fields[1] ;
    compiler.m_version = parse_version(fields[2]) ;
    compiler.m_toolset_version = fields[3] ;
    compiler.m_cl_path = path_from_text(fields[4]) ;
    compiler.m_vcvars_path = path_from_text(fields[5]) ;
    compiler.m_vcvars_argument = fields[6] ;
    compiler.m_is_checked = parse_bool(fields[7]) ;
    if (fields.size() >= 9)
    {
        compiler.m_generator = fields[8] ;
    }
    if (fields.size() >= 10)
    {
        compiler.m_platform_toolset = fields[9] ;
    }
    if (fields.size() >= 13)
    {
        compiler.m_msvc_ver = parse_int(fields[10]) ;
        compiler.m_compiler_version = fields[11] ;
        compiler.m_kind = parse_kind(fields[12]) ;
    }
    return compiler ;
}

/** 解析一行 NX SDK 缓存；字段数与标签对不上返回空。 */
std::optional<NXSdk> parse_nx_sdk_fields(const std::vector<std::string>& fields)
{
    if (fields.empty() || fields[0] != "sdk")
    {
        return std::nullopt ;
    }
    NXSdk sdk ;
    if (fields.size() == 6)
    {
        // v1：没有版本来源与安装根。
        sdk.m_version = fields[1] ;
        sdk.m_uf_header_dir = path_from_text(fields[2]) ;
        sdk.m_nxopen_header_dir = path_from_text(fields[3]) ;
        sdk.m_lib_dir = path_from_text(fields[4]) ;
        sdk.m_is_running = parse_bool(fields[5]) ;
        return sdk ;
    }
    if (fields.size() != 9)
    {
        return std::nullopt ;
    }
    sdk.m_version = fields[1] ;
    sdk.m_version_source = fields[2] ;
    sdk.m_root = path_from_text(fields[3]) ;
    sdk.m_uf_header_dir = path_from_text(fields[4]) ;
    sdk.m_nxopen_header_dir = path_from_text(fields[5]) ;
    sdk.m_lib_dir = path_from_text(fields[6]) ;
    sdk.m_is_checked = parse_bool(fields[7]) ;
    sdk.m_is_running = parse_bool(fields[8]) ;
    return sdk ;
}

/** 解析一行 Windows SDK 缓存；字段数与标签对不上返回空。 */
std::optional<WindowsSdk> parse_windows_sdk_fields(const std::vector<std::string>& fields)
{
    if (fields.size() != 8 || fields[0] != "winsdk")
    {
        return std::nullopt ;
    }
    WindowsSdk sdk ;
    sdk.m_version = fields[1] ;
    sdk.m_source = fields[2] ;
    sdk.m_root = path_from_text(fields[3]) ;
    sdk.m_include_dir = path_from_text(fields[4]) ;
    sdk.m_lib_dir = path_from_text(fields[5]) ;
    sdk.m_bin_dir = path_from_text(fields[6]) ;
    sdk.m_is_checked = parse_bool(fields[7]) ;
    return sdk ;
}

/** 判断文件头是否是认识的缓存版本。 */
bool known_cache_header(const std::string& header)
{
    return header == cache_header || header == v5_cache_header || header == v4_cache_header ||
        header == v3_cache_header || header == v2_cache_header || header == legacy_cache_header ;
}

/** 读取环境变量对应的目录；不存在或为空串返回空。 */
Path environment_directory(const wchar_t* name)
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
    return value.empty() ? Path() : Path(value) ;
}

/** 候选缓存目录，按优先级：NX_DEV_CACHE_DIR → LOCALAPPDATA/nx_dev → TEMP/nx_dev。 */
std::vector<Path> cache_candidates()
{
    std::vector<Path> result ;
    const Path override_directory = environment_directory(L"NX_DEV_CACHE_DIR") ;
    if (!override_directory.empty())
    {
        result.push_back(override_directory) ;
    }
    const Path local_app_data = environment_directory(L"LOCALAPPDATA") ;
    if (!local_app_data.empty())
    {
        result.push_back(local_app_data / L"nx_dev") ;
    }
    const Path temporary = environment_directory(L"TEMP") ;
    if (!temporary.empty())
    {
        result.push_back(temporary / L"nx_dev") ;
    }
    return result ;
}

/** 判断候选缓存目录是否可用：目录本身或它的父目录已存在。 */
bool candidate_usable(const Path& candidate)
{
    if (candidate.empty())
    {
        return false ;
    }
    std::error_code error ;
    if (std::filesystem::is_directory(candidate, error))
    {
        return true ;
    }
    error.clear() ;
    const Path parent = candidate.parent_path() ;
    return !parent.empty() && std::filesystem::is_directory(parent, error) ;
}
} // namespace

namespace engine
{
Path cache_directory()
{
    for (const Path& candidate : cache_candidates())
    {
        if (candidate_usable(candidate))
        {
            return candidate ;
        }
    }
    return {} ;
}
} // namespace engine

Path cache_directory()
{
    return engine::cache_directory() ;
}

Path cache_path()
{
    const Path directory = engine::cache_directory() ;
    if (!directory.empty())
    {
        return directory / cache_file_name ;
    }
    // 缓存目录都不可用时仍给界面一个可显示、可清理的兜底路径；读写接口不会用它。
    std::error_code error ;
    const Path current = std::filesystem::current_path(error) ;
    if (error || current.empty())
    {
        return {} ;
    }
    return current / L"nx_dev" / cache_file_name ;
}

bool save_cache(const ScanReport& report)
{
    const Path directory = engine::cache_directory() ;
    if (directory.empty())
    {
        return false ;
    }
    std::error_code error ;
    std::filesystem::create_directories(directory, error) ;
    if (error)
    {
        return false ;
    }
    std::ofstream stream(directory / cache_file_name, std::ios::binary | std::ios::trunc) ;
    if (!stream)
    {
        return false ;
    }
    stream << cache_header << '\n' ;
    for (const auto& compiler : report.m_compilers)
    {
        stream << "compiler\t" << escape(compiler.m_name) ;
        stream << '\t' << static_cast<int>(compiler.m_version) ;
        stream << '\t' << escape(compiler.m_toolset_version) ;
        stream << '\t' << escape(path_text(compiler.m_cl_path)) ;
        stream << '\t' << escape(path_text(compiler.m_vcvars_path)) ;
        stream << '\t' << escape(compiler.m_vcvars_argument) ;
        stream << '\t' << bool_text(compiler.m_is_checked) ;
        stream << '\t' << escape(compiler.m_generator) ;
        stream << '\t' << escape(compiler.m_platform_toolset) ;
        stream << '\t' << compiler.m_msvc_ver ;
        stream << '\t' << escape(compiler.m_compiler_version) ;
        stream << '\t' << kind_to_number(compiler.m_kind) << '\n' ;
    }
    for (const auto& sdk : report.m_sdks)
    {
        stream << "sdk\t" << escape(sdk.m_version) ;
        stream << '\t' << escape(sdk.m_version_source) ;
        stream << '\t' << escape(path_text(sdk.m_root)) ;
        stream << '\t' << escape(path_text(sdk.m_uf_header_dir)) ;
        stream << '\t' << escape(path_text(sdk.m_nxopen_header_dir)) ;
        stream << '\t' << escape(path_text(sdk.m_lib_dir)) ;
        stream << '\t' << bool_text(sdk.m_is_checked) ;
        stream << '\t' << bool_text(sdk.m_is_running) << '\n' ;
    }
    for (const auto& sdk : report.m_windows_sdks)
    {
        stream << "winsdk\t" << escape(sdk.m_version) ;
        stream << '\t' << escape(sdk.m_source) ;
        stream << '\t' << escape(path_text(sdk.m_root)) ;
        stream << '\t' << escape(path_text(sdk.m_include_dir)) ;
        stream << '\t' << escape(path_text(sdk.m_lib_dir)) ;
        stream << '\t' << escape(path_text(sdk.m_bin_dir)) ;
        stream << '\t' << bool_text(sdk.m_is_checked) << '\n' ;
    }
    stream.flush() ;
    return stream.good() ;
}

ScanReport load_cache()
{
    ScanReport report ;
    const Path directory = engine::cache_directory() ;
    if (directory.empty())
    {
        return report ;
    }
    std::ifstream stream(directory / cache_file_name, std::ios::binary) ;
    if (!stream)
    {
        return report ;
    }
    std::string line ;
    if (!std::getline(stream, line))
    {
        return report ;
    }
    if (!known_cache_header(line))
    {
        return report ;
    }
    // 统一按字段数解析：v1/v2/v3/v4/v5 的差异只体现在每行的列数上。
    while (std::getline(stream, line))
    {
        if (line.empty())
        {
            continue ;
        }
        const std::vector<std::string> fields = split_fields(line) ;
        if (fields.empty())
        {
            continue ;
        }
        if (fields[0] == "compiler")
        {
            if (auto compiler = parse_compiler_fields(fields) ; compiler.has_value())
            {
                report.m_compilers.push_back(std::move(*compiler)) ;
            }
        }
        else if (fields[0] == "sdk")
        {
            if (auto sdk = parse_nx_sdk_fields(fields) ; sdk.has_value())
            {
                report.m_sdks.push_back(std::move(*sdk)) ;
            }
        }
        else if (fields[0] == "winsdk")
        {
            if (auto sdk = parse_windows_sdk_fields(fields) ; sdk.has_value())
            {
                report.m_windows_sdks.push_back(std::move(*sdk)) ;
            }
        }
    }
    return report ;
}
} // namespace scan
