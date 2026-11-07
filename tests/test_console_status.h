#pragma once
// ============================================================================
// test_console_status.h — console 状态区的纯逻辑自检（不接管控制台、不弹窗口）。
//
// 覆盖 console::detail 中状态区相关的纯函数：状态行按列宽裁剪（Unicode 双宽/
// 组合符/ANSI 颜色/控制字符）、行数容量裁剪、状态区行数规划（未密封可增减、
// 密封后保持高度）与状态区行数上限。由 app/main.cpp 调用，不自己写 main()。
// ============================================================================
#include "../console_module/console_control.h"
#include "../console_module/src/console_detail.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int g_console_status_failures = 0 ;
int g_console_status_checks = 0 ;

/** 记一条断言结果；通过打 INFO，失败打 ERROR 并累计。 */
void console_status_report(bool passed, const std::string& what)
{
    ++g_console_status_checks ;
    if (passed)
    {
        std::cout << "[INFO] console status: " << what << '\n' ;
        return ;
    }
    ++g_console_status_failures ;
    std::cerr << "[ERROR] console status: " << what << '\n' ;
}

/** 布尔断言。 */
void console_status_check(bool passed, const std::string& what)
{
    console_status_report(passed, what) ;
}

/** 文本相等断言；失败时附带期望值与实际值。 */
void console_status_text(std::string_view actual, std::string_view expected, const std::string& what)
{
    const bool passed = actual == expected ;
    console_status_report(passed, what) ;
    if (!passed)
    {
        std::cerr << "        期望: [" << expected << "]\n" ;
        std::cerr << "        实际: [" << actual << "]\n" ;
    }
}

/** 尺寸相等断言；失败时附带期望值与实际值。 */
void console_status_number(std::size_t actual, std::size_t expected, const std::string& what)
{
    const bool passed = actual == expected ;
    console_status_report(passed, what) ;
    if (!passed)
    {
        std::cerr << "        期望: " << expected << "\n" ;
        std::cerr << "        实际: " << actual << "\n" ;
    }
}

/** 状态行按列宽裁剪：Unicode 双宽、组合符、ANSI 颜色与控制字符清洗。 */
void console_status_test_clip()
{
    const auto clip = [](const std::string& line, std::size_t columns)
    {
        return console::detail::clip_status_line(line, columns) ;
    } ;
    const auto plain = [](const std::string& text)
    {
        return console::detail::plain_text(text) ;
    } ;

    console_status_text(plain(clip("abcdef", 3)), "abc", "clip: 按列裁剪 ASCII") ;
    console_status_text(plain(clip("你好世界", 3)), "你", "clip: 双宽字符不切半") ;
    console_status_text(plain(clip("e\xCC\x81x", 1)), "e\xCC\x81", "clip: 组合符号零宽不占列") ;
    console_status_text(plain(clip("abc", 0)), "", "clip: 零列裁剪为空") ;
    console_status_text(plain(clip("abcdef", 80)), "abcdef", "clip: 足够宽时保留全文") ;
    console_status_text(plain(clip("a\tb\nc", 80)), "a b c", "clip: 控制字符换成空格") ;
    console_status_text(plain(clip(std::string("a") + static_cast<char>(0x7F) + "b", 80)), "a b",
        "clip: DEL 换成空格") ;

    const auto colored = clip("\033[31m红色\033[0m", 2) ;
    console_status_text(plain(colored), "红", "clip: 带颜色文本按列裁剪") ;
    console_status_check(colored.find("\033[31m") != std::string::npos, "clip: 保留 ANSI 颜色前缀") ;
    console_status_check(colored.size() >= 4 &&
        colored.compare(colored.size() - 4, 4, core::color::reset) == 0, "clip: 行尾补颜色重置") ;

    const auto resized_color = clip("\033[32mab\033[0m", 2) ;
    console_status_text(plain(resized_color), "ab", "clip: ANSI 序列不占列宽") ;
    console_status_text(plain(clip("a\033[2Jb", 80)), "ab", "clip: 非颜色控制序列被丢弃") ;
}

/** 状态行渲染：按每行列宽裁剪并按行数容量丢弃末尾行。 */
void console_status_test_render()
{
    const std::vector<std::string> lines = { "first", "second", "third" } ;
    const auto two = console::detail::render_status_lines(lines, 80, 2) ;
    console_status_number(two.size(), 2, "render: 行数容量裁剪") ;
    console_status_text(console::detail::plain_text(two[0]), "first", "render: 保留首行") ;
    console_status_text(console::detail::plain_text(two[1]), "second", "render: 保留次行") ;

    console_status_number(console::detail::render_status_lines(lines, 80, 0).size(), 0,
        "render: 容量 0 返回空") ;
    console_status_number(console::detail::render_status_lines({}, 80, 4).size(), 0,
        "render: 空输入返回空") ;

    const auto clipped = console::detail::render_status_lines({ "abcdef" }, 2, 4) ;
    console_status_number(clipped.size(), 1, "render: 单行数量") ;
    console_status_text(console::detail::plain_text(clipped[0]), "ab", "render: 每行都按列裁剪") ;
}

