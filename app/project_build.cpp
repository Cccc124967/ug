#include "project_build.h"
#include "../core_module/dev_core.h"
#include "../core_module/file_io.h"
#include "../core_module/git_control.h"
#include "../core_module/project_files.h"
#include "../template_module/template_cmake_builder.h"
#include "../template_module/template_cmake_preset_builder.h"
#include "../template_module/template_toolchain_builder.h"
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace app
{
namespace
{
/** 生成过程中待写盘的一个文件：相对工程根路径 + 最终字节。 */
struct GeneratedFile
{
    /** 相对工程根目录的目标路径。 */
    std::string m_destination ;
    /** 最终写入的字节；配置类文件固定 UTF-8 + LF，源码由模板层处理编码与行尾。 */
    std::string m_content ;
} ;

/** Windows 保留设备名（不区分大小写），做目录名会打开设备而不是建目录。 */
bool is_reserved_device_name(const std::string& lowered_stem)
{
    static const char* const reserved_names[] = { "con" , "prn" , "aux" , "nul" ,
        "com1" , "com2" , "com3" , "com4" , "com5" , "com6" , "com7" , "com8" , "com9" ,
        "lpt1" , "lpt2" , "lpt3" , "lpt4" , "lpt5" , "lpt6" , "lpt7" , "lpt8" , "lpt9" } ;
    for (const char* reserved : reserved_names)
    {
        if (lowered_stem == reserved)
        {
            return true ;
        }
    }
    return false ;
}

/** 项目名能否直接当 Windows 目录名；不合法时给出可读原因。 */
bool validate_project_name(const std::string& project_name, std::string& error)
{
    if (project_name.empty())
    {
        error = "项目名不能为空" ;
        return false ;
    }
    if (project_name == "." || project_name == "..")
    {
        error = "项目名不能是 . 或 .." ;
        return false ;
    }
    const std::string invalid_characters = "<>:\"/\\|?*" ;
    for (const char letter : project_name)
    {
        if (static_cast<unsigned char>(letter) < 0x20)
        {
            error = "项目名不能包含控制字符" ;
            return false ;
        }
        if (invalid_characters.find(letter) != std::string::npos)
        {
            error = "项目名不能包含字符: " + std::string(1, letter) ;
            return false ;
        }
    }
    if (project_name.back() == ' ' || project_name.back() == '.')
    {
        error = "项目名不能以空格或点结尾" ;
        return false ;
    }

    // 保留设备名只按第一个点之前的部分判断（con.txt 同样不可用）。
    std::string stem = project_name.substr(0, project_name.find('.')) ;
    for (char& letter : stem)
    {
        letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter))) ;
    }
    if (is_reserved_device_name(stem))
    {
        error = "项目名不能使用 Windows 保留设备名: " + stem ;
        return false ;
    }
    return true ;
}

/** CMake 生成器的最终取值：构建方案优先，其次请求里的，最后用编译器扫描出的。 */
std::string effective_generator(const ProjectBuildRequest& request)
{
    if (!request.m_build_plan.m_generator.empty())
    {
        return request.m_build_plan.m_generator ;
    }
    if (!request.m_generator.empty())
    {
        return request.m_generator ;
    }
    return request.m_compiler.m_generator ;
}

/** 判断文件是否为已存在的常规文件。 */
bool is_existing_file(const std::filesystem::path& path)
{
    std::error_code error ;
    return std::filesystem::is_regular_file(path, error) ;
}

