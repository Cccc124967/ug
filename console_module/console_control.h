#pragma once
#include "../core_module/dev_core.h"
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace console
{
enum class ConsoleType
{
    MultiSelect,
    SingleSelect,
    Input,
    Selector
} ;

enum class PromptState
{
    Editing,
    Submitted,
    Cancelled,
    Interrupted,
    Unavailable,
    /** 用户要求返回上一步；由流程决定退到哪个节点 */
    Back
} ;

// 普通字符传 Unicode 码点；特殊键放在 Unicode 范围外，避免与 H/P 等字母撞码。
namespace keyboard
{
inline constexpr int enter = 13 ;
inline constexpr int escape = 27 ;
inline constexpr int backspace = 8 ;
inline constexpr int space = 32 ;
inline constexpr int interrupt = 3 ;
inline constexpr int up = 0x110001 ;
inline constexpr int down = 0x110002 ;
inline constexpr int left = 0x110003 ;
inline constexpr int right = 0x110004 ;
inline constexpr int home = 0x110005 ;
inline constexpr int end = 0x110006 ;
inline constexpr int delete_key = 0x110007 ;
inline constexpr int browse = 0x110008 ;
inline constexpr int resize = 0x110009 ;
inline constexpr int unavailable = 0x11000A ;
inline constexpr int back = 0x11000B ;
} // namespace keyboard

struct ConsoleCursorPosition
{
    std::size_t m_line = 0 ;
    std::size_t m_column = 0 ;
} ;

struct ConsoleFrame
{
    std::vector<std::string> m_lines ;
    std::size_t m_focus_line = 0 ;
    std::optional<ConsoleCursorPosition> m_cursor ;
} ;

/** 单个交互组；handle_key 返回是否消费按键，完成状态单独读取。 */
class ConsoleElement
{
    public:
    virtual ~ConsoleElement() = default ;
    /** 设置标题；标题中的控制字符显示为空格。 */
    void set_title(const std::string& title) ;
    /** 设置标题颜色；空指针恢复默认颜色。 */
    void set_title_color(const char* color) ;
    /** 获取标题。 */
    const std::string& get_title() const noexcept ;
    /** 获取标题颜色。 */
    const std::string& get_title_color() const noexcept ;
    /** 获取控件类型。 */
    ConsoleType get_type() const noexcept ;
    /** 获取提交、取消或编辑状态。 */
    PromptState get_state() const noexcept ;
    /** 开始新的交互，保留已配置的值。 */
    void begin() ;
    /** 画到当前光标位置；持续交互使用 ConsoleSession::run。 */
    virtual void draw() const ;
    /** 生成可测试的显示帧；列数用于输入行横向滚动。 */
    virtual ConsoleFrame render(std::size_t columns = 80) const = 0 ;
    /** 消费一个 Unicode 字符或 keyboard 特殊键。 */
    virtual bool handle_key(int key) = 0 ;
    /** 重置控件的值、游标和完成状态。 */
    virtual void reset() = 0 ;
    /** 返回提交后显示的摘要。 */
    virtual std::string result_text() const = 0 ;

    protected:
    explicit ConsoleElement(ConsoleType type) ;
    /** 处理取消、中断和不可用的输入。 */
    bool handle_common_key(int key) ;
    /** 生成标题行。 */
    std::string title_line() const ;
    std::string m_title ;
    std::string m_title_color = core::color::title ;
    ConsoleType m_type ;
    PromptState m_state = PromptState::Editing ;
} ;

class SingleSelectElement : public ConsoleElement
{
    public:
    SingleSelectElement() ;
    /** 生成单选菜单。 */
    ConsoleFrame render(std::size_t columns = 80) const override ;
    /** 上下移动，Home/End 跳转，Enter 提交，Esc 取消。 */
    bool handle_key(int key) override ;
    /** 恢复到第一项。 */
    void reset() override ;
    /** 添加一个选项。 */
    virtual void add_option(const std::string& option) ;
    /** 删除选项并保留当前所指的选项；越界抛出 out_of_range。 */
    virtual void remove_option(std::size_t index) ;
    /** 清空全部选项。 */
    virtual void clear_options() ;
    /** 交换选项，游标随原选项移动。 */
    virtual void swap_options(std::size_t first, std::size_t second) ;
    /** 按下标读取选项；越界抛出 out_of_range。 */
    virtual std::string find_option(std::size_t index) const ;
    /** 设置当前选项；越界抛出 out_of_range。 */
    virtual void set_selected_index(std::size_t index) ;
    /** 获取当前下标；空菜单返回 std::string::npos。 */
    virtual std::size_t get_selected_index() const noexcept ;
    /** 获取选项列表。 */
    virtual const std::vector<std::string>& get_options() const noexcept ;
    /** 设置当前项颜色；空指针恢复默认。 */
    virtual void set_selected_color(const char* color) ;
    /** 设置非当前项颜色；空指针恢复默认。 */
    virtual void set_unselected_color(const char* color) ;
    /** 获取当前选项文本。 */
    std::string result_text() const override ;

    protected:
    std::vector<std::string> m_options ;
    std::size_t m_selected_index = 0 ;
    std::string m_selected_color = core::color::selected ;
    std::string m_unselected_color = core::color::unselected ;
} ;

/** selected_index 为移动游标，勾选结果用 selected_indices 读取。 */
class MultiSelectElement : public SingleSelectElement
{
    public:
    MultiSelectElement() ;
    /** 分别绘制游标、勾选框与文本。 */
    ConsoleFrame render(std::size_t columns = 80) const override ;
    /** 空格切换当前项；Enter 提交，允许不勾选任何项。 */
    bool handle_key(int key) override ;
    /** 清空勾选并回到第一项。 */
    void reset() override ;
    /** 添加一个默认未勾选的选项。 */
    void add_option(const std::string& option) override ;
    /** 删除选项及对应勾选状态。 */
    void remove_option(std::size_t index) override ;
    /** 清空选项、勾选与游标。 */
    void clear_options() override ;
    /** 交换选项；勾选状态随选项移动。 */
    void swap_options(std::size_t first, std::size_t second) override ;
    /** 设置指定选项的勾选状态。 */
    void set_checked(std::size_t index, bool checked) ;
    /** 读取指定选项的勾选状态。 */
    bool is_checked(std::size_t index) const ;
    /** 按显示顺序返回勾选下标。 */
    std::vector<std::size_t> get_selected_indices() const ;
    /** 返回全部勾选项的摘要。 */
    std::string result_text() const override ;

    private:
    std::vector<bool> m_checked ;
} ;

using InputValidator = std::function<std::optional<std::string>(const std::string&)> ;

class InputElement : public ConsoleElement
{
    public:
    InputElement() ;
    /** 生成输入行，随编辑光标横向滚动。 */
    ConsoleFrame render(std::size_t columns = 80) const override ;
    /** 编辑 Unicode 文本，Enter 校验并提交。 */
    bool handle_key(int key) override ;
    /** 恢复默认文本并清除错误。 */
    void reset() override ;
    /** 设置文本，拒绝换行、控制字符与非法 UTF-8。 */
    void set_value(const std::string& value) ;
    /** 设置初始及重置时使用的文本。 */
    void set_default_value(const std::string& value) ;
    /** 返回 UTF-8 文本。 */
    std::string get_value() const ;
    /** 返回编辑光标所在的 Unicode 码点下标。 */
    std::size_t get_cursor_index() const noexcept ;
    /** 设置校验器；返回 nullopt 表示通过。 */
    void set_validator(InputValidator validator) ;
    /** 设置是否允许空文本，默认允许。 */
    void set_allow_empty(bool allow) ;
    /** 获取最近一次校验或选择器错误。 */
    const std::string& get_error() const noexcept ;
    /** 返回当前输入。 */
    std::string result_text() const override ;

    protected:
    /** 校验当前文本；失败信息返回给输入行。 */
    virtual std::optional<std::string> validate() const ;
    /** 返回当前控件的操作提示。 */
    virtual std::string hint_text() const ;
    std::u32string m_value ;
    std::u32string m_default_value ;
    std::size_t m_cursor_index = 0 ;
    std::string m_error ;
    InputValidator m_validator ;
    bool m_allow_empty = true ;
} ;

enum class FilePickerMode
{
    File,
    Directory
} ;

enum class FilePickerStatus
{
    Selected,
    Cancelled,
    Failed
} ;

struct FilePickerFilter
{
    std::string m_name ;
    std::string m_pattern ;
} ;

struct FilePickerOptions
{
    FilePickerMode m_mode = FilePickerMode::File ;
    std::string m_title ;
    std::filesystem::path m_initial_directory ;
    std::vector<FilePickerFilter> m_filters ;
} ;

struct FilePickerResult
{
    FilePickerStatus m_status = FilePickerStatus::Cancelled ;
    std::filesystem::path m_path ;
    std::string m_error ;
} ;

using FilePicker = std::function<FilePickerResult(const FilePickerOptions&)> ;

/** 打开 Windows 原生选择框；取消与系统错误分开返回。 */
FilePickerResult pick_file(const FilePickerOptions& options) ;

/** 最近一次选择器确认过的目录；没有时返回空，作为下一次 F2 的默认起点。 */
std::filesystem::path recent_selector_directory() ;

/** 记录最近一次选择器确认过的目录；空路径表示清除，目录不存在时忽略。 */
void set_recent_selector_directory(const std::filesystem::path& directory) ;

class SelectorElement : public InputElement
{
    public:
    explicit SelectorElement(FilePickerMode mode = FilePickerMode::File) ;
    /** F2 浏览，其他按键直接编辑路径；取消浏览保留原输入。 */
    bool handle_key(int key) override ;
    /** 设置文件或文件夹模式。 */
    void set_mode(FilePickerMode mode) ;
    /** 设置浏览起始目录。 */
    void set_initial_directory(const std::filesystem::path& directory) ;
    /** 设置文件类型过滤；仅文件模式有效。 */
    void set_filters(std::vector<FilePickerFilter> filters) ;
    /** 设置选择器服务，空回调恢复 Windows 选择框。 */
    void set_picker(FilePicker picker) ;
    /** 获取当前 Unicode 文件系统路径。 */
    std::filesystem::path get_path() const ;

    protected:
    /** 检查路径存在且类型符合模式，再调用自定义校验器。 */
    std::optional<std::string> validate() const override ;
    /** 返回直接输入及浏览提示。 */
    std::string hint_text() const override ;

    private:
    FilePickerOptions m_picker_options ;
    FilePicker m_picker ;
} ;

struct ConsoleRuntime ;

/** 状态行提供者；返回若干行（可含 ANSI 颜色），交互空闲时刷新到控件上方。 */
using ConsoleStatusProvider = std::function<std::vector<std::string>()> ;

/** 接管当前控制台并在析构时恢复模式、代码页、光标和颜色。 */
class ConsoleSession
{
    public:
    ConsoleSession() ;
    ~ConsoleSession() ;
    ConsoleSession(const ConsoleSession&) = delete ;
    /** 禁止复制控制台所有权。 */
    ConsoleSession& operator=(const ConsoleSession&) = delete ;
    /** 判断是否连接到可交互的 Windows 控制台。 */
    bool is_ready() const noexcept ;
    /** 就地显示并运行一个控件，直到提交、取消或输入不可用。 */
    PromptState run(ConsoleElement& element) ;
    /** 带状态区运行控件；空闲时每隔 idle_ms 毫秒刷新 status 提供的状态行。 */
    PromptState run(ConsoleElement& element, const ConsoleStatusProvider& status, unsigned idle_ms = 100) ;
    /** 建立或刷新常驻状态区；行数变化时重排，之后各次运行继续复用同一区域。 */
    void set_status(const std::vector<std::string>& lines) ;
    /** 写入一行 UTF-8 文本。 */
    void write_line(const std::string& text) const ;
    /** 覆盖当前行文本；用于扫描进度这类反复刷新的状态行。 */
    void update_line(const std::string& text) const ;
    /** 非阻塞读取中断键；按到 Esc 或 Ctrl+C 时返回 true。 */
    bool poll_interrupt() const ;

    private:
    /** 运行控件主体；状态提供者为空时完全走无状态路径。 */
    PromptState run_impl(ConsoleElement& element, const ConsoleStatusProvider& status, unsigned idle_ms) ;
    std::unique_ptr<ConsoleRuntime> m_runtime ;
} ;
} // namespace console
