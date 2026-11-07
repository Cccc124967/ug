#include "console_theme.h"
#include "../core_module/console_style.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace app
{
namespace theme
{
namespace
{
/** 渐变色标：位置 0-1 与 RGB 分量。 */
struct ColorStop
{
    double m_position ;
    int m_red ;
    int m_green ;
    int m_blue ;
} ;

/** 横幅渐变：蓝 → 青 → 紫 → 粉。 */
constexpr std::array<ColorStop, 4> gradient_stops = {{
    { 0.00 , 0x4F , 0x8C , 0xFF } ,
    { 0.35 , 0x00 , 0xE0 , 0xFF } ,
    { 0.70 , 0xB2 , 0x6B , 0xFF } ,
    { 1.00 , 0xFF , 0x5D , 0xA2 } ,
}} ;

bool g_color_enabled = false ;      // ANSI 颜色是否可用
std::size_t g_console_width = 0 ;   // 控制台窗口宽度；非控制台为 0

/** 读取 NX_DEV_COLOR：1 = 强制开、-1 = 强制关、0 = 自动。 */
int color_override() noexcept
{
    wchar_t buffer[16] = {} ;
    const DWORD written = GetEnvironmentVariableW(L"NX_DEV_COLOR", buffer,
        static_cast<DWORD>(std::size(buffer))) ;
    if (written == 0 || written >= std::size(buffer))
    {
        return 0 ;
    }
    const std::wstring value(buffer) ;
    if (value == L"always" || value == L"1" || value == L"true")
    {
        return 1 ;
    }
    if (value == L"never" || value == L"0" || value == L"false")
    {
        return -1 ;
    }
    return 0 ;
}

/** 生成 24 位真彩色前景色的 ANSI 序列。 */
std::string ansi_rgb(int red, int green, int blue)
{
    char buffer[32] = {} ;
    std::snprintf(buffer, sizeof(buffer), "\033[38;2;%d;%d;%dm", red, green, blue) ;
    return std::string(buffer) ;
}

/** 按 t(0-1) 在色标之间线性插值。 */
std::string gradient_sequence(double t)
{
    t = std::clamp(t, 0.0, 1.0) ;
    for (std::size_t index = 1 ; index < gradient_stops.size() ; ++index)
    {
        const ColorStop& left = gradient_stops[index - 1] ;
        const ColorStop& right = gradient_stops[index] ;
        if (t <= right.m_position)
        {
            const double span = right.m_position - left.m_position ;
            const double ratio = span > 0.0 ? (t - left.m_position) / span : 0.0 ;
            const int red = static_cast<int>(left.m_red + (right.m_red - left.m_red) * ratio + 0.5) ;
            const int green = static_cast<int>(left.m_green + (right.m_green - left.m_green) * ratio + 0.5) ;
            const int blue = static_cast<int>(left.m_blue + (right.m_blue - left.m_blue) * ratio + 0.5) ;
            return ansi_rgb(red, green, blue) ;
        }
    }
    return ansi_rgb(gradient_stops.back().m_red, gradient_stops.back().m_green,
        gradient_stops.back().m_blue) ;
}

/** 返回一个 UTF-8 序列的字节数；非法首字节按 1 处理。 */
std::size_t utf8_sequence_length(unsigned char lead) noexcept
{
    if ((lead & 0x80) == 0)
    {
        return 1 ;
    }
    if ((lead & 0xE0) == 0xC0)
    {
        return 2 ;
    }
    if ((lead & 0xF0) == 0xE0)
    {
        return 3 ;
    }
    if ((lead & 0xF8) == 0xF0)
    {
        return 4 ;
    }
    return 1 ;
}

/** 按 Unicode 码点个数算列宽（横幅里的字符都是单列）。 */
std::size_t text_columns(const std::string& text) noexcept
{
    std::size_t columns = 0 ;
    for (std::size_t index = 0 ; index < text.size() ; ++columns)
    {
        index += utf8_sequence_length(static_cast<unsigned char>(text[index])) ;
    }
    return columns ;
}

/** 按 '\n' 切行，顺便去掉行尾 '\r'。 */
std::vector<std::string> split_lines(std::string_view text)
{
    std::vector<std::string> lines ;
    std::size_t begin = 0 ;
    while (begin <= text.size())
    {
        std::size_t end = text.find('\n', begin) ;
        if (end == std::string_view::npos)
        {
            end = text.size() ;
        }
        std::string line(text.substr(begin, end - begin)) ;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back() ;
        }
        lines.push_back(std::move(line)) ;
        begin = end + 1 ;
    }
    return lines ;
}
} // namespace

bool prepare_console() noexcept
{
    SetConsoleCP(CP_UTF8) ;
    SetConsoleOutputCP(CP_UTF8) ;
    bool console = false ;
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE) ;
    DWORD mode = 0 ;
    if (output != INVALID_HANDLE_VALUE && output != nullptr && GetConsoleMode(output, &mode) != FALSE)
    {
        console = true ;
        SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT) ;
        CONSOLE_SCREEN_BUFFER_INFO info = {} ;
        if (GetConsoleScreenBufferInfo(output, &info) != FALSE)
        {
            const int width = info.srWindow.Right - info.srWindow.Left + 1 ;
            g_console_width = width > 0 ? static_cast<std::size_t>(width) : 0 ;
        }
    }
    bool enabled = console ;
    const int override_value = color_override() ;
    if (override_value > 0)
    {
        enabled = true ;
    }
    else if (override_value < 0)
    {
        enabled = false ;
    }
    g_color_enabled = enabled ;
    core::ansi_color_enabled = enabled ;
    return enabled ;
}

