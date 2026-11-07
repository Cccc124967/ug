#include "prompt_flow_data.h"
#include <exception>
#include <stdexcept>
#include <utility>

namespace
{
/** 在回调边界恢复阶段标记，异常不会留下正在执行的假状态。 */
class PromptPhaseScope
{
    public:
    explicit PromptPhaseScope(bool& flag) : m_flag(flag), m_previous(flag)
    {
        m_flag = true ;
    }
    ~PromptPhaseScope()
    {
        m_flag = m_previous ;
    }
    PromptPhaseScope(const PromptPhaseScope&) = delete ;
    /** 禁止复制阶段作用域。 */
    PromptPhaseScope& operator=(const PromptPhaseScope&) = delete ;

    private:
    bool& m_flag ;
    bool m_previous ;
} ;

/** 保持活动节点存活到提交回调结束，并在离开执行入口时解除占用。 */
class PromptExecutionScope
{
    public:
    explicit PromptExecutionScope(prompt::PromptFlowData& data) : m_data(data)
    {
        m_data.m_executing = true ;
    }
    ~PromptExecutionScope()
    {
        m_data.m_active.reset() ;
        m_data.m_executing = false ;
    }
    PromptExecutionScope(const PromptExecutionScope&) = delete ;
    /** 禁止复制执行作用域。 */
    PromptExecutionScope& operator=(const PromptExecutionScope&) = delete ;

    private:
    prompt::PromptFlowData& m_data ;
} ;

/** 保留停止原因，取消不会伪装成成功完成。 */
prompt::PromptFlowState stopped_state(console::PromptState state)
{
    switch (state)
    {
        case console::PromptState::Cancelled: return prompt::PromptFlowState::Cancelled ;
        case console::PromptState::Interrupted: return prompt::PromptFlowState::Interrupted ;
        case console::PromptState::Unavailable: return prompt::PromptFlowState::Unavailable ;
        default: throw std::logic_error("Prompt runner did not finish the interaction") ;
    }
}
} // namespace

