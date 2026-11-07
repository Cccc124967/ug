#pragma once
#include "../console_module/console_control.h"
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace prompt
{
enum class PromptFlowState
{
    Ready,
    Running,
    Completed,
    Cancelled,
    Interrupted,
    Unavailable,
    Failed
} ;

enum class PromptNodeState
{
    Pending,
    Active,
    Submitted,
    Skipped,
    Failed
} ;

using PromptValue = std::variant<std::string, std::size_t, std::vector<std::size_t>, std::filesystem::path> ;

/** 提交时的答案快照；之后移动、删除或编辑控件不会改变该快照。 */
struct PromptAnswer
{
    std::string m_node_id ;
    console::ConsoleType m_type = console::ConsoleType::Input ;
    PromptValue m_value ;
    std::string m_text ;
} ;

struct PromptRunResult
{
    PromptFlowState m_state = PromptFlowState::Ready ;
    std::string m_node_id ;
    std::string m_message ;
} ;

class PromptFlow ;
using PromptCondition = std::function<bool(const PromptFlow&)> ;
using PromptSubmitHandler = std::function<void(PromptFlow&, const PromptAnswer&)> ;
using PromptRunner = std::function<console::PromptState(console::ConsoleElement&)> ;

/** 一个有稳定标识的步骤；流程独占控件，条件为只读判断，提交回调可修改流程。 */
struct PromptNode
{
    std::string m_id ;
    std::unique_ptr<console::ConsoleElement> m_element ;
    PromptCondition m_condition ;
    PromptSubmitHandler m_on_submit ;
} ;

struct PromptFlowData ;

/**
 * 单线程动态流程；每次提交后按最新顺序寻找第一个未完成且条件成立的节点。
 * 重排不重问已提交节点；Skipped 会重新判定，删除节点同时丢弃其答案。
 */
class PromptFlow
{
    public:
    explicit PromptFlow(PromptRunner runner = {}) ;
    ~PromptFlow() ;
    PromptFlow(const PromptFlow&) = delete ;
    /** 禁止复制流程及其控件所有权。 */
    PromptFlow& operator=(const PromptFlow&) = delete ;

    /** 追加节点；空标识、重名或空控件抛出 invalid_argument。 */
    void append(PromptNode node) ;
    /** 插入到下标前，size() 表示末尾；越界抛出 out_of_range。 */
    void insert(std::size_t index, PromptNode node) ;
    /** 插入到指定节点前；锚点不存在时抛出 out_of_range。 */
    void insert_before(const std::string& anchor_id, PromptNode node) ;
    /** 插入到指定节点后；锚点不存在时抛出 out_of_range。 */
    void insert_after(const std::string& anchor_id, PromptNode node) ;
    /** 删除节点及答案；不存在返回 false，允许提交回调删除自身，移除失败节点后可继续。 */
    bool remove(const std::string& node_id) ;
    /** 移到最终下标；保留节点状态与答案，越界抛出 out_of_range。 */
    void move(const std::string& node_id, std::size_t index) ;
    /** 移到指定节点前；移动到自身位置不产生变化。 */
    void move_before(const std::string& node_id, const std::string& anchor_id) ;
    /** 移到指定节点后；移动到自身位置不产生变化。 */
    void move_after(const std::string& node_id, const std::string& anchor_id) ;
    /** 交换两个节点的位置，保留各自状态与答案。 */
    void swap_nodes(const std::string& first_id, const std::string& second_id) ;
    /** 清空流程及答案；提交回调中调用时，当前回调仍可安全完成。 */
    void clear() ;

    /** 返回当前节点数量。 */
    std::size_t size() const noexcept ;
    /** 判断节点是否仍在流程中。 */
    bool contains(const std::string& node_id) const noexcept ;
    /** 返回当前顺序的节点标识快照。 */
    std::vector<std::string> get_order() const ;
    /** 获取节点状态；节点不存在时抛出 out_of_range。 */
    PromptNodeState get_node_state(const std::string& node_id) const ;
    /** 复制已接受的答案，包含提交回调失败时的答案；未接受或已删除时返回 nullopt。 */
    std::optional<PromptAnswer> get_answer(const std::string& node_id) const ;
    /** 获取正在判断或执行的节点标识；空闲时返回 nullopt。 */
    std::optional<std::string> get_current_id() const ;
    /** 查找控件；删除节点后指针失效，条件回调中禁止获取可写控件。 */
    console::ConsoleElement* find_element(const std::string& node_id) ;
    /** 只读查找控件；删除节点后指针失效。 */
    const console::ConsoleElement* find_element(const std::string& node_id) const noexcept ;
    /** 获取最近一次执行结果。 */
    PromptRunResult get_result() const ;

    /** 更新条件；空条件代表始终启用，已提交节点需显式 reset_node 才重问。 */
    void set_condition(const std::string& node_id, PromptCondition condition) ;
    /** 更新提交回调；当前正在执行的回调不受替换影响。 */
    void set_on_submit(const std::string& node_id, PromptSubmitHandler handler) ;
    /** 设置执行器；空回调使用 ConsoleSession，运行中禁止更换。 */
    void set_runner(PromptRunner runner) ;
    /** 清除节点答案并重新排队；可选择恢复控件默认值，禁止重置当前活动节点。 */
    void reset_node(const std::string& node_id, bool reset_value = true) ;
    /** 清除全部完成状态及失败状态；保留当前节点结构，运行中禁止调用。 */
    void restart(bool reset_values = true) ;

    /** 执行到完成或取消；再次调用从未完成节点继续，Failed 需重置/移除失败节点或 restart。 */
    PromptRunResult run() ;
    /** 只执行一个节点：成功返回 Ready，无可运行节点返回 Completed；禁止重入。 */
    PromptRunResult run_next() ;

    private:
    /** 执行一轮节点选择、交互和提交回调。 */
    PromptRunResult execute_next(const PromptRunner& runner) ;
    /** 建立执行环境并按指定范围推进流程。 */
    PromptRunResult run_impl(bool single_step) ;
    std::unique_ptr<PromptFlowData> m_data ;
} ;
} // namespace prompt
