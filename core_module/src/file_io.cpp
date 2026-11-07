#include "../file_io.h"
#include <windows.h>
#include <fstream>
#include <system_error>

namespace core
{
std::string path_text(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return {} ;
    }
    const std::wstring wide = path.wstring() ;
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
        static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr) ;
    if (length <= 0)
    {
        return {} ;
    }
    std::string text(static_cast<std::size_t>(length), '\0') ;
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        text.data(), length, nullptr, nullptr) ;
    return text ;
}

bool ensure_directory(const std::filesystem::path& directory, std::string& error)
{
    if (directory.empty())
    {
        error = "目录为空" ;
        return false ;
    }
    std::error_code code ;
    std::filesystem::create_directories(directory, code) ;
    if (code)
    {
        error = "建不了目录 " + path_text(directory) + "（" + code.message() +
            "，错误码 " + std::to_string(code.value()) + "）" ;
        return false ;
    }
    return true ;
}

bool write_text_file(const std::filesystem::path& path, std::string_view text, std::string& error)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc) ;
    if (!stream)
    {
        error = "写不了 " + path_text(path) ;
        return false ;
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size())) ;
    if (!stream)
    {
        error = "写入失败 " + path_text(path) ;
        return false ;
    }
    return true ;
}
} // namespace core