/** 生成自包含 toolchain 文件文本；缺件时返回空并写原因。 */
std::string build_generated_toolchain_text(const ProjectBuildRequest& request, std::string& error)
{
    const scan::Compiler& compiler = request.m_compiler ;
    if (compiler.m_cl_path.empty())
    {
        error = "编译器条目里没有 cl.exe 路径" ;
        return {} ;
    }
    const std::filesystem::path cl_directory = compiler.m_cl_path.parent_path() ;
    const std::filesystem::path link_path = cl_directory / L"link.exe" ;
    const std::filesystem::path lib_path = cl_directory / L"lib.exe" ;
    if (!is_existing_file(link_path))
    {
        error = "cl.exe 同目录缺少 link.exe: " + core::path_text(link_path) ;
        return {} ;
    }
    const scan::WindowsSdk& windows_sdk = request.m_windows_sdk ;
    if (windows_sdk.m_include_dir.empty() || windows_sdk.m_lib_dir.empty())
    {
        error = "自动生成工具链需要一套完整的 Windows SDK（扫描结果里没有 include/lib）" ;
        return {} ;
    }
    if (compiler.m_include_dirs.empty())
    {
        error = "编译器缺少工具集 include 目录，无法自动生成 toolchain" ;
        return {} ;
    }
    template_cmake::ToolchainBuilder builder ;
    builder.set_compiler_tools(core::path_text(compiler.m_cl_path), core::path_text(link_path),
        is_existing_file(lib_path) ? core::path_text(lib_path) : std::string()) ;
    const std::filesystem::path rc = windows_sdk.m_bin_dir / L"rc.exe" ;
    const std::filesystem::path mt = windows_sdk.m_bin_dir / L"mt.exe" ;
    builder.set_resource_tools(is_existing_file(rc) ? core::path_text(rc) : std::string(),
        is_existing_file(mt) ? core::path_text(mt) : std::string()) ;
    for (const scan::Path& directory : compiler.m_include_dirs)
    {
        builder.add_include_directory(core::path_text(directory)) ;
    }
    for (const wchar_t* name : { L"ucrt" , L"shared" , L"um" , L"winrt" , L"cppwinrt" })
    {
        const std::filesystem::path directory = windows_sdk.m_include_dir / name ;
        if (std::filesystem::is_directory(directory))
        {
            builder.add_include_directory(core::path_text(directory)) ;
        }
    }
    for (const scan::Path& directory : compiler.m_lib_dirs)
    {
        builder.add_library_directory(core::path_text(directory)) ;
    }
    for (const wchar_t* name : { L"ucrt" , L"um" })
    {
        const std::filesystem::path directory = windows_sdk.m_lib_dir / name / L"x64" ;
        if (std::filesystem::is_directory(directory))
        {
            builder.add_library_directory(core::path_text(directory)) ;
        }
    }
    return builder.build() ;
}

/** CMakePresets 的 toolset 字段：直接用扫描到的真实平台工具集；未知返回空（字段省略）。 */
std::string toolset_text(const scan::Compiler& compiler)
{
    return compiler.m_platform_toolset ;
}

/** 相对路径是否以 .cpp 结尾（模板释放出的 C++ 源文件）。 */
bool is_cpp_source(const std::string& destination)
{
    constexpr std::size_t suffix_size = 4 ;
    return destination.size() > suffix_size &&
        destination.compare(destination.size() - suffix_size, suffix_size, ".cpp") == 0 ;
}

/** 生成 CMakeLists.txt 文本；没有 C++ 源文件时返回空，表示不写该文件。 */
std::string build_cmake_lists_text(const ProjectBuildRequest& request,
    const std::vector<template_release::TemplateFile>& template_files)
{
    std::vector<std::string> sources ;
    for (const template_release::TemplateFile& file : template_files)
    {
        if (is_cpp_source(file.m_destination))
        {
            sources.push_back(file.m_destination) ;
        }
    }
    if (sources.empty())
    {
        return {} ;
    }

    template_cmake::CmakeBuilder builder ;
    builder.set_project_name(request.m_project_name) ;
    for (const std::string& source : sources)
    {
        builder.add_source_file(source) ;
    }
    builder.add_include_directory("${CMAKE_CURRENT_SOURCE_DIR}/templates") ;
    if (!request.m_nx_sdk.m_uf_header_dir.empty())
    {
        builder.add_include_directory(core::path_text(request.m_nx_sdk.m_uf_header_dir)) ;
    }
    if (!request.m_nx_sdk.m_nxopen_header_dir.empty())
    {
        builder.add_include_directory(core::path_text(request.m_nx_sdk.m_nxopen_header_dir)) ;
    }
    if (!request.m_nx_sdk.m_lib_dir.empty())
    {
        builder.add_library_directory(core::path_text(request.m_nx_sdk.m_lib_dir)) ;
    }
    // 自带 toolchain 文件通常自己钉死源码字符集（NXDevKit 固定 GB2312），再叠加会冲突。
    const bool toolchain_owns_coding = request.m_build_plan.m_ok &&
        request.m_build_plan.m_kind == scan::BuildPlanKind::ToolchainFile ;
    if (toolchain_owns_coding)
    {
        builder.disable_source_coding() ;
    }
    else
    {
        builder.set_source_coding(request.m_coding) ;
    }
    return builder.build() ;
}

