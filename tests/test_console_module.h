#pragma once
// ============================================================================
// test_console_module.h — console_module 的纯逻辑自检（不接管控制台、不弹窗口）。
//
// 覆盖：core 文本转换；console::detail 的宽度/裁剪/去色/帧压缩；控件基类状态机；
// SingleSelect / MultiSelect / InputElement / SelectorElement 的按键、渲染帧、
// 边界异常与错误信息。由 app/main.cpp 在 --self-test 时调用，不自己写 main()。
// ============================================================================
#include "../console_module/console_control.h"
#include "../console_module/src/console_detail.h"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
int g_console_module_failures = 0 ;
int g_console_module_checks = 0 ;

/** 记一条断言结果；通过打 INFO，失败打 ERROR 并累计。 */
void console_test_report(bool passed, const std::string& what)
{
    ++g_console_module_checks ;
    if (passed)
    {
        std::cout << "[INFO] console: " << what << '\n' ;
        return ;
    }
    ++g_console_module_failures ;
    std::cerr << "[ERROR] console: " << what << '\n' ;
}

/** 布尔断言。 */
void console_test_check(bool passed, const std::string& what)
{
    console_test_report(passed, what) ;
}

/** 文本相等断言；失败时附带期望值与实际值。 */
void console_test_text(std::string_view actual, std::string_view expected, const std::string& what)
{
    const bool passed = actual == expected ;
    console_test_report(passed, what) ;
    if (!passed)
    {
        std::cerr << "        期望: [" << expected << "]\n" ;
        std::cerr << "        实际: [" << actual << "]\n" ;
    }
}

/** 尺寸相等断言；失败时附带期望值与实际值。 */
void console_test_number(std::size_t actual, std::size_t expected, const std::string& what)
{
    const bool passed = actual == expected ;
    console_test_report(passed, what) ;
    if (!passed)
    {
        std::cerr << "        期望: " << expected << "\n" ;
        std::cerr << "        实际: " << actual << "\n" ;
    }
}

/** 断言 callable 抛出 Exception 类型；抛错类型不对或没抛都记失败。 */
template <typename Exception, typename Callable>
void console_test_throws(Callable&& callable, const std::string& what)
{
    try
    {
        callable() ;
    }
    catch (const Exception&)
    {
        console_test_report(true, what) ;
        return ;
    }
    catch (const std::exception& error)
    {
        console_test_report(false, what + "（异常类型不对）") ;
        std::cerr << "        what(): " << error.what() << '\n' ;
        return ;
    }
    console_test_report(false, what + "（没有抛异常）") ;
}

/** 取渲染帧某一行的可见文本（去掉 ANSI 颜色）；越界返回空串。 */
std::string console_test_line(const console::ConsoleFrame& frame, std::size_t index)
{
    return index < frame.m_lines.size() ? console::detail::plain_text(frame.m_lines[index]) : std::string() ;
}

/** 渲染帧的可见文本里是否有一行包含 needle。 */
bool console_test_frame_contains(const console::ConsoleFrame& frame, std::string_view needle)
{
    for (const auto& line : frame.m_lines)
    {
        if (console::detail::plain_text(line).find(needle) != std::string::npos)
        {
            return true ;
        }
    }
    return false ;
}

/** 建 selector 测试用的临时目录和文件；失败返回空路径。 */
std::filesystem::path console_test_prepare_fixture()
{
    std::error_code error ;
    const auto root = std::filesystem::current_path() / "agent_build" / "test_console_module_fixture" ;
    std::filesystem::remove_all(root, error) ;
    std::filesystem::create_directories(root / "dir", error) ;
    if (error)
    {
        return {} ;
    }
    std::ofstream file(root / "file.txt", std::ios::binary) ;
    file << "fixture" ;
    file.close() ;
    if (!file)
    {
        return {} ;
    }
    return root ;
}

/** 删除 selector 测试夹具。 */
void console_test_clean_fixture(const std::filesystem::path& root)
{
    if (root.empty())
    {
        return ;
    }
    std::error_code error ;
    std::filesystem::remove_all(root, error) ;
}

/** core 文本转换与显示宽度（console_module 的底层依赖）。 */
void console_test_core_text()
{
    const auto code_points = core::utf8_to_utf32("a你😀") ;
    console_test_number(code_points.size(), 3, "core: UTF-8 解码按码点") ;
    console_test_check(!code_points.empty() && code_points[0] == U'a', "core: ASCII 码点") ;
    console_test_check(code_points.size() > 1 && code_points[1] == U'你', "core: 中文码点") ;
    console_test_check(code_points.size() > 2 && code_points[2] == U'\U0001F600', "core: 非 BMP 码点") ;
    console_test_text(core::utf32_to_utf8(code_points), "a你😀", "core: UTF-32 -> UTF-8 往返") ;
    console_test_text(core::wide_to_utf8(core::utf8_to_wide("你好")), "你好", "core: UTF-8 <-> UTF-16 往返") ;
    console_test_throws<std::invalid_argument>([]
    {
        (void) core::utf8_to_utf32(std::string(1, static_cast<char>(0xFF))) ;
    }, "core: 非法 UTF-8 抛 invalid_argument") ;
    console_test_throws<std::invalid_argument>([]
    {
        const std::u32string surrogates(1, static_cast<char32_t>(0xD800)) ;
        (void) core::utf32_to_utf8(surrogates) ;
    }, "core: 代理项码点抛 invalid_argument") ;

    console_test_number(core::console_cell_width(U'a'), 1, "core: ASCII 宽 1") ;
    console_test_number(core::console_cell_width(U'你'), 2, "core: 中文宽 2") ;
    console_test_number(core::console_cell_width(U'\U0001F600'), 2, "core: emoji 宽 2") ;
    console_test_number(core::console_cell_width(static_cast<char32_t>(0x0301)), 0, "core: 组合符宽 0") ;
    console_test_number(core::console_cell_width(static_cast<char32_t>(0x7F)), 0, "core: 控制字符宽 0") ;
}

