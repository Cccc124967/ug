#include "../core_module/app_version.h"
#include "../core_module/command_defs.h"
#include "../core_module/console_style.h"
#include "../core_module/dev_core.h"
#include "../scan_module/scan_control.h"
#include "../template_module/template_main.h"
#include "../tests/test_console_module.h"
#include "../tests/test_console_status.h"
#include "../tests/test_path_env.h"
#include "../tests/test_project_build.h"
#include "../tests/test_prompt_back.h"
#include "../tests/test_registration_record.h"
#include "../tests/test_scan_select.h"
#include "../tests/test_selector_cache.h"
#include "../tests/test_scan_service.h"
#include "../tests/test_template_modules.h"
#include "console_theme.h"
#include "main_entry.h"
#include "register_command.h"
#include "scan_command.h"
#include "template_command.h"
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
/** 取宽字符命令行参数并转成 UTF-8；拿不到时退回 ANSI argv。 */
std::vector<std::string> command_arguments(int argc, char* argv[])
{
    std::vector<std::string> arguments ;
    int wide_count = 0 ;
    wchar_t** wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_count) ;
    if (wide_argv != nullptr)
    {
        for (int index = 0 ; index < wide_count ; ++index)
        {
            arguments.push_back(core::wide_to_utf8(wide_argv[index] == nullptr ? L"" : wide_argv[index])) ;
        }
        LocalFree(wide_argv) ;
        return arguments ;
    }
    for (int index = 0 ; index < argc ; ++index)
    {
        arguments.emplace_back(argv[index] == nullptr ? "" : argv[index]) ;
    }
    return arguments ;
}

/** 先打渐变横幅，再打帮助。 */
void show_help_with_banner()
{
    app::theme::print_banner(template_main::banner_shadow) ;
    show_help_info() ;
}

/** 打印进程版本与构建时间。 */
int print_version()
{
    app::theme::print_banner(template_main::banner_shadow) ;
    const std::string title = core::paint(app_version,
        std::string(core::color::bold) + core::color::bright_cyan) ;
    std::cout << title << "   " << core::paint("—", core::color::bright_black)
              << "   NX 2007 插件工程生成器\n" ;
    std::cout << core::paint("单 exe，模板与工具链信息全部内嵌", core::color::bright_black) << "\n\n" ;
    std::cout << app::theme::divider(48) << '\n' ;
    std::cout << core::paint("build  " + std::string(__DATE__) + " " + std::string(__TIME__),
        core::color::bright_black) << '\n' ;
    return 0 ;
}

/** 删除扫描缓存文件；没有缓存也算成功。 */
int clear_scan_cache()
{
    const scan::Path file = scan::cache_path() ;
    if (file.empty())
    {
        std::cout << core::paint("没有可用的缓存目录（AppData 与 Temp 均不可写），无需清除",
            core::color::bright_yellow) << '\n' ;
        return 0 ;
    }
    std::error_code error ;
    const bool removed = std::filesystem::remove(file, error) ;
    if (error)
    {
        std::cerr << core::paint("清除扫描缓存失败: " + scan::path_text(file), core::color::bright_red) << '\n' ;
        return 1 ;
    }
    const std::string label = removed ? "已清除扫描缓存: " : "没有扫描缓存: " ;
    const std::string style = removed ? std::string(core::color::bold) + core::color::bright_green
        : std::string(core::color::bright_yellow) ;
    std::cout << core::paint(label, style) << core::paint(scan::path_text(file), core::color::bright_black) << '\n' ;
    return 0 ;
}

/** 跑全部控制台自检；返回失败项数量。 */
int run_self_tests()
{
    int failures = 0 ;
    failures += test_scan_select() ;
    failures += test_scan_service() ;
    failures += test_console_module() ;
    failures += test_console_status() ;
    failures += test_template_modules() ;
    failures += test_project_build() ;
    failures += test_path_env() ;
    failures += test_registration_record() ;
    failures += test_prompt_back() ;
    failures += test_selector_cache() ;
    return failures ;
}

/** 读取令牌完整性级别 RID；失败返回 -1。 */
int token_integrity_rid(HANDLE token)
{
    DWORD size = 0 ;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size) ;
    if (size == 0)
    {
        return -1 ;
    }
    std::vector<unsigned char> buffer(size) ;
    if (!GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), size, &size))
    {
        return -1 ;
    }
    const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data()) ;
    const DWORD count = *GetSidSubAuthorityCount(label->Label.Sid) ;
    return static_cast<int>(*GetSidSubAuthority(label->Label.Sid, count - 1)) ;
}

/** 完整性级别 RID 的可读名称。 */
const char* integrity_name(int level)
{
    if (level < 0)
    {
        return "未知" ;
    }
    if (level >= SECURITY_MANDATORY_SYSTEM_RID)
    {
        return "System" ;
    }
    if (level >= SECURITY_MANDATORY_HIGH_RID)
    {
        return "High" ;
    }
    if (level >= SECURITY_MANDATORY_MEDIUM_RID)
    {
        return "Medium" ;
    }
    if (level >= SECURITY_MANDATORY_LOW_RID)
    {
        return "Low" ;
    }
    return "Untrusted" ;
}

