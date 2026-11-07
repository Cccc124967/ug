#pragma once
#include <string>

namespace template_macro
{
inline std::string uf_guard = R"(

#define UFCall(func , on_failed)\
do\
{\
    int nx_error_code__ = func;\
    if(nx_error_code__ != 0)\
    {\
        char nx_error_msg[133] = {0};\
        UF_get_fail_message(nx_error_code__, nx_error_msg);\
        nx_msg("UFCall failed: ", #func, " error_code: ", nx_error_code__, " error_msg: ", nx_error_msg , " at ", __FILE__, ":", __LINE__);\
        on_failed;\
    }\
}while(0)
)";

inline std::string nxopen_guard = R"(

#define NXCall(func , on_failed)\
do\
{\
    try { func ; }\
    catch(const NXOpen::NXException& e)\
    {\
        nx_msg("NXCall failed: ", #func, " error_code: ", e.ErrorCode(), " error_msg: ", e.Message(), " at ", __FILE__, ":", __LINE__);\
        on_failed;\
    }\
    catch(const std::exception& e)\
    {\
        nx_msg("NXCall failed: ", #func, " error_msg: ", e.what(), " at ", __FILE__, ":", __LINE__);\
        on_failed;\
    }\
    catch(...)\
    {\
        nx_msg("NXCall failed: ", #func, " unknown error at ", __FILE__, ":", __LINE__);\
        on_failed;\
    }\
}while(0)
)";

// 目标工程文件 basic_type_defs.h 的完整内容（旧 backup/templates/basic_type_defs.h.txt）。
inline std::string basic_defs = R"(#pragma once

#define NX_NAMESPACE_BEGIN namespace nx{

#define NX_NAMESPACE_END }

#define NX_MAX_DIALOG_MSG_LEN 132
)";
} // namespace template_macro