/** keyboard 特殊键与颜色/符号常量。 */
void console_test_constants()
{
    console_test_number(console::detail::display_width(core::select_arrow), 1, "style: 游标箭头一列") ;
    console_test_text(core::select_space, " ", "style: 游标后一个空格") ;
    console_test_text(core::unselect_space, "  ", "style: 无游标两个空格") ;
    console_test_text(core::checked_box, "[x]", "style: 勾选框") ;
    console_test_text(core::unchecked_box, "[ ]", "style: 空勾选框") ;
    console_test_text(core::checkbox_space, " ", "style: 勾选框后空格") ;
    console_test_number(core::input_cursor_size, 20, "style: 输入光标尺寸") ;
    console_test_check(std::string(core::color::reset).compare(0, 2, "\033[") == 0, "style: 颜色是 ANSI 序列") ;
    console_test_check(core::color::title != core::color::selected, "style: 标题色与选中色不同") ;

    console_test_number(static_cast<std::size_t>(console::keyboard::enter), 13, "keyboard: Enter 码") ;
    console_test_number(static_cast<std::size_t>(console::keyboard::escape), 27, "keyboard: Esc 码") ;
    console_test_number(static_cast<std::size_t>(console::keyboard::backspace), 8, "keyboard: Backspace 码") ;
    console_test_number(static_cast<std::size_t>(console::keyboard::space), 32, "keyboard: Space 码") ;
    console_test_number(static_cast<std::size_t>(console::keyboard::interrupt), 3, "keyboard: Ctrl+C 码") ;

    const int specials[] = { console::keyboard::up, console::keyboard::down, console::keyboard::left,
        console::keyboard::right, console::keyboard::home, console::keyboard::end,
        console::keyboard::delete_key, console::keyboard::browse, console::keyboard::resize,
        console::keyboard::unavailable } ;
    bool above_unicode = true ;
    bool distinct = true ;
    for (std::size_t first = 0 ; first < std::size(specials) ; ++first)
    {
        if (specials[first] <= 0x10FFFF)
        {
            above_unicode = false ;
        }
        for (std::size_t second = first + 1 ; second < std::size(specials) ; ++second)
        {
            if (specials[first] == specials[second])
            {
                distinct = false ;
            }
        }
    }
    console_test_check(above_unicode, "keyboard: 特殊键都在 Unicode 范围外") ;
    console_test_check(distinct, "keyboard: 特殊键互不重复") ;
}

/** console::detail 的文本工具：去色、宽度、安全文本、裁剪、游标前缀。 */
void console_test_detail_tools()
{
    console_test_text(console::detail::plain_text("\033[31m红色\033[0m"), "红色", "detail: 去色") ;
    console_test_text(console::detail::plain_text("\033[1m\033[96mNX\033[0m"), "NX", "detail: 连续序列去色") ;
    console_test_number(console::detail::display_width("abc"), 3, "detail: ASCII 宽度") ;
    console_test_number(console::detail::display_width("你好"), 4, "detail: 中文宽度") ;
    console_test_number(console::detail::display_width("\033[32mab\033[0m"), 2, "detail: 算宽度时忽略颜色") ;
    console_test_number(console::detail::display_width(std::string("e") + "\xCC\x81"), 1, "detail: 组合符号零宽") ;

    console_test_text(console::detail::safe_text("正常文本"), "正常文本", "detail: 安全文本保留普通字符") ;
    console_test_text(console::detail::safe_text("a\tb\nc"), "a b c", "detail: 控制字符换成空格") ;
    console_test_text(console::detail::safe_text(std::string("a") + static_cast<char>(0x7F) + "b"), "a b",
        "detail: DEL 换成空格") ;
    console_test_text(console::detail::safe_text("中文"), "中文", "detail: 安全文本保留中文") ;

    console_test_text(console::detail::plain_text(console::detail::clip_text("abcdef", 3)), "abc",
        "detail: 按列裁剪 ASCII") ;
    console_test_text(console::detail::plain_text(console::detail::clip_text("你好世界", 3)), "你",
        "detail: 双宽字符不切半") ;
    console_test_text(console::detail::plain_text(console::detail::clip_text("abcdef", 0)), "",
        "detail: 零列裁剪为空") ;
    console_test_check(console::detail::clip_text("\033[31mabc", 2).find("\033[31m") != std::string::npos,
        "detail: 裁剪保留颜色前缀") ;
    console_test_check(console::detail::clip_text("abc", 3).find(core::color::reset) != std::string::npos,
        "detail: 裁剪结果重置颜色") ;

    console_test_text(console::detail::plain_text(console::detail::cursor_prefix(true, core::color::red)), "▶ ",
        "detail: 当前项游标前缀") ;
    console_test_text(console::detail::plain_text(console::detail::cursor_prefix(false, core::color::red)), "  ",
        "detail: 非当前项占位") ;
    console_test_number(console::detail::display_width(console::detail::cursor_prefix(true, core::color::red)), 2,
        "detail: 游标前缀占两列") ;
    console_test_check(console::detail::cursor_prefix(true, core::color::red).find(core::color::red) != std::string::npos,
        "detail: 游标带颜色") ;
}

