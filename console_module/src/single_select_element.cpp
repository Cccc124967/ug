#include "console_detail.h"
#include <algorithm>
#include <stdexcept>

namespace console
{
SingleSelectElement::SingleSelectElement() : ConsoleElement(ConsoleType::SingleSelect)
{
}

ConsoleFrame SingleSelectElement::render(std::size_t columns) const
{
    (void) columns ;
    ConsoleFrame frame ;
    frame.m_lines.push_back(title_line()) ;
    for (std::size_t index = 0 ; index < m_options.size() ; ++index)
    {
        const bool active = index == m_selected_index ;
        const auto& color = active ? m_selected_color : m_unselected_color ;
        frame.m_lines.push_back(detail::cursor_prefix(active, m_selected_color) +
            color + m_options[index] + core::color::reset) ;
    }
    if (m_options.empty())
    {
        frame.m_lines.push_back(std::string(core::color::warning) + "（暂无选项，Esc 返回）" + core::color::reset) ;
    }
    frame.m_lines.push_back(std::string(core::color::hint) + "↑↓ 选择 · Home/End 首尾 · Enter 确认 · Esc 取消" + core::color::reset) ;
    frame.m_focus_line = m_options.empty() ? 1 : m_selected_index + 1 ;
    return frame ;
}

bool SingleSelectElement::handle_key(int key)
{
    if (m_state != PromptState::Editing)
    {
        return false ;
    }
    if (handle_common_key(key))
    {
        return true ;
    }
    if (m_options.empty())
    {
        return false ;
    }
    switch (key)
    {
        case keyboard::up:
            if (m_selected_index > 0)
            {
                --m_selected_index ;
            }
            return true ;
        case keyboard::down:
            if (m_selected_index + 1 < m_options.size())
            {
                ++m_selected_index ;
            }
            return true ;
        case keyboard::home:
            m_selected_index = 0 ;
            return true ;
        case keyboard::end:
            m_selected_index = m_options.size() - 1 ;
            return true ;
        case keyboard::enter:
            m_state = PromptState::Submitted ;
            return true ;
        default:
            return false ;
    }
}

void SingleSelectElement::reset()
{
    m_selected_index = 0 ;
    begin() ;
}

void SingleSelectElement::add_option(const std::string& option)
{
    m_options.push_back(detail::safe_text(option)) ;
    begin() ;
}

void SingleSelectElement::remove_option(std::size_t index)
{
    (void) m_options.at(index) ;
    m_options.erase(m_options.begin() + static_cast<std::ptrdiff_t>(index)) ;
    if (index < m_selected_index)
    {
        --m_selected_index ;
    }
    m_selected_index = m_options.empty() ? 0 : std::min(m_selected_index, m_options.size() - 1) ;
    begin() ;
}

void SingleSelectElement::clear_options()
{
    m_options.clear() ;
    m_selected_index = 0 ;
    begin() ;
}

void SingleSelectElement::swap_options(std::size_t first, std::size_t second)
{
    // 先验证两个下标，避免出错时只改了一半。
    (void) m_options.at(first) ;
    (void) m_options.at(second) ;
    std::swap(m_options[first], m_options[second]) ;
    if (m_selected_index == first)
    {
        m_selected_index = second ;
    }
    else if (m_selected_index == second)
    {
        m_selected_index = first ;
    }
    begin() ;
}

std::string SingleSelectElement::find_option(std::size_t index) const
{
    return m_options.at(index) ;
}

void SingleSelectElement::set_selected_index(std::size_t index)
{
    (void) m_options.at(index) ;
    m_selected_index = index ;
    begin() ;
}

std::size_t SingleSelectElement::get_selected_index() const noexcept
{
    return m_options.empty() ? std::string::npos : m_selected_index ;
}

const std::vector<std::string>& SingleSelectElement::get_options() const noexcept
{
    return m_options ;
}

void SingleSelectElement::set_selected_color(const char* color)
{
    m_selected_color = color ? color : core::color::selected ;
}

void SingleSelectElement::set_unselected_color(const char* color)
{
    m_unselected_color = color ? color : core::color::unselected ;
}

std::string SingleSelectElement::result_text() const
{
    return m_options.empty() ? std::string() : m_options[m_selected_index] ;
}
} // namespace console
