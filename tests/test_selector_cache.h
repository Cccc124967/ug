#pragma once
// ============================================================================
// test_selector_cache.h — F2 选择器路径缓存的自动测试。
//
// 用假 picker 驱动 SelectorElement：第一次选中 A 后，第二次 F2 的初始目录
// 应该是 A；手工输入并 Enter 通过的目录也进入缓存。夹具落在 agent_build/。
// ============================================================================
#include "../console_module/console_control.h"
#include "../core_module/dev_core.h"
#include "../core_module/file_io.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace test_selector_cache_detail
{
/** 本组测试的失败次数。 */
inline int& failures()
{
    static int count = 0 ;
    return count ;
}

/** 断言；失败时打印并计数。 */
inline void check(bool condition, const std::string& label)
{
    if (!condition)
    {
        ++failures() ;
        std::cerr << "[FAIL] test_selector_cache: " << label << '\n' ;
    }
}

/** 两个路径是否指向同一目录（不要求文本完全一致）。 */
inline bool same_directory(const std::filesystem::path& left, const std::filesystem::path& right)
{
    std::error_code error ;
    return !left.empty() && !right.empty() &&
        std::filesystem::equivalent(left, right, error) && !error ;
}

/** 夹具目录；沿用仓库的 agent_build 临时区。 */
inline std::filesystem::path fixture_root()
{
    return std::filesystem::current_path() / "agent_build" / "tests" / "selector_cache_fixture" ;
}

/** 构造一个立即返回指定路径的假 picker。 */
inline console::FilePicker selecting_picker(std::filesystem::path selected)
{
    return [selected = std::move(selected)](const console::FilePickerOptions&)
    {
        console::FilePickerResult result ;
        result.m_status = console::FilePickerStatus::Selected ;
        result.m_path = selected ;
        return result ;
    } ;
}

/** 按下 F2 并返回假 picker 收到的初始目录。 */
inline std::filesystem::path browse_once(console::SelectorElement& element)
{
    std::filesystem::path initial ;
    element.set_picker([&initial](const console::FilePickerOptions& options)
    {
        initial = options.m_initial_directory ;
        console::FilePickerResult result ;
        result.m_status = console::FilePickerStatus::Cancelled ;
        return result ;
    }) ;
    element.begin() ;
    element.handle_key(console::keyboard::browse) ;
    return initial ;
}
} // namespace test_selector_cache_detail

/** F2 路径缓存自检；全部通过返回 0。 */
inline int test_selector_cache()
{
    using test_selector_cache_detail::check ;
    using test_selector_cache_detail::failures ;
    using test_selector_cache_detail::same_directory ;
    failures() = 0 ;
    const std::filesystem::path fixture = test_selector_cache_detail::fixture_root() ;
    const std::filesystem::path first_directory = fixture / "first" ;
    const std::filesystem::path typed_directory = fixture / "typed" ;
    std::error_code filesystem_error ;
    std::filesystem::remove_all(fixture, filesystem_error) ;
    std::string io_error ;
    check(core::ensure_directory(first_directory, io_error) &&
        core::ensure_directory(typed_directory, io_error), "创建选择器夹具目录") ;

    console::set_recent_selector_directory(std::filesystem::path()) ;
    check(console::recent_selector_directory() != first_directory, "初始缓存为空") ;

    // 第一次按 F2 选中 first 目录：选中即缓存。
    console::SelectorElement first(console::FilePickerMode::Directory) ;
    first.set_title("first") ;
    first.set_picker(test_selector_cache_detail::selecting_picker(first_directory)) ;
    first.begin() ;
    first.handle_key(console::keyboard::browse) ;
    check(same_directory(first.get_path(), first_directory), "第一次 F2 选中目录") ;
    check(same_directory(console::recent_selector_directory(), first_directory),
        "选中后进入全局路径缓存") ;

    // 第二次 F2：初始目录应该是刚才选中的 first。
    console::SelectorElement second(console::FilePickerMode::Directory) ;
    const std::filesystem::path second_initial = test_selector_cache_detail::browse_once(second) ;
    check(same_directory(second_initial, first_directory), "第二次 F2 从上次选中的目录开始") ;

    // 手工输入路径并 Enter：通过校验后也进入缓存。
    console::SelectorElement typed(console::FilePickerMode::Directory) ;
    typed.set_value(core::wide_to_utf8(typed_directory.wstring())) ;
    typed.begin() ;
    typed.handle_key(console::keyboard::enter) ;
    check(typed.get_state() == console::PromptState::Submitted, "手工输入路径可以提交") ;
    check(same_directory(console::recent_selector_directory(), typed_directory),
        "手工确认的目录也进入缓存") ;

    console::SelectorElement third(console::FilePickerMode::Directory) ;
    const std::filesystem::path third_initial = test_selector_cache_detail::browse_once(third) ;
    check(same_directory(third_initial, typed_directory), "第三次 F2 从手工确认的目录开始") ;

    std::filesystem::remove_all(fixture, filesystem_error) ;
    const int failed = failures() ;
    if (failed == 0)
    {
        std::cout << "[ OK ] test_selector_cache\n" ;
        return 0 ;
    }
    std::cerr << "[FAIL] test_selector_cache: " << failed << " 项失败\n" ;
    return 1 ;
}