namespace prompt
{
PromptAnswer capture_answer(const PromptNode& node)
{
    PromptAnswer answer ;
    answer.m_node_id = node.m_id ;
    answer.m_type = node.m_element->get_type() ;
    answer.m_text = node.m_element->result_text() ;
    // 派生控件必须先于基类判断：MultiSelect -> SingleSelect，Selector -> Input。
    if (const auto* multi = dynamic_cast<const console::MultiSelectElement*>(node.m_element.get()))
    {
        answer.m_value = multi->get_selected_indices() ;
    }
    else if (const auto* selector = dynamic_cast<const console::SelectorElement*>(node.m_element.get()))
    {
        answer.m_value = selector->get_path() ;
    }
    else if (const auto* single = dynamic_cast<const console::SingleSelectElement*>(node.m_element.get()))
    {
        answer.m_value = single->get_selected_index() ;
    }
    else if (const auto* input = dynamic_cast<const console::InputElement*>(node.m_element.get()))
    {
        answer.m_value = input->get_value() ;
    }
    else
    {
        answer.m_value = answer.m_text ;
    }
    return answer ;
}

PromptRunResult PromptFlow::execute_next(const PromptRunner& runner)
{
    std::shared_ptr<PromptEntry> current ;
    try
    {
        for (const auto& entry : m_data->m_entries)
        {
            if (entry->m_state == PromptNodeState::Submitted)
            {
                continue ;
            }
            current = entry ;
            m_data->m_active = current ;
            m_data->m_result = { PromptFlowState::Running, current->m_node.m_id, {} } ;
            bool enabled = true ;
            if (current->m_node.m_condition)
            {
                PromptPhaseScope phase(m_data->m_checking_condition) ;
                enabled = current->m_node.m_condition(*this) ;
            }
            if (enabled)
            {
                break ;
            }
            current->m_state = PromptNodeState::Skipped ;
            current.reset() ;
        }
        if (!current)
        {
            m_data->m_active.reset() ;
            m_data->m_result = { PromptFlowState::Completed, {}, {} } ;
            return m_data->m_result ;
        }

        current->m_state = PromptNodeState::Active ;
        current->m_node.m_element->begin() ;
        console::PromptState state ;
        {
            PromptPhaseScope phase(m_data->m_running_control) ;
            state = runner(*current->m_node.m_element) ;
        }
        if (state != current->m_node.m_element->get_state())
        {
            throw std::logic_error("Prompt runner result does not match the console element state") ;
        }
        if (state == console::PromptState::Back)
        {
            // 返回上一步：把最近一个已提交节点及其之后全部恢复为 Pending；
            // 控件值保留，重问时直接预填，用户只改要改的那一步。
            std::size_t current_index = m_data->m_entries.size() ;
            for (std::size_t index = 0 ; index < m_data->m_entries.size() ; ++index)
            {
                if (m_data->m_entries[index] == current)
                {
                    current_index = index ;
                    break ;
                }
            }
            std::size_t target = m_data->m_entries.size() ;
            for (std::size_t index = current_index ; index > 0 ; --index)
            {
                if (m_data->m_entries[index - 1]->m_state == PromptNodeState::Submitted)
                {
                    target = index - 1 ;
                    break ;
                }
            }
            if (target < m_data->m_entries.size())
            {
                for (std::size_t index = target ; index < m_data->m_entries.size() ; ++index)
                {
                    m_data->m_entries[index]->m_state = PromptNodeState::Pending ;
                }
            }
            current->m_state = PromptNodeState::Pending ;
            m_data->m_result = { PromptFlowState::Ready, current->m_node.m_id, {} } ;
            return m_data->m_result ;
        }
        if (state != console::PromptState::Submitted)
        {
            const auto stopped = stopped_state(state) ;
            current->m_state = PromptNodeState::Pending ;
            m_data->m_result = { stopped, current->m_node.m_id, {} } ;
            return m_data->m_result ;
        }

        const auto answer = capture_answer(current->m_node) ;
        current->m_answer = answer ;
        current->m_state = PromptNodeState::Submitted ;
        // 快照保护当前回调，允许它替换自身回调、删除自身或清空整个流程。
        const auto handler = current->m_node.m_on_submit ;
        if (handler)
        {
            handler(*this, answer) ;
        }
        m_data->m_result = { PromptFlowState::Ready, answer.m_node_id, {} } ;
    }
    catch (const std::exception& error)
    {
        if (current)
        {
            current->m_state = PromptNodeState::Failed ;
        }
        m_data->m_result = { PromptFlowState::Failed, current ? current->m_node.m_id : "", error.what() } ;
    }
    catch (...)
    {
        if (current)
        {
            current->m_state = PromptNodeState::Failed ;
        }
        m_data->m_result = { PromptFlowState::Failed, current ? current->m_node.m_id : "", "Unknown prompt callback exception" } ;
    }
    return m_data->m_result ;
}

PromptRunResult PromptFlow::run_impl(bool single_step)
{
    check_idle(*m_data) ;
    if (m_data->m_result.m_state == PromptFlowState::Failed)
    {
        return m_data->m_result ;
    }
    PromptExecutionScope execution(*m_data) ;
    std::unique_ptr<console::ConsoleSession> session ;
    auto runner = m_data->m_runner ;
    if (!runner)
    {
        session = std::make_unique<console::ConsoleSession>() ;
        runner = [&session](console::ConsoleElement& element)
        {
            return session->run(element) ;
        } ;
    }
    do
    {
        execute_next(runner) ;
        m_data->m_active.reset() ;
    }
    while (!single_step && m_data->m_result.m_state == PromptFlowState::Ready) ;
    return m_data->m_result ;
}

PromptRunResult PromptFlow::run()
{
    return run_impl(false) ;
}

PromptRunResult PromptFlow::run_next()
{
    return run_impl(true) ;
}
} // namespace prompt
