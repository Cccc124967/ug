#pragma once
// ============================================================================
// registration_record.h — --register / --unregister 的注册位置记录。
//
// 注册成功后把注册时的完整目录写进缓存目录下的 registration.tsv；注销时只认
// 这份记录，这样程序被搬到别的目录后仍能摘除原来的注册项。记录缺失或损坏时
// 由调用方决定直接报错，本模块只负责读写与校验。
// ============================================================================
#include <filesystem>
#include <string>

namespace app
{
/** 读注册记录；成功时 directory 是记录里的目录。 */
bool load_registration_record(const std::filesystem::path& file,
    std::filesystem::path& directory, std::string& error) ;

/** 写注册记录；覆盖旧记录。 */
bool save_registration_record(const std::filesystem::path& file,
    const std::filesystem::path& directory, std::string& error) ;

/** 删除注册记录；本来就不存在也算成功。 */
bool clear_registration_record(const std::filesystem::path& file, std::string& error) ;
} // namespace app
