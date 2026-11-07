#include "console_detail.h"
#include <exception>
#include <utility>

namespace console
{
namespace
{
/** 进程内最近一次选择的目录；F2 打开时优先从它开始。 */
std::filesystem::path& recent_directory_state()
{
    static std::filesystem::path path ;
    return path ;
}

/** 规范成已存在的绝对目录；不是目录或路径为空时返回空。 */
std::filesystem::path existing_directory(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return {} ;
    }
    std::error_code error ;
    std::filesystem::path absolute = std::filesystem::absolute(path, error) ;
    if (error)
    {
        absolute = path ;
    }
    if (!std::filesystem::is_directory(absolute, error) || error)
    {
        return {} ;
    }
    return absolute.lexically_normal() ;
}

/** 记住选择器结果：目录本身，或文件所在的目录。 */
void remember_selector_path(const std::filesystem::path& path)
{
    const std::filesystem::path directory = existing_directory(path) ;
    if (!directory.empty())
    {
        recent_directory_state() = directory ;
        return ;
    }
    const std::filesystem::path parent = existing_directory(path.parent_path()) ;
    if (!parent.empty())
    {
        recent_directory_state() = parent ;
    }
}
} // namespace

std::filesystem::path recent_selector_directory()
{
    return recent_directory_state() ;
}

void set_recent_selector_directory(const std::filesystem::path& directory)
{
    if (directory.empty())
    {
        recent_directory_state().clear() ;
        return ;
    }
    const std::filesystem::path existing = existing_directory(directory) ;
    if (!existing.empty())
    {
        recent_directory_state() = existing ;
    }
}

SelectorElement::SelectorElement(FilePickerMode mode) : m_picker(pick_file)
{
    m_type = ConsoleType::Selector ;
    m_picker_options.m_mode = mode ;
    m_allow_empty = false ;
}

bool SelectorElement::handle_key(int key)
{
    if (key != keyboard::browse || m_state != PromptState::Editing)
    {
        return InputElement::handle_key(key) ;
    }
    try
    {
        auto options = m_picker_options ;
        options.m_title = m_title ;
        // F2 起点优先级：上次选过的目录 > 当前输入值（文件取父目录）> 预设起始目录 > 当前工作目录。
        std::error_code error ;
        options.m_initial_directory = recent_selector_directory() ;
        const auto current = get_path() ;
        if (options.m_initial_directory.empty())
        {
            if (!current.empty() && std::filesystem::is_directory(current, error))
            {
                options.m_initial_directory = current ;
            }
            else if (!current.parent_path().empty() &&
                std::filesystem::is_directory(current.parent_path(), error))
            {
                options.m_initial_directory = current.parent_path() ;
            }
        }
        if (options.m_initial_directory.empty())
        {
            options.m_initial_directory = m_picker_options.m_initial_directory ;
        }
        if (options.m_initial_directory.empty())
        {
            options.m_initial_directory = std::filesystem::current_path(error) ;
        }
        const auto picked = m_picker(options) ;
        if (picked.m_status == FilePickerStatus::Selected)
        {
            if (picked.m_path.empty())
            {
                m_error = "选择器没有返回有效路径" ;
            }
            else
            {
                set_value(core::wide_to_utf8(picked.m_path.wstring())) ;
                remember_selector_path(picked.m_path) ;
            }
        }
        else if (picked.m_status == FilePickerStatus::Failed)
        {
            m_error = picked.m_error.empty() ? "无法打开选择器" : picked.m_error ;
        }
        // 取消原生窗口时，原输入与编辑光标原样保留。
    }
    catch (const std::exception& error)
    {
        m_error = std::string("选择路径失败：") + error.what() ;
    }
    return true ;
}

void SelectorElement::set_mode(FilePickerMode mode)
{
    m_picker_options.m_mode = mode ;
    m_error.clear() ;
    begin() ;
}

void SelectorElement::set_initial_directory(const std::filesystem::path& directory)
{
    m_picker_options.m_initial_directory = directory ;
}

void SelectorElement::set_filters(std::vector<FilePickerFilter> filters)
{
    m_picker_options.m_filters = std::move(filters) ;
}

void SelectorElement::set_picker(FilePicker picker)
{
    m_picker = picker ? std::move(picker) : FilePicker(pick_file) ;
}

std::filesystem::path SelectorElement::get_path() const
{
    auto text = get_value() ;
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
    {
        text = text.substr(1, text.size() - 2) ;
    }
    return std::filesystem::path(core::utf8_to_wide(text)) ;
}

std::optional<std::string> SelectorElement::validate() const
{
    if (get_path().empty())
    {
        return "路径不能为空" ;
    }
    std::error_code error ;
    const bool valid = m_picker_options.m_mode == FilePickerMode::Directory ?
        std::filesystem::is_directory(get_path(), error) : std::filesystem::is_regular_file(get_path(), error) ;
    if (!valid || error)
    {
        return m_picker_options.m_mode == FilePickerMode::Directory ?
            "请选择已存在的文件夹" : "请选择已存在的文件" ;
    }
    // 手工输入并通过校验的路径也进入缓存，下一次 F2 从它开始。
    remember_selector_path(get_path()) ;
    return InputElement::validate() ;
}

std::string SelectorElement::hint_text() const
{
    return "直接输入路径 · F2 打开系统选择框（默认上次打开的位置） · Enter 确认 · Esc 取消" ;
}
} // namespace console
