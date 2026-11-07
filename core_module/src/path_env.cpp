#include "../path_env.h"
#include <cstddef>
#include <string>

namespace core
{
namespace
{
/** 去掉首尾空白与成对引号；用于比较 PATH 里的单个目录项。 */
std::wstring trimmed_entry(const std::wstring& text)
{
    const auto is_space = [](wchar_t letter)
    {
        return letter == L' ' || letter == L'\t' || letter == L'\r' || letter == L'\n' ;
    } ;
    std::size_t begin = 0 ;
    std::size_t end = text.size() ;
    while (begin < end && is_space(text[begin]))
    {
        ++begin ;
    }
    while (end > begin && is_space(text[end - 1]))
    {
        --end ;
    }
    std::wstring result = text.substr(begin, end - begin) ;
    if (result.size() >= 2 && result.front() == L'"' && result.back() == L'"')
    {
        result = result.substr(1, result.size() - 2) ;
        while (!result.empty() && is_space(result.back()))
        {
            result.pop_back() ;
        }
    }
    return result ;
}

/** 比较键：ASCII 转小写并去掉末尾的目录分隔符。 */
std::wstring normalized_key(const std::wstring& text)
{
    std::wstring result = trimmed_entry(text) ;
    while (result.size() > 1 && (result.back() == L'\\' || result.back() == L'/'))
    {
        result.pop_back() ;
    }
    for (wchar_t& letter : result)
    {
        if (letter >= L'A' && letter <= L'Z')
        {
            letter = static_cast<wchar_t>(letter - L'A' + L'a') ;
        }
    }
    return result ;
}

/** 目录在 PATH 里的比较键。 */
std::wstring directory_key(const std::filesystem::path& directory)
{
    return normalized_key(directory.wstring()) ;
}

/** 目录写进 PATH 时的文本形式：去掉末尾分隔符。 */
std::wstring directory_text(const std::filesystem::path& directory)
{
    std::wstring text = trimmed_entry(directory.wstring()) ;
    while (text.size() > 1 && (text.back() == L'\\' || text.back() == L'/'))
    {
        text.pop_back() ;
    }
    return text ;
}

/** 依次访问 PATH 值里的每一项；访问函数返回 true 表示停止。 */
template <typename Visitor>
void for_each_entry(const std::wstring& path_value, Visitor visit)
{
    std::size_t begin = 0 ;
    while (begin <= path_value.size())
    {
        const std::size_t separator = path_value.find(L';', begin) ;
        const std::wstring entry = path_value.substr(begin,
            separator == std::wstring::npos ? std::wstring::npos : separator - begin) ;
        if (visit(entry))
        {
            return ;
        }
        if (separator == std::wstring::npos)
        {
            return ;
        }
        begin = separator + 1 ;
    }
}
} // namespace

bool path_has_directory(const std::wstring& path_value, const std::filesystem::path& directory)
{
    const std::wstring key = directory_key(directory) ;
    if (key.empty())
    {
        return false ;
    }
    bool found = false ;
    for_each_entry(path_value, [&key, &found](const std::wstring& entry)
    {
        if (normalized_key(entry) == key)
        {
            found = true ;
            return true ;
        }
        return false ;
    }) ;
    return found ;
}

bool path_same_directory(const std::filesystem::path& left, const std::filesystem::path& right)
{
    const std::wstring left_key = normalized_key(left.wstring()) ;
    return !left_key.empty() && left_key == normalized_key(right.wstring()) ;
}

std::wstring path_add_directory(const std::wstring& path_value,
    const std::filesystem::path& directory)
{
    const std::wstring text = directory_text(directory) ;
    if (text.empty() || path_has_directory(path_value, directory))
    {
        return path_value ;
    }
    if (path_value.empty())
    {
        return text ;
    }
    return text + L";" + path_value ;
}

std::wstring path_remove_directory(const std::wstring& path_value,
    const std::filesystem::path& directory)
{
    const std::wstring key = directory_key(directory) ;
    if (key.empty())
    {
        return path_value ;
    }
    std::wstring result ;
    bool first = true ;
    for_each_entry(path_value, [&key, &result, &first](const std::wstring& entry)
    {
        if (normalized_key(entry) != key)
        {
            if (!first)
            {
                result += L';' ;
            }
            result += entry ;
            first = false ;
        }
        return false ;
    }) ;
    return result ;
}
} // namespace core
