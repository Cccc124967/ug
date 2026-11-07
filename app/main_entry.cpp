#include "main_entry.h"
#include "project_build.h"
#include "../console_module/console_control.h"
#include "../core_module/console_style.h"
#include "../core_module/context.h"
#include "../core_module/dev_core.h"
#include "../core_module/file_io.h"
#include "../prompt_module/prompt_control.h"
#include "../scan_module/scan_select.h"
#include "../scan_module/scan_service.h"
#include "../template_module/template_main.h"
#include "../template_module/template_release.h"
#include "console_theme.h"
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace app
{
namespace
{
/** 默认项目名。 */
constexpr const char* default_project_name = "NXProject" ;

/** 用户取消时的退出码。 */
constexpr int exit_cancelled = 1 ;

/** 用户 Ctrl+C 中断时的退出码。 */
constexpr int exit_interrupted = 130 ;

/** 读取当前进程完整性级别 RID；读取失败返回 -1。 */
int process_integrity_rid()
{
    HANDLE token = nullptr ;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE)
    {
        return -1 ;
    }
    DWORD size = 0 ;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size) ;
    std::vector<unsigned char> buffer(size) ;
    int level = -1 ;
    if (size > 0 && GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), size, &size))
    {
        const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data()) ;
        const DWORD count = *GetSidSubAuthorityCount(label->Label.Sid) ;
        level = static_cast<int>(*GetSidSubAuthority(label->Label.Sid, count - 1)) ;
    }
    CloseHandle(token) ;
    return level ;
}

/** 当前可执行文件所在目录；失败返回空。 */
std::filesystem::path executable_directory()
{
    std::wstring buffer(32768, L'\0') ;
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
        static_cast<DWORD>(buffer.size())) ;
    if (length == 0 || length >= buffer.size())
    {
        return {} ;
    }
    buffer.resize(length) ;
    return std::filesystem::path(buffer).parent_path() ;
}

/** 目录里是否有 *toolchain*.cmake 文件；用于识别便携部署的提示根。 */
bool has_toolchain_cmake(const std::filesystem::path& directory)
{
    std::error_code error ;
    std::filesystem::directory_iterator iterator(directory, error) ;
    if (error)
    {
        return false ;
    }
    for (const auto& entry : iterator)
    {
        std::error_code entry_error ;
        if (!entry.is_regular_file(entry_error))
        {
            continue ;
        }
        std::string name = core::path_text(entry.path().filename()) ;
        for (char& letter : name)
        {
            letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter))) ;
        }
        if (name.ends_with(".cmake") && name.find("toolchain") != std::string::npos)
        {
            return true ;
        }
    }
    return false ;
}

/** 便携部署提示根：exe 向上找 *toolchain*.cmake 的目录，加上 NX_DEVKIT_DIR 提示。 */
std::vector<scan::Path> portable_scan_roots()
{
    std::vector<scan::Path> candidates ;
    std::filesystem::path current = executable_directory() ;
    for (int level = 0 ; level < 4 && !current.empty() ; ++level)
    {
        if (has_toolchain_cmake(current))
        {
            candidates.push_back(current) ;
            break ;
        }
        current = current.parent_path() ;
    }
    const std::string hint = scan::environment_value("NX_DEVKIT_DIR") ;
    if (!hint.empty())
    {
        candidates.push_back(scan::path_from_text(hint)) ;
    }
    std::vector<scan::Path> result ;
    std::vector<std::string> seen ;
    for (const scan::Path& candidate : candidates)
    {
        std::string key = core::path_text(candidate) ;
        for (char& letter : key)
        {
            letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter))) ;
        }
        if (std::find(seen.begin(), seen.end(), key) != seen.end())
        {
            continue ;
        }
        seen.push_back(key) ;
        result.push_back(candidate) ;
    }
    return result ;
}

/** 向导收集到的全部答案。 */
struct WizardAnswers
{
    /** 是否使用后台自动扫描。 */
    bool m_auto_scan = true ;
    /** 手动模式是否已经定位到编译器。 */
    bool m_manual_compiler_found = false ;
    /** 手动模式的编译器。 */
    scan::Compiler m_manual_compiler ;
    /** 手动模式的 NX SDK。 */
    scan::NXSdk m_manual_nx_sdk ;
    /** 手动模式的 Windows SDK；版本为空表示不锁定。 */
    scan::WindowsSdk m_manual_windows_sdk ;
    /** 项目名，同时是 DLL 名。 */
    std::string m_project_name = default_project_name ;
    /** 项目父目录。 */
    std::filesystem::path m_parent_directory ;
    /** 目标目录已存在时是否覆盖。 */
    bool m_overwrite = false ;
    /** 是否快速部署。 */
    bool m_quick_deploy = false ;
    /** 是否创建模板代码。 */
    bool m_create_templates = true ;
    /** 模板模块（已展开依赖）。 */
    std::vector<template_release::TemplateModule> m_template_modules ;
    /** 源码编码。 */
    ctx::Coding m_coding = ctx::Coding::GB2312 ;
    /** 源码行尾。 */
    ctx::LineEnding m_line_ending = ctx::LineEnding::CRLF ;
    /** 是否初始化 git。 */
    bool m_init_git = true ;
    /** 普通路径手动选型选中的编译器下标。 */
    std::size_t m_compiler_index = 0 ;
    /** 普通路径手动选型选中的 NX SDK 下标。 */
    std::size_t m_nx_sdk_index = 0 ;
    /** 普通路径手动选型选中的 Windows SDK 下标。 */
    std::size_t m_windows_sdk_index = 0 ;
} ;

