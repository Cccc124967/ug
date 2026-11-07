#include "../git_control.h"
#include "../file_io.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <system_error>

namespace core
{
namespace
{
/** 一条 cmd 命令的执行结果。 */
struct CommandOutput
{
    bool m_started = false ;   // CreateProcess 是否成功
    bool m_timed_out = false ; // 是否因为超时被终止
    int m_exit_code = -1 ;     // 进程退出码
    std::string m_output ;     // stdout + stderr，已转成 UTF-8
} ;

/** 把本地代码页字节转 UTF-8；非法字节写成替换字符，不抛异常。 */
std::string local_to_utf8(const std::string& bytes)
{
    if (bytes.empty())
    {
        return {} ;
    }
    const int size = static_cast<int>(bytes.size()) ;
    const int wide_length = MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), size, nullptr, 0) ;
    if (wide_length <= 0)
    {
        return {} ;
    }
    std::wstring wide(static_cast<std::size_t>(wide_length), L'\0') ;
    MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), size, wide.data(), wide_length) ;
    const int utf8_length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_length,
        nullptr, 0, nullptr, nullptr) ;
    if (utf8_length <= 0)
    {
        return {} ;
    }
    std::string utf8(static_cast<std::size_t>(utf8_length), '\0') ;
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_length, utf8.data(), utf8_length,
        nullptr, nullptr) ;
    return utf8 ;
}

/** 去掉首尾 ASCII 空白。 */
std::string trim_ascii(std::string text)
{
    const auto not_space = [](unsigned char letter) { return std::isspace(letter) == 0 ; } ;
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space)) ;
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end()) ;
    return text ;
}

/** 取多行输出里的第一行非空内容。 */
std::string first_line(const std::string& text)
{
    std::size_t begin = 0 ;
    while (begin < text.size())
    {
        std::size_t end = text.find('\n', begin) ;
        if (end == std::string::npos)
        {
            end = text.size() ;
        }
        const std::string line = trim_ascii(text.substr(begin, end - begin)) ;
        if (!line.empty())
        {
            return line ;
        }
        begin = end + 1 ;
    }
    return {} ;
}

/** 取命令行解释器：优先 ComSpec，拿不到退回 cmd.exe。 */
std::wstring command_interpreter()
{
    const DWORD size = GetEnvironmentVariableW(L"ComSpec", nullptr, 0) ;
    if (size > 1)
    {
        std::wstring value(size, L'\0') ;
        const DWORD written = GetEnvironmentVariableW(L"ComSpec", value.data(), size) ;
        if (written > 0 && written < size)
        {
            value.resize(written) ;
            return value ;
        }
    }
    return L"cmd.exe" ;
}

/** 在指定目录跑一条 cmd 命令，收 stdout/stderr；带超时保护。 */
CommandOutput run_shell_command(const std::wstring& command,
    const std::filesystem::path& working_directory)
{
    CommandOutput result ;
    SECURITY_ATTRIBUTES attributes {} ;
    attributes.nLength = sizeof(attributes) ;
    attributes.bInheritHandle = TRUE ;
    HANDLE read_pipe = nullptr ;
    HANDLE write_pipe = nullptr ;
    if (CreatePipe(&read_pipe, &write_pipe, &attributes, 0) == FALSE)
    {
        return result ;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0) ;

    STARTUPINFOW startup {} ;
    startup.cb = sizeof(startup) ;
    startup.dwFlags = STARTF_USESTDHANDLES ;
    startup.hStdOutput = write_pipe ;
    startup.hStdError = write_pipe ;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE) ;

    PROCESS_INFORMATION process {} ;
    std::wstring command_line = L"\"" + command_interpreter() + L"\" /c " + command ;
    const std::wstring directory = working_directory.wstring() ;
    const BOOL started = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, directory.empty() ? nullptr : directory.c_str(),
        &startup, &process) ;
    CloseHandle(write_pipe) ;
    if (started == FALSE)
    {
        CloseHandle(read_pipe) ;
        return result ;
    }
    result.m_started = true ;

    constexpr ULONGLONG timeout_ms = 15000 ;
    const ULONGLONG started_tick = GetTickCount64() ;
    std::array<char, 1024> buffer = {} ;
    for (;;)
    {
        DWORD available = 0 ;
        if (PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr) == FALSE)
        {
            break ;
        }
        if (available > 0)
        {
            DWORD read = 0 ;
            if (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) == FALSE ||
                read == 0)
            {
                break ;
            }
            result.m_output.append(buffer.data(), read) ;
            continue ;
        }
        if (WaitForSingleObject(process.hProcess, 20) == WAIT_OBJECT_0)
        {
            // 进程已退出：把管道里剩下的读干净。
            DWORD read = 0 ;
            while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) != FALSE &&
                read > 0)
            {
                result.m_output.append(buffer.data(), read) ;
            }
            break ;
        }
        if (GetTickCount64() - started_tick > timeout_ms)
        {
            TerminateProcess(process.hProcess, 1) ;
            WaitForSingleObject(process.hProcess, 2000) ;
            result.m_timed_out = true ;
            break ;
        }
    }

    DWORD exit_code = 1 ;
    GetExitCodeProcess(process.hProcess, &exit_code) ;
    result.m_exit_code = static_cast<int>(exit_code) ;
    CloseHandle(process.hThread) ;
    CloseHandle(process.hProcess) ;
    CloseHandle(read_pipe) ;
    result.m_output = local_to_utf8(result.m_output) ;
    return result ;
}