/** console::detail::fit_frame 的窗口与光标换算。 */
void console_test_frame_fitting()
{
    console::ConsoleFrame frame ;
    for (int index = 0 ; index < 10 ; ++index)
    {
        frame.m_lines.push_back("L" + std::to_string(index)) ;
    }
    frame.m_focus_line = 5 ;
    frame.m_cursor = console::ConsoleCursorPosition{ 4, 3 } ;

    const auto fitted = console::detail::fit_frame(frame, 5) ;
    console_test_number(fitted.m_lines.size(), 5, "detail: 压缩到 5 行") ;
    console_test_text(fitted.m_lines.front(), "L0", "detail: 压缩保留首行") ;
    console_test_text(fitted.m_lines.back(), "L9", "detail: 压缩保留末行") ;
    console_test_number(fitted.m_focus_line, 2, "detail: 压缩后焦点行重算") ;
    console_test_check(fitted.m_cursor.has_value() && fitted.m_cursor->m_line == 1 &&
            fitted.m_cursor->m_column == 3, "detail: 压缩后光标行重算") ;

    frame.m_cursor = console::ConsoleCursorPosition{ 2, 0 } ;
    const auto dropped = console::detail::fit_frame(frame, 5) ;
    console_test_check(!dropped.m_cursor.has_value(), "detail: 被裁掉的光标不再显示") ;

    const auto unchanged = console::detail::fit_frame(frame, 10) ;
    console_test_number(unchanged.m_lines.size(), 10, "detail: 行数够时不压缩") ;
    console_test_number(unchanged.m_focus_line, 5, "detail: 未压缩保持焦点行") ;

    const auto three = console::detail::fit_frame(frame, 3) ;
    console_test_number(three.m_lines.size(), 3, "detail: 3 行窗口") ;
    console_test_text(console_test_line(three, 0), "L0", "detail: 3 行保留标题") ;
    console_test_text(console_test_line(three, 1), "L5", "detail: 3 行中间是焦点") ;
    console_test_text(console_test_line(three, 2), "L9", "detail: 3 行保留提示") ;
    console_test_number(three.m_focus_line, 1, "detail: 3 行焦点位置") ;

    const auto two = console::detail::fit_frame(frame, 2) ;
    console_test_number(two.m_lines.size(), 2, "detail: 2 行窗口") ;
    console_test_text(console_test_line(two, 0), "L4", "detail: 2 行窗口起点") ;
    console_test_text(console_test_line(two, 1), "L5", "detail: 2 行窗口末行") ;
    console_test_number(two.m_focus_line, 1, "detail: 2 行焦点在末行") ;

    const auto one = console::detail::fit_frame(frame, 1) ;
    console_test_number(one.m_lines.size(), 1, "detail: 1 行窗口") ;
    console_test_text(console_test_line(one, 0), "L5", "detail: 1 行只显示焦点") ;
    console_test_number(one.m_focus_line, 0, "detail: 1 行焦点位置") ;

    const auto zero_rows = console::detail::fit_frame(frame, 0) ;
    console_test_number(zero_rows.m_lines.size(), 1, "detail: 0 行按 1 行处理") ;

    const auto empty = console::detail::fit_frame(console::ConsoleFrame{}, 5) ;
    console_test_number(empty.m_lines.size(), 0, "detail: 空帧原样返回") ;
}

/** 控件基类：标题清洗、颜色、类型、状态机。 */
void console_test_element_base()
{
    console::SingleSelectElement element ;
    console_test_check(element.get_title().empty(), "base: 默认标题为空") ;
    console_test_text(element.get_title_color(), core::color::title, "base: 默认标题颜色") ;
    element.set_title("标题") ;
    console_test_text(element.get_title(), "标题", "base: 标题保留中文") ;
    element.set_title("a\tb\nc") ;
    console_test_text(element.get_title(), "a b c", "base: 标题控制字符换空格") ;
    element.set_title_color(core::color::red) ;
    console_test_text(element.get_title_color(), core::color::red, "base: 设置标题颜色") ;
    element.set_title_color(nullptr) ;
    console_test_text(element.get_title_color(), core::color::title, "base: 空颜色恢复默认") ;
    console_test_check(element.get_type() == console::ConsoleType::SingleSelect, "base: 单选类型") ;
    console_test_check(element.get_state() == console::PromptState::Editing, "base: 初始是编辑态") ;
    element.add_option("A") ;
    console_test_check(element.handle_key(console::keyboard::enter), "base: Enter 被消费") ;
    console_test_check(element.get_state() == console::PromptState::Submitted, "base: Enter 提交") ;
    console_test_check(!element.handle_key(console::keyboard::down), "base: 提交后不再消费按键") ;
    element.begin() ;
    console_test_check(element.get_state() == console::PromptState::Editing, "base: begin 回到编辑态") ;
    element.add_option("B") ;
    console_test_check(element.get_state() == console::PromptState::Editing, "base: 加选项回到编辑态") ;

    const auto frame = element.render(80) ;
    console_test_check(frame.m_focus_line < frame.m_lines.size(), "base: focus_line 不越界") ;
    console_test_text(console_test_line(frame, 0), "a b c", "base: 帧首行是标题") ;
}