/** 允许留空的目录选择器；用于可选的 Windows SDK。 */
class OptionalDirectorySelector : public console::SelectorElement
{
    public:
    /** 固定为文件夹模式。 */
    OptionalDirectorySelector() : console::SelectorElement(console::FilePickerMode::Directory)
    {
    }

    /** 空路径视为合法；非空时仍要求是已存在的文件夹。 */
    std::optional<std::string> validate() const override
    {
        if (get_path().empty())
        {
            return std::nullopt ;
        }
        return console::SelectorElement::validate() ;
    }
} ;

/** 校验项目名：非空、首字符字母或下划线、其余字母数字下划线。 */
std::optional<std::string> validate_project_name(const std::string& name)
{
    if (name.empty())
    {
        return "项目名不能为空" ;
    }
    const unsigned char first = static_cast<unsigned char>(name.front()) ;
    if (std::isalpha(first) == 0 && first != '_')
    {
        return "首字符必须是字母或下划线" ;
    }
    for (const char raw : name)
    {
        const unsigned char letter = static_cast<unsigned char>(raw) ;
        if (std::isalnum(letter) == 0 && letter != '_')
        {
            return "只能用字母、数字、下划线（同时是 DLL 名）" ;
        }
    }
    return std::nullopt ;
}

/** 目标工程目录 = 父目录 / 项目名。 */
std::filesystem::path project_directory(const WizardAnswers& answers)
{
    if (answers.m_parent_directory.empty() || answers.m_project_name.empty())
    {
        return {} ;
    }
    return answers.m_parent_directory / core::utf8_to_wide(answers.m_project_name) ;
}

/** 目标目录是否已存在（文件或目录都算）。 */
bool target_exists(const WizardAnswers& answers)
{
    const std::filesystem::path target = project_directory(answers) ;
    if (target.empty())
    {
        return false ;
    }
    std::error_code error ;
    return std::filesystem::exists(target, error) ;
}

/** 扫描域的中文显示名。 */
std::string domain_label(scan::ScanDomain domain)
{
    switch (domain)
    {
        case scan::ScanDomain::Compiler: return "编译器" ;
        case scan::ScanDomain::NxSdk: return "NX SDK" ;
        case scan::ScanDomain::WindowsSdk: return "Windows SDK" ;
    }
    return "扫描" ;
}

/** 生成状态区文本；已取消时只留一行提示。 */
std::vector<std::string> scan_status_lines(const scan::ScanState& state, bool cancelled)
{
    if (cancelled)
    {
        return { core::paint("自动扫描已取消，改为手动指定目录", core::color::warning) } ;
    }
    std::vector<std::string> lines ;
    lines.reserve(state.m_domains.size()) ;
    for (const scan::DomainScanState& domain : state.m_domains)
    {
        const std::string label = domain_label(domain.m_domain) ;
        if (domain.m_cancelled)
        {
            lines.push_back(core::paint("… " + label + " 已取消", core::color::warning)) ;
            continue ;
        }
        if (!domain.m_finished)
        {
            lines.push_back(core::paint("… " + label + " 扫描中（已访问 " +
                std::to_string(domain.m_visited) + " 个目录）", core::color::warning)) ;
            continue ;
        }
        std::string line = "✔ " + label + " " + (domain.m_from_cache ? "缓存命中" : "扫描完成") ;
        if (!domain.m_message.empty())
        {
            line += "：" + domain.m_message ;
        }
        else
        {
            line += "：" + std::to_string(domain.m_found) + " 个候选" ;
        }
        lines.push_back(core::paint(line, core::color::success)) ;
    }
    return lines ;
}

/** 把控件结束状态映射成退出码。 */
int exit_code_from(console::PromptState state)
{
    return state == console::PromptState::Interrupted ? exit_interrupted : exit_cancelled ;
}

/** 把流程结束状态映射成退出码。 */
int exit_code_from(prompt::PromptFlowState state)
{
    return state == prompt::PromptFlowState::Interrupted ? exit_interrupted : exit_cancelled ;
}

/** 编译器一行摘要：名称 | 工具集版本 | cl 真实版本 | 生成器。 */
std::string compiler_summary(const scan::Compiler& compiler)
{
    std::string text = compiler.m_name ;
    if (!compiler.m_toolset_version.empty())
    {
        text += " | 工具集 " + compiler.m_toolset_version ;
    }
    if (!compiler.m_compiler_version.empty())
    {
        text += " | cl " + compiler.m_compiler_version ;
    }
    if (!compiler.m_generator.empty())
    {
        text += " | " + compiler.m_generator ;
    }
    return text ;
}

/** 构建方案一行摘要。 */
std::string build_plan_summary(const scan::BuildPlan& plan)
{
    if (!plan.m_ok)
    {
        return "未确定" ;
    }
    switch (plan.m_kind)
    {
        case scan::BuildPlanKind::VisualStudio:
            return plan.m_generator + "（VS 安装）" ;
        case scan::BuildPlanKind::ToolchainFile:
            return plan.m_generator + " + " + core::path_text(plan.m_toolchain_file) ;
        case scan::BuildPlanKind::GeneratedToolchain:
            return plan.m_generator + " + 自动生成 toolchain" ;
        case scan::BuildPlanKind::Unresolved:
            break ;
    }
    return "未确定" ;
}

/** NX SDK 一行摘要：版本 | 安装根。 */
std::string nx_sdk_summary(const scan::NXSdk& sdk)
{
    std::string text = sdk.m_version.empty() ? std::string("NX（版本未知）") : "NX " + sdk.m_version ;
    if (!sdk.m_root.empty())
    {
        text += " | " + core::path_text(sdk.m_root) ;
    }
    if (sdk.m_is_running)
    {
        text += " | 运行中" ;
    }
    return text ;
}

