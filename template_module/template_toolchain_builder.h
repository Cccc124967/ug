#pragma once
// ============================================================================
// template_toolchain_builder.h — 自包含 CMake toolchain 文件的字符串构建器。
//
// 当编译器不在完整 VS 安装里、也没有自带 toolchain 文件时，用扫描到的 cl、
// Windows SDK 与工具集 include/lib 生成一份绝对路径 toolchain：编译器/链接器/
// 资源编译器全部钉死，include 与 lib 走 flags 初始化，所以构建时不需要 vcvars
// 环境（Ninja 与 NMake 都适用）。本类只拼字符串、不碰文件系统。
// ============================================================================
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace template_cmake
{
/** 目标工程 CMake toolchain 构建器；setter 累积参数，build() 输出完整文本。 */
class ToolchainBuilder
{
    public:
    /** 设置编译器、链接器与静态库工具路径；cl 为空时 build() 抛 invalid_argument。 */
    ToolchainBuilder& set_compiler_tools(std::string cl, std::string link, std::string lib)
    {
        m_cl = normalize_path(std::move(cl)) ;
        m_link = normalize_path(std::move(link)) ;
        m_lib = normalize_path(std::move(lib)) ;
        return *this ;
    }

    /** 设置资源编译器与清单工具路径；空串表示不写该字段。 */
    ToolchainBuilder& set_resource_tools(std::string rc, std::string mt)
    {
        m_rc = normalize_path(std::move(rc)) ;
        m_mt = normalize_path(std::move(mt)) ;
        return *this ;
    }

    /** 追加一个 include 目录；重复目录只保留第一次。 */
    ToolchainBuilder& add_include_directory(std::string directory)
    {
        append_unique(m_include_directories, normalize_path(std::move(directory))) ;
        return *this ;
    }

    /** 追加一个库目录；重复目录只保留第一次。 */
    ToolchainBuilder& add_library_directory(std::string directory)
    {
        append_unique(m_library_directories, normalize_path(std::move(directory))) ;
        return *this ;
    }

    /** 生成完整 toolchain 文本（UTF-8、LF）；cl 为空时抛 invalid_argument。 */
    std::string build() const
    {
        if (m_cl.empty())
        {
            throw std::invalid_argument("自动生成的 toolchain 必须指定 cl.exe") ;
        }
        std::string text ;
        text += "# ============================================================================\n" ;
        text += "# nx_dev_toolchain.cmake — 由 nx_dev 生成的自包含 MSVC 工具链\n" ;
        text += "#\n" ;
        text += "# 编译器、链接器与资源编译器全部用绝对路径钉死；include 与 lib 写进\n" ;
        text += "# *_FLAGS_INIT，构建时不需要 vcvars/VS 开发环境。\n" ;
        text += "# ============================================================================\n\n" ;
        text += "set(CMAKE_C_COMPILER \"" + m_cl + "\")\n" ;
        text += "set(CMAKE_CXX_COMPILER \"" + m_cl + "\")\n" ;
        if (!m_link.empty())
        {
            text += "set(CMAKE_LINKER \"" + m_link + "\")\n" ;
        }
        if (!m_lib.empty())
        {
            text += "set(CMAKE_AR \"" + m_lib + "\")\n" ;
        }
        if (!m_rc.empty())
        {
            text += "set(CMAKE_RC_COMPILER \"" + m_rc + "\")\n" ;
        }
        if (!m_mt.empty())
        {
            text += "set(CMAKE_MT \"" + m_mt + "\")\n" ;
        }
        text += '\n' ;

        const std::string include_flags = join_flags("/I", m_include_directories) ;
        if (!include_flags.empty())
        {
            text += "set(_nx_dev_include_flags \"" + include_flags + "\")\n" ;
            text += "set(CMAKE_C_FLAGS_INIT \"${_nx_dev_include_flags}\")\n" ;
            text += "set(CMAKE_CXX_FLAGS_INIT \"${_nx_dev_include_flags}\")\n" ;
            text += "set(CMAKE_RC_FLAGS_INIT \"${_nx_dev_include_flags}\")\n" ;
        }
        const std::string library_flags = join_flags("/LIBPATH:", m_library_directories) ;
        if (!library_flags.empty())
        {
            text += "set(_nx_dev_library_flags \"" + library_flags + "\")\n" ;
            text += "set(CMAKE_EXE_LINKER_FLAGS_INIT \"${_nx_dev_library_flags}\")\n" ;
            text += "set(CMAKE_SHARED_LINKER_FLAGS_INIT \"${_nx_dev_library_flags}\")\n" ;
            text += "set(CMAKE_MODULE_LINKER_FLAGS_INIT \"${_nx_dev_library_flags}\")\n" ;
        }
        return text ;
    }

    private:
    /** 把目录拼成形如 /I\"C:/a b\" 的 CMake 字符串内容。 */
    static std::string join_flags(const std::string& prefix, const std::vector<std::string>& directories)
    {
        std::string result ;
        for (const std::string& directory : directories)
        {
            if (!result.empty())
            {
                result += " " ;
            }
            result += prefix + "\\\"" + directory + "\\\"" ;
        }
        return result ;
    }

    /** 把路径里的反斜杠换成正斜杠，并去掉末尾斜杠。 */
    static std::string normalize_path(std::string path)
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

    /** 追加且去重；空串直接忽略。 */
    static void append_unique(std::vector<std::string>& values, std::string value)
    {
        if (value.empty())
        {
            return ;
        }
        for (const std::string& existing : values)
        {
            if (existing == value)
            {
                return ;
            }
        }
        values.push_back(std::move(value)) ;
    }

    std::string m_cl ;
    std::string m_link ;
    std::string m_lib ;
    std::string m_rc ;
    std::string m_mt ;
    std::vector<std::string> m_include_directories ;
    std::vector<std::string> m_library_directories ;
} ;
} // namespace template_cmake
