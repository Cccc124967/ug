#pragma once
// ============================================================================
// template_cmake_preset_builder.h — 目标工程 CMakePresets.json 的字符串构建器。
//
// CMakeLists.txt 里锁不住工具集（VS 生成器的工具集/Windows SDK 只能用预置或 -T 指定），
// 所以环境那一段单独产出成 CMakePresets.json：生成器、架构、MSVC 工具集、Windows SDK
// 版本都写在 configure 预置里，再配一个 Release 的 build 预置。
// 本类只拼字符串、不碰文件系统；写盘编码与行尾由组装层统一处理。
// ============================================================================
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace template_cmake
{
/** 目标工程 CMakePresets.json 构建器；setter 累积参数，build() 输出完整 JSON 文本。 */
class CmakePresetBuilder
{
    public:
    /** 设置 configure 预设名，默认 "nx"。 */
    CmakePresetBuilder& set_configure_preset_name(std::string name)
    {
        m_configure_preset_name = std::move(name) ;
        return *this ;
    }

    /** 设置 build 预设名，默认 "nx-release"。 */
    CmakePresetBuilder& set_build_preset_name(std::string name)
    {
        m_build_preset_name = std::move(name) ;
        return *this ;
    }

    /** 设置预设显示名；空则按工具集与 Windows SDK 自动拼。 */
    CmakePresetBuilder& set_display_name(std::string name)
    {
        m_display_name = std::move(name) ;
        return *this ;
    }

    /** 设置预设说明，默认一句"工具链已锁定"。 */
    CmakePresetBuilder& set_description(std::string description)
    {
        m_description = std::move(description) ;
        return *this ;
    }

    /** 设置生成器（必填），如 "Visual Studio <主版本> <年份>"。 */
    CmakePresetBuilder& set_generator(std::string generator)
    {
        m_generator = std::move(generator) ;
        return *this ;
    }

    /** 设置目标架构，默认 "x64"；空串表示不下发该字段。 */
    CmakePresetBuilder& set_architecture(std::string architecture)
    {
        m_architecture = std::move(architecture) ;
        return *this ;
    }

    /** 设置 MSVC 平台工具集，如 "v1xx"；空串表示不锁。 */
    CmakePresetBuilder& set_toolset(std::string toolset)
    {
        m_toolset = std::move(toolset) ;
        return *this ;
    }

    /** 设置 Windows SDK 版本，如 "10.0.26100.0"；空串表示不锁。 */
    CmakePresetBuilder& set_windows_sdk_version(std::string version)
    {
        m_windows_sdk_version = std::move(version) ;
        return *this ;
    }

    /** 设置工具链文件路径；空串表示不写该字段。 */
    CmakePresetBuilder& set_toolchain_file(std::string toolchain_file)
    {
        m_toolchain_file = std::move(toolchain_file) ;
        return *this ;
    }

    /** 设置单配置生成器（Ninja / NMake）；单配置下不写 VS 专属字段。 */
    CmakePresetBuilder& set_single_config(bool single_config) noexcept
    {
        m_single_config = single_config ;
        return *this ;
    }

    /** 设置单配置生成器要显式指定的 make 程序路径；空串表示不写。 */
    CmakePresetBuilder& set_make_program(std::string make_program)
    {
        m_make_program = std::move(make_program) ;
        return *this ;
    }

    /** 设置构建类型，默认 "Release"；空串表示不下发该字段。 */
    CmakePresetBuilder& set_build_type(std::string build_type)
    {
        m_build_type = std::move(build_type) ;
        return *this ;
    }

    /** 设置二进制目录，默认 "${sourceDir}/build"。 */
    CmakePresetBuilder& set_binary_directory(std::string directory)
    {
        m_binary_directory = std::move(directory) ;
        return *this ;
    }

    /** 生成完整 CMakePresets.json 文本（UTF-8、LF）；生成器或预设名为空时抛 invalid_argument。 */
    std::string build() const
    {
        if (m_generator.empty())
        {
            throw std::invalid_argument("CMakePresets 必须指定生成器") ;
        }
        if (m_configure_preset_name.empty() || m_build_preset_name.empty())
        {
            throw std::invalid_argument("CMakePresets 的预置名不能为空") ;
        }

        std::string text = "{\n" ;
        text += "  \"version\": 3,\n" ;
        text += "  \"cmakeMinimumRequired\": {\n" ;
        text += "    \"major\": 3,\n" ;
        text += "    \"minor\": 21,\n" ;
        text += "    \"patch\": 0\n" ;
        text += "  },\n" ;
        text += "  \"configurePresets\": [\n" ;
        text += "    {\n" ;
        append_members(text, configure_preset_members()) ;
        text += "    }\n" ;
        text += "  ],\n" ;
        text += "  \"buildPresets\": [\n" ;
        text += "    {\n" ;
        append_members(text, build_preset_members()) ;
        text += "    }\n" ;
        text += "  ]\n" ;
        text += "}\n" ;
        return text ;
    }

    private:
    /** configure 预置的成员行（已含 6 空格缩进、不含逗号）。 */
    std::vector<std::string> configure_preset_members() const
    {
        const std::string indent = "      " ;
        std::vector<std::string> members ;
        members.push_back(json_member(indent, "name", m_configure_preset_name)) ;
        members.push_back(json_member(indent, "displayName", display_name())) ;
        members.push_back(json_member(indent, "description", description_text())) ;
        members.push_back(json_member(indent, "generator", m_generator)) ;
        if (!m_toolchain_file.empty())
        {
            members.push_back(json_member(indent, "toolchainFile", m_toolchain_file)) ;
        }
        if (!m_single_config)
        {
            if (!m_architecture.empty())
            {
                members.push_back(json_member(indent, "architecture", m_architecture)) ;
            }
            if (!m_toolset.empty())
            {
                members.push_back(json_member(indent, "toolset", m_toolset)) ;
            }
        }
        members.push_back(json_member(indent, "binaryDir", m_binary_directory)) ;
        std::vector<std::string> cache_entries ;
        if (!m_single_config && !m_windows_sdk_version.empty())
        {
            cache_entries.push_back(json_member("        ",
                "CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION", m_windows_sdk_version)) ;
        }
        if (m_single_config && !m_build_type.empty())
        {
            cache_entries.push_back("        \"CMAKE_BUILD_TYPE\": \"" + escape_json(m_build_type) + "\"") ;
        }
        if (m_single_config && !m_make_program.empty())
        {
            cache_entries.push_back("        \"CMAKE_MAKE_PROGRAM\": \"" + escape_json(m_make_program) + "\"") ;
        }
        if (!cache_entries.empty())
        {
            std::string block = json_object_start(indent, "cacheVariables") + "\n" ;
            for (std::size_t index = 0 ; index < cache_entries.size() ; ++index)
            {
                block += cache_entries[index] ;
                block += index + 1 < cache_entries.size() ? ",\n" : "\n" ;
            }
            block += indent + "}" ;
            members.push_back(block) ;
        }
        return members ;
    }

    /** build 预置的成员行（已含 6 空格缩进、不含逗号）。 */
    std::vector<std::string> build_preset_members() const
    {
        const std::string indent = "      " ;
        std::vector<std::string> members ;
        members.push_back(json_member(indent, "name", m_build_preset_name)) ;
        members.push_back(json_member(indent, "configurePreset", m_configure_preset_name)) ;
        if (!m_single_config && !m_build_type.empty())
        {
            members.push_back(json_member(indent, "configuration", m_build_type)) ;
        }
        return members ;
    }

    /** 显示名：优先用设置值，否则按工具集与 Windows SDK 自动拼。 */
    std::string display_name() const
    {
        if (!m_display_name.empty())
        {
            return m_display_name ;
        }
        std::string text = "MSVC " + (m_toolset.empty() ? std::string("(默认工具集)") : m_toolset) ;
        if (!m_windows_sdk_version.empty())
        {
            text += " / Windows SDK " + m_windows_sdk_version ;
        }
        return text ;
    }

    /** 说明文字：优先用设置值，否则给一句默认说明。 */
    std::string description_text() const
    {
        if (!m_description.empty())
        {
            return m_description ;
        }
        return "由 nx_dev 生成：生成器与工具集已锁定，直接用本预置构建。" ;
    }

    /** 生成一行带缩进的键值。 */
    static std::string json_member(const std::string& indent, const std::string& key, const std::string& value)
    {
        return indent + "\"" + escape_json(key) + "\": \"" + escape_json(value) + "\"" ;
    }

    /** 生成多行对象的起始行：缩进 + "key": {。 */
    static std::string json_object_start(const std::string& indent, const std::string& key)
    {
        return indent + "\"" + escape_json(key) + "\": {" ;
    }

    /** 把成员行拼进对象体，成员之间补逗号与换行。 */
    static void append_members(std::string& text, const std::vector<std::string>& members)
    {
        for (std::size_t index = 0 ; index < members.size() ; ++index)
        {
            text += members[index] ;
            text += index + 1 < members.size() ? ",\n" : "\n" ;
        }
    }

    /** JSON 字符串转义：引号、反斜杠、控制字符；UTF-8 字节原样保留。 */
    static std::string escape_json(const std::string& text)
    {
        std::string result ;
        result.reserve(text.size()) ;
        for (const char letter : text)
        {
            switch (letter)
            {
                case '"': result += "\\\"" ; break ;
                case '\\': result += "\\\\" ; break ;
                case '\b': result += "\\b" ; break ;
                case '\f': result += "\\f" ; break ;
                case '\n': result += "\\n" ; break ;
                case '\r': result += "\\r" ; break ;
                case '\t': result += "\\t" ; break ;
                default:
                    if (static_cast<unsigned char>(letter) < 0x20)
                    {
                        static const char* digits = "0123456789abcdef" ;
                        const unsigned char value = static_cast<unsigned char>(letter) ;
                        result += "\\u00" ;
                        result.push_back(digits[value >> 4]) ;
                        result.push_back(digits[value & 0x0F]) ;
                    }
                    else
                    {
                        result.push_back(letter) ;
                    }
                    break ;
            }
        }
        return result ;
    }

    std::string m_configure_preset_name = "nx" ;                 // configure 预置名
    std::string m_build_preset_name = "nx-release" ;             // build 预置名
    std::string m_display_name ;                                 // 预设显示名；空则自动拼
    std::string m_description ;                                  // 预设说明；空则用默认句
    std::string m_generator ;                                    // CMake 生成器（必填）
    std::string m_architecture = "x64" ;                         // 目标架构
    std::string m_toolset ;                                      // MSVC 工具集，空则不锁
    std::string m_windows_sdk_version ;                          // Windows SDK 版本，空则不锁
    std::string m_toolchain_file ;                               // CMake toolchain 文件，空则不写
    std::string m_make_program ;                                 // 单配置生成器的 make 程序，空则不写
    bool m_single_config = false ;                                // 是否单配置生成器（Ninja / NMake）
    std::string m_build_type = "Release" ;                       // 构建类型
    std::string m_binary_directory = "${sourceDir}/build" ;      // 构建目录
} ;
} // namespace template_cmake