/** Windows SDK 一行摘要：版本 | 根。 */
std::string windows_sdk_summary(const scan::WindowsSdk& sdk)
{
    if (sdk.m_version.empty())
    {
        return "不锁定版本" ;
    }
    std::string text = "Windows SDK " + sdk.m_version ;
    if (!sdk.m_root.empty())
    {
        text += " | " + core::path_text(sdk.m_root) ;
    }
    return text ;
}

/** 模板模块的界面标题。 */
std::string module_title(template_release::TemplateModule module)
{
    for (const template_release::TemplateModuleInfo& info : template_release::template_modules())
    {
        if (info.m_module == module)
        {
            return info.m_title ;
        }
    }
    return {} ;
}

/** 全部模板模块（快速部署用）。 */
std::vector<template_release::TemplateModule> all_template_modules()
{
    std::vector<template_release::TemplateModule> modules ;
    for (const template_release::TemplateModuleInfo& info : template_release::template_modules())
    {
        modules.push_back(info.m_module) ;
    }
    return modules ;
}

/** 模块集合里是否包含入口代码。 */
bool has_entry_code(const std::vector<template_release::TemplateModule>& modules)
{
    return std::find(modules.begin(), modules.end(), template_release::TemplateModule::EntryCode) != modules.end() ;
}

/** 把模块标题拼成一行。 */
std::string join_module_titles(const std::vector<template_release::TemplateModule>& modules)
{
    if (modules.empty())
    {
        return "不创建（空工程）" ;
    }
    std::string text ;
    for (std::size_t index = 0 ; index < modules.size() ; ++index)
    {
        if (index > 0)
        {
            text += "、" ;
        }
        text += module_title(modules[index]) ;
    }
    return text ;
}

/** 显示用的选项文本：编译器。 */
std::string compiler_option_text(const scan::Compiler& compiler)
{
    std::string text = compiler_summary(compiler) ;
    if (!compiler.m_cl_path.empty())
    {
        text += " | " + core::path_text(compiler.m_cl_path) ;
    }
    return text ;
}

/** 显示用的选项文本：NX SDK。 */
std::string nx_sdk_option_text(const scan::NXSdk& sdk)
{
    return nx_sdk_summary(sdk) ;
}

/** 显示用的选项文本：Windows SDK。 */
std::string windows_sdk_option_text(const scan::WindowsSdk& sdk)
{
    return windows_sdk_summary(sdk) ;
}

/** 阻塞等待扫描收敛，期间刷新状态区；被中断时返回 false。 */
bool wait_for_scan(console::ConsoleSession& session, scan::ScanSession& scan_session, bool cancelled)
{
    scan::ScanState state = scan_session.state() ;
    while (!state.all_finished())
    {
        session.set_status(scan_status_lines(state, cancelled)) ;
        if (session.poll_interrupt())
        {
            scan_session.cancel() ;
            scan_session.wait() ;
            session.write_line(core::paint("已中断扫描", core::color::warning)) ;
            return false ;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50)) ;
        state = scan_session.state() ;
    }
    session.set_status(scan_status_lines(state, cancelled)) ;
    return true ;
}

/** 定向扫描一个目录并实时显示进度；返回空表示没找到或被打断。 */
template <typename ResultType>
std::optional<ResultType> locate_with_progress(console::ConsoleSession& session,
    const std::filesystem::path& hint_directory, bool& interrupted,
    const std::function<std::optional<ResultType>(const scan::Path&, const scan::LocateObserver&)>& locate_function)
{
    interrupted = false ;
    auto last_tick = std::chrono::steady_clock::now() ;
    const scan::LocateObserver observer = [&session, &interrupted, &last_tick](
        const scan::Path&, std::size_t visited)
    {
        const auto now = std::chrono::steady_clock::now() ;
        if (visited == 0 || now - last_tick >= std::chrono::milliseconds(100))
        {
            last_tick = now ;
            session.update_line(core::paint("扫描中… 已访问 " + std::to_string(visited) + " 个目录",
                core::color::bright_black)) ;
        }
        if (session.poll_interrupt())
        {
            interrupted = true ;
            return false ;
        }
        return true ;
    } ;
    std::optional<ResultType> result = locate_function(hint_directory, observer) ;
    session.update_line("") ;
    return result ;
}

