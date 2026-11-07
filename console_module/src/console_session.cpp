#include "console_detail.h"
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace console::detail
{
ConsoleFrame fit_frame(ConsoleFrame frame, std::size_t rows)
{
    rows = std::max<std::size_t>(rows, 1) ;
    if (frame.m_lines.size() <= rows)
    {
        return frame ;
    }
    ConsoleFrame fitted ;
    std::vector<std::size_t> indices ;
    const auto focus = std::min(frame.m_focus_line, frame.m_lines.size() - 1) ;
    if (rows < 3)
    {
        const auto first = focus > 0 && rows == 2 ? focus - 1 : focus ;
        for (std::size_t index = first ; index < frame.m_lines.size() && indices.size() < rows ; ++index)
        {
            indices.push_back(index) ;
        }
    }
    else
    {
        indices.push_back(0) ;
        const std::size_t count = rows - 2 ;
        const auto first = std::clamp(focus > count / 2 ? focus - count / 2 : 1,
            std::size_t(1), frame.m_lines.size() - 1 - count) ;
        for (std::size_t index = first ; index < first + count ; ++index)
        {
            indices.push_back(index) ;
        }
        indices.push_back(frame.m_lines.size() - 1) ;
    }
    for (const auto index : indices)
    {
        if (index == focus)
        {
            fitted.m_focus_line = fitted.m_lines.size() ;
        }
        if (frame.m_cursor && frame.m_cursor->m_line == index)
        {
            fitted.m_cursor = ConsoleCursorPosition{ fitted.m_lines.size(), frame.m_cursor->m_column } ;
        }
        fitted.m_lines.push_back(std::move(frame.m_lines[index])) ;
    }
    return fitted ;
}

ConsoleStatusPlan plan_status_rows(std::size_t requested, std::size_t previous, std::size_t capacity,
    bool sealed) noexcept
{
    if (sealed)
    {
        // 密封后下方已有常驻输出，只维持既有高度，多余状态行丢弃。
        const std::size_t area = std::min(previous, capacity) ;
        return ConsoleStatusPlan{ area, std::min(requested, area) } ;
    }
    const std::size_t rows = std::min(requested, capacity) ;
    return ConsoleStatusPlan{ rows, rows } ;
}

std::string clip_status_line(const std::string& line, std::size_t columns)
{
    const auto characters = core::utf8_to_utf32(line) ;
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
            if (index < characters.size() && characters[index] == U'm')
            {
                result.append(characters, first, index - first + 1) ;
            }
            continue ;
        }
        char32_t code_point = characters[index] ;
        if (code_point < 32 || (code_point >= 0x7F && code_point < 0xA0))
        {
            code_point = U' ' ;
        }
        const auto width = core::console_cell_width(code_point) ;
        if (width > columns - used)
        {
            break ;
        }
        result.push_back(code_point) ;
        used += width ;
    }
    return core::utf32_to_utf8(result) + core::color::reset ;
}

std::vector<std::string> render_status_lines(const std::vector<std::string>& lines, std::size_t columns,
    std::size_t capacity)
{
    const std::size_t count = std::min(lines.size(), capacity) ;
    std::vector<std::string> rendered ;
    rendered.reserve(count) ;
    for (std::size_t index = 0 ; index < count ; ++index)
    {
        rendered.push_back(clip_status_line(lines[index], columns)) ;
    }
    return rendered ;
}

std::size_t status_capacity(std::size_t window_rows) noexcept
{
    return window_rows > 1 ? window_rows - 1 : 0 ;
}
} // namespace console::detail

namespace console
{
struct ConsoleRuntime
{
    HANDLE m_input = GetStdHandle(STD_INPUT_HANDLE) ;
    HANDLE m_output = GetStdHandle(STD_OUTPUT_HANDLE) ;
    DWORD m_input_mode = 0 ;
    DWORD m_output_mode = 0 ;
    UINT m_input_code_page = 0 ;
    UINT m_output_code_page = 0 ;
    CONSOLE_CURSOR_INFO m_cursor_info = {} ;
    WORD m_attributes = 0 ;
    bool m_saved = false ;
    bool m_ready = false ;
    int m_repeat_key = 0 ;
    unsigned m_repeat_count = 0 ;
    char32_t m_high_surrogate = 0 ;

