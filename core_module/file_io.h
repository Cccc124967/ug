#pragma once
// ============================================================================
// file_io.h — 生成工程时的文件读写工具（core 统一负责文件生成与写入）。
//
// 只做最小职责：路径转文本、建目录、按原样写字节；编码/行尾由调用方拼好。
// ============================================================================
#include <filesystem>
#include <string>
#include <string_view>

namespace core
{
/** 宽路径转 UTF-8 文本；不抛异常，非法码位写成替换字符。 */
std::string path_text(const std::filesystem::path& path) ;

/** 递归创建目录；已存在也算成功，失败时给出原因。 */
bool ensure_directory(const std::filesystem::path& directory, std::string& error) ;

/** 写 UTF-8 文本文件（二进制写，不改行尾）；失败时给出原因。 */
bool write_text_file(const std::filesystem::path& path, std::string_view text, std::string& error) ;
} // namespace core
