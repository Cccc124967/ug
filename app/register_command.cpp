#include "register_command.h"
#include "registration_record.h"
#include "../core_module/console_style.h"
#include "../core_module/dev_core.h"
#include "../core_module/file_io.h"
#include "../core_module/path_env.h"
#include "../scan_module/scan_control.h"
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace app
{
namespace
{
/** 用户环境变量所在的注册表键。 */
constexpr const wchar_t* user_environment_key = L"Environment" ;

/** PATH 键值名。 */
constexpr const wchar_t* user_path_value_name = L"Path" ;

/** 注册记录文件名；和扫描缓存放在同一个缓存目录。 */
constexpr const wchar_t* registration_file_name = L"registration.tsv" ;

/** 取当前可执行文件的完整路径；失败返回空。 */
std::filesystem::path current_executable_path()
{
    std::wstring buffer(32768, L'\0') ;
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
        static_cast<DWORD>(buffer.size())) ;
    if (length == 0 || length >= buffer.size())
    {
        return {} ;
    }
    buffer.resize(length) ;
    return std::filesystem::path(buffer) ;
}

/** 注册记录文件路径；缓存目录不可用时返回空。 */
std::filesystem::path registration_record_file()
{
    const scan::Path directory = scan::cache_directory() ;
    if (directory.empty())
    {
        return {} ;
    }
    return directory / registration_file_name ;
}

/** 打开用户环境变量键；失败返回 nullptr，并在 error 里写错误码。 */
HKEY open_user_environment(DWORD& error)
{
    HKEY key = nullptr ;
    const LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, user_environment_key, 0,
        KEY_READ | KEY_WRITE, &key) ;
    if (status != ERROR_SUCCESS)
    {
        error = static_cast<DWORD>(status) ;
        return nullptr ;
    }
    return key ;
}

/** 读取用户 PATH；值不存在按空串处理。 */
bool read_user_path(HKEY key, std::wstring& value, DWORD& value_type, DWORD& error)
{
    value.clear() ;
    value_type = REG_EXPAND_SZ ;
    DWORD stored_type = 0 ;
    DWORD buffer_size = 0 ;
    LSTATUS status = RegQueryValueExW(key, user_path_value_name, nullptr, &stored_type,
        nullptr, &buffer_size) ;
    if (status == ERROR_FILE_NOT_FOUND)
    {
        return true ;
    }
    if (status != ERROR_SUCCESS)
    {
        error = static_cast<DWORD>(status) ;
        return false ;
    }
    std::vector<wchar_t> buffer(buffer_size / sizeof(wchar_t) + 1, L'\0') ;
    status = RegQueryValueExW(key, user_path_value_name, nullptr, &stored_type,
        reinterpret_cast<LPBYTE>(buffer.data()), &buffer_size) ;
    if (status != ERROR_SUCCESS)
    {
        error = static_cast<DWORD>(status) ;
        return false ;
    }
    value.assign(buffer.data(), buffer_size / sizeof(wchar_t)) ;
    while (!value.empty() && value.back() == L'\0')
    {
        value.pop_back() ;
    }
    value_type = stored_type == REG_SZ ? REG_SZ : REG_EXPAND_SZ ;
    return true ;
}

/** 写回用户 PATH；空串表示删除该键值。 */
bool write_user_path(HKEY key, const std::wstring& value, DWORD value_type, DWORD& error)
{
    LSTATUS status = ERROR_SUCCESS ;
    if (value.empty())
    {
        status = RegDeleteValueW(key, user_path_value_name) ;
        if (status == ERROR_FILE_NOT_FOUND)
        {
            status = ERROR_SUCCESS ;
        }
    }
    else
    {
        status = RegSetValueExW(key, user_path_value_name, 0, value_type,
            reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) ;
    }
    if (status != ERROR_SUCCESS)
    {
        error = static_cast<DWORD>(status) ;
        return false ;
    }
    return true ;
}

/** 通知资源管理器环境变量已变化，让新启动的进程拿到新 PATH。 */
void broadcast_environment_change()
{
    DWORD_PTR result = 0 ;
    const wchar_t* environment_name = L"Environment" ;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        reinterpret_cast<LPARAM>(environment_name), SMTO_ABORTIFHUNG, 5000, &result) ;
}

