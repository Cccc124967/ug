#include "../dev_core.h"
#include <windows.h>
#include <limits>
#include <memory>
#include <stdexcept>

namespace core
{
std::wstring utf8_to_wide(const std::string& text)
{
    if (text.empty())
    {
        return {} ;
    }
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        throw std::length_error("UTF-8 text is too long") ;
    }
    const int size = static_cast<int>(text.size()) ;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0) ;
    if (count == 0)
    {
        throw std::invalid_argument("Invalid UTF-8") ;
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0') ;
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, result.data(), count) ;
    return result ;
}

std::wstring uft8_to_wide(const std::string& text)
{
    return utf8_to_wide(text) ;
}

std::string wide_to_utf8(const std::wstring& text)
{
    if (text.empty())
    {
        return {} ;
    }
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        throw std::length_error("UTF-16 text is too long") ;
    }
    const int size = static_cast<int>(text.size()) ;
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, nullptr, 0, nullptr, nullptr) ;
    if (count == 0)
    {
        throw std::invalid_argument("Invalid UTF-16") ;
    }
    std::string result(static_cast<std::size_t>(count), '\0') ;
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, result.data(), count, nullptr, nullptr) ;
    return result ;
}

std::string utf8_to_gb2312(const std::string& text)
{
    const std::wstring wide = utf8_to_wide(text) ;
    if (wide.empty())
    {
        return {} ;
    }
    if (wide.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        throw std::length_error("UTF-16 text is too long") ;
    }
    const int size = static_cast<int>(wide.size()) ;
    BOOL used_default = FALSE ;
    const int count = WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS, wide.data(), size, nullptr, 0,
        nullptr, &used_default) ;
    if (count <= 0 || used_default != FALSE)
    {
        throw std::invalid_argument("Some characters cannot be represented in GB2312") ;
    }
    std::string result(static_cast<std::size_t>(count), '\0') ;
    BOOL used_default_written = FALSE ;
    WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS, wide.data(), size, result.data(), count, nullptr,
        &used_default_written) ;
    if (used_default_written != FALSE)
    {
        throw std::invalid_argument("Some characters cannot be represented in GB2312") ;
    }
    return result ;
}

std::u32string utf8_to_utf32(std::string_view text)
{
    const auto wide = utf8_to_wide(std::string(text)) ;
    std::u32string result ;
    for (std::size_t index = 0 ; index < wide.size() ; ++index)
    {
        char32_t code_point = wide[index] ;
        if (code_point >= 0xD800 && code_point <= 0xDBFF)
        {
            code_point = 0x10000 + ((code_point - 0xD800) << 10) + (wide[++index] - 0xDC00) ;
        }
        result.push_back(code_point) ;
    }
    return result ;
}

std::string utf32_to_utf8(std::u32string_view text)
{
    std::wstring wide ;
    for (char32_t code_point : text)
    {
        if (code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF))
        {
            throw std::invalid_argument("Invalid Unicode code point") ;
        }
        if (code_point <= 0xFFFF)
        {
            wide.push_back(static_cast<wchar_t>(code_point)) ;
        }
        else
        {
            code_point -= 0x10000 ;
            wide.push_back(static_cast<wchar_t>(0xD800 + (code_point >> 10))) ;
            wide.push_back(static_cast<wchar_t>(0xDC00 + (code_point & 0x3FF))) ;
        }
    }
    return wide_to_utf8(wide) ;
}

StringList get_env_var()
{
    const auto release = [](wchar_t* block)
    {
        FreeEnvironmentStringsW(block) ;
    } ;
    std::unique_ptr<wchar_t, decltype(release)> block(GetEnvironmentStringsW(), release) ;
    StringList result ;
    if (block)
    {
        for (const wchar_t* entry = block.get() ; *entry != L'\0' ; entry += wcslen(entry) + 1)
        {
            result.push_back(wide_to_utf8(entry)) ;
        }
    }
    return result ;
}

std::size_t console_cell_width(char32_t code_point) noexcept
{
    if (code_point < 0x20 || (code_point >= 0x7F && code_point < 0xA0) ||
        (code_point >= 0x300 && code_point <= 0x36F) ||
        (code_point >= 0xFE00 && code_point <= 0xFE0F) || code_point == 0x200D)
    {
        return 0 ;
    }
    if ((code_point >= 0x1100 && code_point <= 0x115F) ||
        (code_point >= 0x2E80 && code_point <= 0xA4CF) ||
        (code_point >= 0xAC00 && code_point <= 0xD7A3) ||
        (code_point >= 0xF900 && code_point <= 0xFAFF) ||
        (code_point >= 0xFE10 && code_point <= 0xFE6F) ||
        (code_point >= 0xFF01 && code_point <= 0xFF60) ||
        (code_point >= 0xFFE0 && code_point <= 0xFFE6) ||
        (code_point >= 0x1F300 && code_point <= 0x1FAFF) ||
        (code_point >= 0x20000 && code_point <= 0x3FFFD))
    {
        return 2 ;
    }
    return 1 ;
}
} // namespace core
