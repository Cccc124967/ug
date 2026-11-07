#pragma once
// ============================================================================
// test_template_modules.h — template_release 的纯逻辑自检（不写本机，不弹窗口）。
//
// 覆盖：模块元信息与依赖表、依赖闭包展开与排序、按模块释放的文件落位、
// 基础定义文件的隐式带入、编码与行尾只作用于源码。由 app/main.cpp 调用，
// 不自己写 main()。
// ============================================================================
#include "../template_module/template_release.h"
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int g_template_test_failures = 0 ;
int g_template_test_checks = 0 ;

/** 记一条断言结果；通过打 INFO，失败打 ERROR 并累计。 */
void template_test_report(bool passed, const std::string& what)
{
    ++g_template_test_checks ;
    if (passed)
    {
        std::cout << "[INFO] template: " << what << '\n' ;
        return ;
    }
    ++g_template_test_failures ;
    std::cerr << "[ERROR] template: " << what << '\n' ;
}

/** 布尔断言。 */
void template_test_check(bool passed, const std::string& what)
{
    template_test_report(passed, what) ;
}

/** 文本相等断言；失败时附带期望值与实际值。 */
void template_test_text(const std::string& actual, const std::string& expected, const std::string& what)
{
    const bool passed = actual == expected ;
    template_test_report(passed, what) ;
    if (!passed)
    {
        std::cerr << "        期望: [" << expected << "]\n" ;
        std::cerr << "        实际: [" << actual << "]\n" ;
    }
}

/** 数量相等断言。 */
void template_test_number(std::size_t actual, std::size_t expected, const std::string& what)
{
    const bool passed = actual == expected ;
    template_test_report(passed, what) ;
    if (!passed)
    {
        std::cerr << "        期望: " << expected << "\n" ;
        std::cerr << "        实际: " << actual << "\n" ;
    }
}

/** 按模块标识找元信息；找不到返回 nullptr。 */
const template_release::TemplateModuleInfo* template_test_info(template_release::TemplateModule module)
{
    for (const template_release::TemplateModuleInfo& info : template_release::template_modules())
    {
        if (info.m_module == module)
        {
            return &info ;
        }
    }
    return nullptr ;
}

/** 模块集合里是否有某个模块。 */
bool template_test_has_module(const std::vector<template_release::TemplateModule>& modules,
    template_release::TemplateModule module)
{
    for (const template_release::TemplateModule existing : modules)
    {
        if (existing == module)
        {
            return true ;
        }
    }
    return false ;
}

/** 两个模块序列完全相等。 */
bool template_test_same_modules(const std::vector<template_release::TemplateModule>& actual,
    const std::vector<template_release::TemplateModule>& expected)
{
    if (actual.size() != expected.size())
    {
        return false ;
    }
    for (std::size_t index = 0 ; index < actual.size() ; ++index)
    {
        if (actual[index] != expected[index])
        {
            return false ;
        }
    }
    return true ;
}

/** 按目标相对路径找释放出的文件；找不到返回 nullptr。 */
const template_release::TemplateFile* template_test_find_file(
    const std::vector<template_release::TemplateFile>& files, const std::string& destination)
{
    for (const template_release::TemplateFile& file : files)
    {
        if (file.m_destination == destination)
        {
            return &file ;
        }
    }
    return nullptr ;
}

/** 释放结果里是否包含某个目标相对路径。 */
bool template_test_has_destination(const std::vector<template_release::TemplateFile>& files,
    const std::string& destination)
{
    return template_test_find_file(files, destination) != nullptr ;
}

/** 内容里是否包含一段文本。 */
bool template_test_contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos ;
}

/** 文本里是否完全没有 CR（LF 行尾）。 */
bool template_test_has_no_cr(const std::string& text)
{
    return text.find('\r') == std::string::npos ;
}

/** 文本是否是纯 CRLF 行尾：每个 LF 前面都有 CR。 */
bool template_test_is_crlf_only(const std::string& text)
{
    bool has_newline = false ;
    for (std::size_t index = 0 ; index < text.size() ; ++index)
    {
        if (text[index] != '\n')
        {
            continue ;
        }
        has_newline = true ;
        if (index == 0 || text[index - 1] != '\r')
        {
            return false ;
        }
    }
    return has_newline ;
}

/** 目录里释放出的相对路径序列完全一致。 */
void template_test_destinations(const std::vector<template_release::TemplateFile>& files,
    const std::vector<std::string>& expected, const std::string& what)
{
    std::vector<std::string> actual ;
    actual.reserve(files.size()) ;
    for (const template_release::TemplateFile& file : files)
    {
        actual.push_back(file.m_destination) ;
    }
    const bool passed = actual == expected ;
    template_test_report(passed, what) ;
    if (passed)
    {
        return ;
    }
    std::cerr << "        期望: [" ;
    for (const std::string& destination : expected)
    {
        std::cerr << destination << " | " ;
    }
    std::cerr << "]\n        实际: [" ;
    for (const std::string& destination : actual)
    {
        std::cerr << destination << " | " ;
    }
    std::cerr << "]\n" ;
}

