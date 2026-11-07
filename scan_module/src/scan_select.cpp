#include "../scan_select.h"
#include "scan_internal.h"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace scan
{
namespace
{
/** 选型记忆文件名，与扫描缓存同目录。 */
constexpr const char* selection_memory_file = "selection_memory.tsv" ;

/** 选型记忆文件头；用于识别格式与损坏。 */
constexpr const char* selection_memory_header = "# nx_dev selection memory v1" ;

/** 自动选型优先级：V142 最优、V143 次之、V145 仅为兜底；负数表示不参与。 */
int auto_compiler_rank(ToolChainVersion version)
{
    switch (version)
    {
        case ToolChainVersion::V142: return 0 ;
        case ToolChainVersion::V143: return 1 ;
        case ToolChainVersion::V145: return 2 ;
        default: return -1 ;
    }
}

/** 手动菜单排序用的大类顺序：参与自动选型的排在前面，其余按版本名兜底。 */
int menu_compiler_rank(ToolChainVersion version)
{
    const int rank = auto_compiler_rank(version) ;
    return rank >= 0 ? rank : 3 ;
}

/** 把 ASCII 字母转小写。 */
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

/** 去掉文本首尾的 ASCII 空白。 */
std::string trim_ascii(std::string text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
        text.front() == '\r' || text.front() == '\n'))
    {
        text.erase(text.begin()) ;
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
        text.back() == '\r' || text.back() == '\n'))
    {
        text.pop_back() ;
    }
    return text ;
}

/** 比较两段路径文本，忽略 ASCII 大小写。 */
bool same_text(const std::string& left, const std::string& right)
{
    return ascii_lower(left) == ascii_lower(right) ;
}

/** 把文本转义成一行能存回去的形式。 */
std::string escape_text(const std::string& text)
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
std::string unescape_text(const std::string& text)
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

/** 判断候选 NX SDK 是否比当前最优更适合自动选型：运行中优先，其次有版本优先，再按版本降序。 */
bool better_nx_sdk(const NXSdk& candidate, const NXSdk& best)
{
    if (candidate.m_is_running != best.m_is_running)
    {
        return candidate.m_is_running ;
    }
    const bool candidate_has_version = !candidate.m_version.empty() ;
    const bool best_has_version = !best.m_version.empty() ;
    if (candidate_has_version != best_has_version)
    {
        return candidate_has_version ;
    }
    if (!candidate_has_version)
    {
        return false ;
    }
    return engine::compare_version_text(candidate.m_version, best.m_version) > 0 ;
}

/** 选型记忆文件的完整路径；缓存目录不可用时返回空。 */
Path selection_memory_path()
{
    const Path directory = engine::cache_directory() ;
    if (directory.empty())
    {
        return {} ;
    }
    return directory / selection_memory_file ;
}
} // namespace

std::optional<Compiler> pick_compiler(const std::vector<Compiler>& candidates, std::string& warning)
{
    warning.clear() ;
    const Compiler* best = nullptr ;
    for (const Compiler& compiler : candidates)
    {
        if (!compiler.m_is_checked)
        {
            continue ;
        }
        const int rank = auto_compiler_rank(compiler.m_version) ;
        if (rank < 0)
        {
            continue ;
        }
        if (best == nullptr || rank < auto_compiler_rank(best->m_version) ||
            (rank == auto_compiler_rank(best->m_version) &&
                engine::compare_version_text(compiler.m_toolset_version, best->m_toolset_version) > 0))
        {
            best = &compiler ;
        }
    }
    if (best == nullptr)
    {
        return std::nullopt ;
    }
    if (best->m_version == ToolChainVersion::V145)
    {
        warning = "本机未找到 MSVC v142/v143 工具集，已自动选用 v145；v145 较新，"
            "若目标 NX 版本对编译器工具集有特定要求，请自行确认兼容性。" ;
    }
    return *best ;
}

