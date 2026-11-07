#pragma once
#include <string>

namespace template_header
{
inline std::string ui_header = R"(
#include <uf.h>
#include <uf_ui.h>
)";

inline std::string exception_header = R"(
#include <NXOpen/NXException.hxx>
)";

inline std::string std_header = R"(
#include <sstream>
#include <utility>
)";

inline std::string cpp20_header = R"(
#include <concepts>
)";

inline std::string string_header = R"(
#include <string>
)";

inline std::string modl_header = R"(
#include <uf_modl_utilities.h>
)";

inline std::string basic_defs_header = R"(
#include "basic_type_defs.h"
)";
} // namespace template_header
