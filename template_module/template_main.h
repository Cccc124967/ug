#pragma once
#include <string>

namespace template_main
{
inline std::string ufusr = R"(

/** 入口函数 */
extern "C" DllExport void ufusr(char* params , int* retcode , int len)
{
    /** 初始化NX环境 */
    UF_initialize();

    /** TODO 此处写主逻辑 */
    uc1601("欢迎来到NX世界" , 1);

    /** 销毁NX环境 */
    UF_terminate();
}
)";

inline std::string ufusr_ask_unload = R"(

/** 卸载插件方式 */
extern "C" DllExport int ufusr_ask_unload()
{
    /** 插件运行完成立即卸载 */
    return UF_UNLOAD_IMMEDIATELY;

    /** 手动卸载插件 文件->实用工具->卸载共享镜像 */
    // return UF_UNLOAD_SEL_DIALOG ;

    /** 跟随NX主进程生命周期,NX存活期间永不卸载 */
    // return UF_UNLOAD_UG_TERMINATE ;
}
)";

inline std::string ufusr_cleanup = R"(

/** 资源清理钩子 */
extern "C" DllExport void ufusr_cleanup()
{
    /** 此处清理资源,该钩子会在插件被卸载之前自动调用 */
}
)";

inline std::string banner_shadow = R"(
███╗   ██╗██╗  ██╗
████╗  ██║╚██╗██╔╝
██╔██╗ ██║ ╚███╔╝ 
██║╚██╗██║ ██╔██╗ 
██║ ╚████║██╔╝ ██╗
╚═╝  ╚═══╝╚═╝  ╚═╝
)";

} // namespace template_main
