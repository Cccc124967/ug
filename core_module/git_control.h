#pragma once
// ============================================================================
// git_control.h — 生成工程时的 git 支持：探测 git、初始化仓库、写忽略文件。
//
// 只在用户勾选"生成后初始化 git"时调用；不弹窗，全部走 cmd.exe 控制台命令。
// 流程：where git（有输出才继续）→ git init → 切到 main 分支 →
// 写 .gitignore / .gitattributes（排除 build 产物，行尾跟随目标工程选择）。
// ============================================================================
#include "context.h"
#include <filesystem>
#include <string>

namespace core
{
/** git 探测结果。 */
struct GitProbe
{
    /** where git 是否找到了可执行文件。 */
    bool m_found = false ;
    /** where git 输出的第一行（git.exe 路径）；没找到为空。 */
    std::string m_path = {} ;
    /** 失败原因或原始输出，供界面提示。 */
    std::string m_message = {} ;
} ;

/** git 仓库初始化结果。 */
struct GitInitResult
{
    /** init、切 main 分支、写文件是否全部成功。 */
    bool m_ok = false ;
    /** 失败原因或成功摘要，供界面提示。 */
    std::string m_message = {} ;
} ;

/** 探测 git：cmd /c where git；有输出视为可用，否则视为环境未就绪。 */
GitProbe scan_git() ;

/** 在工程目录初始化 git：git init → 切到 main → 写 .gitignore 与 .gitattributes。 */
GitInitResult initialize_git_repository(const std::filesystem::path& project_dir,
    ctx::LineEnding line_ending = ctx::LineEnding::CRLF) ;
} // namespace core
