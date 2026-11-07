#pragma once
#include "../prompt_control.h"

namespace prompt
{
struct PromptEntry
{
    PromptNode m_node ;
    PromptNodeState m_state = PromptNodeState::Pending ;
    std::optional<PromptAnswer> m_answer ;
} ;

struct PromptFlowData
{
    std::vector<std::shared_ptr<PromptEntry>> m_entries ;
    std::shared_ptr<PromptEntry> m_active ;
    PromptRunner m_runner ;
    PromptRunResult m_result ;
    bool m_executing = false ;
    bool m_checking_condition = false ;
    bool m_running_control = false ;
} ;

/** 条件与控件交互中禁止结构修改，提交回调允许修改。 */
void check_mutation(const PromptFlowData& data) ;
/** 检查流程没有正在执行的调用。 */
void check_idle(const PromptFlowData& data) ;
/** 找到稳定标识的当前位置；找不到返回 size()。 */
std::size_t find_index(const PromptFlowData& data, const std::string& node_id) noexcept ;
/** 找到稳定标识的当前位置；找不到抛出 out_of_range。 */
std::size_t require_index(const PromptFlowData& data, const std::string& node_id) ;
/** 让空闲流程在结构修改后可继续执行，保留未显式处理的 Failed。 */
void notify_change(PromptFlowData& data) ;
/** 提取控件的提交快照；类型保持为文本、单选、多选或路径。 */
PromptAnswer capture_answer(const PromptNode& node) ;
} // namespace prompt