/** 元信息：顺序、稳定键、标题提示与依赖表。 */
void template_test_module_metadata()
{
    const std::vector<template_release::TemplateModuleInfo>& modules = template_release::template_modules() ;
    template_test_number(modules.size(), 6, "元信息: 6 个可选模块") ;
    if (modules.size() != 6)
    {
        return ;
    }

    const std::vector<std::string> expected_keys = { "entry_code" , "guards" , "vector" , "matrix" ,
        "point" , "quaternion" } ;
    for (std::size_t index = 0 ; index < modules.size() ; ++index)
    {
        template_test_text(modules[index].m_key, expected_keys[index], "元信息: 第 " +
            std::to_string(index + 1) + " 个稳定键") ;
        template_test_check(!modules[index].m_title.empty(), "元信息: " + modules[index].m_key + " 有标题") ;
        template_test_check(!modules[index].m_hint.empty(), "元信息: " + modules[index].m_key + " 有提示") ;
    }

    const auto dependencies = [](template_release::TemplateModule module)
    {
        std::vector<template_release::TemplateModule> result ;
        const template_release::TemplateModuleInfo* info = template_test_info(module) ;
        if (info != nullptr)
        {
            result = info->m_dependencies ;
        }
        return result ;
    } ;
    template_test_check(dependencies(template_release::TemplateModule::EntryCode).empty(),
        "元信息: 入口代码不依赖其它模块") ;
    template_test_check(dependencies(template_release::TemplateModule::Guards).empty(),
        "元信息: 守卫宏不依赖其它模块") ;
    template_test_check(dependencies(template_release::TemplateModule::VectorLib).empty(),
        "元信息: 向量库不依赖其它模块") ;
    template_test_check(template_test_same_modules(
        dependencies(template_release::TemplateModule::MatrixLib),
        { template_release::TemplateModule::VectorLib }), "元信息: 矩阵库依赖向量库") ;
    template_test_check(template_test_same_modules(
        dependencies(template_release::TemplateModule::PointLib),
        { template_release::TemplateModule::VectorLib }), "元信息: 点库依赖向量库") ;
    template_test_check(template_test_same_modules(
        dependencies(template_release::TemplateModule::QuaternionLib),
        { template_release::TemplateModule::MatrixLib , template_release::TemplateModule::VectorLib }),
        "元信息: 四元数库依赖矩阵库与向量库") ;
}

/** 依赖闭包：展开、去重、按固定顺序排序。 */
void template_test_resolve_closure()
{
    const std::vector<template_release::TemplateModule> empty ;
    template_test_check(template_release::resolve_modules(empty).empty(), "闭包: 空选择为空") ;

    template_test_check(template_test_same_modules(
        template_release::resolve_modules({ template_release::TemplateModule::MatrixLib }),
        { template_release::TemplateModule::VectorLib , template_release::TemplateModule::MatrixLib }),
        "闭包: 矩阵库带上向量库并排好顺序") ;

    template_test_check(template_test_same_modules(
        template_release::resolve_modules({ template_release::TemplateModule::QuaternionLib }),
        { template_release::TemplateModule::VectorLib , template_release::TemplateModule::MatrixLib ,
          template_release::TemplateModule::QuaternionLib }),
        "闭包: 四元数库带上矩阵库与向量库") ;

    template_test_check(template_test_same_modules(
        template_release::resolve_modules({ template_release::TemplateModule::PointLib ,
            template_release::TemplateModule::VectorLib }),
        { template_release::TemplateModule::VectorLib , template_release::TemplateModule::PointLib }),
        "闭包: 重复勾选去重") ;

    template_test_check(template_test_same_modules(
        template_release::resolve_modules({ template_release::TemplateModule::EntryCode ,
            template_release::TemplateModule::MatrixLib }),
        { template_release::TemplateModule::EntryCode , template_release::TemplateModule::VectorLib ,
          template_release::TemplateModule::MatrixLib }),
        "闭包: 入口代码与库模块共存") ;

    template_test_check(template_test_same_modules(
        template_release::resolve_modules({ template_release::TemplateModule::MatrixLib ,
            template_release::TemplateModule::MatrixLib }),
        { template_release::TemplateModule::VectorLib , template_release::TemplateModule::MatrixLib }),
        "闭包: 同一模块重复出现只留一个") ;

    template_test_check(template_test_same_modules(
        template_release::resolve_modules({ template_release::TemplateModule::EntryCode }),
        { template_release::TemplateModule::EntryCode }), "闭包: 入口代码独立") ;
}