std::optional<NXSdk> pick_nx_sdk(const std::vector<NXSdk>& sdks)
{
    const NXSdk* best = nullptr ;
    for (const NXSdk& sdk : sdks)
    {
        // 只看完整可用的条目；版本探测失败的条目也参与，只是排在有版本之后。
        if (!sdk.m_is_checked)
        {
            continue ;
        }
        if (best == nullptr || better_nx_sdk(sdk, *best))
        {
            best = &sdk ;
        }
    }
    if (best == nullptr)
    {
        return std::nullopt ;
    }
    return *best ;
}

std::optional<WindowsSdk> pick_windows_sdk(const std::vector<WindowsSdk>& sdks)
{
    const WindowsSdk* best = nullptr ;
    for (const WindowsSdk& sdk : sdks)
    {
        // 版本为空的不参与：Presets 要锁版本；全部缺版本时返回空由调用方按不锁版本处理。
        if (!sdk.m_is_checked || sdk.m_version.empty())
        {
            continue ;
        }
        if (best == nullptr || engine::compare_version_text(sdk.m_version, best->m_version) > 0)
        {
            best = &sdk ;
        }
    }
    if (best == nullptr)
    {
        return std::nullopt ;
    }
    return *best ;
}

std::string cmake_generator_name(const Compiler& compiler)
{
    return trim_ascii(compiler.m_generator) ;
}

ToolchainSelection pick_toolchain(const ScanReport& report, bool ninja_available)
{
    ToolchainSelection selection ;
    selection.m_ok = false ;
    std::string warning ;
    const auto compiler = pick_compiler(report.m_compilers, warning) ;
    if (compiler.has_value())
    {
        selection.m_compiler = *compiler ;
        selection.m_warning = warning ;
    }
    const auto nx_sdk = pick_nx_sdk(report.m_sdks) ;
    if (nx_sdk.has_value())
    {
        selection.m_nx_sdk = *nx_sdk ;
    }
    const auto windows_sdk = pick_windows_sdk(report.m_windows_sdks) ;
    if (windows_sdk.has_value())
    {
        selection.m_windows_sdk = *windows_sdk ;
    }
    if (!compiler.has_value())
    {
        selection.m_message = "没有可用的 MSVC 工具链：需要 v142/v143/v145 且 cl.exe 与 vcvars 关键文件齐全。" ;
        return selection ;
    }
    if (!nx_sdk.has_value())
    {
        selection.m_message = "没有可用的 NX SDK：需要 uf.h、NXOpen 头文件与 libufun.lib 齐全。" ;
        return selection ;
    }
    const BuildPlan plan = resolve_build_plan(*compiler, ninja_available) ;
    if (!plan.m_ok)
    {
        selection.m_message = plan.m_message.empty() ? "无法从所选编译器推导构建方案。" : plan.m_message ;
        return selection ;
    }
    selection.m_build_plan = plan ;
    selection.m_generator = plan.m_generator ;
    selection.m_ok = true ;
    return selection ;
}

void sort_compilers(std::vector<Compiler>& candidates)
{
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const Compiler& left, const Compiler& right)
        {
            const int left_rank = menu_compiler_rank(left.m_version) ;
            const int right_rank = menu_compiler_rank(right.m_version) ;
            if (left_rank != right_rank)
            {
                return left_rank < right_rank ;
            }
            const int compared = engine::compare_version_text(left.m_toolset_version, right.m_toolset_version) ;
            if (compared != 0)
            {
                return compared > 0 ;
            }
            return path_text(left.m_cl_path) < path_text(right.m_cl_path) ;
        }) ;
}

void sort_nx_sdks(std::vector<NXSdk>& sdks)
{
    std::stable_sort(sdks.begin(), sdks.end(),
        [](const NXSdk& left, const NXSdk& right)
        {
            if (left.m_is_running != right.m_is_running)
            {
                return left.m_is_running ;
            }
            const int compared = engine::compare_version_text(left.m_version, right.m_version) ;
            if (compared != 0)
            {
                return compared > 0 ;
            }
            return path_text(left.m_root) < path_text(right.m_root) ;
        }) ;
}

