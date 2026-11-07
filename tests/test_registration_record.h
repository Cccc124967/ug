#pragma once
// ============================================================================
// test_registration_record.h — 注册位置记录读写的自动测试（夹具落 agent_build）。
//
// 由 app/main.cpp 的 main() 调 test_registration_record()，不自己写 main()。
// ============================================================================
#include "../app/registration_record.h"
#include "../core_module/file_io.h"
#include "../core_module/path_env.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>

namespace test_registration_record_detail
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
        std::cerr << "[FAIL] test_registration_record: " << label << '\n' ;
    }
}

/** 夹具目录；沿用仓库的 agent_build 临时区。 */
inline std::filesystem::path fixture_root()
{
    return std::filesystem::current_path() / "agent_build" / "tests" / "registration_fixture" ;
}

/** 按原样写一段文本。 */
inline bool write_raw(const std::filesystem::path& file, const std::string& text)
{
    std::string error ;
    return core::write_text_file(file, text, error) ;
}

/** 读整个文件文本。 */
inline bool read_raw(const std::filesystem::path& file, std::string& text)
{
    std::ifstream stream(file, std::ios::binary) ;
    if (!stream)
    {
        return false ;
    }
    text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()) ;
    return true ;
}
} // namespace test_registration_record_detail

/** 注册位置记录自检；全部通过返回 0。 */
inline int test_registration_record()
{
    using test_registration_record_detail::check ;
    using test_registration_record_detail::failures ;
    failures() = 0 ;
    const std::filesystem::path fixture = test_registration_record_detail::fixture_root() ;
    std::error_code filesystem_error ;
    std::filesystem::remove_all(fixture, filesystem_error) ;
    std::string io_error ;
    check(core::ensure_directory(fixture, io_error), "创建注册记录夹具目录") ;

    const std::filesystem::path record = fixture / "registration.tsv" ;
    const std::filesystem::path registered = L"D:\\A B\\nx_dev\\bin" ;
    std::filesystem::path loaded ;
    std::string load_error ;

    check(!app::load_registration_record(record, loaded, load_error), "没有记录时读取失败") ;
    check(!load_error.empty(), "没有记录时给出原因") ;

    std::string save_error ;
    check(app::save_registration_record(record, registered, save_error), "写入注册记录") ;
    check(app::load_registration_record(record, loaded, load_error), "读回注册记录") ;
    check(core::path_same_directory(loaded, registered), "记录里的目录往返一致") ;
    std::string raw ;
    check(test_registration_record_detail::read_raw(record, raw), "读得到记录原文") ;
    check(raw.find("registration v1") != std::string::npos, "记录文件头带版本") ;

    const std::filesystem::path second = L"D:\\Other\\bin" ;
    check(app::save_registration_record(record, second, save_error), "覆盖写注册记录") ;
    check(app::load_registration_record(record, loaded, load_error) &&
        core::path_same_directory(loaded, second), "覆盖后读回新目录") ;

    check(test_registration_record_detail::write_raw(record, "# bad header\nD:\\A B\\nx_dev\\bin\n"),
        "写入损坏文件头") ;
    check(!app::load_registration_record(record, loaded, load_error), "文件头损坏时读取失败") ;

    check(test_registration_record_detail::write_raw(record, "# nx_dev registration v1\n"),
        "写入缺少目录行的记录") ;
    check(!app::load_registration_record(record, loaded, load_error), "缺少目录行时读取失败") ;

    check(test_registration_record_detail::write_raw(record, "# nx_dev registration v1\n\n"),
        "写入空目录行的记录") ;
    check(!app::load_registration_record(record, loaded, load_error), "空目录行时读取失败") ;

    check(test_registration_record_detail::write_raw(record,
        "# nx_dev registration v1\r\nD:\\A B\\nx_dev\\bin\r\n"), "写入 CRLF 记录") ;
    check(app::load_registration_record(record, loaded, load_error) &&
        core::path_same_directory(loaded, registered), "CRLF 记录也能读") ;

    check(test_registration_record_detail::write_raw(record,
        "\xEF\xBB\xBF# nx_dev registration v1\nD:\\A B\\nx_dev\\bin\n"), "写入带 BOM 的记录") ;
    check(app::load_registration_record(record, loaded, load_error) &&
        core::path_same_directory(loaded, registered), "带 BOM 的记录也能读") ;

    std::string clear_error ;
    check(app::clear_registration_record(record, clear_error), "删除注册记录") ;
    check(!std::filesystem::exists(record), "记录文件已删除") ;
    check(app::clear_registration_record(record, clear_error), "重复删除也算成功") ;

    std::filesystem::remove_all(fixture, filesystem_error) ;
    const int failed = failures() ;
    if (failed == 0)
    {
        std::cout << "[ OK ] test_registration_record\n" ;
        return 0 ;
    }
    std::cerr << "[FAIL] test_registration_record: " << failed << " 项失败\n" ;
    return 1 ;
}