/** 打印当前进程的用户、完整性级别、提权与容器状态；返回完整性级别，读取失败返回 -1。 */
int print_process_identity()
{
    HANDLE token = nullptr ;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE)
    {
        std::cout << "  无法打开进程令牌，错误码 " << GetLastError() << '\n' ;
        return -1 ;
    }
    DWORD size = 0 ;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size) ;
    std::vector<unsigned char> user_buffer(size) ;
    if (size > 0 && GetTokenInformation(token, TokenUser, user_buffer.data(), size, &size))
    {
        const auto* user = reinterpret_cast<const TOKEN_USER*>(user_buffer.data()) ;
        wchar_t name[256] = {} ;
        wchar_t domain[256] = {} ;
        DWORD name_length = 256 ;
        DWORD domain_length = 256 ;
        SID_NAME_USE use = SidTypeUnknown ;
        if (LookupAccountSidW(nullptr, user->User.Sid, name, &name_length, domain, &domain_length, &use))
        {
            std::cout << "  user: " << core::wide_to_utf8(std::wstring(domain)) << "\\"
                      << core::wide_to_utf8(std::wstring(name)) << '\n' ;
        }
    }
    const int integrity_level = token_integrity_rid(token) ;
    std::cout << "  完整性级别: " << integrity_name(integrity_level) << "（" << integrity_level << "）\n" ;
    TOKEN_ELEVATION elevation = {} ;
    size = sizeof(elevation) ;
    if (GetTokenInformation(token, TokenElevation, &elevation, size, &size))
    {
        std::cout << "  已提权: " << (elevation.TokenIsElevated != 0 ? "是" : "否") << '\n' ;
    }
    DWORD is_app_container = 0 ;
    size = sizeof(is_app_container) ;
    if (GetTokenInformation(token, TokenIsAppContainer, &is_app_container, size, &size))
    {
        std::cout << "  AppContainer: " << (is_app_container != 0 ? "是" : "否") << '\n' ;
    }
    CloseHandle(token) ;
    DWORD session_id = 0 ;
    ProcessIdToSessionId(GetCurrentProcessId(), &session_id) ;
    std::cout << "  session: " << session_id << '\n' ;
    if (integrity_level >= 0 && integrity_level < SECURITY_MANDATORY_MEDIUM_RID)
    {
        std::cout << "（提示：完整性级别为 Low 时，写入默认 Medium 目录会被系统拒绝并报错误码 5，"
                     "与目录 ACL 无关，属于进程沙箱/受限上下文）\n" ;
    }
    return integrity_level ;
}

/** 打印父进程 PID、镜像路径与完整性级别；读取失败时给出错误码。 */
void print_parent_process(int self_level)
{
    DWORD parent_pid = 0 ;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) ;
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W entry = {} ;
        entry.dwSize = sizeof(entry) ;
        const DWORD self = GetCurrentProcessId() ;
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (entry.th32ProcessID == self)
                {
                    parent_pid = entry.th32ParentProcessID ;
                    break ;
                }
            } while (Process32NextW(snapshot, &entry)) ;
        }
        CloseHandle(snapshot) ;
    }
    std::cout << "  父进程 PID: " << parent_pid << '\n' ;
    if (parent_pid == 0)
    {
        return ;
    }
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parent_pid) ;
    if (process == nullptr)
    {
        std::cout << "  打开父进程失败，错误码 " << GetLastError() << '\n' ;
        return ;
    }
    wchar_t image_path[32768] = {} ;
    DWORD path_length = static_cast<DWORD>(sizeof(image_path) / sizeof(image_path[0])) ;
    if (QueryFullProcessImageNameW(process, 0, image_path, &path_length))
    {
        std::cout << "  父进程: " << core::wide_to_utf8(std::wstring(image_path, path_length)) << '\n' ;
    }
    else
    {
        std::cout << "  读取父进程路径失败，错误码 " << GetLastError() << '\n' ;
    }
    HANDLE parent_token = nullptr ;
    if (OpenProcessToken(process, TOKEN_QUERY, &parent_token))
    {
        const int parent_level = token_integrity_rid(parent_token) ;
        std::cout << "  父进程完整性级别: " << integrity_name(parent_level) << "（" << parent_level << "）\n" ;
        if (self_level >= 0 && parent_level > self_level)
        {
            std::cout << "  （父进程等级高于本进程：可执行文件很可能带 Low 完整性标签——"
                         "Windows 会把进程压到不高于 exe 镜像标签；删掉该 exe 后由普通终端重新生成，"
                         "或用管理员 icacls 查看 SACL）\n" ;
        }
        CloseHandle(parent_token) ;
    }
    else
    {
        std::cout << "  读取父进程令牌失败，错误码 " << GetLastError() << '\n' ;
    }
    CloseHandle(process) ;
}