/** 全量释放：文件落位与内容片段。 */
void template_test_release_layout()
{
    const std::vector<template_release::TemplateFile> files =
        template_release::release_templates(ctx::Coding::UTF8, ctx::LineEnding::LF) ;
    template_test_destinations(files, { "templates/basic_type_defs.h" , "main.cpp" ,
        "templates/output.hpp" , "templates/vector.hpp" , "templates/matrix.hpp" ,
        "templates/point.hpp" , "templates/quaternion.hpp" }, "落位: 全量释放的 7 个文件") ;

    const auto content = [&files](const std::string& destination) -> std::string
    {
        const template_release::TemplateFile* file = template_test_find_file(files, destination) ;
        return file == nullptr ? std::string() : file->m_content ;
    } ;
    template_test_check(template_test_contains(content("main.cpp"), "ufusr"), "内容: main.cpp 有 ufusr") ;
    template_test_check(template_test_contains(content("main.cpp"), "ufusr_ask_unload"),
        "内容: main.cpp 有卸载询问钩子") ;
    template_test_check(template_test_contains(content("templates/basic_type_defs.h"), "NX_NAMESPACE_BEGIN"),
        "内容: 基础定义有命名空间宏") ;
    template_test_check(template_test_contains(content("templates/output.hpp"), "nx_msg"),
        "内容: 守卫宏有输出工具") ;
    template_test_check(template_test_contains(content("templates/output.hpp"), "class UFGuard"),
        "内容: 守卫宏有 RAII 句柄") ;
    template_test_check(template_test_contains(content("templates/vector.hpp"), "struct Vec3"),
        "内容: 向量库有 Vec3") ;
    template_test_check(template_test_contains(content("templates/matrix.hpp"), "struct Matrix4x4"),
        "内容: 矩阵库有 Matrix4x4") ;
    template_test_check(template_test_contains(content("templates/point.hpp"), "struct Point"),
        "内容: 点库有 Point") ;
    template_test_check(template_test_contains(content("templates/quaternion.hpp"), "struct Quaternion"),
        "内容: 四元数库有 Quaternion") ;

    // 旧入口 = 全量；新入口按模块列表手动拼出全量应完全一致。
    std::vector<template_release::TemplateModule> all_modules ;
    for (const template_release::TemplateModuleInfo& info : template_release::template_modules())
    {
        all_modules.push_back(info.m_module) ;
    }
    template_test_destinations(template_release::release_templates(all_modules,
        ctx::Coding::UTF8, ctx::LineEnding::LF), { "templates/basic_type_defs.h" , "main.cpp" ,
        "templates/output.hpp" , "templates/vector.hpp" , "templates/matrix.hpp" ,
        "templates/point.hpp" , "templates/quaternion.hpp" },
        "落位: 模块列表全量释放与旧入口一致") ;
}

/** 按模块裁剪：依赖自动带入，基础定义随任一非入口模块出现。 */
void template_test_release_selection()
{
    const std::vector<template_release::TemplateFile> entry_only =
        template_release::release_templates({ template_release::TemplateModule::EntryCode },
            ctx::Coding::UTF8, ctx::LineEnding::LF) ;
    template_test_destinations(entry_only, { "main.cpp" }, "裁剪: 只勾入口代码只出 main.cpp") ;

    const std::vector<template_release::TemplateFile> matrix_only =
        template_release::release_templates({ template_release::TemplateModule::MatrixLib },
            ctx::Coding::UTF8, ctx::LineEnding::LF) ;
    template_test_destinations(matrix_only, { "templates/basic_type_defs.h" ,
        "templates/vector.hpp" , "templates/matrix.hpp" }, "裁剪: 矩阵库自动带上向量库与基础定义") ;
    template_test_check(!template_test_has_destination(matrix_only, "main.cpp"),
        "裁剪: 没勾入口就不出 main.cpp") ;

    const std::vector<template_release::TemplateFile> point_only =
        template_release::release_templates({ template_release::TemplateModule::PointLib },
            ctx::Coding::UTF8, ctx::LineEnding::LF) ;
    template_test_destinations(point_only, { "templates/basic_type_defs.h" ,
        "templates/vector.hpp" , "templates/point.hpp" }, "裁剪: 点库自动带上向量库") ;

    const std::vector<template_release::TemplateFile> quaternion_only =
        template_release::release_templates({ template_release::TemplateModule::QuaternionLib },
            ctx::Coding::UTF8, ctx::LineEnding::LF) ;
    template_test_destinations(quaternion_only, { "templates/basic_type_defs.h" ,
        "templates/vector.hpp" , "templates/matrix.hpp" , "templates/quaternion.hpp" },
        "裁剪: 四元数库自动带上矩阵库与向量库") ;

    const std::vector<template_release::TemplateFile> guards_only =
        template_release::release_templates({ template_release::TemplateModule::Guards },
            ctx::Coding::UTF8, ctx::LineEnding::LF) ;
    template_test_destinations(guards_only, { "templates/basic_type_defs.h" ,
        "templates/output.hpp" }, "裁剪: 守卫宏单独释放也带基础定义") ;

    const std::vector<template_release::TemplateFile> nothing =
        template_release::release_templates({}, ctx::Coding::UTF8, ctx::LineEnding::LF) ;
    template_test_check(nothing.empty(), "裁剪: 空选择不释放任何文件") ;

    const std::vector<template_release::TemplateFile> mixed = template_release::release_templates(
        { template_release::TemplateModule::EntryCode , template_release::TemplateModule::QuaternionLib },
        ctx::Coding::UTF8, ctx::LineEnding::LF) ;
    template_test_destinations(mixed, { "templates/basic_type_defs.h" , "main.cpp" ,
        "templates/vector.hpp" , "templates/matrix.hpp" , "templates/quaternion.hpp" },
        "裁剪: 入口 + 四元数库") ;
}