bool color_enabled() noexcept
{
    return g_color_enabled ;
}

std::string gradient_banner(std::string_view banner)
{
    std::vector<std::string> lines = split_lines(banner) ;
    while (!lines.empty() && lines.front().empty())
    {
        lines.erase(lines.begin()) ;
    }
    while (!lines.empty() && lines.back().empty())
    {
        lines.pop_back() ;
    }
    if (lines.empty())
    {
        return {} ;
    }
    if (!g_color_enabled)
    {
        std::string text ;
        for (const std::string& line : lines)
        {
            text += line ;
            text += '\n' ;
        }
        return text ;
    }

    std::size_t max_columns = 0 ;
    for (const std::string& line : lines)
    {
        max_columns = (std::max)(max_columns, text_columns(line)) ;
    }
    const std::size_t height = lines.size() ;
    std::string text ;
    for (std::size_t row = 0 ; row < height ; ++row)
    {
        const std::string& line = lines[row] ;
        const std::size_t line_columns = text_columns(line) ;
        if (g_console_width > line_columns)
        {
            text.append((g_console_width - line_columns) / 2, ' ') ;
        }
        const double row_ratio = height > 1 ?
            static_cast<double>(row) / static_cast<double>(height - 1) : 0.0 ;
        std::size_t column = 0 ;
        for (std::size_t index = 0 ; index < line.size() ; ++column)
        {
            const std::size_t length = utf8_sequence_length(static_cast<unsigned char>(line[index])) ;
            const double column_ratio = max_columns > 1 ?
                static_cast<double>(column) / static_cast<double>(max_columns - 1) : 0.0 ;
            text += gradient_sequence(0.65 * column_ratio + 0.35 * row_ratio) ;
            text.append(line, index, length) ;
            index += length ;
        }
        text += core::color::reset ;
        text += '\n' ;
    }
    return text ;
}

void print_banner(std::string_view banner)
{
    const std::string text = gradient_banner(banner) ;
    std::cout << text ;
    if (!text.empty())
    {
        std::cout << '\n' ;
    }
}

std::string divider(std::size_t width)
{
    std::string line ;
    line.reserve(width * 3) ;
    for (std::size_t index = 0 ; index < width ; ++index)
    {
        line += "\u2500" ;
    }
    return core::paint(line, core::color::bright_black) ;
}
} // namespace theme
} // namespace app
