#pragma once
// ============================================================================
// path_env.h — PATH 环境变量值的纯文本编辑（不碰注册表、不改进程环境）。
//
// 注册/摘除自身目录时用；全部按 Windows 的 PATH 语义处理：分号分隔、忽略
// 大小写、容忍首尾空白与成对引号、去掉目录末尾分隔符。注册与摘除互逆。
// ============================================================================
#include <filesystem>
#include <string>

namespace core
{
/** PATH 值里是否已有该目录。 */
bool path_has_directory(const std::wstring& path_value, const std::filesystem::path& directory) ;

/** 两个目录是否等价（忽略大小写与末尾分隔符）。 */
bool path_same_directory(const std::filesystem::path& left, const std::filesystem::path& right) ;

/** 把目录插到 PATH 值最前面；已经存在时原样返回。 */
std::wstring path_add_directory(const std::wstring& path_value,
    const std::filesystem::path& directory) ;

/** 从 PATH 值里摘掉该目录；不存在时原样返回。 */
std::wstring path_remove_directory(const std::wstring& path_value,
    const std::filesystem::path& directory) ;
} // namespace core
