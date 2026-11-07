#pragma once
// ============================================================================
// test_prompt_back.h — 流程回退（Ctrl+Z）的自动测试。
//
// 用 InputElement + 模拟按键驱动 PromptFlow：第二次进入的节点按 Back 键，
// 验证流程退到最近一个已提交节点并保留控件里已填的值。不弹窗口，不写文件。
// ============================================================================
#include "../prompt_module/prompt_control.h"
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace test_prompt_back_detail
{
/** 本组测试的失败次数。 */
inline int& failures()
{
    static int count = 0 ;
    return count ;
}

/** 断言；失败时打印并计数。 */
inline void check(bool condition, const std::string& label)
{
    if (!condition)
    {
        ++failures() ;
        std::cerr << "[FAIL] test_prompt_back: " << label << '\n' ;
    }
}

/** 把一段 ASCII 文本逐个按键敲进控件。 */
inline void type_text(console::ConsoleElement& element, const std::string& text)
{
    for (const char letter : text)
    {
        element.handle_key(static_cast<unsigned char>(letter)) ;
    }
}
} // namespace test_prompt_back_detail

/** 流程回退自检；全部通过返回 0。 */
inline int test_prompt_back()
{
    using test_prompt_back_detail::check ;
    using test_prompt_back_detail::failures ;
    using test_prompt_back_detail::type_text ;
    failures() = 0 ;

    // 两个输入节点：第二个节点第一次进入时按 Ctrl+Z 退回，重问第一个。
    std::vector<std::string> visited ;
    int back_budget = 1 ;
    std::string first_value ;
    std::string second_value ;
    const prompt::PromptRunner runner = [&visited, &back_budget, &first_value, &second_value](
        console::ConsoleElement& element)
    {
        const std::string title = element.get_title() ;
        const std::size_t visits = static_cast<std::size_t>(
            std::count(visited.begin(), visited.end(), title)) + 1 ;
        visited.push_back(title) ;
        if (title == "second" && back_budget > 0 && visits == 1)
        {
            --back_budget ;
            element.handle_key(console::keyboard::back) ;
            return element.get_state() ;
        }
        if (title == "first" && visits == 1)
        {
            type_text(element, "a") ;
        }
        if (title == "first" && visits > 1)
        {
            // 回退后控件里应保留着 "a"，再补一个字符验证是预填而不是清空。
            type_text(element, "x") ;
        }
        if (title == "second" && visits > 1)
        {
            type_text(element, "b") ;
        }
        element.handle_key(console::keyboard::enter) ;
        return element.get_state() ;
    } ;

    prompt::PromptFlow flow(runner) ;
    {
        auto element = std::make_unique<console::InputElement>() ;
        element->set_title("first") ;
        element->set_allow_empty(true) ;
        prompt::PromptNode node ;
        node.m_id = "first" ;
        node.m_element = std::move(element) ;
        node.m_on_submit = [&first_value](prompt::PromptFlow&, const prompt::PromptAnswer& answer)
        {
            first_value = std::get<std::string>(answer.m_value) ;
        } ;
        flow.append(std::move(node)) ;
    }
    {
        auto element = std::make_unique<console::InputElement>() ;
        element->set_title("second") ;
        element->set_allow_empty(true) ;
        prompt::PromptNode node ;
        node.m_id = "second" ;
        node.m_element = std::move(element) ;
        node.m_on_submit = [&second_value](prompt::PromptFlow&, const prompt::PromptAnswer& answer)
        {
            second_value = std::get<std::string>(answer.m_value) ;
        } ;
        flow.append(std::move(node)) ;
    }

    const prompt::PromptRunResult result = flow.run() ;
    check(result.m_state == prompt::PromptFlowState::Completed, "回退后流程仍能跑完") ;
    check(visited.size() == 4 && visited[0] == "first" && visited[1] == "second" &&
        visited[2] == "first" && visited[3] == "second", "回退访问顺序是 first/second/first/second") ;
    check(first_value == "ax", "回退重问保留原值并追加修改") ;
    check(second_value == "b", "回退后第二个节点重新提交") ;

    // 第一步按 Ctrl+Z：原地重问，然后正常提交。
    std::vector<std::string> first_visited ;
    int first_back_budget = 1 ;
    std::string single_value ;
    const prompt::PromptRunner single_runner = [&first_visited, &first_back_budget](
        console::ConsoleElement& element)
    {
        first_visited.push_back(element.get_title()) ;
        if (first_back_budget > 0)
        {
            --first_back_budget ;
            element.handle_key(console::keyboard::back) ;
            return element.get_state() ;
        }
        type_text(element, "z") ;
        element.handle_key(console::keyboard::enter) ;
        return element.get_state() ;
    } ;
    prompt::PromptFlow single_flow(single_runner) ;
    {
        auto element = std::make_unique<console::InputElement>() ;
        element->set_title("only") ;
        element->set_allow_empty(true) ;
        prompt::PromptNode node ;
        node.m_id = "only" ;
        node.m_element = std::move(element) ;
        node.m_on_submit = [&single_value](prompt::PromptFlow&, const prompt::PromptAnswer& answer)
        {
            single_value = std::get<std::string>(answer.m_value) ;
        } ;
        single_flow.append(std::move(node)) ;
    }
    const prompt::PromptRunResult single_result = single_flow.run() ;
    check(single_result.m_state == prompt::PromptFlowState::Completed, "第一步回退后仍能跑完") ;
    check(first_visited.size() == 2, "第一步回退会原地重问一次") ;
    check(single_value == "z", "第一步回退后正常提交") ;

    const int failed = failures() ;
    if (failed == 0)
    {
        std::cout << "[ OK ] test_prompt_back\n" ;
        return 0 ;
    }
    std::cerr << "[FAIL] test_prompt_back: " << failed << " 项失败\n" ;
    return 1 ;
}
