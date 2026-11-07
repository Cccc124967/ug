#pragma once
// ============================================================================
// console_theme.h — 控制台风格：UTF-8 / ANSI 颜色 / 渐变横幅。
//
// 入口先调 prepare_console()：切 UTF-8 代码页、打开 ANSI（VT）颜色，
// 并按控制台能力设置 core::ansi_color_enabled；之后所有彩色输出自动生效。
// ============================================================================
#include <cstddef>
#include <string>
#include <string_view>

namespace app
{
namespace theme
{
/** 初始化控制台：UTF-8 + ANSI 颜色；返回颜色是否可用。 */
bool prepare_console() noexcept ;

/** 是否可用 ANSI 颜色（NX_DEV_COLOR=always/never 可覆盖自动判断）。 */
bool color_enabled() noexcept ;

/** 用 24 位渐变色渲染多行横幅（去掉首尾空行）；无颜色时返回纯文本。 */
std::string gradient_banner(std::string_view banner) ;

/** 打印渐变横幅（控制台里按窗口宽度居中）并补一个空行。 */
void print_banner(std::string_view banner) ;

/** 生成一条暗淡的分隔线，如 ────────────────。 */
std::string divider(std::size_t width = 64) ;
} // namespace theme
} // namespace app