/** 命令是否正常结束且退出码为 0。 */
bool command_ok(const CommandOutput& output)
{
    return output.m_started && !output.m_timed_out && output.m_exit_code == 0 ;
}

/** 失败时的可读原因：优先取命令输出的第一行。 */
std::string command_failure_text(const CommandOutput& output)
{
    if (!output.m_started)
    {
        return "启动 cmd.exe 失败" ;
    }
    if (output.m_timed_out)
    {
        return "命令执行超时" ;
    }
    const std::string line = first_line(output.m_output) ;
    if (!line.empty())
    {
        return line ;
    }
    return "退出码 " + std::to_string(output.m_exit_code) ;
}

/** 排除构建产物。 */
constexpr const char* gitignore_text = "# 构建产物\r\nbuild/\r\n" ;

/** .gitattributes 内容：eol 跟随生成工程的行尾选择；文件自身保持 CRLF 不变。 */
std::string build_gitattributes_text(ctx::LineEnding line_ending)
{
    const bool crlf = line_ending == ctx::LineEnding::CRLF ;
    std::string text = "# 文本文件统一 " ;
    text += crlf ? "CRLF" : "LF" ;
    text += " 行尾\r\n* text=auto eol=" ;
    text += crlf ? "crlf" : "lf" ;
    text += "\r\n" ;
    return text ;
}
} // namespace

GitProbe scan_git()
{
    GitProbe probe ;
    const CommandOutput output = run_shell_command(L"where git", std::filesystem::path()) ;
    if (!output.m_started)
    {
        probe.m_message = "启动 cmd.exe 失败" ;
        return probe ;
    }
    if (output.m_timed_out)
    {
        probe.m_message = "where git 执行超时" ;
        return probe ;
    }
    const std::string text = trim_ascii(output.m_output) ;
    if (output.m_exit_code != 0 || text.empty())
    {
        probe.m_message = "where git 没有输出：没装 Git 或没加进 PATH" ;
        return probe ;
    }
    probe.m_found = true ;
    probe.m_path = first_line(text) ;
    probe.m_message = text ;
    return probe ;
}

GitInitResult initialize_git_repository(const std::filesystem::path& project_dir,
    ctx::LineEnding line_ending)
{
    GitInitResult result ;
    std::error_code error ;
    if (project_dir.empty() || !std::filesystem::is_directory(project_dir, error))
    {
        result.m_message = "工程目录不存在，无法初始化 git: " + path_text(project_dir) ;
        return result ;
    }
    const GitProbe probe = scan_git() ;
    if (!probe.m_found)
    {
        result.m_message = "没有找到 git: " + probe.m_message ;
        return result ;
    }

    const CommandOutput initialized = run_shell_command(L"git init", project_dir) ;
    if (!command_ok(initialized))
    {
        result.m_message = "git init 失败: " + command_failure_text(initialized) ;
        return result ;
    }

    // 新仓库的未出生分支也能读出来；不是 main 就切过去，避免跟着全局 init.defaultBranch 走。
    const CommandOutput current = run_shell_command(L"git symbolic-ref --short HEAD", project_dir) ;
    const std::string branch = trim_ascii(current.m_output) ;
    if (branch != "main")
    {
        const CommandOutput switched = run_shell_command(L"git switch -c main", project_dir) ;
        if (!command_ok(switched))
        {
            result.m_message = "切换到 main 分支失败: " + command_failure_text(switched) ;
            return result ;
        }
    }

    std::string write_error ;
    if (!write_text_file(project_dir / L".gitignore", gitignore_text, write_error) ||
        !write_text_file(project_dir / L".gitattributes", build_gitattributes_text(line_ending), write_error))
    {
        result.m_message = "写 git 忽略文件失败: " + write_error ;
        return result ;
    }
    result.m_ok = true ;
    result.m_message = "已初始化 git 仓库（分支 main）" ;
    return result ;
}
} // namespace core