/** 手动模式：依次定位编译器、NX SDK 与 Windows SDK；取消返回 false。 */
bool ask_manual_toolchains(console::ConsoleSession& session, WizardAnswers& answers)
{
    // 编译器工具链是必需项。
    while (true)
    {
        console::SelectorElement element(console::FilePickerMode::Directory) ;
        element.set_title("编译器工具链目录（给近似路径即可，递归 5 层定位）") ;
        const auto state = session.run(element) ;
        if (state != console::PromptState::Submitted)
        {
            return false ;
        }
        bool interrupted = false ;
        const auto locate = [](const scan::Path& path, const scan::LocateObserver& observer)
        {
            return scan::locate_compiler(path, 5, observer) ;
        } ;
        const auto found = locate_with_progress<scan::Compiler>(session, element.get_path(), interrupted, locate) ;
        if (interrupted)
        {
            return false ;
        }
        if (!found.has_value())
        {
            session.write_line(core::paint("未识别到编译器：在 " + core::path_text(element.get_path()) +
                " 下递归 5 层没有找到可用的 cl.exe", core::color::failure)) ;
            session.write_line(core::paint("需要以下任一布局：VS 安装根（VC\\Auxiliary\\Build\\vcvarsall.bat），"
                "或便携 MSVC（bin\\<host>\\<arch>\\cl.exe，可带 *toolchain*.cmake）", core::color::hint)) ;
            continue ;
        }
        answers.m_manual_compiler = found.value() ;
        answers.m_manual_compiler_found = true ;
        session.write_line(core::paint("✔ 已定位编译器: " + compiler_summary(found.value()),
            core::color::success)) ;
        break ;
    }

    // NX SDK 是必需项。
    while (true)
    {
        console::SelectorElement element(console::FilePickerMode::Directory) ;
        element.set_title("NX SDK 目录（给近似路径即可，递归 5 层定位）") ;
        const auto state = session.run(element) ;
        if (state != console::PromptState::Submitted)
        {
            return false ;
        }
        bool interrupted = false ;
        const auto locate = [](const scan::Path& path, const scan::LocateObserver& observer)
        {
            return scan::locate_nx_sdk(path, 5, observer) ;
        } ;
        const auto found = locate_with_progress<scan::NXSdk>(session, element.get_path(), interrupted, locate) ;
        if (interrupted)
        {
            return false ;
        }
        if (!found.has_value())
        {
            session.write_line(core::paint("未识别到 NX SDK：在 " + core::path_text(element.get_path()) +
                " 下递归 5 层没有找到可用 SDK", core::color::failure)) ;
            session.write_line(core::paint("需要 NX 安装根（含 UGII 与 NXOPEN/UGOPEN），"
                "或便携 ug_sdk（include\\uf.h + include\\NXOpen\\Session.hxx + lib\\libufun.lib）",
                core::color::hint)) ;
            continue ;
        }
        answers.m_manual_nx_sdk = found.value() ;
        session.write_line(core::paint("✔ 已定位 NX SDK: " + nx_sdk_summary(found.value()),
            core::color::success)) ;
        break ;
    }

    // Windows SDK 可留空。
    while (true)
    {
        OptionalDirectorySelector element ;
        element.set_title("Windows SDK 目录（可留空不锁定版本；或给近似路径，递归 5 层定位）") ;
        const auto state = session.run(element) ;
        if (state != console::PromptState::Submitted)
        {
            return false ;
        }
        if (element.get_path().empty())
        {
            session.write_line(core::paint("已跳过 Windows SDK，生成时不锁定版本", core::color::hint)) ;
            return true ;
        }
        bool interrupted = false ;
        const auto locate = [](const scan::Path& path, const scan::LocateObserver& observer)
        {
            return scan::locate_windows_sdk(path, 5, observer) ;
        } ;
        const auto found = locate_with_progress<scan::WindowsSdk>(session, element.get_path(), interrupted, locate) ;
        if (interrupted)
        {
            return false ;
        }
        if (!found.has_value())
        {
            session.write_line(core::paint("未识别到 Windows SDK：在 " + core::path_text(element.get_path()) +
                " 下递归 5 层没有找到可用 SDK", core::color::failure)) ;
            session.write_line(core::paint("需要 kits 根：Include\\<版本>\\um + Lib\\<版本>\\um\\x64 + "
                "bin\\<版本>\\x64\\rc.exe", core::color::hint)) ;
            continue ;
        }
        answers.m_manual_windows_sdk = found.value() ;
        session.write_line(core::paint("✔ 已定位 Windows SDK: " + windows_sdk_summary(found.value()),
            core::color::success)) ;
        return true ;
    }
}

/** 构造单选节点。 */
prompt::PromptNode make_select_node(std::string node_id, std::string title,
    const std::vector<std::string>& options, std::size_t initial_index,
    prompt::PromptCondition condition, prompt::PromptSubmitHandler handler)
{
    auto element = std::make_unique<console::SingleSelectElement>() ;
    element->set_title(std::move(title)) ;
    for (const std::string& option : options)
    {
        element->add_option(option) ;
    }
    if (!options.empty() && initial_index < options.size())
    {
        element->set_selected_index(initial_index) ;
    }
    prompt::PromptNode node ;
    node.m_id = std::move(node_id) ;
    node.m_element = std::move(element) ;
    node.m_condition = std::move(condition) ;
    node.m_on_submit = std::move(handler) ;
    return node ;
}

/** 构造多选节点。 */
prompt::PromptNode make_multi_node(std::string node_id, std::string title,
    const std::vector<std::string>& options, const std::vector<std::size_t>& default_checked,
    prompt::PromptCondition condition, prompt::PromptSubmitHandler handler)
{
    auto element = std::make_unique<console::MultiSelectElement>() ;
    element->set_title(std::move(title)) ;
    for (const std::string& option : options)
    {
        element->add_option(option) ;
    }
    for (const std::size_t index : default_checked)
    {
        if (index < options.size())
        {
            element->set_checked(index, true) ;
        }
    }
    prompt::PromptNode node ;
    node.m_id = std::move(node_id) ;
    node.m_element = std::move(element) ;
    node.m_condition = std::move(condition) ;
    node.m_on_submit = std::move(handler) ;
    return node ;
}