void sort_windows_sdks(std::vector<WindowsSdk>& sdks)
{
    std::stable_sort(sdks.begin(), sdks.end(),
        [](const WindowsSdk& left, const WindowsSdk& right)
        {
            const int compared = engine::compare_version_text(left.m_version, right.m_version) ;
            if (compared != 0)
            {
                return compared > 0 ;
            }
            return path_text(left.m_root) < path_text(right.m_root) ;
        }) ;
}

SelectionMemory load_selection_memory()
{
    SelectionMemory memory ;
    const Path file = selection_memory_path() ;
    if (file.empty())
    {
        return memory ;
    }
    std::ifstream stream(file, std::ios::binary) ;
    if (!stream)
    {
        return memory ;
    }
    std::string line ;
    if (!std::getline(stream, line) || line != selection_memory_header)
    {
        return memory ;
    }
    while (std::getline(stream, line))
    {
        const auto separator = line.find('\t') ;
        if (separator == std::string::npos)
        {
            continue ;
        }
        const std::string name = line.substr(0, separator) ;
        const std::string value = unescape_text(line.substr(separator + 1)) ;
        if (name == "compiler")
        {
            memory.m_compiler_key = value ;
        }
        else if (name == "nx_sdk")
        {
            memory.m_nx_sdk_key = value ;
        }
        else if (name == "windows_sdk")
        {
            memory.m_windows_sdk_key = value ;
        }
    }
    return memory ;
}

bool save_selection_memory(const SelectionMemory& memory)
{
    const Path file = selection_memory_path() ;
    if (file.empty())
    {
        return false ;
    }
    std::error_code error ;
    std::filesystem::create_directories(file.parent_path(), error) ;
    if (error)
    {
        return false ;
    }
    std::ofstream stream(file, std::ios::binary | std::ios::trunc) ;
    if (!stream)
    {
        return false ;
    }
    stream << selection_memory_header << '\n' ;
    stream << "compiler\t" << escape_text(memory.m_compiler_key) << '\n' ;
    stream << "nx_sdk\t" << escape_text(memory.m_nx_sdk_key) << '\n' ;
    stream << "windows_sdk\t" << escape_text(memory.m_windows_sdk_key) << '\n' ;
    stream.flush() ;
    return stream.good() ;
}

std::size_t find_compiler_index(const std::vector<Compiler>& candidates, const SelectionMemory& memory)
{
    if (memory.m_compiler_key.empty())
    {
        return 0 ;
    }
    for (std::size_t index = 0 ; index < candidates.size() ; ++index)
    {
        if (same_text(path_text(candidates[index].m_cl_path), memory.m_compiler_key))
        {
            return index ;
        }
    }
    return 0 ;
}

std::size_t find_nx_sdk_index(const std::vector<NXSdk>& sdks, const SelectionMemory& memory)
{
    if (memory.m_nx_sdk_key.empty())
    {
        return 0 ;
    }
    for (std::size_t index = 0 ; index < sdks.size() ; ++index)
    {
        if (same_text(path_text(sdks[index].m_root), memory.m_nx_sdk_key))
        {
            return index ;
        }
    }
    return 0 ;
}

std::size_t find_windows_sdk_index(const std::vector<WindowsSdk>& sdks, const SelectionMemory& memory)
{
    if (memory.m_windows_sdk_key.empty())
    {
        return 0 ;
    }
    for (std::size_t index = 0 ; index < sdks.size() ; ++index)
    {
        if (same_text(sdks[index].m_version, memory.m_windows_sdk_key))
        {
            return index ;
        }
    }
    return 0 ;
}
} // namespace scan
