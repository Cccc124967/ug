#include "prompt_flow_data.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace prompt
{
void check_mutation(const PromptFlowData& data)
{
    if (data.m_checking_condition || data.m_running_control)
    {
        throw std::logic_error("Flow edits are allowed before execution or in on_submit, not in conditions or runners") ;
    }
}

void check_idle(const PromptFlowData& data)
{
    if (data.m_executing)
    {
        throw std::logic_error("PromptFlow execution is not reentrant") ;
    }
}

std::size_t find_index(const PromptFlowData& data, const std::string& node_id) noexcept
{
    for (std::size_t index = 0 ; index < data.m_entries.size() ; ++index)
    {
        if (data.m_entries[index]->m_node.m_id == node_id)
        {
            return index ;
        }
    }
    return data.m_entries.size() ;
}

std::size_t require_index(const PromptFlowData& data, const std::string& node_id)
{
    const auto index = find_index(data, node_id) ;
    if (index == data.m_entries.size())
    {
        throw std::out_of_range("Unknown prompt node: " + node_id) ;
    }
    return index ;
}

void notify_change(PromptFlowData& data)
{
    if (!data.m_executing && data.m_result.m_state != PromptFlowState::Failed)
    {
        data.m_result = {} ;
    }
}

PromptFlow::PromptFlow(PromptRunner runner) : m_data(std::make_unique<PromptFlowData>())
{
    m_data->m_runner = std::move(runner) ;
}

PromptFlow::~PromptFlow() = default ;

void PromptFlow::append(PromptNode node)
{
    insert(size(), std::move(node)) ;
}

void PromptFlow::insert(std::size_t index, PromptNode node)
{
    check_mutation(*m_data) ;
    if (index > size())
    {
        throw std::out_of_range("Prompt insertion index is out of range") ;
    }
    if (node.m_id.empty() || !node.m_element || contains(node.m_id))
    {
        throw std::invalid_argument("Prompt node requires a unique nonempty id and an owned console element") ;
    }
    auto entry = std::make_shared<PromptEntry>() ;
    entry->m_node = std::move(node) ;
    m_data->m_entries.insert(m_data->m_entries.begin() + static_cast<std::ptrdiff_t>(index), std::move(entry)) ;
    notify_change(*m_data) ;
}

void PromptFlow::insert_before(const std::string& anchor_id, PromptNode node)
{
    insert(require_index(*m_data, anchor_id), std::move(node)) ;
}

void PromptFlow::insert_after(const std::string& anchor_id, PromptNode node)
{
    insert(require_index(*m_data, anchor_id) + 1, std::move(node)) ;
}

bool PromptFlow::remove(const std::string& node_id)
{
    check_mutation(*m_data) ;
    const auto index = find_index(*m_data, node_id) ;
    if (index == size())
    {
        return false ;
    }
    m_data->m_entries.erase(m_data->m_entries.begin() + static_cast<std::ptrdiff_t>(index)) ;
    if (!m_data->m_executing && m_data->m_result.m_state == PromptFlowState::Failed &&
        m_data->m_result.m_node_id == node_id)
    {
        m_data->m_result = {} ;
    }
    notify_change(*m_data) ;
    return true ;
}

void PromptFlow::move(const std::string& node_id, std::size_t index)
{
    check_mutation(*m_data) ;
    const auto current = require_index(*m_data, node_id) ;
    if (index >= size())
    {
        throw std::out_of_range("Prompt move index is out of range") ;
    }
    auto& entries = m_data->m_entries ;
    const auto begin = entries.begin() ;
    if (current < index)
    {
        std::rotate(begin + current, begin + current + 1, begin + index + 1) ;
    }
    else if (current > index)
    {
        std::rotate(begin + index, begin + current, begin + current + 1) ;
    }
    notify_change(*m_data) ;
}

void PromptFlow::move_before(const std::string& node_id, const std::string& anchor_id)
{
    const auto current = require_index(*m_data, node_id) ;
    const auto anchor = require_index(*m_data, anchor_id) ;
    move(node_id, current < anchor ? anchor - 1 : anchor) ;
}

void PromptFlow::move_after(const std::string& node_id, const std::string& anchor_id)
{
    const auto current = require_index(*m_data, node_id) ;
    const auto anchor = require_index(*m_data, anchor_id) ;
    move(node_id, current > anchor ? anchor + 1 : anchor) ;
}

void PromptFlow::swap_nodes(const std::string& first_id, const std::string& second_id)
{
    check_mutation(*m_data) ;
    const auto first = require_index(*m_data, first_id) ;
    const auto second = require_index(*m_data, second_id) ;
    std::swap(m_data->m_entries[first], m_data->m_entries[second]) ;
    notify_change(*m_data) ;
}

void PromptFlow::clear()
{
    check_mutation(*m_data) ;
    m_data->m_entries.clear() ;
    if (!m_data->m_executing)
    {
        m_data->m_result = {} ;
    }
}

std::size_t PromptFlow::size() const noexcept
{
    return m_data->m_entries.size() ;
}

bool PromptFlow::contains(const std::string& node_id) const noexcept
{
    return find_index(*m_data, node_id) != size() ;
}

std::vector<std::string> PromptFlow::get_order() const
{
    std::vector<std::string> result ;
    result.reserve(size()) ;
    for (const auto& entry : m_data->m_entries)
    {
        result.push_back(entry->m_node.m_id) ;
    }
    return result ;
}

PromptNodeState PromptFlow::get_node_state(const std::string& node_id) const
{
    return m_data->m_entries[require_index(*m_data, node_id)]->m_state ;
}

std::optional<PromptAnswer> PromptFlow::get_answer(const std::string& node_id) const
{
    const auto index = find_index(*m_data, node_id) ;
    return index == size() ? std::nullopt : m_data->m_entries[index]->m_answer ;
}

std::optional<std::string> PromptFlow::get_current_id() const
{
    return m_data->m_active ? std::optional<std::string>(m_data->m_active->m_node.m_id) : std::nullopt ;
}

console::ConsoleElement* PromptFlow::find_element(const std::string& node_id)
{
    if (m_data->m_checking_condition)
    {
        throw std::logic_error("Prompt conditions must be read-only") ;
    }
    const auto index = find_index(*m_data, node_id) ;
    return index == size() ? nullptr : m_data->m_entries[index]->m_node.m_element.get() ;
}

const console::ConsoleElement* PromptFlow::find_element(const std::string& node_id) const noexcept
{
    const auto index = find_index(*m_data, node_id) ;
    return index == size() ? nullptr : m_data->m_entries[index]->m_node.m_element.get() ;
}

PromptRunResult PromptFlow::get_result() const
{
    return m_data->m_result ;
}

void PromptFlow::set_condition(const std::string& node_id, PromptCondition condition)
{
    check_mutation(*m_data) ;
    m_data->m_entries[require_index(*m_data, node_id)]->m_node.m_condition = std::move(condition) ;
    notify_change(*m_data) ;
}

void PromptFlow::set_on_submit(const std::string& node_id, PromptSubmitHandler handler)
{
    check_mutation(*m_data) ;
    m_data->m_entries[require_index(*m_data, node_id)]->m_node.m_on_submit = std::move(handler) ;
}

void PromptFlow::set_runner(PromptRunner runner)
{
    check_idle(*m_data) ;
    m_data->m_runner = std::move(runner) ;
}

void PromptFlow::reset_node(const std::string& node_id, bool reset_value)
{
    check_mutation(*m_data) ;
    const auto entry = m_data->m_entries[require_index(*m_data, node_id)] ;
    if (entry == m_data->m_active)
    {
        throw std::logic_error("Cannot reset the active prompt node") ;
    }
    if (reset_value)
    {
        entry->m_node.m_element->reset() ;
    }
    entry->m_state = PromptNodeState::Pending ;
    entry->m_answer.reset() ;
    if (m_data->m_result.m_state == PromptFlowState::Failed && m_data->m_result.m_node_id == node_id)
    {
        m_data->m_result = {} ;
    }
    notify_change(*m_data) ;
}

void PromptFlow::restart(bool reset_values)
{
    check_idle(*m_data) ;
    for (const auto& entry : m_data->m_entries)
    {
        if (reset_values)
        {
            entry->m_node.m_element->reset() ;
        }
        entry->m_state = PromptNodeState::Pending ;
        entry->m_answer.reset() ;
    }
    m_data->m_result = {} ;
}
} // namespace prompt
