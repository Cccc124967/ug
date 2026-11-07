#pragma once
// ============================================================================
// register_command.h — --register / --unregister：把本程序目录挂进用户 PATH。
//
// 只改当前用户的环境变量（HKCU\Environment 的 Path），不需要管理员权限；
// 注册/摘除后广播 WM_SETTINGCHANGE，新开的终端即可直接按程序名调用。
// ============================================================================
#include <string>
#include <vector>

namespace app
{
/** 把本程序所在目录注册进用户 PATH；返回进程退出码。 */
int run_register_command(const std::vector<std::string>& arguments) ;

/** 把本程序所在目录从用户 PATH 摘除；返回进程退出码。 */
int run_unregister_command(const std::vector<std::string>& arguments) ;
} // namespace app
