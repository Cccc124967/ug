#include "console_detail.h"
#include <iostream>

namespace console::detail
{
std::string safe_text(const std::string& text)
{
    auto characters = core::utf8_to_utf32(text) ;
    for (auto& code_point : characters)
    {
        if (code_point < 32 || (code_point >= 0x7F && code_point < 0xA0))
        {
            code_point = U' ' ;
        }
    }
    return core::utf32_to_utf8(characters) ;
}

/** 遍历带颜色的文本，截取指定列数或去色。 */
std::string filter_text(const std::string& text, std::size_t columns, bool keep_color)
{
    const auto characters = core::utf8_to_utf32(text) ;
    std::u32string result ;
    std::size_t used = 0 ;
    for (std::size_t index = 0 ; index < characters.size() ; ++index)
    {
        if (characters[index] == U'\033' && index + 1 < characters.size() && characters[index + 1] == U'[')
        {
            const std::size_t first = index ;
            index += 2 ;
            while (index < characters.size() && !(characters[index] >= 0x40 && characters[index] <= 0x7E))
            {
                ++index ;
            }
            if (keep_color && index < characters.size() && characters[index] == U'm')
            {
                result.append(characters, first, index - first + 1) ;
            }
            continue ;
        }
        const auto width = core::console_cell_width(characters[index]) ;
        if (width > columns - used)
        {
            break ;
        }
        result.push_back(characters[index]) ;
        used += width ;
    }
    return core::utf32_to_utf8(result) ;
}

std::string plain_text(const std::string& text)
{
    return filter_text(text, std::string::npos, false) ;
}

std::size_t display_width(const std::string& text)
{
    std::size_t width = 0 ;
    for (const auto code_point : core::utf8_to_utf32(plain_text(text)))
    {
        width += core::console_cell_width(code_point) ;
    }
    return width ;
}

std::string clip_text(const std::string& text, std::size_t columns)
{
    return filter_text(text, columns, true) + core::color::reset ;
}

std::string cursor_prefix(bool active, const std::string& color)
{
    if (active)
    {
        return color + core::select_arrow + core::color::reset + core::select_space ;
    }
    return core::unselect_space ;
}
} // namespace console::detail

namespace console
{
ConsoleElement::ConsoleElement(ConsoleType type) : m_type(type)
{
}

void ConsoleElement::set_title(const std::string& title)
{
    m_title = detail::safe_text(title) ;
}

void ConsoleElement::set_title_color(const char* color)
{
    m_title_color = color ? color : core::color::title ;
}

const std::string& ConsoleElement::get_title() const noexcept
{
    return m_title ;
}

const std::string& ConsoleElement::get_title_color() const noexcept
{
    return m_title_color ;
}

ConsoleType ConsoleElement::get_type() const noexcept
{
    return m_type ;
}

PromptState ConsoleElement::get_state() const noexcept
{
    return m_state ;
}

void ConsoleElement::begin()
{
    m_state = PromptState::Editing ;
}

void ConsoleElement::draw() const
{
    for (const auto& line : render().m_lines)
    {
        std::cout << line << core::color::reset << '\n' ;
    }
    std::cout.flush() ;
}

bool ConsoleElement::handle_common_key(int key)
{
    if (key == keyboard::back)
    {
        m_state = PromptState::Back ;
        return true ;
    }
    if (key == keyboard::escape || key == keyboard::interrupt || key == keyboard::unavailable)
    {
        m_state = key == keyboard::escape ? PromptState::Cancelled :
            (key == keyboard::interrupt ? PromptState::Interrupted : PromptState::Unavailable) ;
        return true ;
    }
    return false ;
}

std::string ConsoleElement::title_line() const
{
    return m_title_color + m_title + core::color::reset ;
}
} // namespace console