/** 单选控件：导航、提交/取消、渲染、越界与选项维护。 */
void console_test_single_select()
{
    console::SingleSelectElement element ;
    element.set_title("T") ;
    element.add_option("甲") ;
    element.add_option("乙") ;
    element.add_option("丙") ;
    console_test_number(element.get_options().size(), 3, "single: 选项数量") ;
    console_test_text(element.find_option(1), "乙", "single: 按下标取选项") ;
    console_test_number(element.get_selected_index(), 0, "single: 默认选第一项") ;
    console_test_text(element.result_text(), "甲", "single: result_text 为当前项") ;

    console_test_check(element.handle_key(console::keyboard::down), "single: Down 被消费") ;
    console_test_number(element.get_selected_index(), 1, "single: Down 下移") ;
    console_test_check(element.handle_key(console::keyboard::down), "single: 再按 Down") ;
    console_test_number(element.get_selected_index(), 2, "single: 到末项") ;
    console_test_check(element.handle_key(console::keyboard::down), "single: 末项再按 Down 仍消费") ;
    console_test_number(element.get_selected_index(), 2, "single: 末项不越界") ;
    console_test_check(element.handle_key(console::keyboard::up), "single: Up 被消费") ;
    console_test_number(element.get_selected_index(), 1, "single: Up 上移") ;
    console_test_check(element.handle_key(console::keyboard::home), "single: Home 被消费") ;
    console_test_number(element.get_selected_index(), 0, "single: Home 到首项") ;
    console_test_check(element.handle_key(console::keyboard::end), "single: End 被消费") ;
    console_test_number(element.get_selected_index(), 2, "single: End 到末项") ;
    console_test_check(!element.handle_key('x'), "single: 普通字符不消费") ;
    element.set_selected_index(1) ;
    console_test_text(element.result_text(), "乙", "single: 设置下标后 result_text") ;

    const auto frame = element.render(80) ;
    console_test_number(frame.m_lines.size(), 5, "single: 帧 = 标题 + 3 项 + 提示") ;
    console_test_text(console_test_line(frame, 0), "T", "single: 帧首行是标题") ;
    console_test_text(console_test_line(frame, 1), "  甲", "single: 非当前项无游标") ;
    console_test_text(console_test_line(frame, 2), "▶ 乙", "single: 当前项带游标") ;
    console_test_text(console_test_line(frame, 3), "  丙", "single: 末项无游标") ;
    console_test_check(console_test_frame_contains(frame, "Enter"), "single: 提示含 Enter") ;
    console_test_number(frame.m_focus_line, 2, "single: focus_line 指向当前项") ;
    console_test_check(frame.m_lines[2].find(core::color::selected) != std::string::npos,
        "single: 当前项用选中色") ;
    console_test_check(frame.m_lines[1].find(core::color::unselected) != std::string::npos,
        "single: 非当前项用未选中色") ;
    element.set_selected_color(core::color::red) ;
    console_test_check(element.render(80).m_lines[2].find(core::color::red) != std::string::npos,
        "single: 可覆盖选中色") ;
    element.set_selected_color(nullptr) ;
    console_test_check(element.render(80).m_lines[2].find(core::color::selected) != std::string::npos,
        "single: 空颜色恢复选中色") ;
    element.set_unselected_color(core::color::green) ;
    console_test_check(element.render(80).m_lines[1].find(core::color::green) != std::string::npos,
        "single: 可覆盖未选中色") ;
    element.set_unselected_color(nullptr) ;
    console_test_check(element.render(80).m_lines[1].find(core::color::unselected) != std::string::npos,
        "single: 空颜色恢复未选中色") ;

    element.handle_key(console::keyboard::enter) ;
    console_test_check(element.get_state() == console::PromptState::Submitted, "single: Enter 提交") ;
    console_test_check(!element.handle_key(console::keyboard::down), "single: 提交后方向键不消费") ;
    element.reset() ;
    console_test_number(element.get_selected_index(), 0, "single: reset 回到首项") ;
    console_test_check(element.get_state() == console::PromptState::Editing, "single: reset 回到编辑态") ;

    console::SingleSelectElement escaping ;
    escaping.add_option("A") ;
    console_test_check(escaping.handle_key(console::keyboard::escape), "single: Esc 被消费") ;
    console_test_check(escaping.get_state() == console::PromptState::Cancelled, "single: Esc 取消") ;
    console::SingleSelectElement interrupting ;
    interrupting.add_option("A") ;
    interrupting.handle_key(console::keyboard::interrupt) ;
    console_test_check(interrupting.get_state() == console::PromptState::Interrupted, "single: Ctrl+C 中断") ;
    console::SingleSelectElement unavailable ;
    unavailable.add_option("A") ;
    unavailable.handle_key(console::keyboard::unavailable) ;
    console_test_check(unavailable.get_state() == console::PromptState::Unavailable, "single: 输入不可用") ;

    console::SingleSelectElement empty ;
    empty.set_title("E") ;
    console_test_check(empty.get_selected_index() == std::string::npos, "single: 空菜单无当前项") ;
    console_test_text(empty.result_text(), "", "single: 空菜单摘要为空") ;
    console_test_check(!empty.handle_key(console::keyboard::down), "single: 空菜单不消费方向键") ;
    console_test_check(!empty.handle_key(console::keyboard::enter), "single: 空菜单 Enter 不提交") ;
    console_test_check(empty.get_state() == console::PromptState::Editing, "single: 空菜单仍是编辑态") ;
    const auto empty_frame = empty.render(80) ;
    console_test_number(empty_frame.m_lines.size(), 3, "single: 空菜单帧 = 标题 + 提示 + 提示") ;
    console_test_check(console_test_frame_contains(empty_frame, "暂无选项"), "single: 空菜单给空提示") ;
    console_test_number(empty_frame.m_focus_line, 1, "single: 空菜单 focus_line") ;

    console::SingleSelectElement guarded ;
    console_test_throws<std::out_of_range>([&guarded]
    {
        (void) guarded.find_option(0) ;
    }, "single: 空菜单取选项抛 out_of_range") ;
    guarded.add_option("A") ;
    console_test_throws<std::out_of_range>([&guarded]
    {
        guarded.set_selected_index(3) ;
    }, "single: 越界设置下标抛 out_of_range") ;
    console_test_throws<std::out_of_range>([&guarded]
    {
        guarded.remove_option(2) ;
    }, "single: 越界删除抛 out_of_range") ;
    console_test_throws<std::out_of_range>([&guarded]
    {
        guarded.swap_options(0, 2) ;
    }, "single: 越界交换抛 out_of_range") ;
    console_test_number(guarded.get_options().size(), 1, "single: 越界操作不破坏选项") ;

    console::SingleSelectElement removing ;
    removing.add_option("A") ;
    removing.add_option("B") ;
    removing.add_option("C") ;
    removing.set_selected_index(2) ;
    removing.remove_option(0) ;
    console_test_number(removing.get_selected_index(), 1, "single: 删除前面的项后游标跟随") ;
    console_test_text(removing.find_option(1), "C", "single: 删除后选项重排") ;
    removing.remove_option(1) ;
    console_test_number(removing.get_selected_index(), 0, "single: 删掉当前项后游标收敛") ;
    removing.remove_option(0) ;
    console_test_check(removing.get_selected_index() == std::string::npos, "single: 删空后无当前项") ;

    console::SingleSelectElement swapping ;
    swapping.add_option("A") ;
    swapping.add_option("B") ;
    swapping.add_option("C") ;
    swapping.set_selected_index(0) ;
    swapping.swap_options(0, 2) ;
    console_test_text(swapping.find_option(0), "C", "single: 交换后顺序变化") ;
    console_test_number(swapping.get_selected_index(), 2, "single: 交换后游标跟着原选项") ;
    console_test_text(swapping.result_text(), "A", "single: 交换后当前项不变") ;
    swapping.add_option("x\ty") ;
    console_test_text(swapping.find_option(3), "x y", "single: 选项里的控制字符换空格") ;
    swapping.clear_options() ;
    console_test_number(swapping.get_options().size(), 0, "single: clear 清空选项") ;
    console_test_check(swapping.get_selected_index() == std::string::npos, "single: clear 后无当前项") ;
}

