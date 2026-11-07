#pragma once
// ============================================================================
// test_path_env.h — PATH 文本编辑纯函数自检（不读不写注册表）。
//
// 由 app/main.cpp 的 main() 调 test_path_env()，不自己写 main()。
// ============================================================================
#include "../core_module/path_env.h"
#include <filesystem>
#include <iostream>
#include <string>

namespace test_path_env_detail
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
        std::cerr << "[FAIL] test_path_env: " << label << '\n' ;
    }
}
} // namespace test_path_env_detail

/** PATH 文本编辑自检；全部通过返回 0。 */
inline int test_path_env()
{
    using test_path_env_detail::check ;
    using test_path_env_detail::failures ;
    failures() = 0 ;
    const std::filesystem::path directory = L"D:\\Coder\\works\\nx_dev\\bin" ;
    const std::wstring directory_text = L"D:\\Coder\\works\\nx_dev\\bin" ;

    check(!core::path_has_directory(L"", directory), "空 PATH：没有该目录") ;
    check(core::path_add_directory(L"", directory) == directory_text, "空 PATH：加入后就是该目录") ;
    check(core::path_has_directory(directory_text, directory), "加入后能检出") ;
    check(core::path_add_directory(directory_text, directory) == directory_text,
        "重复加入不产生重复项") ;
    check(core::path_add_directory(L"C:\\Windows", directory) ==
        directory_text + L";C:\\Windows", "加入时插在最前面并保留原项") ;
    check(core::path_add_directory(L"C:\\Windows;", directory) ==
        directory_text + L";C:\\Windows;", "原值末尾多余分号原样保留") ;

    check(core::path_has_directory(L"d:\\coder\\works\\NX_DEV\\bin\\", directory),
        "大小写与末尾反斜杠不影响识别") ;
    check(core::path_has_directory(L"\"D:\\Coder\\works\\nx_dev\\bin\"", directory),
        "带引号的目录项也能识别") ;
    check(core::path_has_directory(L" C:\\Windows ; D:\\Coder\\works\\nx_dev\\bin ", directory),
        "目录项两侧空白不影响识别") ;
    check(!core::path_has_directory(L"D:\\Coder\\works\\nx_dev", directory), "父目录不算命中") ;
    check(core::path_same_directory(directory, L"d:\\CODER\\works\\nx_dev\\bin\\"),
        "目录等价：大小写与末尾分隔符无关") ;
    check(!core::path_same_directory(directory, L"D:\\Coder\\works\\nx_dev\\bin2"),
        "目录等价：不同目录判定为不同") ;

    check(core::path_remove_directory(L"C:\\Windows;" + directory_text + L";D:\\Other", directory) ==
        L"C:\\Windows;D:\\Other", "摘除中间项后其余项原样保留") ;
    check(core::path_remove_directory(directory_text, directory).empty(), "摘除唯一项后为空串") ;
    check(core::path_remove_directory(directory_text + L";" + directory_text + L";D:\\Other",
        directory) == L"D:\\Other", "重复项一次摘干净") ;
    check(core::path_remove_directory(L"C:\\Windows", directory) == L"C:\\Windows",
        "摘除不存在的目录时原样返回") ;
    check(core::path_remove_directory(L"", directory).empty(), "空 PATH 摘除后仍为空") ;

    // 注册 -> 摘除必须回到原值。
    const std::wstring original = L"C:\\Windows;D:\\Tools;%USERPROFILE%\\bin" ;
    const std::wstring registered = core::path_add_directory(original, directory) ;
    check(core::path_remove_directory(registered, directory) == original,
        "注册再摘除可还原原 PATH") ;

    const int failed = failures() ;
    if (failed == 0)
    {
        std::cout << "[ OK ] test_path_env\n" ;
        return 0 ;
    }
    std::cerr << "[FAIL] test_path_env: " << failed << " 项失败\n" ;
    return 1 ;
}