/** 打印最终配置摘要。 */
void print_configuration(console::ConsoleSession& session, const WizardAnswers& answers,
    const scan::ToolchainSelection& selection, const scan::NinjaTool& ninja)
{
    const auto line = [&session](const std::string& label, const std::string& value)
    {
        session.write_line("  " + core::paint(label, core::color::bright_black) + " " + value) ;
    } ;
    session.write_line(core::paint("—— 最终配置 ——", std::string(core::color::bold) +
        core::color::bright_cyan)) ;
    line("项目名称  :", answers.m_project_name) ;
    line("项目位置  :", core::path_text(project_directory(answers))) ;
    line("模板代码  :", join_module_titles(answers.m_create_templates ? answers.m_template_modules :
        std::vector<template_release::TemplateModule>{})) ;
    line("源码编码  :", answers.m_coding == ctx::Coding::GB2312 ? "GB2312" : "UTF-8") ;
    line("源码行尾  :", answers.m_line_ending == ctx::LineEnding::CRLF ? "CRLF" : "LF") ;
    line("编译器    :", compiler_summary(selection.m_compiler)) ;
    line("NX SDK    :", nx_sdk_summary(selection.m_nx_sdk)) ;
    line("Windows SDK:", windows_sdk_summary(selection.m_windows_sdk)) ;
    line("构建方案  :", build_plan_summary(selection.m_build_plan)) ;
    line("生成器    :", selection.m_generator) ;
    line("Ninja     :", ninja.m_found ? "已检测到 " + ninja.m_version +
        "（settings 写 cmake.generator）" : "未检测到（settings 不写 cmake.generator）") ;
    line("初始化 git:", answers.m_quick_deploy ? "是" : (answers.m_init_git ? "是" : "否")) ;
    session.write_line("") ;
}

/** 打印生成成功后的下一步提示。 */
void print_next_steps(console::ConsoleSession& session, const ProjectBuildResult& result,
    const std::vector<template_release::TemplateModule>& modules)
{
    session.write_line(core::paint("✔ ", std::string(core::color::bold) + core::color::success) +
        result.m_message) ;
    if (result.m_git_ok && !result.m_git_message.empty())
    {
        session.write_line("  " + core::paint("git: ", core::color::bright_black) + result.m_git_message) ;
    }
    else if (!result.m_git_message.empty())
    {
        session.write_line("  " + core::paint("git: ", core::color::bright_black) + result.m_git_message) ;
    }
    session.write_line("") ;
    if (!has_entry_code(modules))
    {
        session.write_line(core::paint("空工程：还没有源文件，所以没有生成 CMakeLists.txt。", core::color::hint)) ;
        return ;
    }
    session.write_line(core::paint("下一步:", std::string(core::color::bold) + core::color::bright_cyan)) ;
    session.write_line("  1. 用 VS Code 打开工程目录: " + core::path_text(result.m_project_directory)) ;
    session.write_line("  2. 命令行构建: cmake --preset nx && cmake --build --preset nx-release") ;
    session.write_line("  3. 产物: build/" + std::string("（与工程同名的 .dll）")) ;
}
} // namespace