/** 多选控件：勾选、渲染、提交、选项/勾选状态同步。 */
void console_test_multi_select()
{
    console::MultiSelectElement element ;
    element.set_title("多选") ;
    element.add_option("A") ;
    element.add_option("B") ;
    element.add_option("C") ;
    console_test_check(element.get_type() == console::ConsoleType::MultiSelect, "multi: 类型") ;
    console_test_check(element.get_selected_indices().empty(), "multi: 默认不勾选") ;
    console_test_text(element.result_text(), "（未选择）", "multi: 空选择摘要") ;
    console_test_check(element.handle_key(console::keyboard::space), "multi: 空格被消费") ;
    console_test_number(element.get_selected_indices().size(), 1, "multi: 空格勾选一项") ;
    console_test_check(element.is_checked(0), "multi: 第一项已勾选") ;
    element.handle_key(console::keyboard::down) ;
    element.handle_key(console::keyboard::space) ;
    console_test_number(element.get_selected_indices().size(), 2, "multi: 勾选两项") ;
    console_test_text(element.result_text(), "A、B", "multi: 摘要用顿号连接") ;
    element.handle_key(console::keyboard::space) ;
    console_test_number(element.get_selected_indices().size(), 1, "multi: 再按空格取消勾选") ;
    element.set_checked(2, true) ;
    console_test_check(element.is_checked(2), "multi: set_checked 置位") ;
    element.set_checked(2, false) ;
    console_test_check(!element.is_checked(2), "multi: set_checked 清零") ;
    element.set_checked(2, true) ;
    console_test_number(element.get_selected_indices().size(), 2, "multi: 勾选两项(0,2)") ;
    console_test_text(element.result_text(), "A、C", "multi: 摘要按显示顺序") ;

    element.set_selected_index(0) ;
    element.set_checked(0, true) ;
    element.set_checked(1, false) ;
    const auto frame = element.render(80) ;
    console_test_number(frame.m_lines.size(), 5, "multi: 帧 = 标题 + 3 项 + 提示") ;
    console_test_text(console_test_line(frame, 1), "▶ [x] A", "multi: 当前项带游标与勾选框") ;
    console_test_text(console_test_line(frame, 2), "  [ ] B", "multi: 未勾选显示空框") ;
    console_test_text(console_test_line(frame, 3), "  [x] C", "multi: 勾选项显示 x") ;
    console_test_check(console_test_frame_contains(frame, "空格"), "multi: 提示含空格键说明") ;
    console_test_number(frame.m_focus_line, 1, "multi: focus_line 指向当前项") ;

    console::MultiSelectElement unchecked ;
    unchecked.add_option("A") ;
    console_test_check(unchecked.handle_key(console::keyboard::enter), "multi: 无勾选也能 Enter") ;
    console_test_check(unchecked.get_state() == console::PromptState::Submitted, "multi: 无勾选提交") ;
    console_test_text(unchecked.result_text(), "（未选择）", "multi: 无勾选摘要") ;
    console_test_check(!unchecked.handle_key(console::keyboard::space), "multi: 提交后空格不消费") ;

    console::MultiSelectElement empty ;
    console_test_check(!empty.handle_key(console::keyboard::space), "multi: 空菜单空格不消费") ;
    console_test_check(!empty.handle_key(console::keyboard::enter), "multi: 空菜单 Enter 不提交") ;

    console::MultiSelectElement removing ;
    removing.add_option("A") ;
    removing.add_option("B") ;
    removing.add_option("C") ;
    removing.set_checked(0, true) ;
    removing.set_checked(2, true) ;
    removing.remove_option(0) ;
    console_test_number(removing.get_options().size(), 2, "multi: 删除选项") ;
    console_test_text(removing.find_option(0), "B", "multi: 删除后重排") ;
    console_test_check(!removing.is_checked(0), "multi: 删除后勾选状态同步") ;
    console_test_check(removing.is_checked(1), "multi: 删除后保留原勾选") ;
    console_test_number(removing.get_selected_indices().size(), 1, "multi: 删除后勾选数量") ;

    console::MultiSelectElement swapping ;
    swapping.add_option("A") ;
    swapping.add_option("B") ;
    swapping.add_option("C") ;
    swapping.set_checked(0, true) ;
    swapping.swap_options(0, 2) ;
    console_test_text(swapping.find_option(0), "C", "multi: 交换后顺序变化") ;
    console_test_check(!swapping.is_checked(0), "multi: 交换后勾选跟随选项(0)") ;
    console_test_check(swapping.is_checked(2), "multi: 交换后勾选跟随选项(2)") ;

    console::MultiSelectElement resetting ;
    resetting.add_option("A") ;
    resetting.add_option("B") ;
    resetting.set_checked(0, true) ;
    resetting.handle_key(console::keyboard::down) ;
    resetting.reset() ;
    console_test_check(resetting.get_selected_indices().empty(), "multi: reset 清空勾选") ;
    console_test_number(resetting.get_selected_index(), 0, "multi: reset 回到首项") ;
    console_test_check(resetting.get_state() == console::PromptState::Editing, "multi: reset 回到编辑态") ;

    console_test_throws<std::out_of_range>([&resetting]
    {
        resetting.set_checked(9, true) ;
    }, "multi: 越界勾选抛 out_of_range") ;
    console_test_throws<std::out_of_range>([&resetting]
    {
        (void) resetting.is_checked(9) ;
    }, "multi: 越界读取勾选抛 out_of_range") ;

    resetting.clear_options() ;
    console_test_number(resetting.get_options().size(), 0, "multi: clear 清空选项") ;
    console_test_check(resetting.get_selected_indices().empty(), "multi: clear 清空勾选") ;
}

