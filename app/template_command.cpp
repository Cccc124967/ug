#include "template_command.h"
#include "../core_module/console_style.h"
#include "../core_module/context.h"
#include "../core_module/dev_core.h"
#include "../core_module/file_io.h"
#include "../template_module/template_release.h"
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace app
{
namespace
{
/** 错误输出统一红色。 */
void print_error(const std::string& text)
{
    std::cerr << core::paint("nx_dev --build-template: " + text, core::color::bright_red) << '\n' ;
}
} // namespace

int run_build_template_command(const std::vector<std::string>& arguments)
{
    if (arguments.empty() || arguments[0].empty())
    {
        print_error("缺少输出目录") ;
        std::cerr << "用法: nx_dev --build-template <目录>\n" ;
        return 1 ;
    }
    if (arguments.size() > 1)
    {
        print_error("只接受一个目录参数") ;
        return 1 ;
    }
    try
    {
        const std::filesystem::path target = core::utf8_to_wide(arguments[0]) ;
        if (target.empty())
        {
            print_error("目录参数为空") ;
            return 1 ;
        }
        // 默认按生成项目的标准约定：GB2312 源码 + CRLF 行尾；--build-template 释放全量模块。
        std::vector<template_release::TemplateModule> modules ;
        for (const template_release::TemplateModuleInfo& info : template_release::template_modules())
        {
            modules.push_back(info.m_module) ;
        }
        const std::vector<template_release::TemplateFile> files =
            template_release::release_templates(modules, ctx::Coding::GB2312, ctx::LineEnding::CRLF) ;
        for (const template_release::TemplateFile& file : files)
        {
            const std::filesystem::path destination = target / core::utf8_to_wide(file.m_destination) ;
            std::string error ;
            if (!core::ensure_directory(destination.parent_path(), error) ||
                !core::write_text_file(destination, file.m_content, error))
            {
                print_error("写模板代码失败: " + file.m_destination + ": " + error) ;
                return 1 ;
            }
            std::cout << "  " << core::paint(file.m_destination, core::color::bright_cyan)
                      << core::paint("  (" + std::to_string(file.m_content.size()) + " bytes)",
                          core::color::bright_black)
                      << '\n' ;
        }
        std::cout << core::paint("已生成 " + std::to_string(files.size()) + " 个模板代码文件 -> ",
            std::string(core::color::bold) + core::color::bright_green)
                  << core::paint(core::path_text(std::filesystem::absolute(target)), core::color::bright_black)
                  << '\n' ;
        return 0 ;
    }
    catch (const std::exception& error)
    {
        print_error(std::string("生成模板代码失败: ") + error.what()) ;
        return 1 ;
    }
}
} // namespace app
