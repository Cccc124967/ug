#pragma once
#include <string>
#include <string_view>

namespace core
{
namespace color
{
inline constexpr const char* reset = "\033[0m" ;
inline constexpr const char* black = "\033[30m" ;
inline constexpr const char* red = "\033[31m" ;
inline constexpr const char* green = "\033[32m" ;
inline constexpr const char* yellow = "\033[33m" ;
inline constexpr const char* blue = "\033[34m" ;
inline constexpr const char* magenta = "\033[35m" ;
inline constexpr const char* cyan = "\033[36m" ;
inline constexpr const char* white = "\033[37m" ;
inline constexpr const char* bright_black = "\033[90m" ;
inline constexpr const char* bright_red = "\033[91m" ;
inline constexpr const char* bright_green = "\033[92m" ;
inline constexpr const char* bright_yellow = "\033[93m" ;
inline constexpr const char* bright_blue = "\033[94m" ;
inline constexpr const char* bright_magenta = "\033[95m" ;
inline constexpr const char* bright_cyan = "\033[96m" ;
inline constexpr const char* bright_white = "\033[97m" ;
inline constexpr const char* bold = "\033[1m" ;

// 语义颜色表；控件默认值统一从这里取。
inline constexpr const char* title = bright_yellow ;
inline constexpr const char* selected = bright_cyan ;
inline constexpr const char* unselected = white ;
inline constexpr const char* checked = bright_green ;
inline constexpr const char* unchecked = bright_black ;
inline constexpr const char* success = bright_green ;
inline constexpr const char* failure = bright_red ;
inline constexpr const char* warning = bright_yellow ;
inline constexpr const char* information = bright_cyan ;
inline constexpr const char* hint = bright_black ;
inline constexpr const char* input = bright_white ;
} // namespace color

/** 控制台是否已开启 ANSI 颜色；由程序入口在开启 VT 后设置。 */
inline bool ansi_color_enabled = false ;

/** 有颜色时用 ANSI 序列包一层，否则原样返回。 */
inline std::string paint(const std::string& text, std::string_view ansi)
{
    if (!ansi_color_enabled || ansi.empty())
    {
        return text ;
    }
    std::string result ;
    result.reserve(text.size() + ansi.size() + 5) ;
    result.append(ansi) ;
    result += text ;
    result += color::reset ;
    return result ;
}

// 有游标：箭头 + 一个空格；无游标：两个空格。不能再额外追加公共空格。
inline std::string select_arrow = "▶" ;
inline std::string select_space = " " ;
inline std::string unselect_space = "  " ;
// 勾选框始终占三个字符；框内空格和框后的空格各有自己的位置。
inline std::string checked_box = "[x]" ;
inline std::string unchecked_box = "[ ]" ;
inline std::string checkbox_space = " " ;
inline constexpr unsigned input_cursor_size = 20 ;
} // namespace core
