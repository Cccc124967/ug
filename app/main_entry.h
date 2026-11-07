#pragma once
#include <string>
#include <vector>

namespace app
{
/** 执行主入口命令（new / create / code）：交互式生成 NX 2007 插件工程；返回进程退出码。 */
int run_main_entry_command(const std::vector<std::string>& arguments) ;
} // namespace app
