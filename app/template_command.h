#pragma once
#include <string>
#include <vector>

namespace app
{
/** 命令实现：--build-template <目录>，把全量模板代码按编码/行尾写到该目录。 */
int run_build_template_command(const std::vector<std::string>& arguments) ;
} // namespace app
