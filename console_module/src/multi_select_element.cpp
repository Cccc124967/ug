#include "console_detail.h"
#include <algorithm>

namespace console
{
MultiSelectElement::MultiSelectElement()
{
    m_type = ConsoleType::MultiSelect ;
}

ConsoleFrame MultiSelectElement::render(std::size_t columns) const
{
    (void) columns ;
    ConsoleFrame frame ;
    frame.m_lines.push_back(title_line()) ;
    for (std::size_t index = 0 ; index < m_options.size() ; ++index)
    {
        const bool active = index == m_selected_index ;
        const bool checked = m_checked[index] ;
        const auto& color = active ? m_selected_color : m_unselected_color ;
        frame.m_lines.push_back(detail::cursor_prefix(active, m_selected_color) +
            (checked ? core::color::checked : core::color::unchecked) +
            (checked ? core::checked_box : core::unchecked_box) + core::color::reset +
            core::checkbox_space + color + m_options[index] + core::color::reset) ;
    }
    if (m_options.empty())
    {
        frame.m_lines.push_back(std::string(core::color::warning) + "（暂无选项，Esc 返回）" + core::color::reset) ;
    }
    frame.m_lines.push_back(std::string(core::color::hint) + "↑↓ 移动 · 空格 勾选/取消 · Enter 确认 · Esc 取消" + core::color::reset) ;
    frame.m_focus_line = m_options.empty() ? 1 : m_selected_index + 1 ;
    return frame ;
}

bool MultiSelectElement::handle_key(int key)
{
    if (m_state == PromptState::Editing && key == keyboard::space && !m_options.empty())
    {
        m_checked[m_selected_index] = !m_checked[m_selected_index] ;
        return true ;
    }
    return SingleSelectElement::handle_key(key) ;
}

void MultiSelectElement::reset()
{
    std::fill(m_checked.begin(), m_checked.end(), false) ;
    SingleSelectElement::reset() ;
}

void MultiSelectElement::add_option(const std::string& option)
{
    m_checked.reserve(m_options.size() + 1) ;
    SingleSelectElement::add_option(option) ;
    m_checked.push_back(false) ;
}

void MultiSelectElement::remove_option(std::size_t index)
{
    SingleSelectElement::remove_option(index) ;
    m_checked.erase(m_checked.begin() + static_cast<std::ptrdiff_t>(index)) ;
}

void MultiSelectElement::clear_options()
{
    SingleSelectElement::clear_options() ;
    m_checked.clear() ;
}

void MultiSelectElement::swap_options(std::size_t first, std::size_t second)
{
    SingleSelectElement::swap_options(first, second) ;
    const bool checked = m_checked[first] ;
    m_checked[first] = m_checked[second] ;
    m_checked[second] = checked ;
}

void MultiSelectElement::set_checked(std::size_t index, bool checked)
{
    m_checked.at(index) = checked ;
    begin() ;
}

bool MultiSelectElement::is_checked(std::size_t index) const
{
    return m_checked.at(index) ;
}

std::vector<std::size_t> MultiSelectElement::get_selected_indices() const
{
    std::vector<std::size_t> result ;
    for (std::size_t index = 0 ; index < m_checked.size() ; ++index)
    {
        if (m_checked[index])
        {
            result.push_back(index) ;
        }
    }
    return result ;
}

std::string MultiSelectElement::result_text() const
{
    std::string result ;
    for (const auto index : get_selected_indices())
    {
        if (!result.empty())
        {
            result += "、" ;
        }
        result += m_options[index] ;
    }
    return result.empty() ? "（未选择）" : result ;
}
} // namespace console
