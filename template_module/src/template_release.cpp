#include "../template_release.h"
#include "../../core_module/dev_core.h"
#include "../template_header.h"
#include "../template_macro.h"
#include "../template_main.h"
#include "../template_matrix.h"
#include "../template_output.h"
#include "../template_point.h"
#include "../template_quaternion.h"
#include "../template_raii.h"
#include "../template_vector.h"
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace template_release
{
namespace
{
/** 入口代码释放到工程根的文件名。 */
constexpr const char* entry_destination = "main.cpp" ;

/** 基础定义文件名；由任一非入口模块隐式带上，输出到 templates/ 下。 */
constexpr const char* basic_defs_destination = "templates/basic_type_defs.h" ;

/** 按片段拼出目标工程的 templates/output.hpp（UTF-8、LF）。 */
std::string assemble_output_header()
{
    std::string text = "#pragma once\n" ;
    text += template_header::ui_header ;
    text += template_header::modl_header ;
    text += template_header::exception_header ;
    text += template_header::string_header ;
    text += template_header::std_header ;
    text += template_header::cpp20_header ;
    text += template_header::basic_defs_header ;
    text += template_output::nx_msg ;
    text += template_output::nx_status ;
    text += template_macro::uf_guard ;
    text += template_macro::nxopen_guard ;
    text += template_raii::concept_template ;
    text += template_raii::uf_guard ;
    text += template_raii::nxopen_guard ;
    text += template_raii::list_guard ;
    return text ;
}

/** 按片段拼出目标工程的 main.cpp（UTF-8、LF）。 */
std::string assemble_main_source()
{
    std::string text = template_header::ui_header ;
    text += template_main::ufusr ;
    text += template_main::ufusr_ask_unload ;
    text += template_main::ufusr_cleanup ;
    return text ;
}

/** 模块对应的目标相对路径；入口在工程根，其余在 templates/ 下。 */
const char* module_destination(TemplateModule module)
{
    switch (module)
    {
        case TemplateModule::EntryCode: return entry_destination ;
        case TemplateModule::Guards: return "templates/output.hpp" ;
        case TemplateModule::VectorLib: return "templates/vector.hpp" ;
        case TemplateModule::MatrixLib: return "templates/matrix.hpp" ;
        case TemplateModule::PointLib: return "templates/point.hpp" ;
        case TemplateModule::QuaternionLib: return "templates/quaternion.hpp" ;
    }
    return "" ;
}

/** 模块对应的源码内容（UTF-8、LF）；基础定义不在这里。 */
std::string module_content(TemplateModule module)
{
    switch (module)
    {
        case TemplateModule::EntryCode: return assemble_main_source() ;
        case TemplateModule::Guards: return assemble_output_header() ;
        case TemplateModule::VectorLib: return template_math::vector_hpp ;
        case TemplateModule::MatrixLib: return template_math::matrix_hpp ;
        case TemplateModule::PointLib: return template_math::point_hpp ;
        case TemplateModule::QuaternionLib: return template_math::quaternion_hpp ;
    }
    return {} ;
}

/** 把内容里的 LF 换成目标行尾；内容里的其他字节不动。 */
std::string with_line_ending(const std::string& text, ctx::LineEnding line_ending)
{
    if (line_ending == ctx::LineEnding::LF)
    {
        return text ;
    }
    std::string result ;
    result.reserve(text.size() + text.size() / 16 + 1) ;
    for (const char letter : text)
    {
        if (letter == '\n')
        {
            result.push_back('\r') ;
        }
        result.push_back(letter) ;
    }
    return result ;
}

/** 按目标工程的源码编码转码；UTF-8 原样返回。 */
std::string with_coding(const std::string& text, ctx::Coding coding)
{
    return coding == ctx::Coding::GB2312 ? core::utf8_to_gb2312(text) : text ;
}

/** 组装一个待写盘文件：转码 + 行尾。 */
TemplateFile make_file(const std::string& destination, const std::string& utf8_text,
    ctx::Coding coding, ctx::LineEnding line_ending)
{
    TemplateFile file ;
    file.m_destination = destination ;
    file.m_content = with_line_ending(with_coding(utf8_text, coding), line_ending) ;
    return file ;
}

/** 按模块标识找元信息；找不到返回 nullptr。 */
const TemplateModuleInfo* find_module_info(TemplateModule module)
{
    for (const TemplateModuleInfo& info : template_modules())
    {
        if (info.m_module == module)
        {
            return &info ;
        }
    }
    return nullptr ;
}

/** 模块是否已在集合里。 */
bool contains_module(const std::vector<TemplateModule>& modules, TemplateModule module)
{
    for (const TemplateModule existing : modules)
    {
        if (existing == module)
        {
            return true ;
        }
    }
    return false ;
}
} // namespace

const std::vector<TemplateModuleInfo>& template_modules()
{
    static const std::vector<TemplateModuleInfo> modules
    {
        { TemplateModule::EntryCode , "entry_code" , "入口代码" ,
            "生成插件入口 main.cpp（ufusr / 卸载询问 / 清理钩子）" , {} } ,
        { TemplateModule::Guards , "guards" , "守卫宏" ,
            "生成 templates/output.hpp：输出工具、UF / NXOpen 调用守卫与 RAII 句柄" , {} } ,
        { TemplateModule::VectorLib , "vector" , "向量库" ,
            "生成 templates/vector.hpp：Vec2 / Vec3 / Vec4" , {} } ,
        { TemplateModule::MatrixLib , "matrix" , "矩阵库" ,
            "生成 templates/matrix.hpp：Matrix3x3 / Matrix4x4（自动带上向量库）" ,
            { TemplateModule::VectorLib } } ,
        { TemplateModule::PointLib , "point" , "点库" ,
            "生成 templates/point.hpp：Point / Point3（自动带上向量库）" ,
            { TemplateModule::VectorLib } } ,
        { TemplateModule::QuaternionLib , "quaternion" , "四元数库" ,
            "生成 templates/quaternion.hpp：Quaternion（自动带上矩阵库与向量库）" ,
            { TemplateModule::MatrixLib , TemplateModule::VectorLib } } ,
    } ;
    return modules ;
}

std::vector<TemplateModule> resolve_modules(const std::vector<TemplateModule>& selected)
{
    std::vector<TemplateModule> closure = selected ;
    for (std::size_t index = 0 ; index < closure.size() ; ++index)
    {
        const TemplateModuleInfo* info = find_module_info(closure[index]) ;
        if (info == nullptr)
        {
            continue ;
        }
        for (const TemplateModule dependency : info->m_dependencies)
        {
            if (!contains_module(closure, dependency))
            {
                closure.push_back(dependency) ;
            }
        }
    }

    // 依赖闭包按固定顺序重排，保证输出文件顺序与界面顺序一致。
    std::vector<TemplateModule> resolved ;
    for (const TemplateModuleInfo& info : template_modules())
    {
        if (contains_module(closure, info.m_module))
        {
            resolved.push_back(info.m_module) ;
        }
    }
    return resolved ;
}

std::vector<TemplateFile> release_templates(const std::vector<TemplateModule>& modules,
    ctx::Coding coding, ctx::LineEnding line_ending)
{
    std::vector<TemplateFile> files ;
    const std::vector<TemplateModule> resolved = resolve_modules(modules) ;

    // 基础定义不属于可选模块；勾了任一非入口模块就隐式带上。
    bool needs_basic_defs = false ;
    for (const TemplateModule module : resolved)
    {
        if (module != TemplateModule::EntryCode)
        {
            needs_basic_defs = true ;
            break ;
        }
    }
    if (needs_basic_defs)
    {
        files.push_back(make_file(basic_defs_destination, template_macro::basic_defs, coding, line_ending)) ;
    }

    for (const TemplateModule module : resolved)
    {
        files.push_back(make_file(module_destination(module), module_content(module), coding, line_ending)) ;
    }
    return files ;
}

std::vector<TemplateFile> release_templates(ctx::Coding coding, ctx::LineEnding line_ending)
{
    std::vector<TemplateModule> all_modules ;
    for (const TemplateModuleInfo& info : template_modules())
    {
        all_modules.push_back(info.m_module) ;
    }
    return release_templates(all_modules, coding, line_ending) ;
}
} // namespace template_release
