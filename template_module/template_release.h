#pragma once
// ============================================================================
// template_release.h — 按模块释放内嵌模板代码，并统一编码与行尾。
//
// 入口代码释放到工程根 main.cpp；其余模块释放到 templates/ 目录。
// 基础定义文件由依赖隐式带上，不出现在界面选项里。编码与行尾只作用于源码，
// 写盘由调用方用 core::file_io 完成。
// ============================================================================
#include "../core_module/context.h"
#include <string>
#include <vector>

namespace template_release
{
/** 可勾选的模板模块；基础定义不在界面出现，由依赖自动带上。 */
enum class TemplateModule
{
    EntryCode ,
    Guards ,
    VectorLib ,
    MatrixLib ,
    PointLib ,
    QuaternionLib
} ;

/** 模块元信息，供界面生成多选项与依赖提示。 */
struct TemplateModuleInfo
{
    /** 模块标识。 */
    TemplateModule m_module = TemplateModule::EntryCode ;
    /** 稳定键，用于日志与测试。 */
    std::string m_key ;
    /** 界面标题。 */
    std::string m_title ;
    /** 界面提示。 */
    std::string m_hint ;
    /** 依赖模块，不含自身。 */
    std::vector<TemplateModule> m_dependencies ;
} ;

/** 一个待写盘的目标工程文件。 */
struct TemplateFile
{
    /** 相对工程根目录的目标路径，如 "main.cpp"、"templates/output.hpp"。 */
    std::string m_destination ;
    /** 已按指定编码与行尾处理好的最终字节。 */
    std::string m_content ;
} ;

/** 全部可选模块的元信息，按固定顺序。 */
const std::vector<TemplateModuleInfo>& template_modules() ;

/** 展开依赖闭包并按固定顺序去重；入口代码独立，不依赖其它模块。 */
std::vector<TemplateModule> resolve_modules(const std::vector<TemplateModule>& selected) ;

/** 按模块集合释放代码；编码与行尾只作用于源码文件。 */
std::vector<TemplateFile> release_templates(const std::vector<TemplateModule>& modules,
    ctx::Coding coding, ctx::LineEnding line_ending) ;

/** 兼容旧入口：释放全量模块。 */
std::vector<TemplateFile> release_templates(ctx::Coding coding, ctx::LineEnding line_ending) ;
} // namespace template_release