/** 打印修改 PATH 失败的原因；返回退出码 1。 */
int report_environment_failure(const std::string& action, DWORD error)
{
    std::cerr << core::paint(action + "失败（错误码 " + std::to_string(error) + "）",
        core::color::failure) << '\n' ;
    return 1 ;
}

/** 注册自身：先落注册记录再改 PATH，记录写不成就不动 PATH。 */
int register_self()
{
    const std::filesystem::path executable = current_executable_path() ;
    if (executable.empty())
    {
        std::cerr << core::paint("读取自身路径失败；PATH 未修改", core::color::failure) << '\n' ;
        return 1 ;
    }
    const std::filesystem::path directory = executable.parent_path() ;
    const std::string directory_text = core::wide_to_utf8(directory.wstring()) ;
    const std::string program_name = core::wide_to_utf8(executable.stem().wstring()) ;
    const std::filesystem::path record_file = registration_record_file() ;
    if (record_file.empty())
    {
        std::cerr << core::paint("缓存目录不可用，无法记录注册位置；为安全起见 PATH 未修改",
            core::color::failure) << '\n' ;
        return 1 ;
    }
    std::string io_error ;
    if (!core::ensure_directory(record_file.parent_path(), io_error))
    {
        std::cerr << core::paint("创建缓存目录失败: " + io_error + "；PATH 未修改",
            core::color::failure) << '\n' ;
        return 1 ;
    }

    // 旧记录指向别处时拒绝注册，避免同一个程序在 PATH 里留下两个孤儿目录。
    std::error_code filesystem_error ;
    const bool has_record = std::filesystem::is_regular_file(record_file, filesystem_error) ;
    if (has_record)
    {
        std::filesystem::path recorded ;
        std::string record_error ;
        if (!load_registration_record(record_file, recorded, record_error))
        {
            std::cerr << core::paint("注册记录损坏（" + record_error + "）；为安全起见 PATH 未修改",
                core::color::failure) << '\n' ;
            return 1 ;
        }
        if (!core::path_same_directory(recorded, directory))
        {
            std::cerr << core::paint("已从 " + core::wide_to_utf8(recorded.wstring()) +
                " 注册过；请先运行 --unregister 摘除旧位置再重新注册", core::color::warning) << '\n' ;
            std::cerr << core::paint("PATH 未修改", core::color::hint) << '\n' ;
            return 1 ;
        }
    }

    DWORD error = ERROR_SUCCESS ;
    HKEY key = open_user_environment(error) ;
    if (key == nullptr)
    {
        return report_environment_failure("打开用户环境变量", error) ;
    }
    std::wstring path_value ;
    DWORD value_type = REG_EXPAND_SZ ;
    if (!read_user_path(key, path_value, value_type, error))
    {
        RegCloseKey(key) ;
        return report_environment_failure("读取用户 PATH ", error) ;
    }
    if (core::path_has_directory(path_value, directory))
    {
        // PATH 里已经有这一项（旧版本或手工加的）：补登记，方便以后注销。
        if (!has_record && !save_registration_record(record_file, directory, io_error))
        {
            RegCloseKey(key) ;
            std::cerr << core::paint("写注册记录失败: " + io_error + "；PATH 未修改",
                core::color::failure) << '\n' ;
            return 1 ;
        }
        RegCloseKey(key) ;
        std::cout << core::paint("✔ 已在用户 PATH 中: " + directory_text,
            core::color::success) << '\n' ;
        std::cout << core::paint("  注册记录: " + core::path_text(record_file),
            core::color::hint) << '\n' ;
        return 0 ;
    }

    // 先写记录再动 PATH：中途失败时 PATH 还没变，最坏只留下一条记录。
    if (!save_registration_record(record_file, directory, io_error))
    {
        RegCloseKey(key) ;
        std::cerr << core::paint("写注册记录失败: " + io_error + "；PATH 未修改",
            core::color::failure) << '\n' ;
        return 1 ;
    }
    const std::wstring updated = core::path_add_directory(path_value, directory) ;
    if (!write_user_path(key, updated, value_type, error))
    {
        std::string rollback_error ;
        (void) clear_registration_record(record_file, rollback_error) ;
        RegCloseKey(key) ;
        return report_environment_failure("写回用户 PATH ", error) ;
    }
    RegCloseKey(key) ;
    broadcast_environment_change() ;
    std::cout << core::paint("✔ 已将 " + directory_text + " 加入用户 PATH",
        core::color::success) << '\n' ;
    std::cout << core::paint("  注册记录: " + core::path_text(record_file),
        core::color::hint) << '\n' ;
    std::cout << core::paint("  新开终端后可直接运行: " + program_name + " --version",
        core::color::hint) << '\n' ;
    return 0 ;
}