/** 对一个目录分别用 Win32 与标准库做写入探测。 */
void probe_directory_writability(const std::filesystem::path& directory)
{
    std::cout << "目录: " << scan::path_text(directory) << '\n' ;
    const DWORD attributes = GetFileAttributesW(directory.c_str()) ;
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        std::cout << "  GetFileAttributes: 失败，错误码 " << GetLastError() << '\n' ;
    }
    else
    {
        std::cout << "  GetFileAttributes: 成功，属性 " << attributes << '\n' ;
    }
    const std::filesystem::path probe_directory = directory / L".nx_dev_write_probe" ;
    SetLastError(ERROR_SUCCESS) ;
    const BOOL created = CreateDirectoryW(probe_directory.c_str(), nullptr) ;
    const DWORD created_error = GetLastError() ;
    std::cout << "  CreateDirectoryW: " << (created != FALSE ? "成功" : "失败") ;
    if (created == FALSE)
    {
        std::cout << "，错误码 " << created_error ;
    }
    std::cout << '\n' ;
    if (created != FALSE)
    {
        RemoveDirectoryW(probe_directory.c_str()) ;
    }
    std::error_code error ;
    const bool created_filesystem = std::filesystem::create_directory(probe_directory, error) ;
    if (created_filesystem)
    {
        std::filesystem::remove(probe_directory, error) ;
    }
    std::cout << "  std::filesystem: " << (created_filesystem ? "成功" : "失败")
              << "，错误码 " << error.value() << "（" << error.message() << "）\n" ;
    const std::filesystem::path probe_file = directory / L".nx_dev_write_probe.tmp" ;
    SetLastError(ERROR_SUCCESS) ;
    const HANDLE file = CreateFileW(probe_file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr) ;
    const DWORD file_error = GetLastError() ;
    std::cout << "  CreateFileW: " << (file != INVALID_HANDLE_VALUE ? "成功" : "失败") ;
    if (file == INVALID_HANDLE_VALUE)
    {
        std::cout << "，错误码 " << file_error ;
    }
    std::cout << '\n' ;
    if (file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file) ;
        DeleteFileW(probe_file.c_str()) ;
    }
}

/** --diag：打印进程身份与目标目录的写入探测结果。 */
int run_path_diagnostic(const std::vector<std::string>& arguments)
{
    std::cout << "nx_dev 路径诊断（只读探测，不写实际文件）\n" ;
    const int self_level = print_process_identity() ;
    print_parent_process(self_level) ;
    std::cout << '\n' ;
    std::vector<std::filesystem::path> directories ;
    for (const std::string& argument : arguments)
    {
        directories.push_back(core::utf8_to_wide(argument)) ;
    }
    if (directories.empty())
    {
        directories.push_back(std::filesystem::current_path()) ;
    }
    for (const std::filesystem::path& directory : directories)
    {
        probe_directory_writability(directory) ;
        std::cout << '\n' ;
    }
    return 0 ;
}
} // namespace

int main(int argc, char* argv[])
{
    try
    {
        app::theme::prepare_console() ;
        const std::vector<std::string> arguments = command_arguments(argc, argv) ;
        const std::string command = arguments.size() > 1 ? arguments[1] : std::string() ;
        std::vector<std::string> rest ;
        for (std::size_t index = 2 ; index < arguments.size() ; ++index)
        {
            rest.push_back(arguments[index]) ;
        }

        if (command.empty())
        {
            show_help_with_banner() ;
            return 0 ;
        }
        if (command == cmd_version || command == cmd_version2)
        {
            return print_version() ;
        }
        if (command == cmd_help || command == cmd_help2)
        {
            show_help_with_banner() ;
            return 0 ;
        }
        if (command == cmd_scan)
        {
            return app::run_scan_command(rest) ;
        }
        if (command == cmd_create_template_code)
        {
            return app::run_build_template_command(rest) ;
        }
        if (command == cmd_clear_cache)
        {
            return clear_scan_cache() ;
        }
        if (command == cmd_self_test)
        {
            const int failures = run_self_tests() ;
            if (failures == 0)
            {
                std::cout << core::paint("全部自检通过", std::string(core::color::bold) +
                    core::color::bright_green) << '\n' ;
                return 0 ;
            }
            std::cout << core::paint("自检失败项: " + std::to_string(failures), std::string(core::color::bold) +
                core::color::bright_red) << '\n' ;
            return 1 ;
        }
        if (command == cmd_diag)
        {
            return run_path_diagnostic(rest) ;
        }
        if (command == cmd_register)
        {
            return app::run_register_command(rest) ;
        }
        if (command == cmd_unregister)
        {
            return app::run_unregister_command(rest) ;
        }
        if (command == cmd_main || command == cmd_main2 || command == cmd_main3)
        {
            return app::run_main_entry_command(rest) ;
        }

        std::cout << core::paint("未知命令: " + command, core::color::bright_red) << "\n\n" ;
        show_help_info() ;
        return 2 ;
    }
    catch (const std::exception& error)
    {
        std::cerr << core::paint(std::string("nx_dev: ") + error.what(), core::color::bright_red) << '\n' ;
        return 1 ;
    }
}