/** 生成 CMakePresets.json 文本；没有生成器时返回空，表示不写该文件。 */
std::string build_presets_text(const ProjectBuildRequest& request, const std::string& generator,
    const std::string& toolchain_file)
{
    if (generator.empty())
    {
        return {} ;
    }
    const scan::BuildPlan& plan = request.m_build_plan ;
    template_cmake::CmakePresetBuilder builder ;
    builder.set_generator(generator) ;
    builder.set_build_type("Release") ;
    builder.set_binary_directory("${sourceDir}/build") ;
    if (plan.m_ok && plan.m_single_config)
    {
        builder.set_single_config(true) ;
        if (!toolchain_file.empty())
        {
            builder.set_toolchain_file(toolchain_file) ;
        }
        if (!plan.m_make_program.empty())
        {
            builder.set_make_program(core::path_text(plan.m_make_program)) ;
        }
        std::string label = "MSVC" ;
        if (!request.m_compiler.m_toolset_version.empty())
        {
            label += " " + request.m_compiler.m_toolset_version ;
        }
        builder.set_display_name(label + " / " + generator) ;
        builder.set_description(
            "由 nx_dev 生成：编译器与 Windows SDK 已钉死（toolchain 文件），用本预置直接构建。") ;
        return builder.build() ;
    }
    builder.set_architecture("x64") ;
    builder.set_toolset(toolset_text(request.m_compiler)) ;
    if (!request.m_windows_sdk.m_version.empty())
    {
        builder.set_windows_sdk_version(request.m_windows_sdk.m_version) ;
    }
    return builder.build() ;
}

/** 把生成文件写进临时目录；父目录不存在时自动创建。 */
bool write_generated_file(const std::filesystem::path& directory, const GeneratedFile& file,
    std::string& error)
{
    const std::filesystem::path destination = directory / core::utf8_to_wide(file.m_destination) ;
    if (!core::ensure_directory(destination.parent_path(), error))
    {
        error = "创建 " + file.m_destination + " 的父目录失败: " + error ;
        return false ;
    }
    if (!core::write_text_file(destination, file.m_content, error))
    {
        error = "写 " + file.m_destination + " 失败: " + error ;
        return false ;
    }
    return true ;
}

/** 删除文件或目录；删完仍存在也算失败。 */
bool remove_path_checked(const std::filesystem::path& target, std::string& error)
{
    std::error_code filesystem_error ;
    std::filesystem::remove_all(target, filesystem_error) ;
    std::error_code exists_error ;
    if (filesystem_error || std::filesystem::exists(target, exists_error))
    {
        error = "删除失败: " + core::path_text(target) ;
        if (filesystem_error)
        {
            error += "（" + filesystem_error.message() + "）" ;
        }
        return false ;
    }
    return true ;
}

/** 递归复制目录；rename 失败时的退路。 */
bool copy_directory_tree(const std::filesystem::path& source,
    const std::filesystem::path& destination, std::string& error)
{
    std::error_code filesystem_error ;
    std::filesystem::copy(source, destination,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        filesystem_error) ;
    if (filesystem_error)
    {
        error = "复制临时工程到目标目录失败: " + filesystem_error.message() ;
        return false ;
    }
    return true ;
}

