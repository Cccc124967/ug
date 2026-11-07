#pragma once
// ============================================================================
// project_build.h — 把一次交互的全部选择落到磁盘，生成可构建的 NX 插件工程。
//
// 先在目标同级临时目录写完整工程，成功后再替换目标目录；中途失败时旧目录
// 原封不动。生成顺序：模板源码 → CMakeLists.txt → CMakePresets.json →
// .vscode/settings.json → git 初始化。
// ============================================================================
#include "../core_module/context.h"
#include "../scan_module/build_plan.h"
#include "../scan_module/tool_chain_scan.h"
#include "../template_module/template_release.h"
#include <filesystem>
#include <string>
#include <vector>

namespace app
{
/** 一次工程生成的完整输入。 */
struct ProjectBuildRequest
{
    /** 工程所在父目录；最终目录为 <父目录>/<项目名>。 */
    std::filesystem::path m_parent_directory ;
    /** 项目名，同时是产物 DLL 名。 */
    std::string m_project_name ;
    /** 目标目录已存在时是否先替换。 */
    bool m_overwrite = false ;
    /** 勾选的模板模块；空表示空工程（不写源码与 CMakeLists）。 */
    std::vector<template_release::TemplateModule> m_template_modules ;
    /** 源码编码。 */
    ctx::Coding m_coding = ctx::Coding::GB2312 ;
    /** 源码行尾。 */
    ctx::LineEnding m_line_ending = ctx::LineEnding::CRLF ;
    /** 是否初始化 git。 */
    bool m_init_git = true ;
    /** 选中的编译器；提供 include 之外的生成器信息。 */
    scan::Compiler m_compiler ;
    /** 选中的 NX SDK；提供 include 与库目录。 */
    scan::NXSdk m_nx_sdk ;
    /** 选中的 Windows SDK；版本为空表示不锁。 */
    scan::WindowsSdk m_windows_sdk ;
    /** CMake 生成器名，如 "Visual Studio <主版本> <年份>"；为空表示无法生成 CMakePresets。 */
    std::string m_generator ;
    /** 构建方案：决定生成器、工具链文件与 make 程序；未解析时退回上面两个旧字段。 */
    scan::BuildPlan m_build_plan ;
    /** 本机是否探测到可用的 Ninja；旧路径下才用来决定 settings 里的 cmake.generator。 */
    bool m_ninja_available = false ;
} ;

/** 生成结果。 */
struct ProjectBuildResult
{
    /** 是否整体成功。 */
    bool m_ok = false ;
    /** 失败原因或成功摘要。 */
    std::string m_message ;
    /** 实际落盘的项目目录。 */
    std::filesystem::path m_project_directory ;
    /** 写出的文件相对路径列表。 */
    std::vector<std::string> m_files ;
    /** git 步骤说明；没有执行 git 时为空。 */
    std::string m_git_message ;
    /** git 是否成功初始化。 */
    bool m_git_ok = false ;
} ;

/** 生成工程；失败时返回原因且不破坏已存在的目标目录。 */
ProjectBuildResult build_project(const ProjectBuildRequest& request) ;
} // namespace app