/** 编码与行尾：LF 无 CR、CRLF 纯 CRLF、GB2312 真的转码。 */
void template_test_coding_and_line_ending()
{
    const std::vector<template_release::TemplateFile> lf_files =
        template_release::release_templates(ctx::Coding::UTF8, ctx::LineEnding::LF) ;
    bool all_lf = !lf_files.empty() ;
    for (const template_release::TemplateFile& file : lf_files)
    {
        if (!template_test_has_no_cr(file.m_content))
        {
            all_lf = false ;
        }
    }
    template_test_check(all_lf, "行尾: UTF-8 + LF 全部输出没有 CR") ;

    const std::vector<template_release::TemplateFile> crlf_files =
        template_release::release_templates(ctx::Coding::UTF8, ctx::LineEnding::CRLF) ;
    bool all_crlf = !crlf_files.empty() ;
    for (const template_release::TemplateFile& file : crlf_files)
    {
        if (!template_test_is_crlf_only(file.m_content))
        {
            all_crlf = false ;
        }
    }
    template_test_check(all_crlf, "行尾: CRLF 全部输出是纯 CRLF") ;

    const std::vector<template_release::TemplateFile> utf8_entry_files =
        template_release::release_templates({ template_release::TemplateModule::EntryCode },
            ctx::Coding::UTF8, ctx::LineEnding::LF) ;
    const template_release::TemplateFile* utf8_entry =
        template_test_find_file(utf8_entry_files, "main.cpp") ;
    template_test_check(utf8_entry != nullptr, "编码: UTF-8 入口文件存在") ;
    if (utf8_entry != nullptr)
    {
        template_test_check(template_test_contains(utf8_entry->m_content,
            std::string("\xE6\xAC\xA2\xE8\xBF\x8E")), "编码: UTF-8 入口含 UTF-8 中文") ;
    }

    const std::vector<template_release::TemplateFile> gb_files =
        template_release::release_templates({ template_release::TemplateModule::EntryCode },
            ctx::Coding::GB2312, ctx::LineEnding::CRLF) ;
    const template_release::TemplateFile* gb_entry = template_test_find_file(gb_files, "main.cpp") ;
    template_test_check(gb_entry != nullptr, "编码: GB2312 入口文件存在") ;
    if (gb_entry != nullptr && utf8_entry != nullptr)
    {
        template_test_check(template_test_contains(gb_entry->m_content,
            std::string("\xBB\xB6\xD3\xAD")), "编码: GB2312 入口含 GB2312 中文") ;
        template_test_check(!template_test_contains(gb_entry->m_content,
            std::string("\xE6\xAC\xA2")), "编码: GB2312 入口不再是 UTF-8 字节") ;
        template_test_check(gb_entry->m_content.size() != utf8_entry->m_content.size(),
            "编码: GB2312 与 UTF-8 字节数不同") ;
        template_test_check(template_test_is_crlf_only(gb_entry->m_content),
            "编码: GB2312 + CRLF 仍是纯 CRLF") ;
    }
}
} // namespace

/** template_release 的自动自检；返回失败条数（0 = 全部通过）。 */
inline int test_template_modules()
{
    g_template_test_failures = 0 ;
    g_template_test_checks = 0 ;
    template_test_module_metadata() ;
    template_test_resolve_closure() ;
    template_test_release_layout() ;
    template_test_release_selection() ;
    template_test_coding_and_line_ending() ;

    if (g_template_test_failures == 0)
    {
        std::cout << "[INFO] template_release 自检通过: " << g_template_test_checks
                  << " 项断言全部通过\n" ;
    }
    else
    {
        std::cerr << "[ERROR] template_release 自检失败: " << g_template_test_failures << " / "
                  << g_template_test_checks << " 项\n" ;
    }
    return g_template_test_failures ;
}