/** 备份式替换：旧目标先改名备份，临时目录改名成目标，成功后删备份；失败时把备份改回目标。 */
bool replace_target_directory(const std::filesystem::path& temp_directory,
    const std::filesystem::path& target_directory, const std::filesystem::path& backup_directory,
    std::string& error, std::string& warning)
{
    std::error_code filesystem_error ;
    const bool target_exists = std::filesystem::exists(target_directory, filesystem_error) ;
    bool has_backup = false ;
    if (target_exists)
    {
        if (std::filesystem::exists(backup_directory, filesystem_error))
        {
            if (!remove_path_checked(backup_directory, error))
            {
                error = "清理上次残留的工程备份失败: " + error ;
                return false ;
            }
        }
        std::error_code backup_error ;
        std::filesystem::rename(target_directory, backup_directory, backup_error) ;
        if (backup_error)
        {
            // 备份都没做成，旧目录一个字节都没动。
            error = "备份旧目标目录失败（旧目录未改动）: " + core::path_text(target_directory) +
                "（" + backup_error.message() + "）" ;
            return false ;
        }
        has_backup = true ;
    }
    else
    {
        std::error_code backup_exists_error ;
        if (std::filesystem::exists(backup_directory, backup_exists_error))
        {
            warning = "发现上次残留的工程备份未处理: " + core::path_text(backup_directory) ;
        }
    }

    std::error_code rename_error ;
    std::filesystem::rename(temp_directory, target_directory, rename_error) ;
    if (!rename_error)
    {
        if (has_backup)
        {
            std::string backup_cleanup_error ;
            if (!remove_path_checked(backup_directory, backup_cleanup_error))
            {
                warning = "旧工程备份未能删除（" + backup_cleanup_error + "），请手动删除 " +
                    core::path_text(backup_directory) ;
            }
        }
        return true ;
    }

    // 新目录落地失败：恢复备份，保证旧目录内容原封不动。
    const std::string rename_reason = rename_error.message() ;
    if (!has_backup)
    {
        if (!copy_directory_tree(temp_directory, target_directory, error))
        {
            std::string cleanup_error ;
            (void) remove_path_checked(target_directory, cleanup_error) ;
            return false ;
        }
        return true ;
    }
    std::error_code restore_error ;
    std::filesystem::rename(backup_directory, target_directory, restore_error) ;
    if (restore_error)
    {
        // 极端情况：旧工程备份保留下来，告诉调用方手动恢复的位置。
        error = "新工程目录改名失败（" + rename_reason + "），恢复旧目录也失败（" +
            restore_error.message() + "）；旧工程备份保留在 " + core::path_text(backup_directory) ;
        return false ;
    }
    error = "新工程目录改名失败: " + rename_reason + "；旧目录已原样恢复" ;
    return false ;
}

/** 尽力删除临时目录；清理失败不影响已经写出的结果。 */
void discard_directory(const std::filesystem::path& directory)
{
    if (directory.empty())
    {
        return ;
    }
    std::error_code filesystem_error ;
    std::filesystem::remove_all(directory, filesystem_error) ;
}
} // namespace

