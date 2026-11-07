#pragma once
// ============================================================================
// template_cmake_builder.h — 目标工程 CMakeLists.txt 的字符串构建器。
//
// 和模板源码字符串的区别：CMakeLists.txt 依赖本机环境（NX SDK 路径、源码编码），
// 没法写死成 R 字符串，所以用 setter 累积参数，最后由 build() 生成完整文本。
// 本类只拼字符串、不碰文件系统；写盘编码与行尾由组装层统一处理。
// 生成的项目固定按 NX 2007 插件约定：MSVC / x64 / Windows、/MD、C++ 标准可配。
// ============================================================================
#include "../core_module/context.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace template_cmake
{
/** 目标工程 CMakeLists.txt 构建器；setter 累积参数，build() 输出完整文本。 */
class CmakeBuilder
{
    public:
    /** 设置项目名；同时是产物 DLL 名，空名会让 build() 抛 invalid_argument。 */
    CmakeBuilder& set_project_name(std::string name)
    {
        m_project_name = std::move(name) ;
        return *this ;
    }

    /** 追加一个源文件；相对工程根目录，反斜杠会归一成正斜杠。 */
    CmakeBuilder& add_source_file(std::string relative_path)
    {
        append_unique(m_sources, normalize_path(relative_path)) ;
        return *this ;
    }

    /** 追加一个 include 目录；重复目录只保留第一次。 */
    CmakeBuilder& add_include_directory(std::string directory)
    {
        append_unique(m_include_directories, normalize_path(directory)) ;
        return *this ;
    }

    /** 追加一个库目录；目录下的 .lib 会全部链接（NX SDK 惯例），重复目录只保留第一次。 */
    CmakeBuilder& add_library_directory(std::string directory)
    {
        append_unique(m_library_directories, normalize_path(directory)) ;
        return *this ;
    }

    /** 追加一个额外链接库；NX SDK 库目录以外的库才需要，重复库只保留第一次。 */
    CmakeBuilder& add_link_library(std::string library)
    {
        append_unique(m_link_libraries, std::move(library)) ;
        return *this ;
    }

    /** 设置 C++ 标准版本，默认 20。 */
    CmakeBuilder& set_cxx_standard(int standard) noexcept
    {
        m_cxx_standard = standard ;
        return *this ;
    }

    /** 设置目标工程源码编码；只影响 MSVC 的源码字符集参数。 */
    CmakeBuilder& set_source_coding(ctx::Coding coding) noexcept
    {
        m_source_coding = coding ;
        m_source_coding_enabled = true ;
        return *this ;
    }

    /** 关闭源码字符集参数；自带 toolchain 文件的工程由工具链自己决定。 */
    CmakeBuilder& disable_source_coding() noexcept
    {
        m_source_coding_enabled = false ;
        return *this ;
    }

    /** 生成完整 CMakeLists.txt 文本（UTF-8、LF）；项目名为空或没有源文件时抛 invalid_argument。 */
    std::string build() const
    {
        if (m_project_name.empty())
        {
            throw std::invalid_argument("CMake 项目名不能为空") ;
        }
        if (m_sources.empty())
        {
            throw std::invalid_argument("CMake 工程至少要有一个源文件") ;
        }

        std::string text = build_banner() ;
        text += "cmake_minimum_required(VERSION 3.21)\n" ;
        text += "project(\"" + m_project_name + "\" LANGUAGES CXX)\n\n" ;
        text += "set(CMAKE_CXX_STANDARD " + std::to_string(m_cxx_standard) + ")\n" ;
        text += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n" ;
        text += "set(CMAKE_CXX_EXTENSIONS OFF)\n\n" ;
        text += "# NX 插件必须与 NX 进程共用运行时库\n" ;
        text += "set(CMAKE_MSVC_RUNTIME_LIBRARY \"MultiThreadedDLL\")\n\n" ;

        text += "# ---- 源文件（新增 cpp 加到这里） ----\n" ;
        text += build_list_variable("SRC_FILES", m_sources) ;
        text += "\nadd_library(${PROJECT_NAME} SHARED ${SRC_FILES})\n\n" ;

        text += "# ---- 头文件路径 ----\n" ;
        text += "# 工程根目录与 templates/ 是模板头文件目录，可直接 #include\n" ;
        if (m_include_directories.empty())
        {
            text += "target_include_directories(${PROJECT_NAME} PRIVATE\n" ;
        }
        else
        {
            text += "# 其余是 NX SDK 头文件根（由 nx_dev 扫描到的本机绝对路径）\n" ;
            text += "target_include_directories(${PROJECT_NAME} PRIVATE\n" ;
        }
        text += "    \"${CMAKE_CURRENT_SOURCE_DIR}\"\n" ;
        for (const std::string& directory : m_include_directories)
        {
            text += "    \"" + directory + "\"\n" ;
        }
        text += ")\n\n" ;

        if (!m_library_directories.empty())
        {
            text += "# ---- NX Open 库 ----\n" ;
            text += "target_link_directories(${PROJECT_NAME} PRIVATE\n" ;
            for (const std::string& directory : m_library_directories)
            {
                text += "    \"" + directory + "\"\n" ;
            }
            text += ")\n\n" ;
            text += "# 库目录下的 .lib 全部链接（不用自己挑库）；额外库在下面追加\n" ;
            text += build_list_variable("NX_LIBRARY_DIRS", m_library_directories) ;
            text += "set(NX_LIBRARIES \"\")\n" ;
            text += "foreach(NX_LIBRARY_DIR ${NX_LIBRARY_DIRS})\n" ;
            text += "    file(GLOB NX_DIR_LIBRARIES \"${NX_LIBRARY_DIR}/*.lib\")\n" ;
            text += "    list(APPEND NX_LIBRARIES ${NX_DIR_LIBRARIES})\n" ;
            text += "endforeach()\n" ;
            append_link_libraries(text) ;
            text += "target_link_libraries(${PROJECT_NAME} PRIVATE ${NX_LIBRARIES})\n\n" ;
        }
        else if (!m_link_libraries.empty())
        {
            text += "# ---- 额外链接库 ----\n" ;
            text += "target_link_libraries(${PROJECT_NAME} PRIVATE\n" ;
            for (const std::string& library : m_link_libraries)
            {
                text += "    \"" + library + "\"\n" ;
            }
            text += ")\n\n" ;
        }

        text += "# ---- 编译选项 ----\n" ;
        if (!m_source_coding_enabled)
        {
            text += "# 源码字符集：由 CMakePresets 引用的工具链文件决定，这里不重复设置\n\n" ;
        }
        else if (m_source_coding == ctx::Coding::GB2312)
        {
            text += "# 源码字符集：GB2312 按代码页 936 读（与 NX 2007 工程约定一致）\n" ;
            text += "target_compile_options(${PROJECT_NAME} PRIVATE /source-charset:.936)\n\n" ;
        }
        else
        {
            text += "# 源码字符集：UTF-8\n" ;
            text += "target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)\n\n" ;
        }

        text += "set_target_properties(${PROJECT_NAME} PROPERTIES\n" ;
        text += "    WINDOWS_EXPORT_ALL_SYMBOLS ON\n" ;
        text += "    RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}\"\n" ;
        text += ")\n" ;
        return text ;
    }

    private:
    /** 生成文件头注释：项目名、源码编码与运行库约定。 */
    std::string build_banner() const
    {
        const std::string coding = m_source_coding == ctx::Coding::GB2312
            ? "GB2312（MSVC 按代码页 936 读源码）"
            : "UTF-8（MSVC 加 /utf-8）" ;
        std::string text = "# ============================================================================\n" ;
        text += "# " + m_project_name + " — NX 2007 插件工程（由 nx_dev 生成）\n" ;
        text += "#\n" ;
        text += "# 源码编码：" + coding + "\n" ;
        text += "# 运行库：/MD（与 NX 进程共用 CRT）\n" ;
        text += "# ============================================================================\n" ;
        return text ;
    }

    /** 生成 CMake 列表变量块：每项一行、四空格缩进。 */
    static std::string build_list_variable(const std::string& name, const std::vector<std::string>& values)
    {
        std::string text = "set(" + name + "\n" ;
        for (const std::string& value : values)
        {
            text += "    \"" + value + "\"\n" ;
        }
        text += ")\n" ;
        return text ;
    }

    /** 把额外链接库追加到 NX_LIBRARIES；没有额外库时什么也不做。 */
    void append_link_libraries(std::string& text) const
    {
        for (const std::string& library : m_link_libraries)
        {
            text += "list(APPEND NX_LIBRARIES \"" + library + "\")\n" ;
        }
    }

    /** 把路径里的反斜杠换成正斜杠，并去掉末尾斜杠。 */
    static std::string normalize_path(const std::string& path)
    {
        std::string result ;
        result.reserve(path.size()) ;
        for (const char letter : path)
        {
            result.push_back(letter == '\\' ? '/' : letter) ;
        }
        while (result.size() > 1 && result.back() == '/')
        {
            result.pop_back() ;
        }
        return result ;
    }

    /** 按插入顺序追加且去重；空串直接忽略。 */
    static void append_unique(std::vector<std::string>& values, std::string value)
    {
        if (value.empty())
        {
            return ;
        }
        if (std::find(values.begin(), values.end(), value) == values.end())
        {
            values.push_back(std::move(value)) ;
        }
    }

    std::string m_project_name ;                            // 项目名 = 产物 DLL 名
    std::vector<std::string> m_sources ;                    // 源文件相对路径
    std::vector<std::string> m_include_directories ;        // include 目录绝对路径
    std::vector<std::string> m_library_directories ;        // 库目录绝对路径（.lib 全链）
    std::vector<std::string> m_link_libraries ;             // 额外链接库
    int m_cxx_standard = 20 ;                               // C++ 标准版本
    ctx::Coding m_source_coding = ctx::Coding::GB2312 ;     // 目标工程源码编码
    bool m_source_coding_enabled = true ;                    // 是否输出源码字符集参数
} ;
} // namespace template_cmake