/** 输入控件：编辑、游标、校验、错误信息、渲染与横向滚动。 */
void console_test_input()
{
    console::InputElement element ;
    console_test_check(element.get_type() == console::ConsoleType::Input, "input: 类型") ;
    console_test_text(element.get_value(), "", "input: 默认空文本") ;
    console_test_number(element.get_cursor_index(), 0, "input: 默认游标在 0") ;
    console_test_check(element.get_error().empty(), "input: 默认无错误") ;
    element.handle_key('a') ;
    element.handle_key('b') ;
    element.handle_key('c') ;
    console_test_text(element.get_value(), "abc", "input: 输入 ASCII") ;
    console_test_number(element.get_cursor_index(), 3, "input: 输入后游标在末尾") ;
    element.handle_key(0x4F60) ;
    console_test_text(element.get_value(), "abc你", "input: 输入中文") ;
    console_test_number(element.get_cursor_index(), 4, "input: 游标按码点计数") ;
    element.handle_key(console::keyboard::left) ;
    console_test_number(element.get_cursor_index(), 3, "input: 左移一格") ;
    element.handle_key('X') ;
    console_test_text(element.get_value(), "abcX你", "input: 中间插入") ;
    console_test_number(element.get_cursor_index(), 4, "input: 插入后游标右移") ;
    element.handle_key(console::keyboard::home) ;
    console_test_number(element.get_cursor_index(), 0, "input: Home 到开头") ;
    element.handle_key('Z') ;
    console_test_text(element.get_value(), "ZabcX你", "input: 开头插入") ;
    element.handle_key(console::keyboard::end) ;
    console_test_number(element.get_cursor_index(), 6, "input: End 到末尾") ;
    console_test_number(element.get_cursor_index(), 6, "input: End 后再右移不越界") ;
    element.handle_key(console::keyboard::right) ;
    console_test_number(element.get_cursor_index(), 6, "input: 右移不越界") ;
    element.handle_key(console::keyboard::backspace) ;
    console_test_text(element.get_value(), "ZabcX", "input: Backspace 删除前一个码点") ;
    console_test_number(element.get_cursor_index(), 5, "input: Backspace 后游标左移") ;
    element.handle_key(console::keyboard::delete_key) ;
    console_test_text(element.get_value(), "ZabcX", "input: Delete 在末尾无效") ;
    element.handle_key(console::keyboard::home) ;
    element.handle_key(console::keyboard::backspace) ;
    console_test_text(element.get_value(), "ZabcX", "input: Backspace 在开头无效") ;
    element.handle_key(console::keyboard::delete_key) ;
    console_test_text(element.get_value(), "abcX", "input: Delete 删除光标处") ;
    console_test_number(element.get_cursor_index(), 0, "input: Delete 不移动游标") ;
    console_test_check(element.handle_key(console::keyboard::enter), "input: Enter 被消费") ;
    console_test_check(element.get_state() == console::PromptState::Submitted, "input: Enter 提交") ;
    console_test_text(element.result_text(), "abcX", "input: result_text 即文本") ;
    console_test_check(!element.handle_key('a'), "input: 提交后不再接受输入") ;

    console::InputElement rejecting ;
    const int rejected[] = { 10, 9, console::keyboard::browse, 0x7F, 0x80, 0xD800, 0x110000 } ;
    for (const int key : rejected)
    {
        console_test_check(!rejecting.handle_key(key), "input: 拒绝控制/非法码点 " + std::to_string(key)) ;
    }
    console_test_text(rejecting.get_value(), "", "input: 非法输入不改变文本") ;

    console::InputElement required ;
    required.set_allow_empty(false) ;
    required.handle_key(console::keyboard::enter) ;
    console_test_text(required.get_error(), "内容不能为空", "input: 不允许空时拒绝提交") ;
    console_test_check(required.get_state() == console::PromptState::Editing, "input: 空值提交后仍在编辑") ;
    required.handle_key('a') ;
    console_test_check(required.get_error().empty(), "input: 继续输入清除错误") ;
    required.handle_key(console::keyboard::enter) ;
    console_test_check(required.get_state() == console::PromptState::Submitted, "input: 非空后可以提交") ;

    console::InputElement validated ;
    validated.set_validator([](const std::string& text) -> std::optional<std::string>
    {
        if (text.size() < 2)
        {
            return std::string("至少两个字符") ;
        }
        return std::nullopt ;
    }) ;
    validated.handle_key('a') ;
    validated.handle_key(console::keyboard::enter) ;
    console_test_text(validated.get_error(), "至少两个字符", "input: 校验失败写错误") ;
    console_test_check(validated.get_state() == console::PromptState::Editing, "input: 校验失败不提交") ;
    validated.handle_key('b') ;
    console_test_check(validated.get_error().empty(), "input: 编辑清除校验错误") ;
    validated.handle_key(console::keyboard::enter) ;
    console_test_check(validated.get_state() == console::PromptState::Submitted, "input: 校验通过提交") ;

    console::InputElement value_element ;
    value_element.set_value("你好") ;
    console_test_text(value_element.get_value(), "你好", "input: set_value 接受中文") ;
    console_test_number(value_element.get_cursor_index(), 2, "input: set_value 后游标在末尾") ;
    console_test_throws<std::invalid_argument>([&value_element]
    {
        value_element.set_value("a\nb") ;
    }, "input: set_value 拒绝换行") ;
    console_test_throws<std::invalid_argument>([&value_element]
    {
        value_element.set_value(std::string("a") + static_cast<char>(0x7F)) ;
    }, "input: set_value 拒绝 DEL") ;
    console_test_throws<std::invalid_argument>([&value_element]
    {
        value_element.set_value(std::string(1, static_cast<char>(0xFF))) ;
    }, "input: set_value 拒绝非法 UTF-8") ;
    console_test_text(value_element.get_value(), "你好", "input: 失败的 set_value 不改文本") ;

    console::InputElement resetting ;
    resetting.set_default_value("初始") ;
    resetting.handle_key('X') ;
    console_test_text(resetting.get_value(), "初始X", "input: 在默认值后继续输入") ;
    resetting.reset() ;
    console_test_text(resetting.get_value(), "初始", "input: reset 恢复默认值") ;
    console_test_number(resetting.get_cursor_index(), 2, "input: reset 后游标在末尾") ;
    console_test_check(resetting.get_state() == console::PromptState::Editing, "input: reset 回到编辑态") ;

    console::InputElement renderer ;
    renderer.set_title("输入") ;
    renderer.set_value("abc") ;
    auto frame = renderer.render(80) ;
    console_test_number(frame.m_lines.size(), 3, "input: 帧 = 标题 + 输入行 + 提示") ;
    console_test_text(console_test_line(frame, 0), "输入", "input: 帧首行是标题") ;
    console_test_text(console_test_line(frame, 1), "▶ abc", "input: 输入行带游标前缀") ;
    console_test_check(console_test_frame_contains(frame, "Enter"), "input: 提示含 Enter") ;
    console_test_number(frame.m_focus_line, 1, "input: focus_line 指向输入行") ;
    console_test_check(frame.m_cursor.has_value() && frame.m_cursor->m_line == 1 &&
            frame.m_cursor->m_column == 5, "input: 光标列 = 前缀 + 文本宽度") ;

    renderer.set_value("你好") ;
    frame = renderer.render(80) ;
    console_test_check(frame.m_cursor.has_value() && frame.m_cursor->m_column == 6,
        "input: 中文按两列算光标") ;

    renderer.set_value("abcdef") ;
    frame = renderer.render(4) ;
    console_test_text(console_test_line(frame, 1), "▶ f", "input: 窄宽度下横向滚动") ;
    console_test_check(frame.m_cursor.has_value() && frame.m_cursor->m_column == 3,
        "input: 滚动后光标列") ;
    frame = renderer.render(1) ;
    console_test_text(console_test_line(frame, 1), "", "input: 极窄宽度不显示前缀") ;
    console_test_check(frame.m_cursor.has_value() && frame.m_cursor->m_column == 0,
        "input: 极窄宽度光标贴左") ;

    console::InputElement erroring ;
    erroring.set_title("E") ;
    erroring.set_allow_empty(false) ;
    erroring.handle_key(console::keyboard::enter) ;
    const auto error_frame = erroring.render(80) ;
    console_test_number(error_frame.m_lines.size(), 4, "input: 有错误时多一行") ;
    console_test_text(console_test_line(error_frame, 3), "内容不能为空", "input: 错误行内容") ;
    erroring.handle_key('a') ;
    console_test_number(erroring.render(80).m_lines.size(), 3, "input: 继续输入后错误行消失") ;
}

