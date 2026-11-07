#pragma once
#include "../console_control.h"

namespace console::detail
{
/** 把标题和选项中的控制字符换为空格，避免破坏终端布局。 */
std::string safe_text(const std::string& text) ;
/** 计算文字列宽，忽略 ANSI 颜色序列。 */
std::size_t display_width(const std::string& text) ;
/** 按终端列截断，保留完整 Unicode 字符与 ANSI 颜色序列。 */
std::string clip_text(const std::string& text, std::size_t columns) ;
/** 去除 ANSI 颜色，用于重定向输出及测试。 */
std::string plain_text(const std::string& text) ;
/** 生成统一的游标占位；当前项与非当前项不共用尾部空格。 */
std::string cursor_prefix(bool active, const std::string& color) ;
/** 让标题、当前项和底部提示适配可见行数。 */
ConsoleFrame fit_frame(ConsoleFrame frame, std::size_t rows) ;

/** 状态区行数规划结果。 */
struct ConsoleStatusPlan
{
    /** 状态区应占用的行数（含末尾补空行）；0 表示不显示状态区。 */
    std::size_t m_area_rows = 0 ;
    /** 实际渲染的状态行数；不超过 m_area_rows。 */
    std::size_t m_visible_rows = 0 ;
} ;

/** 规划状态区行数；密封后保持既有高度，避免覆盖下方常驻输出。 */
ConsoleStatusPlan plan_status_rows(std::size_t requested, std::size_t previous, std::size_t capacity, bool sealed) noexcept ;
/** 按列宽裁剪一行状态文本：保留 ANSI 颜色、清洗控制字符、行尾补重置色。 */
std::string clip_status_line(const std::string& line, std::size_t columns) ;
/** 按列宽与行数容量渲染状态行；超出容量的末尾行丢弃。 */
std::vector<std::string> render_status_lines(const std::vector<std::string>& lines, std::size_t columns, std::size_t capacity) ;
/** 状态区最多可占用的行数；至少给控件留一行。 */
std::size_t status_capacity(std::size_t window_rows) noexcept ;
} // namespace console::detail
