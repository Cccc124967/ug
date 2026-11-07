#include "registration_record.h"
#include "../core_module/dev_core.h"
#include "../core_module/file_io.h"
#include <exception>
#include <fstream>
#include <string>
#include <system_error>

namespace app
{
namespace
{
/** 注册记录文件头；格式变化时换版本号。 */
constexpr const char* registration_header = "# nx_dev registration v1" ;

/** 去掉行尾的 CR 与开头的 UTF-8 BOM，容忍手工编辑过的文件。 */
std::string normalize_line(std::string line)
{
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
    {
        line.erase(0, 3) ;
    }
    while (!line.empty() && line.back() == '\r')
    {
        line.pop_back() ;
    }
    return line ;
}
} // namespace

bool load_registration_record(const std::filesystem::path& file,
    std::filesystem::path& directory, std::string& error)
{
    directory.clear() ;
    if (file.empty())
    {
        error = "注册记录路径为空" ;
        return false ;
    }
    std::ifstream stream(file, std::ios::binary) ;
    if (!stream)
    {
        error = "读不到注册记录文件" ;
        return false ;
    }
    std::string header ;
    if (!std::getline(stream, header) || normalize_line(header) != registration_header)
    {
        error = "注册记录文件头不认识" ;
        return false ;
    }
    std::string path_line ;
    if (!std::getline(stream, path_line))
    {
        error = "注册记录缺少目录行" ;
        return false ;
    }
    path_line = normalize_line(path_line) ;
    if (path_line.empty())
    {
        error = "注册记录里的目录为空" ;
        return false ;
    }
    try
    {
        directory = std::filesystem::path(core::utf8_to_wide(path_line)) ;
    }
    catch (const std::exception& exception)
    {
        error = std::string("注册记录里的目录无法解析: ") + exception.what() ;
        return false ;
    }
    return true ;
}

bool save_registration_record(const std::filesystem::path& file,
    const std::filesystem::path& directory, std::string& error)
{
    if (file.empty() || directory.empty())
    {
        error = "注册记录参数为空" ;
        return false ;
    }
    std::string text = registration_header ;
    text += '\n' ;
    text += core::path_text(directory) ;
    text += '\n' ;
    return core::write_text_file(file, text, error) ;
}

bool clear_registration_record(const std::filesystem::path& file, std::string& error)
{
    if (file.empty())
    {
        error = "注册记录路径为空" ;
        return false ;
    }
    std::error_code filesystem_error ;
    std::filesystem::remove(file, filesystem_error) ;
    if (filesystem_error)
    {
        error = filesystem_error.message() ;
        return false ;
    }
    return true ;
}
} // namespace app