/** 选择器控件：F2 浏览、选择器结果/取消/失败、路径校验与引号处理。 */
void console_test_selector()
{
    const auto fixture = console_test_prepare_fixture() ;
    console_test_check(!fixture.empty(), "selector: 创建测试夹具") ;
    if (fixture.empty())
    {
        return ;
    }
    const std::filesystem::path fixture_dir = fixture / "dir" ;
    const std::filesystem::path fixture_file = fixture / "file.txt" ;
    const std::string fixture_dir_text = core::wide_to_utf8(fixture_dir.wstring()) ;
    const std::string fixture_file_text = core::wide_to_utf8(fixture_file.wstring()) ;

    console::SelectorElement element ;
    console_test_check(element.get_type() == console::ConsoleType::Selector, "selector: 类型") ;
    console_test_text(element.get_value(), "", "selector: 默认空路径") ;
    console_test_check(element.get_path().empty(), "selector: 空输入取路径为空") ;
    console_test_check(element.handle_key(console::keyboard::enter), "selector: Enter 被消费") ;
    console_test_text(element.get_error(), "路径不能为空", "selector: 空路径拒绝提交") ;
    console_test_check(element.get_state() == console::PromptState::Editing, "selector: 空路径仍是编辑态") ;

    console::FilePickerOptions captured ;
    console::FilePickerResult picker_result ;
    bool picker_called = false ;
    picker_result.m_status = console::FilePickerStatus::Selected ;
    picker_result.m_path = fixture_dir ;
    element.set_picker([&picker_called, &captured, &picker_result](const console::FilePickerOptions& options)
    {
        picker_called = true ;
        captured = options ;
        return picker_result ;
    }) ;
    element.set_title("选择目录") ;
    element.set_mode(console::FilePickerMode::Directory) ;
    element.set_filters({ console::FilePickerFilter{ "文本文件", "*.txt" } }) ;
    element.set_initial_directory(fixture_dir) ;
    console_test_check(element.get_error().empty(), "selector: set_mode 清错误") ;
    console_test_check(element.handle_key(console::keyboard::browse), "selector: F2 被消费") ;
    console_test_check(picker_called, "selector: F2 调用选择器") ;
    console_test_text(captured.m_title, "选择目录", "selector: 选择器拿到标题") ;
    console_test_check(captured.m_mode == console::FilePickerMode::Directory, "selector: 选择器拿到目录模式") ;
    console_test_number(captured.m_filters.size(), 1, "selector: 过滤器透传") ;
    console_test_text(captured.m_filters[0].m_pattern, "*.txt", "selector: 过滤模式透传") ;
    console_test_check(captured.m_initial_directory == fixture_dir, "selector: 空输入时用预设起始目录") ;
    console_test_check(element.get_path() == fixture_dir, "selector: 选中结果写回路径") ;
    console_test_text(element.get_value(), fixture_dir_text, "selector: 选中结果写回文本") ;
    console_test_check(element.get_error().empty(), "selector: 选中后无错误") ;

    captured = {} ;
    element.handle_key(console::keyboard::browse) ;
    console_test_check(captured.m_initial_directory == fixture_dir, "selector: 当前值是目录时作为起始目录") ;

    // 有路径缓存时优先用缓存（上一次确认过的目录）。
    element.set_value(fixture_file_text) ;
    captured = {} ;
    element.handle_key(console::keyboard::browse) ;
    console_test_check(captured.m_initial_directory == fixture_dir, "selector: 有缓存时优先用缓存") ;

    // 清掉缓存后再验当前值逻辑（上一次 F2 把值写回成了目录，这里恢复成文件）。
    console::set_recent_selector_directory(std::filesystem::path()) ;
    element.set_value(fixture_file_text) ;
    captured = {} ;
    element.handle_key(console::keyboard::browse) ;
    console_test_check(captured.m_initial_directory == fixture, "selector: 当前值是文件时用父目录") ;

    picker_result.m_status = console::FilePickerStatus::Cancelled ;
    element.set_value("旧值") ;
    element.handle_key(console::keyboard::browse) ;
    console_test_text(element.get_value(), "旧值", "selector: 取消选择保留原输入") ;

    picker_result.m_status = console::FilePickerStatus::Failed ;
    picker_result.m_error = "拒绝访问" ;
    element.set_value("旧值") ;
    element.handle_key(console::keyboard::browse) ;
    console_test_text(element.get_error(), "拒绝访问", "selector: 失败消息透传") ;
    picker_result.m_error.clear() ;
    element.set_value("旧值") ;
    element.handle_key(console::keyboard::browse) ;
    console_test_text(element.get_error(), "无法打开选择器", "selector: 失败无消息时用默认文案") ;

    picker_result.m_status = console::FilePickerStatus::Selected ;
    picker_result.m_path.clear() ;
    element.set_value("旧值") ;
    element.handle_key(console::keyboard::browse) ;
    console_test_text(element.get_error(), "选择器没有返回有效路径", "selector: 空路径结果被拒绝") ;

    element.set_picker([](const console::FilePickerOptions&) -> console::FilePickerResult
    {
        throw std::runtime_error("模拟异常") ;
    }) ;
    element.set_value("旧值") ;
    element.handle_key(console::keyboard::browse) ;
    console_test_check(element.get_error().find("选择路径失败") != std::string::npos,
        "selector: 选择器异常被兜住") ;
    console_test_check(element.get_error().find("模拟异常") != std::string::npos,
        "selector: 异常消息透传") ;
    console_test_text(element.get_value(), "旧值", "selector: 异常时保留原输入") ;
    console_test_check(element.get_state() == console::PromptState::Editing, "selector: 异常后仍是编辑态") ;

    console::SelectorElement file_mode ;
    file_mode.set_value(fixture_dir_text) ;
    file_mode.handle_key(console::keyboard::enter) ;
    console_test_text(file_mode.get_error(), "请选择已存在的文件", "selector: 文件模式拒绝目录") ;
    file_mode.set_value(fixture_dir_text + "\\不存在") ;
    file_mode.handle_key(console::keyboard::enter) ;
    console_test_text(file_mode.get_error(), "请选择已存在的文件", "selector: 文件模式拒绝不存在路径") ;
    file_mode.set_value(fixture_file_text) ;
    file_mode.handle_key(console::keyboard::enter) ;
    console_test_check(file_mode.get_state() == console::PromptState::Submitted, "selector: 文件模式接受文件") ;

    console::SelectorElement directory_mode(console::FilePickerMode::Directory) ;
    directory_mode.set_value(fixture_file_text) ;
    directory_mode.handle_key(console::keyboard::enter) ;
    console_test_text(directory_mode.get_error(), "请选择已存在的文件夹", "selector: 目录模式拒绝文件") ;
    directory_mode.set_value(fixture_dir_text) ;
    directory_mode.handle_key(console::keyboard::enter) ;
    console_test_check(directory_mode.get_state() == console::PromptState::Submitted,
        "selector: 目录模式接受目录") ;

    console::SelectorElement quoted(console::FilePickerMode::Directory) ;
    quoted.set_value("\"" + fixture_dir_text + "\"") ;
    console_test_check(quoted.get_path() == fixture_dir, "selector: 去掉两端引号取路径") ;
    quoted.handle_key(console::keyboard::enter) ;
    console_test_check(quoted.get_state() == console::PromptState::Submitted, "selector: 带引号路径可提交") ;

    console::SelectorElement submitted ;
    submitted.set_value(fixture_file_text) ;
    submitted.handle_key(console::keyboard::enter) ;
    console_test_check(!submitted.handle_key(console::keyboard::browse), "selector: 提交后 F2 不响应") ;

    const auto frame = quoted.render(80) ;
    console_test_check(console_test_frame_contains(frame, "F2"), "selector: 提示含 F2") ;

    console_test_clean_fixture(fixture) ;
}

} // namespace

/** console_module 的自动自检；返回失败条数（0 = 全部通过）。 */
inline int test_console_module()
{
    g_console_module_failures = 0 ;
    g_console_module_checks = 0 ;
    console_test_core_text() ;
    console_test_constants() ;
    console_test_detail_tools() ;
    console_test_frame_fitting() ;
    console_test_element_base() ;
    console_test_single_select() ;
    console_test_multi_select() ;
    console_test_input() ;
    console_test_selector() ;

    if (g_console_module_failures == 0)
    {
        std::cout << "[INFO] console_module 自检通过: " << g_console_module_checks
                  << " 项断言全部通过\n" ;
    }
    else
    {
        std::cerr << "[ERROR] console_module 自检失败: " << g_console_module_failures << " / "
                  << g_console_module_checks << " 项\n" ;
    }
    return g_console_module_failures ;
}
