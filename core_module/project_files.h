#pragma once
// ============================================================================
// project_files.h — 生成工程的编辑器配置文件（.vscode/settings.json）。
//
// 把生成项目的源码编码、行尾与 CMake 预置约定固定下来，避免 VS Code 用错
// 编码打开 GB2312 源码（乱码/误存）。内容随 ctx::Coding / ctx::LineEnding
// 与 CMake 生成器变化；默认值（GB2312 + CRLF）就是标准答案。另外固定隐藏
// build、.git 等与本工程无关的条目，让资源管理器里只留下要看的源码。
// ============================================================================
#include "context.h"
#include <filesystem>
#include <string>

namespace core
{
/** 写工程配置文件的结果。 */
struct ProjectFileResult
{
    /** 是否写入成功。 */
    bool m_ok = false ;
    /** 失败原因或成功说明，供界面提示。 */
    std::string m_message = {} ;
} ;

/** 写 .vscode/settings.json；.vscode 目录不存在时自动创建，生成器为空则不写 cmake.generator。 */
ProjectFileResult write_vscode_settings(const std::filesystem::path& project_dir,
    ctx::Coding coding = ctx::Coding::GB2312,
    ctx::LineEnding line_ending = ctx::LineEnding::CRLF,
    const std::string& cmake_generator = {}) ;
} // namespace core