/** 状态区行数上限：至少给控件留一行。 */
void console_status_test_capacity()
{
    console_status_number(console::detail::status_capacity(30), 29, "capacity: 普通窗口") ;
    console_status_number(console::detail::status_capacity(2), 1, "capacity: 两行窗口") ;
    console_status_number(console::detail::status_capacity(1), 0, "capacity: 单行窗口不显示状态区") ;
    console_status_number(console::detail::status_capacity(0), 0, "capacity: 零行窗口") ;
}

/** 状态区行数规划：未密封可增减，密封后保持高度并丢弃或补空行。 */
void console_status_test_plan()
{
    const auto unsealed_grow = console::detail::plan_status_rows(5, 3, 20, false) ;
    console_status_number(unsealed_grow.m_area_rows, 5, "plan: 未密封增加行数") ;
    console_status_number(unsealed_grow.m_visible_rows, 5, "plan: 未密封增加时全部可见") ;

    const auto unsealed_shrink = console::detail::plan_status_rows(1, 3, 20, false) ;
    console_status_number(unsealed_shrink.m_area_rows, 1, "plan: 未密封减少行数") ;
    console_status_number(unsealed_shrink.m_visible_rows, 1, "plan: 未密封减少时全部可见") ;

    const auto unsealed_capped = console::detail::plan_status_rows(30, 3, 10, false) ;
    console_status_number(unsealed_capped.m_area_rows, 10, "plan: 未密封受容量上限约束") ;
    console_status_number(unsealed_capped.m_visible_rows, 10, "plan: 上限内全部可见") ;

    const auto unsealed_empty = console::detail::plan_status_rows(0, 3, 20, false) ;
    console_status_number(unsealed_empty.m_area_rows, 0, "plan: 未密封清空收起区域") ;
    console_status_number(unsealed_empty.m_visible_rows, 0, "plan: 未密封清空无可见行") ;

    const auto sealed_grow = console::detail::plan_status_rows(5, 3, 20, true) ;
    console_status_number(sealed_grow.m_area_rows, 3, "plan: 密封后不增加区域") ;
    console_status_number(sealed_grow.m_visible_rows, 3, "plan: 密封后多余行被丢弃") ;

    const auto sealed_shrink = console::detail::plan_status_rows(1, 3, 20, true) ;
    console_status_number(sealed_shrink.m_area_rows, 3, "plan: 密封后不减少区域") ;
    console_status_number(sealed_shrink.m_visible_rows, 1, "plan: 密封后不足处补空行") ;

    const auto sealed_empty = console::detail::plan_status_rows(0, 3, 20, true) ;
    console_status_number(sealed_empty.m_area_rows, 3, "plan: 密封后清空仍保留区域") ;
    console_status_number(sealed_empty.m_visible_rows, 0, "plan: 密封后清空无可见行") ;

    const auto sealed_shrunk_window = console::detail::plan_status_rows(20, 8, 6, true) ;
    console_status_number(sealed_shrunk_window.m_area_rows, 6, "plan: 密封后窗口缩水受容量约束") ;
    console_status_number(sealed_shrunk_window.m_visible_rows, 6, "plan: 缩水后可见行不超过区域") ;

    const auto zero_capacity = console::detail::plan_status_rows(5, 3, 0, false) ;
    console_status_number(zero_capacity.m_area_rows, 0, "plan: 零容量无区域") ;
    console_status_number(zero_capacity.m_visible_rows, 0, "plan: 零容量无可见行") ;
}
} // namespace

/** console 状态区的纯逻辑自检；返回失败条数（0 = 全部通过）。 */
inline int test_console_status()
{
    g_console_status_failures = 0 ;
    g_console_status_checks = 0 ;
    console_status_test_clip() ;
    console_status_test_render() ;
    console_status_test_capacity() ;
    console_status_test_plan() ;

    if (g_console_status_failures == 0)
    {
        std::cout << "[INFO] console 状态区自检通过: " << g_console_status_checks << " 项断言全部通过\n" ;
    }
    else
    {
        std::cerr << "[ERROR] console 状态区自检失败: " << g_console_status_failures << " / "
                  << g_console_status_checks << " 项\n" ;
    }
    return g_console_status_failures ;
}