int run_main_entry_command(const std::vector<std::string>& arguments)
{
    (void) arguments ;
    app::theme::print_banner(template_main::banner_shadow) ;

    console::ConsoleSession session ;
    if (!session.is_ready())
    {
        std::cerr << "nx_dev: 当前环境无法接管交互控制台（请直接在 Windows 控制台运行）\n" ;
        return 1 ;
    }
    session.write_line(core::paint("NX 2007 插件工程生成向导", std::string(core::color::bold) +
        core::color::bright_cyan)) ;
    session.write_line(app::theme::divider(48)) ;
    session.write_line("") ;
    const int integrity_level = process_integrity_rid() ;
    if (integrity_level >= 0 && integrity_level < SECURITY_MANDATORY_MEDIUM_RID)
    {
        session.write_line(core::paint("⚠ 当前进程完整性级别为 Low（多半是沙箱 / 受限终端）："
            "写入工作区外目录会被 Windows 拒绝（错误码 5），原生文件选择框也可能打不开。"
            "建议在普通 cmd / PowerShell 窗口中运行。", core::color::warning)) ;
        session.write_line("") ;
    }

    // 便携部署提示根：exe 附近或 NX_DEVKIT_DIR 指向的目录交给三域顺带扫描。
    scan::ScanSessionOptions scan_options ;
    scan_options.m_extra_roots = portable_scan_roots() ;
    if (!scan_options.m_extra_roots.empty())
    {
        std::string roots_text ;
        for (const scan::Path& root : scan_options.m_extra_roots)
        {
            roots_text += roots_text.empty() ? "" : "、" ;
            roots_text += core::path_text(root) ;
        }
        session.write_line(core::paint("额外扫描根: " + roots_text, core::color::hint)) ;
    }
    scan::ScanSession scan_session(scan_options) ;
    // 后台扫描线程开跑的同时顺手探测 Ninja；有的话生成工程会写 cmake.generator=Ninja。
    const scan::NinjaTool ninja = scan::detect_ninja() ;
    WizardAnswers answers ;
    bool scan_cancelled = false ;
    const console::ConsoleStatusProvider status_provider =
        [&scan_session, &scan_cancelled]()
        {
            return scan_status_lines(scan_session.state(), scan_cancelled) ;
        } ;
    const prompt::PromptRunner runner =
        [&session, &status_provider](console::ConsoleElement& element)
        {
            return session.run(element, status_provider, 100) ;
        } ;

    // Q0：先问是否自动扫描，回答期间后台三个线程已经在跑。
    {
        console::SingleSelectElement element ;
        element.set_title("是否自动扫描工具链？") ;
        element.add_option("是（后台并行扫描编译器 / NX SDK / Windows SDK）") ;
        element.add_option("否（手动指定三个目录）") ;
        const auto state = session.run(element, status_provider, 100) ;
        if (state != console::PromptState::Submitted)
        {
            scan_session.cancel() ;
            scan_session.wait() ;
            return exit_code_from(state) ;
        }
        answers.m_auto_scan = element.get_selected_index() == 0 ;
    }
    if (!answers.m_auto_scan)
    {
        scan_session.cancel() ;
        scan_session.wait() ;
        scan_cancelled = true ;
        session.set_status(scan_status_lines(scan_session.state(), true)) ;
        if (!ask_manual_toolchains(session, answers))
        {
            return exit_cancelled ;
        }
    }

    // 第一阶段：名称、路径、覆盖、快速部署、模板、git。
    prompt::PromptFlow flow_a(runner) ;
    {
        auto element = std::make_unique<console::InputElement>() ;
        element->set_title("请输入项目名称（默认 NXProject，同时是 DLL 名）") ;
        element->set_default_value(default_project_name) ;
        element->set_value(default_project_name) ;
        element->set_validator(&validate_project_name) ;
        prompt::PromptNode node ;
        node.m_id = "project_name" ;
        node.m_element = std::move(element) ;
        node.m_on_submit = [&answers](prompt::PromptFlow& flow, const prompt::PromptAnswer& answer)
        {
            answers.m_project_name = std::get<std::string>(answer.m_value) ;
            flow.reset_node("overwrite", false) ;
        } ;
        flow_a.append(std::move(node)) ;
    }
    {
        auto element = std::make_unique<console::SelectorElement>(console::FilePickerMode::Directory) ;
        element->set_title("项目路径（父目录；工程建在 <路径>/<项目名>）") ;
        element->set_value(core::path_text(std::filesystem::current_path())) ;
        prompt::PromptNode node ;
        node.m_id = "project_path" ;
        node.m_element = std::move(element) ;
        node.m_on_submit = [&answers](prompt::PromptFlow&, const prompt::PromptAnswer& answer)
        {
            answers.m_parent_directory = std::get<std::filesystem::path>(answer.m_value) ;
        } ;
        flow_a.append(std::move(node)) ;
    }
    flow_a.append(make_select_node("overwrite", "目标目录已存在，是否覆盖？",
        { "是（删掉旧目录，重新生成）", "否（回到项目名称重新输入）" }, 1,
        [&answers](const prompt::PromptFlow&)
        {
            return target_exists(answers) ;
        },
        [&answers](prompt::PromptFlow& flow, const prompt::PromptAnswer& answer)
        {
            const std::size_t index = std::get<std::size_t>(answer.m_value) ;
            answers.m_overwrite = index == 0 ;
            if (answers.m_overwrite)
            {
                return ;
            }
            flow.reset_node("project_name", false) ;
        })) ;
    flow_a.append(make_select_node("quick_deploy", "是否快速部署？",
        { "是（全自动：工具链/SDK 自动选、全量模板、git、配置一键生成）",
          "否（逐步选择模板、编码、行尾、git 与工具链）" }, 0,
        [&answers](const prompt::PromptFlow&)
        {
            return !target_exists(answers) || answers.m_overwrite ;
        },
        [&answers](prompt::PromptFlow&, const prompt::PromptAnswer& answer)
        {
            answers.m_quick_deploy = std::get<std::size_t>(answer.m_value) == 0 ;
        })) ;
    const prompt::PromptCondition normal_path = [&answers](const prompt::PromptFlow&)
    {
        return !answers.m_quick_deploy ;
    } ;
    const prompt::PromptCondition normal_templates = [&answers](const prompt::PromptFlow&)
    {
        return !answers.m_quick_deploy && answers.m_create_templates ;
    } ;
    flow_a.append(make_select_node("create_templates", "是否创建代码模板？",
        { "是", "否（空工程，只生成配置与 git）" }, 0, normal_path,
        [&answers](prompt::PromptFlow&, const prompt::PromptAnswer& answer)
        {
            answers.m_create_templates = std::get<std::size_t>(answer.m_value) == 0 ;
        })) ;
    {
        std::vector<std::string> options ;
        std::vector<std::size_t> default_checked ;
        for (const template_release::TemplateModuleInfo& info : template_release::template_modules())
        {
            std::string option = info.m_title ;
            if (!info.m_hint.empty())
            {
                option += "（" + info.m_hint + "）" ;
            }
            if (info.m_module == template_release::TemplateModule::EntryCode)
            {
                default_checked.push_back(options.size()) ;
            }
            options.push_back(std::move(option)) ;
        }
        flow_a.append(make_multi_node("template_modules", "请选择需要创建的模板代码",
            options, default_checked, normal_templates,
            [&answers, &session](prompt::PromptFlow&, const prompt::PromptAnswer& answer)
            {
                const auto indices = std::get<std::vector<std::size_t>>(answer.m_value) ;
                const auto& infos = template_release::template_modules() ;
                std::vector<template_release::TemplateModule> selected ;
                for (const std::size_t index : indices)
                {
                    if (index < infos.size())
                    {
                        selected.push_back(infos[index].m_module) ;
                    }
                }
                const auto resolved = template_release::resolve_modules(selected) ;
                answers.m_template_modules = resolved ;
                if (resolved.size() > selected.size())
                {
                    std::vector<template_release::TemplateModule> added ;
                    for (const auto module : resolved)
                    {
                        if (std::find(selected.begin(), selected.end(), module) == selected.end())
                        {
                            added.push_back(module) ;
                        }
                    }
                    session.write_line(core::paint("已自动补齐依赖：" + join_module_titles(added),
                        core::color::hint)) ;
                }
            })) ;
    }
    flow_a.append(make_select_node("template_coding", "请选择模板编码",
        { "GB2312（与 NX 2007 工程约定一致）", "UTF-8（编译加 /utf-8）" }, 0, normal_templates,
        [&answers](prompt::PromptFlow&, const prompt::PromptAnswer& answer)
        {
            answers.m_coding = std::get<std::size_t>(answer.m_value) == 0 ?
                ctx::Coding::GB2312 : ctx::Coding::UTF8 ;
        })) ;
    flow_a.append(make_select_node("template_line_ending", "请选择模板换行符",
        { "CRLF（Windows 标准）", "LF" }, 0, normal_templates,
        [&answers](prompt::PromptFlow&, const prompt::PromptAnswer& answer)
        {
            answers.m_line_ending = std::get<std::size_t>(answer.m_value) == 0 ?
                ctx::LineEnding::CRLF : ctx::LineEnding::LF ;
        })) ;
    flow_a.append(make_select_node("init_git", "是否初始化 git？",
        { "是（检测不到 git 会自动跳过）", "否" }, 0, normal_path,
        [&answers](prompt::PromptFlow&, const prompt::PromptAnswer& answer)
        {
            answers.m_init_git = std::get<std::size_t>(answer.m_value) == 0 ;
        })) ;

    session.write_line(core::paint("提示：Enter 确认 · Ctrl+Z（或 Alt+←）返回上一步 · Esc 取消整个向导",
        core::color::hint)) ;
    session.write_line("") ;
    const prompt::PromptRunResult flow_a_result = flow_a.run() ;
    if (flow_a_result.m_state != prompt::PromptFlowState::Completed)
    {
        scan_session.cancel() ;
        scan_session.wait() ;
        if (!flow_a_result.m_message.empty())
        {
            session.write_line(core::paint(flow_a_result.m_message, core::color::failure)) ;
        }
        return exit_code_from(flow_a_result.m_state) ;
    }

    // 收敛后台扫描；普通路径在此时手动挑工具链。
    scan::ScanReport report ;
    if (answers.m_auto_scan)
    {
        if (!wait_for_scan(session, scan_session, scan_cancelled))
        {
            return exit_cancelled ;
        }
        report = scan_session.results() ;
        (void) scan_session.save() ;
    }

    std::vector<scan::Compiler> compilers ;
    std::vector<scan::NXSdk> nx_sdks ;
    std::vector<scan::WindowsSdk> windows_sdks ;
    if (answers.m_auto_scan && !answers.m_quick_deploy)
    {
        for (const scan::Compiler& compiler : report.m_compilers)
        {
            if (compiler.m_is_checked)
            {
                compilers.push_back(compiler) ;
            }
        }
        for (const scan::NXSdk& sdk : report.m_sdks)
        {
            if (sdk.m_is_checked)
            {
                nx_sdks.push_back(sdk) ;
            }
        }
        for (const scan::WindowsSdk& sdk : report.m_windows_sdks)
        {
            if (sdk.m_is_checked)
            {
                windows_sdks.push_back(sdk) ;
            }
        }
        scan::sort_compilers(compilers) ;
        scan::sort_nx_sdks(nx_sdks) ;
        scan::sort_windows_sdks(windows_sdks) ;
        if (compilers.empty() || nx_sdks.empty())
        {
            session.write_line(core::paint(compilers.empty() ? "扫描没有找到可用的编译器工具链"
                : "扫描没有找到可用的 NX SDK", core::color::failure)) ;
            session.write_line(core::paint("请先在“是否自动扫描工具链”里选“否”，手动指定目录。",
                core::color::hint)) ;
            return exit_cancelled ;
        }

        const scan::SelectionMemory memory = scan::load_selection_memory() ;
        const prompt::PromptCondition toolchain_stage = [&answers](const prompt::PromptFlow&)
        {
            return answers.m_auto_scan && !answers.m_quick_deploy ;
        } ;
        {
            std::vector<std::string> options ;
            for (const scan::Compiler& compiler : compilers)
            {
                options.push_back(compiler_option_text(compiler)) ;
            }
            flow_a.append(make_select_node("compiler", "请选择编译器工具链", options,
                scan::find_compiler_index(compilers, memory), toolchain_stage,
                [&answers](prompt::PromptFlow&, const prompt::PromptAnswer& answer)
                {
                    answers.m_compiler_index = std::get<std::size_t>(answer.m_value) ;
                })) ;
        }
        {
            std::vector<std::string> options ;
            for (const scan::NXSdk& sdk : nx_sdks)
            {
                options.push_back(nx_sdk_option_text(sdk)) ;
            }
            flow_a.append(make_select_node("nx_sdk", "请选择 NX SDK", options,
                scan::find_nx_sdk_index(nx_sdks, memory), toolchain_stage,
                [&answers](prompt::PromptFlow&, const prompt::PromptAnswer& answer)
                {
                    answers.m_nx_sdk_index = std::get<std::size_t>(answer.m_value) ;
                })) ;
        }
        if (!windows_sdks.empty())
        {
            std::vector<std::string> options ;
            for (const scan::WindowsSdk& sdk : windows_sdks)
            {
                options.push_back(windows_sdk_option_text(sdk)) ;
            }
            flow_a.append(make_select_node("windows_sdk", "请选择 Windows SDK", options,
                scan::find_windows_sdk_index(windows_sdks, memory), toolchain_stage,
                [&answers](prompt::PromptFlow&, const prompt::PromptAnswer& answer)
                {
                    answers.m_windows_sdk_index = std::get<std::size_t>(answer.m_value) ;
                })) ;
        }
        // 工具链节点接在同一个流程后面继续跑；Ctrl+Z 能退回前面的步骤。
        const prompt::PromptRunResult flow_result = flow_a.run() ;
        if (flow_result.m_state != prompt::PromptFlowState::Completed)
        {
            if (!flow_result.m_message.empty())
            {
                session.write_line(core::paint(flow_result.m_message, core::color::failure)) ;
            }
            return exit_code_from(flow_result.m_state) ;
        }
    }

    // 解析最终选型。
    scan::ToolchainSelection selection ;
    if (!answers.m_auto_scan)
    {
        selection.m_compiler = answers.m_manual_compiler ;
        selection.m_nx_sdk = answers.m_manual_nx_sdk ;
        selection.m_windows_sdk = answers.m_manual_windows_sdk ;
        selection.m_build_plan = scan::resolve_build_plan(selection.m_compiler, ninja.m_found) ;
        selection.m_generator = selection.m_build_plan.m_generator ;
        selection.m_ok = answers.m_manual_compiler_found && selection.m_build_plan.m_ok ;
        if (!selection.m_ok)
        {
            selection.m_message = selection.m_build_plan.m_message.empty() ?
                "无法从手动指定的编译器推导构建方案。" : selection.m_build_plan.m_message ;
        }
    }
    else if (answers.m_quick_deploy)
    {
        selection = scan::pick_toolchain(report, ninja.m_found) ;
    }
    else
    {
        selection.m_compiler = compilers[answers.m_compiler_index] ;
        selection.m_nx_sdk = nx_sdks[answers.m_nx_sdk_index] ;
        if (!windows_sdks.empty() && answers.m_windows_sdk_index < windows_sdks.size())
        {
            selection.m_windows_sdk = windows_sdks[answers.m_windows_sdk_index] ;
        }
        selection.m_build_plan = scan::resolve_build_plan(selection.m_compiler, ninja.m_found) ;
        selection.m_generator = selection.m_build_plan.m_generator ;
        selection.m_ok = selection.m_build_plan.m_ok ;
        if (!selection.m_ok)
        {
            selection.m_message = selection.m_build_plan.m_message.empty() ?
                "无法从所选编译器推导构建方案。" : selection.m_build_plan.m_message ;
        }
    }
    // 自动生成 toolchain 的方案必须有完整的 Windows SDK；缺了就提前说清楚。
    if (selection.m_ok && selection.m_build_plan.needs_generated_toolchain() &&
        (selection.m_windows_sdk.m_include_dir.empty() || selection.m_windows_sdk.m_lib_dir.empty()))
    {
        selection.m_ok = false ;
        selection.m_message = "该编译器没有现成的 toolchain 文件，需要自动生成一份；"
            "但当前没有完整的 Windows SDK 选型（缺 include/lib）。请在手动模式下补选 Windows SDK，"
            "或改用带 vcvarsall.bat / *toolchain*.cmake 的编译器目录。" ;
    }
    if (!selection.m_ok)
    {
        session.write_line(core::paint(selection.m_message.empty() ? "工具链选型失败" :
            selection.m_message, core::color::failure)) ;
        return exit_cancelled ;
    }
    if (!selection.m_warning.empty())
    {
        session.write_line(core::paint("警告: " + selection.m_warning, core::color::warning)) ;
    }

    // 快速部署的默认值：全量模板、GB2312、CRLF、初始化 git。
    if (answers.m_quick_deploy)
    {
        answers.m_create_templates = true ;
        answers.m_template_modules = template_release::resolve_modules(all_template_modules()) ;
        answers.m_coding = ctx::Coding::GB2312 ;
        answers.m_line_ending = ctx::LineEnding::CRLF ;
        answers.m_init_git = true ;
    }

    print_configuration(session, answers, selection, ninja) ;
    {
        console::SingleSelectElement element ;
        element.set_title("是否确定生成项目？") ;
        element.add_option("是，开始生成") ;
        element.add_option("否，退出不生成") ;
        const auto state = session.run(element, status_provider, 100) ;
        if (state != console::PromptState::Submitted)
        {
            return exit_code_from(state) ;
        }
        if (element.get_selected_index() != 0)
        {
            session.write_line(core::paint("已放弃，未生成任何文件", core::color::warning)) ;
            return exit_cancelled ;
        }
    }

    ProjectBuildRequest request ;
    request.m_parent_directory = answers.m_parent_directory ;
    request.m_project_name = answers.m_project_name ;
    request.m_overwrite = answers.m_overwrite ;
    request.m_template_modules = answers.m_create_templates ? answers.m_template_modules :
        std::vector<template_release::TemplateModule>{} ;
    request.m_coding = answers.m_coding ;
    request.m_line_ending = answers.m_line_ending ;
    request.m_init_git = answers.m_init_git ;
    request.m_compiler = selection.m_compiler ;
    request.m_nx_sdk = selection.m_nx_sdk ;
    request.m_windows_sdk = selection.m_windows_sdk ;
    request.m_generator = selection.m_generator ;
    request.m_build_plan = selection.m_build_plan ;
    request.m_ninja_available = ninja.m_found ;

    const ProjectBuildResult build_result = build_project(request) ;
    if (!build_result.m_ok)
    {
        session.write_line(core::paint("生成失败: " + build_result.m_message, core::color::failure)) ;
        return exit_cancelled ;
    }
    print_next_steps(session, build_result, request.m_template_modules) ;

    if (answers.m_auto_scan && !answers.m_quick_deploy)
    {
        scan::SelectionMemory memory ;
        memory.m_compiler_key = core::path_text(selection.m_compiler.m_cl_path) ;
        memory.m_nx_sdk_key = core::path_text(selection.m_nx_sdk.m_root) ;
        memory.m_windows_sdk_key = selection.m_windows_sdk.m_version ;
        (void) scan::save_selection_memory(memory) ;
    }
    return 0 ;
}
} // namespace app
