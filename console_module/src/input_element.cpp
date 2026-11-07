#include "console_detail.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace console
{
InputElement::InputElement() : ConsoleElement(ConsoleType::Input)
{
}

ConsoleFrame InputElement::render(std::size_t columns) const
{
    ConsoleFrame frame ;
    frame.m_lines.push_back(title_line()) ;
    columns = std::max<std::size_t>(columns, 1) ;
    auto prefix = detail::cursor_prefix(true, core::color::selected) ;
    auto prefix_width = detail::display_width(prefix) ;
    if (columns <= prefix_width)
    {
        prefix.clear() ;
        prefix_width = 0 ;
    }
    const auto available = columns - prefix_width ;
    // 预留一个光标列；过长输入仅横向滚动，不限制用户的文本长度。
    std::size_t first = m_cursor_index ;
    std::size_t cursor_columns = 0 ;
    while (first > 0 && core::console_cell_width(m_value[first - 1]) <= available - 1 - cursor_columns)
    {
        cursor_columns += core::console_cell_width(m_value[--first]) ;
    }
    std::size_t last = first ;
    std::size_t used = 0 ;
    while (last < m_value.size() && core::console_cell_width(m_value[last]) <= available - used)
    {
        used += core::console_cell_width(m_value[last++]) ;
    }
    frame.m_lines.push_back(prefix + core::color::input +
        core::utf32_to_utf8(std::u32string_view(m_value).substr(first, last - first)) + core::color::reset) ;
    frame.m_lines.push_back(std::string(core::color::hint) + hint_text() + core::color::reset) ;
    if (!m_error.empty())
    {
        frame.m_lines.push_back(std::string(core::color::failure) + detail::safe_text(m_error) + core::color::reset) ;
    }
    frame.m_focus_line = 1 ;
    frame.m_cursor = ConsoleCursorPosition{ 1, prefix_width + cursor_columns } ;
    return frame ;
}

bool InputElement::handle_key(int key)
{
    if (m_state != PromptState::Editing)
    {
        return false ;
    }
    if (handle_common_key(key))
    {
        return true ;
    }
    switch (key)
    {
        case keyboard::enter:
        {
            const auto error = validate() ;
            m_error = error.value_or("") ;
            if (!error)
            {
                m_state = PromptState::Submitted ;
            }
            return true ;
        }
        case keyboard::left:
            if (m_cursor_index > 0)
            {
                --m_cursor_index ;
            }
            return true ;
        case keyboard::right:
            m_cursor_index = std::min(m_cursor_index + 1, m_value.size()) ;
            return true ;
        case keyboard::home:
            m_cursor_index = 0 ;
            return true ;
        case keyboard::end:
            m_cursor_index = m_value.size() ;
            return true ;
        case keyboard::backspace:
            if (m_cursor_index > 0)
            {
                m_value.erase(--m_cursor_index, 1) ;
            }
            break ;
        case keyboard::delete_key:
            if (m_cursor_index < m_value.size())
            {
                m_value.erase(m_cursor_index, 1) ;
            }
            break ;
        default:
            if (key < 32 || key > 0x10FFFF || (key >= 0x7F && key < 0xA0) ||
                (key >= 0xD800 && key <= 0xDFFF))
            {
                return false ;
            }
            m_value.insert(m_cursor_index++, 1, static_cast<char32_t>(key)) ;
            break ;
    }
    m_error.clear() ;
    return true ;
}

void InputElement::reset()
{
    m_value = m_default_value ;
    m_cursor_index = m_value.size() ;
    m_error.clear() ;
    begin() ;
}

void InputElement::set_value(const std::string& value)
{
    auto characters = core::utf8_to_utf32(value) ;
    for (const auto code_point : characters)
    {
        if (code_point < 32 || (code_point >= 0x7F && code_point < 0xA0))
        {
            throw std::invalid_argument("Input must be a single line without control characters") ;
        }
    }
    m_value = std::move(characters) ;
    m_cursor_index = m_value.size() ;
    m_error.clear() ;
    begin() ;
}

void InputElement::set_default_value(const std::string& value)
{
    set_value(value) ;
    m_default_value = m_value ;
}

std::string InputElement::get_value() const
{
    return core::utf32_to_utf8(m_value) ;
}

std::size_t InputElement::get_cursor_index() const noexcept
{
    return m_cursor_index ;
}

void InputElement::set_validator(InputValidator validator)
{
    m_validator = std::move(validator) ;
    m_error.clear() ;
    begin() ;
}

void InputElement::set_allow_empty(bool allow)
{
    m_allow_empty = allow ;
    m_error.clear() ;
    begin() ;
}

const std::string& InputElement::get_error() const noexcept
{
    return m_error ;
}

std::string InputElement::result_text() const
{
    return get_value() ;
}

std::optional<std::string> InputElement::validate() const
{
    if (!m_allow_empty && m_value.empty())
    {
        return "内容不能为空" ;
    }
    return m_validator ? m_validator(get_value()) : std::nullopt ;
}

std::string InputElement::hint_text() const
{
    return "直接输入 · ←→ 编辑 · Home/End 首尾 · Enter 确认 · Esc 取消" ;
}
} // namespace console