ProjectBuildResult build_project(const ProjectBuildRequest& request)
{
    ProjectBuildResult result ;
    std::filesystem::path temp_directory ;
    try
    {
        std::string error ;
        if (!validate_project_name(request.m_project_name, error))
        {
            result.m_message = error ;
            return result ;
        }
        if (request.m_parent_directory.empty())
        {
            result.m_message = "工程父目录不能为空" ;
            return result ;
        }

        const std::wstring project_name = core::utf8_to_wide(request.m_project_name) ;
        const std::filesystem::path target_directory = request.m_parent_directory / project_name ;
        result.m_project_directory = target_directory ;
        temp_directory = request.m_parent_directory / (L"." + project_name + L".nx_dev_tmp") ;

        if (!core::ensure_directory(request.m_parent_directory, error))
        {
            result.m_message = "创建工程父目录失败: " + error ;
            return result ;
        }
        std::error_code filesystem_error ;
        if (std::filesystem::exists(target_directory, filesystem_error) && !request.m_overwrite)
        {
            result.m_message = "目标目录已存在且未勾选覆盖: " + core::path_text(target_directory) ;
            return result ;
        }

        // 同级临时目录里写完整工程，成功后再整体替换目标目录。
        if (std::filesystem::exists(temp_directory, filesystem_error))
        {
            if (!remove_path_checked(temp_directory, error))
            {
                result.m_message = "清理上次残留的临时目录失败: " + error ;
                return result ;
            }
        }
        if (!core::ensure_directory(temp_directory, error))
        {
            result.m_message = "创建临时工程目录失败: " + error ;
            return result ;
        }

        const std::vector<template_release::TemplateFile> template_files =
            template_release::release_templates(request.m_template_modules,
                request.m_coding, request.m_line_ending) ;
        std::vector<GeneratedFile> generated_files ;
        generated_files.reserve(template_files.size() + 2) ;
        for (const template_release::TemplateFile& file : template_files)
        {
            GeneratedFile generated ;
            generated.m_destination = file.m_destination ;
            generated.m_content = file.m_content ;
            generated_files.push_back(std::move(generated)) ;
        }

        const std::string generator = effective_generator(request) ;
        // 自动生成 toolchain 的方案：先算好文本，作为工程文件一起写出。
        std::string toolchain_file_value ;
        if (request.m_build_plan.m_ok && request.m_build_plan.needs_generated_toolchain())
        {
            std::string toolchain_error ;
            const std::string toolchain_text = build_generated_toolchain_text(request, toolchain_error) ;
            if (toolchain_text.empty())
            {
                discard_directory(temp_directory) ;
                result.m_message = "生成 toolchain 失败: " + toolchain_error ;
                return result ;
            }
            GeneratedFile generated ;
            generated.m_destination = "cmake/nx_dev_toolchain.cmake" ;
            generated.m_content = toolchain_text ;
            generated_files.push_back(std::move(generated)) ;
            toolchain_file_value = "${sourceDir}/cmake/nx_dev_toolchain.cmake" ;
        }
        else if (request.m_build_plan.m_ok &&
            request.m_build_plan.m_kind == scan::BuildPlanKind::ToolchainFile)
        {
            toolchain_file_value = core::path_text(request.m_build_plan.m_toolchain_file) ;
        }
        const std::string cmake_lists_text = build_cmake_lists_text(request, template_files) ;
        if (!cmake_lists_text.empty())
        {
            GeneratedFile generated ;
            generated.m_destination = "CMakeLists.txt" ;
            generated.m_content = cmake_lists_text ;
            generated_files.push_back(std::move(generated)) ;
        }
        const std::string presets_text = build_presets_text(request, generator, toolchain_file_value) ;
        if (!presets_text.empty())
        {
            GeneratedFile generated ;
            generated.m_destination = "CMakePresets.json" ;
            generated.m_content = presets_text ;
            generated_files.push_back(std::move(generated)) ;
        }

        std::vector<std::string> written_destinations ;
        for (const GeneratedFile& file : generated_files)
        {
            if (!write_generated_file(temp_directory, file, error))
            {
                discard_directory(temp_directory) ;
                result.m_files.clear() ;
                result.m_message = error ;
                return result ;
            }
            written_destinations.push_back(file.m_destination) ;
        }

        // VS Code 的 cmake.generator：单配置方案写具体生成器；旧路径下探测到 Ninja 才写。
        std::string vscode_generator ;
        if (request.m_build_plan.m_ok && request.m_build_plan.m_single_config)
        {
            vscode_generator = request.m_build_plan.m_generator ;
        }
        else if (request.m_ninja_available)
        {
            vscode_generator = "Ninja" ;
        }
        const core::ProjectFileResult settings =
            core::write_vscode_settings(temp_directory, request.m_coding, request.m_line_ending,
                vscode_generator) ;
        if (!settings.m_ok)
        {
            discard_directory(temp_directory) ;
            result.m_files.clear() ;
            result.m_message = settings.m_message ;
            return result ;
        }
        written_destinations.push_back(".vscode/settings.json") ;

        if (request.m_init_git)
        {
            const core::GitInitResult git =
                core::initialize_git_repository(temp_directory, request.m_line_ending) ;
            result.m_git_ok = git.m_ok ;
            result.m_git_message = git.m_message ;
            if (git.m_ok)
            {
                written_destinations.push_back(".gitignore") ;
                written_destinations.push_back(".gitattributes") ;
            }
        }

        // 到这里临时工程已经完整；用备份式替换：旧目录先改名备份，任何一步失败都还原。
        const std::filesystem::path backup_directory =
            request.m_parent_directory / (L"." + project_name + L".nx_dev_old") ;
        std::string replace_warning ;
        const bool replaced = replace_target_directory(temp_directory, target_directory,
            backup_directory, error, replace_warning) ;
        discard_directory(temp_directory) ;
        if (!replaced)
        {
            result.m_files.clear() ;
            result.m_message = error ;
            return result ;
        }

        result.m_ok = true ;
        result.m_files = std::move(written_destinations) ;
        result.m_message = "已生成工程: " + core::path_text(target_directory) +
            "（共 " + std::to_string(result.m_files.size()) + " 个文件）" ;
        if (generator.empty())
        {
            result.m_message += "；未提供 CMake 生成器，已跳过 CMakePresets.json" ;
        }
        if (!replace_warning.empty())
        {
            result.m_message += "；警告: " + replace_warning ;
        }
        return result ;
    }
    catch (const std::exception& exception)
    {
        discard_directory(temp_directory) ;
        result.m_ok = false ;
        result.m_files.clear() ;
        result.m_message = std::string("生成工程失败: ") + exception.what() ;
        return result ;
    }
}
} // namespace app
