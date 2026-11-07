#pragma once
#include <string>

namespace template_output
{
inline std::string nx_msg = R"(

template <typename... Args>
inline void nx_msg(Args&& ... args)
{
    std::ostringstream ss;
    static bool listen_window_opened = false;
    (ss << ... << std::forward<Args>(args)) << '\n';
    auto str = ss.str();
    if(str.size() >= NX_MAX_DIALOG_MSG_LEN)
    {
        if(!listen_window_opened) { listen_window_opened = (UF_UI_open_listing_window() == 0) ; }
        if(!listen_window_opened) return ;
        UF_UI_write_listing_window(str.c_str()) ;
    }else
    {
        uc1601(str.c_str(), 1);
    }
}
)";

inline std::string nx_status = R"(

template <typename... Args>
inline void nx_status(Args&& ... args)
{
    std::ostringstream ss;
    static bool listen_window_opened = false;
    (ss << ... << std::forward<Args>(args)) << '\n';
    auto str = ss.str();
    if(str.size() > NX_MAX_DIALOG_MSG_LEN) str.resize(NX_MAX_DIALOG_MSG_LEN - 1);
    uc1601(str.c_str() , 0);
}
)";
} // namespace template_output