    /** 状态区锚点行是否已建立；建立后跨 run 复用。 */
    bool m_status_active = false ;
    /** 状态区首行的缓冲区行号。 */
    SHORT m_status_anchor = 0 ;
    /** 状态区当前占用的行数；密封后不再增长。 */
    std::size_t m_status_rows = 0 ;
    /** 上一次渲染的状态行（已裁剪、带颜色），用于跳过重复刷新。 */
    std::vector<std::string> m_status_lines ;
    /** 状态区下方是否已有常驻输出；密封后不能向下扩展。 */
    bool m_status_sealed = false ;

    ConsoleRuntime()
    {
        CONSOLE_SCREEN_BUFFER_INFO info = {} ;
        if (!GetConsoleMode(m_input, &m_input_mode) || !GetConsoleMode(m_output, &m_output_mode) ||
            !GetConsoleCursorInfo(m_output, &m_cursor_info) || !GetConsoleScreenBufferInfo(m_output, &info))
        {
            return ;
        }
        m_attributes = info.wAttributes ;
        m_input_code_page = GetConsoleCP() ;
        m_output_code_page = GetConsoleOutputCP() ;
        m_saved = true ;
        const DWORD input_mode = (m_input_mode | ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT) &
            ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE |
              ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_MOUSE_INPUT) ;
        m_ready = SetConsoleMode(m_output, m_output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
            ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT) &&
            SetConsoleMode(m_input, input_mode) && SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8) ;
    }

    ~ConsoleRuntime()
    {
        if (m_saved)
        {
            SetConsoleTextAttribute(m_output, m_attributes) ;
            SetConsoleCursorInfo(m_output, &m_cursor_info) ;
            SetConsoleMode(m_input, m_input_mode) ;
            SetConsoleMode(m_output, m_output_mode) ;
            SetConsoleCP(m_input_code_page) ;
            SetConsoleOutputCP(m_output_code_page) ;
        }
    }

    /** 写入 Unicode 文本与 VT 控制序列。 */
    void write(const std::string& text) const
    {
        const auto wide = core::utf8_to_wide(text) ;
        std::size_t offset = 0 ;
        while (offset < wide.size())
        {
            DWORD written = 0 ;
            const auto count = static_cast<DWORD>(std::min<std::size_t>(wide.size() - offset, 16384)) ;
            if (!WriteConsoleW(m_output, wide.data() + offset, count, &written, nullptr) || written == 0)
            {
                throw std::runtime_error("Console output is unavailable") ;
            }
            offset += written ;
        }
    }

    /** 读取当前终端大小及位置。 */
    CONSOLE_SCREEN_BUFFER_INFO screen() const
    {
        CONSOLE_SCREEN_BUFFER_INFO info = {} ;
        if (!GetConsoleScreenBufferInfo(m_output, &info))
        {
            throw std::runtime_error("Console screen is unavailable") ;
        }
        return info ;
    }

    /** 移动到控制台缓冲区中的指定坐标。 */
    void move_to(SHORT x, SHORT y) const
    {
        if (!SetConsoleCursorPosition(m_output, COORD{ x, y }))
        {
            throw std::runtime_error("Console cursor is unavailable") ;
        }
    }

    /** 在菜单隐藏光标，在输入框显示 core 定义的光标样式。 */
    void show_cursor(bool visible) const
    {
        CONSOLE_CURSOR_INFO cursor = { core::input_cursor_size, visible ? TRUE : FALSE } ;
        SetConsoleCursorInfo(m_output, &cursor) ;
    }

    /** 窗口可见列数；与旧绘制口径一致，右侧留一列防自动折行。 */
    std::size_t window_columns(const CONSOLE_SCREEN_BUFFER_INFO& info) const
    {
        return static_cast<std::size_t>(std::max(1, info.srWindow.Right - info.srWindow.Left)) ;
    }

    /** 窗口可见行数；与旧绘制口径一致。 */
    std::size_t window_rows(const CONSOLE_SCREEN_BUFFER_INFO& info) const
    {
        return static_cast<std::size_t>(std::max(1, info.srWindow.Bottom - info.srWindow.Top)) ;
    }

    /** 状态区行数上限；至少给控件留一行。 */
    std::size_t status_row_capacity(const CONSOLE_SCREEN_BUFFER_INFO& info) const
    {
        return detail::status_capacity(window_rows(info)) ;
    }

    /** 把光标移到行首；必要时先换行，返回行首所在行。 */
    SHORT align_line_start()
    {
        auto info = screen() ;
        if (info.dwCursorPosition.X != info.srWindow.Left)
        {
            write("\r\n") ;
            info = screen() ;
        }
        return info.dwCursorPosition.Y ;
    }

    /** 从指定行起写状态行并预留空间；返回状态区锚点（已处理滚动）。 */
    SHORT write_status_block(SHORT top, const std::vector<std::string>& lines)
    {
        if (lines.empty())
        {
            return top ;
        }
        auto info = screen() ;
        const SHORT left = info.srWindow.Left ;
        SHORT row = std::clamp(top, info.srWindow.Top, info.srWindow.Bottom) ;
        for (const auto& line : lines)
        {
            // 每行显式定位，避免窗口横向滚动时从缓冲区第 0 列继续写。
            move_to(left, row) ;
            write("\033[2K" + line + "\r\n") ;
            info = screen() ;
            row = info.dwCursorPosition.Y ;
        }
        return static_cast<SHORT>(row - static_cast<SHORT>(lines.size())) ;
    }

    /** 清空一组行并停在块顶；不滚动屏幕。 */
    void clear_lines(SHORT top, std::size_t count)
    {
        if (count == 0)
        {
            return ;
        }
        auto info = screen() ;
        const SHORT left = info.srWindow.Left ;
        const SHORT first = std::clamp(top, info.srWindow.Top, info.srWindow.Bottom) ;
        const auto visible = static_cast<std::size_t>(info.srWindow.Bottom - first + 1) ;
        const auto rows = std::min(count, visible) ;
        for (std::size_t index = 0 ; index < rows ; ++index)
        {
            move_to(left, static_cast<SHORT>(first + static_cast<SHORT>(index))) ;
            write("\033[2K") ;
        }
        move_to(left, first) ;
    }

    /** 原地重画状态区并保持光标位置；超出可见窗口的行跳过。 */
    void repaint_status(const std::vector<std::string>& lines)
    {
        auto info = screen() ;
        if (m_status_anchor < info.srWindow.Top || m_status_anchor > info.srWindow.Bottom)
        {
            return ;
        }
        const auto available = static_cast<std::size_t>(info.srWindow.Bottom - m_status_anchor + 1) ;
        const auto rows = std::min(m_status_rows, available) ;
        const COORD saved = info.dwCursorPosition ;
        const SHORT left = info.srWindow.Left ;
        for (std::size_t index = 0 ; index < rows ; ++index)
        {
            const std::string text = index < lines.size() ? lines[index] : std::string() ;
            move_to(left, static_cast<SHORT>(m_status_anchor + static_cast<SHORT>(index))) ;
            write("\033[2K" + text + core::color::reset) ;
        }
        move_to(saved.X, saved.Y) ;
    }

    /** 按当前终端宽度与行数上限渲染状态行。 */
    std::vector<std::string> render_status(const std::vector<std::string>& lines)
    {
        const auto info = screen() ;
        return detail::render_status_lines(lines, window_columns(info), status_row_capacity(info)) ;
    }

    /** 规划状态区应占行数与可见行数。 */
    detail::ConsoleStatusPlan plan_status(std::size_t requested)
    {
        const auto info = screen() ;
        return detail::plan_status_rows(requested, m_status_rows, status_row_capacity(info), m_status_sealed) ;
    }

    /** 在现有区域内重画状态行；多余行丢弃，不足处补空行。 */
    void apply_status(const std::vector<std::string>& rendered, std::size_t visible)
    {
        auto lines = rendered ;
        lines.resize(std::min(lines.size(), visible)) ;
        if (lines == m_status_lines)
        {
            return ;
        }
        m_status_lines = std::move(lines) ;
        repaint_status(m_status_lines) ;
    }

    /** 建立状态区：在光标所在行预留并写入状态行。 */
    std::size_t establish_status(const std::vector<std::string>& lines)
    {
        const auto rendered = render_status(lines) ;
        if (rendered.empty())
        {
            return 0 ;
        }
        const auto info = screen() ;
        m_status_anchor = write_status_block(info.dwCursorPosition.Y, rendered) ;
        m_status_rows = rendered.size() ;
        m_status_lines = rendered ;
        m_status_active = true ;
        m_status_sealed = false ;
        return m_status_rows ;
    }

    /** 重排状态区与控件：清空旧块后重写状态区，控件锚点随状态行数移动。 */
    void reflow_status(const std::vector<std::string>& rendered, SHORT block_top, SHORT& widget_anchor,
        std::size_t& widget_rows)
    {
        clear_lines(block_top, m_status_rows + widget_rows) ;
        const auto info = screen() ;
        const SHORT first = std::clamp(block_top, info.srWindow.Top, info.srWindow.Bottom) ;
        m_status_anchor = rendered.empty() ? first : write_status_block(first, rendered) ;
        m_status_rows = rendered.size() ;
        m_status_lines = rendered ;
        m_status_active = !rendered.empty() ;
        m_status_sealed = false ;
        widget_anchor = static_cast<SHORT>(m_status_anchor + static_cast<SHORT>(m_status_rows)) ;
        widget_rows = 0 ;
    }

    /** 依据光标位置判断状态区下方是否已有常驻输出。 */
    void detect_status_sealed()
    {
        if (!m_status_active)
        {
            return ;
        }
        const auto info = screen() ;
        m_status_sealed = info.dwCursorPosition.Y >
            static_cast<SHORT>(m_status_anchor + static_cast<SHORT>(m_status_rows)) ;
    }

    /** 控件是否紧贴状态区下方。 */
    bool status_adjacent(SHORT widget_anchor) const
    {
        return m_status_active &&
            widget_anchor == static_cast<SHORT>(m_status_anchor + static_cast<SHORT>(m_status_rows)) ;
    }

    /** 写入后若发生滚动，按光标实际行修正状态区锚点。 */
    void sync_status_anchor(SHORT expected_cursor_y)
    {
        if (!m_status_active)
        {
            return ;
        }
        const auto info = screen() ;
        m_status_anchor += static_cast<SHORT>(info.dwCursorPosition.Y - expected_cursor_y) ;
    }

    /** 带超时读取完整语义按键；timeout_ms 为 0 表示无限等待，返回 false 表示超时。 */
    bool read_key(unsigned timeout_ms, int& key)
    {
        if (m_repeat_count > 0)
        {
            --m_repeat_count ;
            key = m_repeat_key ;
            return true ;
        }
        const auto deadline = timeout_ms > 0 ? GetTickCount64() + timeout_ms : 0ULL ;
        while (true)
        {
            if (timeout_ms > 0)
            {
                const auto now = GetTickCount64() ;
                if (now >= deadline)
                {
                    return false ;
                }
                const auto remaining =
                    static_cast<unsigned>(std::min<unsigned long long>(deadline - now, 0x7FFFFFFFULL)) ;
                const auto waited = WaitForSingleObject(m_input, remaining) ;
                if (waited == WAIT_TIMEOUT)
                {
                    return false ;
                }
                if (waited != WAIT_OBJECT_0)
                {
                    key = keyboard::unavailable ;
                    return true ;
                }
            }
            INPUT_RECORD event = {} ;
            DWORD count = 0 ;
            if (!ReadConsoleInputW(m_input, &event, 1, &count) || count == 0)
            {
                key = keyboard::unavailable ;
                return true ;
            }
            if (event.EventType == WINDOW_BUFFER_SIZE_EVENT)
            {
                key = keyboard::resize ;
                return true ;
            }
            if (event.EventType != KEY_EVENT || !event.Event.KeyEvent.bKeyDown)
            {
                continue ;
            }
            const auto& press = event.Event.KeyEvent ;
            int value = 0 ;
            const bool control = (press.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0 ;
            const bool alt = (press.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0 ;
            if (press.uChar.UnicodeChar == 3 || (control && press.wVirtualKeyCode == 'C'))
            {
                value = keyboard::interrupt ;
            }
            else if (control && press.wVirtualKeyCode == 'Z')
            {
                value = keyboard::back ;
            }
            else if (alt && press.wVirtualKeyCode == VK_LEFT)
            {
                value = keyboard::back ;
            }
            else
            {
                switch (press.wVirtualKeyCode)
                {
                    case VK_UP: value = keyboard::up ; break ;
                    case VK_DOWN: value = keyboard::down ; break ;
                    case VK_LEFT: value = keyboard::left ; break ;
                    case VK_RIGHT: value = keyboard::right ; break ;
                    case VK_HOME: value = keyboard::home ; break ;
                    case VK_END: value = keyboard::end ; break ;
                    case VK_DELETE: value = keyboard::delete_key ; break ;
                    case VK_RETURN: value = keyboard::enter ; break ;
                    case VK_ESCAPE: value = keyboard::escape ; break ;
                    case VK_BACK: value = keyboard::backspace ; break ;
                    case VK_F2: value = keyboard::browse ; break ;
                    default: value = press.uChar.UnicodeChar ; break ;
                }
            }
            if (value == 0)
            {
                continue ;
            }
            if (value >= 0xD800 && value <= 0xDBFF)
            {
                m_high_surrogate = static_cast<char32_t>(value) ;
                continue ;
            }
            if (value >= 0xDC00 && value <= 0xDFFF)
            {
                if (m_high_surrogate == 0)
                {
                    continue ;
                }
                value = static_cast<int>(0x10000 + ((m_high_surrogate - 0xD800) << 10) + (value - 0xDC00)) ;
            }
            m_high_surrogate = 0 ;
            m_repeat_key = value ;
            m_repeat_count = press.wRepeatCount > 0 ? press.wRepeatCount - 1 : 0 ;
            key = value ;
            return true ;
        }
    }

    /** 在原位置重画，必要时滚动终端以预留完整帧。 */
    void paint(const ConsoleFrame& frame, SHORT& anchor, std::size_t& previous_rows) const
    {
        show_cursor(false) ;
        auto info = screen() ;
        const auto capacity = static_cast<std::size_t>(std::max(1, info.srWindow.Bottom - info.srWindow.Top)) ;
        const auto rows = std::min(capacity, std::max(previous_rows, frame.m_lines.size())) ;
        anchor = std::clamp(anchor, info.srWindow.Top, info.srWindow.Bottom) ;
        move_to(info.srWindow.Left, anchor) ;
        // 每行末尾留一列，防止双宽字符或终端自动折行污染下一帧。
        for (std::size_t index = 0 ; index < rows ; ++index)
        {
            write("\r\n") ;
        }
        info = screen() ;
        anchor = static_cast<SHORT>(info.dwCursorPosition.Y - rows) ;
        const auto columns = static_cast<std::size_t>(std::max(1, info.srWindow.Right - info.srWindow.Left)) ;
        for (std::size_t index = 0 ; index < rows ; ++index)
        {
            move_to(info.srWindow.Left, static_cast<SHORT>(anchor + index)) ;
            write("\033[2K" + (index < frame.m_lines.size() ? detail::clip_text(frame.m_lines[index], columns) : "")) ;
        }
        previous_rows = rows ;
        if (frame.m_cursor)
        {
            const auto column = std::min(frame.m_cursor->m_column, columns - 1) ;
            move_to(static_cast<SHORT>(info.srWindow.Left + column),
                static_cast<SHORT>(anchor + frame.m_cursor->m_line)) ;
            show_cursor(true) ;
        }
    }
} ;

namespace
{
/** 刷新状态区：按行数变化重排或原地重画，控件需要重画时置 redraw。 */
void refresh_status_area(ConsoleRuntime& runtime, const std::vector<std::string>& lines, SHORT& widget_anchor,
    std::size_t& widget_rows, bool& redraw)
{
    const auto rendered = runtime.render_status(lines) ;
    const auto plan = runtime.plan_status(rendered.size()) ;
    if (!runtime.m_status_active)
    {
        if (rendered.empty())
        {
            return ;
        }
        runtime.reflow_status(rendered, widget_anchor, widget_anchor, widget_rows) ;
        redraw = true ;
        return ;
    }
    if (!runtime.m_status_sealed && plan.m_area_rows != runtime.m_status_rows &&
        runtime.status_adjacent(widget_anchor))
    {
        runtime.reflow_status(rendered, runtime.m_status_anchor, widget_anchor, widget_rows) ;
        redraw = true ;
        return ;
    }
    runtime.apply_status(rendered, plan.m_visible_rows) ;
}
} // namespace

ConsoleSession::ConsoleSession() : m_runtime(std::make_unique<ConsoleRuntime>())
{
}

ConsoleSession::~ConsoleSession() = default ;

bool ConsoleSession::is_ready() const noexcept
{
    return m_runtime->m_ready ;
}

PromptState ConsoleSession::run(ConsoleElement& element)
{
    return run_impl(element, ConsoleStatusProvider(), 0) ;
}

PromptState ConsoleSession::run(ConsoleElement& element, const ConsoleStatusProvider& status, unsigned idle_ms)
{
    return run_impl(element, status, idle_ms) ;
}

PromptState ConsoleSession::run_impl(ConsoleElement& element, const ConsoleStatusProvider& status, unsigned idle_ms)
{
    element.begin() ;
    if (!is_ready())
    {
        element.handle_key(keyboard::unavailable) ;
        return element.get_state() ;
    }
    m_runtime->m_repeat_count = 0 ;
    m_runtime->m_high_surrogate = 0 ;
    m_runtime->align_line_start() ;
    std::vector<std::string> initial_status ;
    if (status)
    {
        initial_status = status() ;
        if (!m_runtime->m_status_active && !initial_status.empty())
        {
            m_runtime->establish_status(initial_status) ;
        }
    }
    auto info = m_runtime->screen() ;
    SHORT anchor = info.dwCursorPosition.Y ;
    std::size_t drawn_rows = 0 ;
    bool redraw = true ;
    if (status && m_runtime->m_status_active)
    {
        // 复用既有状态区：控件画在状态区下方，或画在既有输出之下。
        m_runtime->detect_status_sealed() ;
        refresh_status_area(*m_runtime, initial_status, anchor, drawn_rows, redraw) ;
    }
    while (element.get_state() == PromptState::Editing)
    {
        if (redraw)
        {
            info = m_runtime->screen() ;
            const bool anchor_visible = anchor >= info.srWindow.Top && anchor <= info.srWindow.Bottom ;
            auto rows = m_runtime->window_rows(info) ;
            if (m_runtime->status_adjacent(anchor))
            {
                rows = std::max<std::size_t>(1, rows - m_runtime->m_status_rows) ;
            }
            const auto frame = detail::fit_frame(element.render(m_runtime->window_columns(info)), rows) ;
            const SHORT before = anchor ;
            m_runtime->paint(frame, anchor, drawn_rows) ;
            if (m_runtime->m_status_active && anchor_visible)
            {
                m_runtime->m_status_anchor += static_cast<SHORT>(anchor - before) ;
            }
        }
        int key = 0 ;
        if (!m_runtime->read_key(status ? idle_ms : 0, key))
        {
            if (status)
            {
                refresh_status_area(*m_runtime, status(), anchor, drawn_rows, redraw) ;
            }
            continue ;
        }
        if (key == keyboard::resize)
        {
            if (status)
            {
                refresh_status_area(*m_runtime, status(), anchor, drawn_rows, redraw) ;
            }
            redraw = true ;
            continue ;
        }
        redraw = element.handle_key(key) ;
    }
    info = m_runtime->screen() ;
    const bool anchor_visible = anchor >= info.srWindow.Top && anchor <= info.srWindow.Bottom ;
    const SHORT before_clear = anchor ;
    m_runtime->paint(ConsoleFrame{}, anchor, drawn_rows) ;
    if (m_runtime->m_status_active && anchor_visible)
    {
        m_runtime->m_status_anchor += static_cast<SHORT>(anchor - before_clear) ;
    }
    info = m_runtime->screen() ;
    m_runtime->move_to(info.srWindow.Left, anchor) ;
    const auto state = element.get_state() ;
    const auto color = state == PromptState::Submitted ? core::color::success : core::color::warning ;
    const auto summary = state == PromptState::Submitted ? element.result_text() :
        (state == PromptState::Back ? "↩ 返回上一步" :
        (state == PromptState::Interrupted ? "已中断" :
        (state == PromptState::Unavailable ? "输入不可用" : "已取消"))) ;
    auto summary_line = std::string(color) + element.get_title() + "：" + detail::safe_text(summary) +
        core::color::reset ;
    if (m_runtime->m_status_active)
    {
        // 状态区常驻时摘要也按列宽裁剪，避免折行干扰滚动补偿。
        summary_line = detail::clip_status_line(summary_line, m_runtime->window_columns(m_runtime->screen())) ;
    }
    m_runtime->write(summary_line + "\r\n") ;
    if (m_runtime->m_status_active)
    {
        // 摘要写在状态区下方后该区域变为常驻，之后只能原地刷新。
        m_runtime->sync_status_anchor(static_cast<SHORT>(anchor + 1)) ;
        m_runtime->m_status_sealed = true ;
    }
    SetConsoleCursorInfo(m_runtime->m_output, &m_runtime->m_cursor_info) ;
    return state ;
}

void ConsoleSession::set_status(const std::vector<std::string>& lines)
{
    if (!is_ready())
    {
        return ;
    }
    if (!m_runtime->m_status_active)
    {
        m_runtime->align_line_start() ;
        m_runtime->establish_status(lines) ;
        return ;
    }
    m_runtime->detect_status_sealed() ;
    const auto rendered = m_runtime->render_status(lines) ;
    const auto plan = m_runtime->plan_status(rendered.size()) ;
    if (!m_runtime->m_status_sealed && plan.m_area_rows != m_runtime->m_status_rows)
    {
        SHORT widget_anchor = m_runtime->m_status_anchor ;
        std::size_t widget_rows = 0 ;
        m_runtime->reflow_status(rendered, m_runtime->m_status_anchor, widget_anchor, widget_rows) ;
        return ;
    }
    m_runtime->apply_status(rendered, plan.m_visible_rows) ;
}

void ConsoleSession::write_line(const std::string& text) const
{
    if (is_ready())
    {
        m_runtime->write(text + core::color::reset + "\r\n") ;
    }
    else
    {
        std::cout << detail::plain_text(text) << '\n' ;
    }
}

void ConsoleSession::update_line(const std::string& text) const
{
    if (is_ready())
    {
        m_runtime->write("\r\033[2K" + text + core::color::reset) ;
    }
    else
    {
        std::cout << '\r' << detail::plain_text(text) ;
    }
}

bool ConsoleSession::poll_interrupt() const
{
    if (!is_ready())
    {
        return false ;
    }
    DWORD count = 0 ;
    if (!GetNumberOfConsoleInputEvents(m_runtime->m_input, &count) || count == 0)
    {
        return false ;
    }
    bool interrupted = false ;
    for (DWORD index = 0 ; index < count ; ++index)
    {
        INPUT_RECORD event = {} ;
        DWORD read = 0 ;
        if (!ReadConsoleInputW(m_runtime->m_input, &event, 1, &read) || read == 0)
        {
            break ;
        }
        if (event.EventType != KEY_EVENT || !event.Event.KeyEvent.bKeyDown)
        {
            continue ;
        }
        const auto& press = event.Event.KeyEvent ;
        const bool control = (press.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0 ;
        if (press.uChar.UnicodeChar == 3 || (control && press.wVirtualKeyCode == 'C') ||
            press.wVirtualKeyCode == VK_ESCAPE)
        {
            interrupted = true ;
        }
    }
    return interrupted ;
}
} // namespace console
