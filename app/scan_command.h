#pragma once
#include <string>
#include <vector>

namespace app
{
/** 执行 --chain-list 命令：扫描本机工具链、NX SDK 与 Windows SDK 并打印报告；返回进程退出码。 */
int run_scan_command(const std::vector<std::string>& arguments) ;
} // namespace app
