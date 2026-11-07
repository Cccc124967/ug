#pragma once
#include "console_style.h"
#include <string>
#include <string_view>
#include <vector>

namespace core
{
using StringList = std::vector<std::string> ;

/** 严格转换 UTF-8；非法编码抛出 invalid_argument。 */
std::wstring utf8_to_wide(const std::string& text) ;
/** 保留旧接口的拼写；新代码使用 utf8_to_wide。 */
std::wstring uft8_to_wide(const std::string& text) ;
/** 严格转换 UTF-16；非法编码抛出 invalid_argument。 */
std::string wide_to_utf8(const std::wstring& text) ;
/** 严格转换到 GB2312/GBK(代码页 936)；有字符表示不了抛出 invalid_argument。 */
std::string utf8_to_gb2312(const std::string& text) ;
/** 把 UTF-8 转成 Unicode 码点供文本编辑。 */
std::u32string utf8_to_utf32(std::string_view text) ;
/** 把 Unicode 码点转成 UTF-8。 */
std::string utf32_to_utf8(std::u32string_view text) ;
/** 获取环境变量列表，每项为 NAME=VALUE。 */
StringList get_env_var() ;
/** 估算终端列宽：中日韩文字及常见 emoji 两列，组合标记零列。 */
std::size_t console_cell_width(char32_t code_point) noexcept ;
} // namespace core