/** 注销自身：只认注册记录里的目录，记录缺失或损坏时拒绝修改 PATH。 */
int unregister_self()
{
    const std::filesystem::path record_file = registration_record_file() ;
    if (record_file.empty())
    {
        std::cerr << core::paint("缓存目录不可用，无法确认注册位置；为安全起见 PATH 未修改，请手动清理",
            core::color::failure) << '\n' ;
        return 1 ;
    }
    std::filesystem::path recorded ;
    std::string record_error ;
    if (!load_registration_record(record_file, recorded, record_error))
    {
        std::error_code filesystem_error ;
        const bool has_record = std::filesystem::is_regular_file(record_file, filesystem_error) ;
        if (has_record)
        {
            std::cerr << core::paint("注册记录损坏（" + record_error +
                "）；为安全起见 PATH 未修改，请手动清理", core::color::failure) << '\n' ;
        }
        else
        {
            std::cerr << core::paint(
                "没有注册记录，无法确认曾注册的位置；为安全起见 PATH 未修改，请手动清理",
                core::color::failure) << '\n' ;
        }
        return 1 ;
    }
    const std::string recorded_text = core::wide_to_utf8(recorded.wstring()) ;
    DWORD error = ERROR_SUCCESS ;
    HKEY key = open_user_environment(error) ;
    if (key == nullptr)
    {
        return report_environment_failure("打开用户环境变量", error) ;
    }
    std::wstring path_value ;
    DWORD value_type = REG_EXPAND_SZ ;
    if (!read_user_path(key, path_value, value_type, error))
    {
        RegCloseKey(key) ;
        return report_environment_failure("读取用户 PATH ", error) ;
    }
    if (!core::path_has_directory(path_value, recorded))
    {
        // PATH 里已经没有了：只清过期记录，不动环境变量。
        RegCloseKey(key) ;
        std::string clear_error ;
        if (!clear_registration_record(record_file, clear_error))
        {
            std::cerr << core::paint("记录里的 " + recorded_text +
                " 不在用户 PATH 中，但清理注册记录失败: " + clear_error,
                core::color::warning) << '\n' ;
            return 1 ;
        }
        std::cout << core::paint("记录里的 " + recorded_text +
            " 不在用户 PATH 中（可能已手动清理），已清除过期注册记录", core::color::warning) << '\n' ;
        return 0 ;
    }
    const std::wstring updated = core::path_remove_directory(path_value, recorded) ;
    if (!write_user_path(key, updated, value_type, error))
    {
        RegCloseKey(key) ;
        return report_environment_failure("写回用户 PATH ", error) ;
    }
    RegCloseKey(key) ;
    std::string clear_error ;
    if (!clear_registration_record(record_file, clear_error))
    {
        std::cerr << core::paint("PATH 已清理，但删除注册记录失败: " + clear_error +
            "，可手动删除 " + core::path_text(record_file), core::color::warning) << '\n' ;
    }
    broadcast_environment_change() ;
    std::cout << core::paint("✔ 已从用户 PATH 摘除: " + recorded_text,
        core::color::success) << '\n' ;
    std::cout << core::paint("  新开终端后生效", core::color::hint) << '\n' ;
    return 0 ;
}
} // namespace

int run_register_command(const std::vector<std::string>& arguments)
{
    (void) arguments ;
    return register_self() ;
}

int run_unregister_command(const std::vector<std::string>& arguments)
{
    (void) arguments ;
    return unregister_self() ;
}
} // namespace app
