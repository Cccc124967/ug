#pragma once
#include "console_style.h"
#include <cstddef>
#include <iostream>
#include <string>

/** 进程版本号 */
inline std::string cmd_version = "--version";
inline std::string cmd_version2 = "-v" ;


/** 重新扫描并展示全部工具链信息 */
inline std::string cmd_scan = "--chain-list" ;

/** 主入口 */
inline std::string cmd_main = "new";
inline std::string cmd_main2 = "create";
inline std::string cmd_main3 = "code";

/** 清除缓存 */
inline std::string cmd_clear_cache = "--clear" ;

/** 生成全量模板代码 */
inline std::string cmd_create_template_code = "--build-template" ;

/** 运行控制台自检 */
inline std::string cmd_self_test = "--self-test" ;

/** 目录可写性诊断 */
inline std::string cmd_diag = "--diag" ;

/** 把自己注册进用户 PATH */
inline std::string cmd_register = "--register" ;

/** 把自己从用户 PATH 摘除 */
inline std::string cmd_unregister = "--unregister" ;

/** 帮助信息 */
inline std::string cmd_help = "--help";
inline std::string cmd_help2 = "-h" ;

/** 打印帮助信息 */
inline void show_help_info()
{
    const std::string section_style = std::string(core::color::bold) + core::color::bright_cyan ;
    const std::string command_style = core::color::bright_yellow ;
    const std::string hint_style = core::color::bright_black ;
    // 粗略算显示宽度：CJK / 全角按两列，其余按一列。
    const auto display_width = [](const std::string& text)
    {
        std::size_t width = 0 ;
        for (std::size_t index = 0 ; index < text.size() ; )
        {
            const unsigned char lead = static_cast<unsigned char>(text[index]) ;
            std::size_t length = 1 ;
            if (lead >= 0xF0)
            {
                length = 4 ;
            }
            else if (lead >= 0xE0)
            {
                length = 3 ;
            }
            else if (lead >= 0xC0)
            {
                length = 2 ;
            }
            width += (length >= 3) ? 2 : 1 ;
            index += length ;
        }
        return width ;
    } ;
    const auto command_line = [&display_width, &command_style, &hint_style](
        const std::string& command, const std::string& description)
    {
        const std::size_t column = 28 ;
        const std::size_t width = display_width(command) ;
        const std::string padded = command + std::string(width < column ? column - width : 1, ' ') ;
        return "  " + core::paint(padded, command_style) + core::paint(description, hint_style) ;
    } ;

    std::cout << core::paint("用法", section_style) << '\n'
              << "  ug <命令> [参数]\n\n"
              << core::paint("命令", section_style) << '\n'
              << "  " << core::paint(cmd_main + " | " + cmd_main2 + " | " + cmd_main3, command_style)
              << core::paint("  交互式生成 NX 2007 插件工程", hint_style) << '\n'
              << command_line(cmd_scan, "重新扫描并展示全部工具链信息（可带扫描参数）") << '\n'
              << command_line(cmd_create_template_code + " <目录>", "生成全量模板代码到指定目录") << '\n'
              << command_line(cmd_self_test, "运行控制台自检（扫描 / 控制台 / 生成流水线）") << '\n'
              << command_line(cmd_diag + " <目录>", "诊断进程身份与目录可写性（生成失败排查）") << '\n'
              << command_line(cmd_register, "把自身所在目录注册进用户 PATH（新开终端可直接 ug --version）") << '\n'
              << command_line(cmd_unregister, "把自身所在目录从用户 PATH 摘除") << '\n'
              << command_line(cmd_clear_cache, "清除扫描缓存") << '\n'
              << command_line(cmd_version + ", " + cmd_version2, "显示版本") << '\n'
              << command_line(cmd_help + ", " + cmd_help2, "显示本帮助") << '\n' ;
}
