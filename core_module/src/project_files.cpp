#include "../project_files.h"
#include "../file_io.h"
#include <cstddef>
#include <string>
#include <vector>

namespace core
{
namespace
{
/** 设置文件里用的 VS Code 编码名。 */
std::string coding_name(ctx::Coding coding)
{
    return coding == ctx::Coding::GB2312 ? "gb2312" : "utf8" ;
}

/** JSON 字符串里的行尾转义形式。 */
std::string eol_escape(ctx::LineEnding line_ending)
{
    return line_ending == ctx::LineEnding::CRLF ? "\\r\\n" : "\\n" ;
}

/** 实际写盘用的行尾字节。 */
std::string eol_bytes(ctx::LineEnding line_ending)
{
    return line_ending == ctx::LineEnding::CRLF ? "\r\n" : "\n" ;
}

/** 转义 JSON 字符串值里的引号与反斜杠；生成器名一般只含 ASCII。 */
std::string escape_json(const std::string& text)
{
    std::string result ;
    result.reserve(text.size() + 8) ;
    for (const char letter : text)
    {
        if (letter == '"' || letter == '\\')
        {
            result.push_back('\\') ;
        }
        result.push_back(letter) ;
    }
    return result ;
}

/** 按标准答案拼 .vscode/settings.json 内容；生成器为空时不写 cmake.generator（只有探测到 Ninja 才传值）。 */
std::string build_settings_text(ctx::Coding coding, ctx::LineEnding line_ending,
    const std::string& cmake_generator)
{
    const std::string encoding = coding_name(coding) ;
    const std::string eol = eol_escape(line_ending) ;
    std::vector<std::string> lines ;
    lines.push_back("{") ;
    lines.push_back("  \"cmake.useCMakePresets\": \"always\",") ;
    lines.push_back("  \"cmake.configureOnOpen\": true,") ;
    if (!cmake_generator.empty())
    {
        lines.push_back("  \"cmake.generator\": \"" + escape_json(cmake_generator) + "\",") ;
    }
    lines.push_back("  \"cmake.configureSettings\": {") ;
    lines.push_back("    \"CMAKE_EXPORT_COMPILE_COMMANDS\": \"ON\",") ;
    lines.push_back("    \"CMAKE_CXX_COMPILER\": \"cl.exe\"") ;
    lines.push_back("  },") ;
    lines.push_back("  \"files.exclude\": {") ;
    lines.push_back("    \"build\": true,") ;
    lines.push_back("    \".gitattributes\": true,") ;
    lines.push_back("    \".gitignore\": true,") ;
    lines.push_back("    \".cache\": true,") ;
    lines.push_back("    \".git\": true,") ;
    lines.push_back("    \"CMakePresets.json\": true,") ;
    lines.push_back("    \".vscode\": true") ;
    lines.push_back("  },") ;
    lines.push_back("  \"files.encoding\": \"" + encoding + "\",") ;
    lines.push_back("  \"files.autoGuessEncoding\": false,") ;
    lines.push_back("  \"files.eol\": \"" + eol + "\",") ;
    lines.push_back("  \"[cpp]\": {") ;
    lines.push_back("    \"files.encoding\": \"" + encoding + "\",") ;
    lines.push_back("    \"files.eol\": \"" + eol + "\"") ;
    lines.push_back("  },") ;
    lines.push_back("  \"[c]\": {") ;
    lines.push_back("    \"files.encoding\": \"" + encoding + "\",") ;
    lines.push_back("    \"files.eol\": \"" + eol + "\"") ;
    lines.push_back("  },") ;
    lines.push_back("  \"[cmake]\": {") ;
    lines.push_back("    \"files.encoding\": \"utf8\"") ;
    lines.push_back("  },") ;
    lines.push_back("  \"[json]\": {") ;
    lines.push_back("    \"files.encoding\": \"utf8\"") ;
    lines.push_back("  },") ;
    lines.push_back("  \"[markdown]\": {") ;
    lines.push_back("    \"files.encoding\": \"utf8\"") ;
    lines.push_back("  }") ;
    lines.push_back("}") ;

    const std::string separator = eol_bytes(line_ending) ;
    std::string text ;
    for (const std::string& line : lines)
    {
        text += line ;
        text += separator ;
    }
    return text ;
}
} // namespace

ProjectFileResult write_vscode_settings(const std::filesystem::path& project_dir,
    ctx::Coding coding, ctx::LineEnding line_ending, const std::string& cmake_generator)
{
    ProjectFileResult result ;
    if (project_dir.empty())
    {
        result.m_message = "工程目录为空，无法写 .vscode/settings.json" ;
        return result ;
    }
    std::string error ;
    if (!ensure_directory(project_dir / L".vscode", error))
    {
        result.m_message = "创建 .vscode 目录失败: " + error ;
        return result ;
    }
    if (!write_text_file(project_dir / L".vscode" / L"settings.json",
        build_settings_text(coding, line_ending, cmake_generator), error))
    {
        result.m_message = "写 .vscode/settings.json 失败: " + error ;
        return result ;
    }
    result.m_ok = true ;
    result.m_message = "已写 .vscode/settings.json" ;
    return result ;
}
} // namespace core
