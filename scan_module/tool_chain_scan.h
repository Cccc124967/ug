#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace scan
{
    using Path = std::filesystem::path ;

    /// MARK: 编译工具链
    enum class ToolChainVersion
    {
        V140 ,
        V141 ,
        V142 ,
        V143 ,
        V145 ,
        Undefined
    };

    /** 编译器来源类型：能不能拿到 VS 生成器，决定构建方案怎么选 */
    enum class ToolChainKind
    {
        /** 完整 Visual Studio 安装，有生成器与平台工具集 */
        VisualStudio ,
        /** 便携/非标目录里的 MSVC 工具集，没有 VS 安装证据 */
        Portable ,
        /** 尚未判定（旧缓存或正在扫描） */
        Unknown
    };

    struct Compiler
    {
        /** 编译器名字，如 MSVC v143 */
        std::string m_name = "null" ;
        /** 工具链大类版本 */
        ToolChainVersion m_version = ToolChainVersion::Undefined ;
        /** 工具集完整版本，如 14.xx.yyyyy；未知时为空 */
        std::string m_toolset_version = {} ;
        /** 编译器路径 */
        Path m_cl_path = {} ;
        /** 环境脚本位置（VS 是 vcvarsall.bat；便携工具链可能是 nx_toolchain.cmake） */
        Path m_vcvars_path = {} ;
        /** 非默认工具集要传给 vcvarsall 的参数，如 -vcvars_ver=14.xx；默认工具集为空 */
        std::string m_vcvars_argument = {} ;
        /** 关键文件是否齐全 */
        bool m_is_checked = false ;
        /** 所属 Visual Studio 的 CMake 生成器名，如 "Visual Studio <主版本> <年份>"；未知时为空 */
        std::string m_generator = {} ;
        /** 该 VS 安装里真实可用的平台工具集（如 v143 / v145）；未知时为空 */
        std::string m_platform_toolset = {} ;
        /** 来源类型：VS 安装 / 便携工具集 */
        ToolChainKind m_kind = ToolChainKind::Unknown ;
        /** cl 自报的 _MSC_VER（如 1929）；未探测时为 0 */
        int m_msvc_ver = 0 ;
        /** cl 自报的完整版本（如 19.29.30159）；未探测时为空 */
        std::string m_compiler_version = {} ;
        /** 工具集 include 目录（从 cl 路径反推，VS 与便携布局通用） */
        std::vector<Path> m_include_dirs = {} ;
        /** 工具集库目录（从 cl 路径反推，VS 与便携布局通用） */
        std::vector<Path> m_lib_dirs = {} ;
    };

    /** 扫描出来的编译器们 */
    inline std::vector<Compiler> compilers = {} ;

    /** 扫描,递归,带深度,填充compilers */
    bool scan_msvc(const Path& target_dir , int depth = 5);

    /** 对一个编译器做关键文件检查 */
    bool check_compiler(const Compiler& compiler);


    /// MARK: NX sdk

    /** 代表一个sdk */
    struct NXSdk
    {
        /** NX版本(sdk版本)，如 2007；未知时为空 */
        std::string m_version = "" ;
        /** 版本来源：env_print / registry / directory */
        std::string m_version_source = "" ;
        /** NX 安装根(含 UGII、NXBIN 的那一层) */
        Path m_root = {} ;
        /** uf 头文件所在 include 根 */
        Path m_uf_header_dir = {} ;
        /** NXOpen 头文件 include 根(可直接 #include <NXOpen/...>) */
        Path m_nxopen_header_dir = {} ;
        /** lib 目录 */
        Path m_lib_dir = {} ;
        /** 关键头文件与库是否齐全 */
        bool m_is_checked = false ;
        /** 是否正在运行 */
        bool m_is_running = false ;
    };

    /** 扫描sdk */
    std::vector<NXSdk> scan_nx_sdks(const Path& target_path , int depth = 5);

    /** 从正在活跃的进程中反向抓dir, flags == 依据 , 或关系 */
    Path find_dir_by_active_process(const std::string& process_name , const std::vector<std::string>& flags = { "UGII" , "NXBIN" });

    /// MARK: Windows SDK

    /** 代表一个 Windows SDK 安装（同一个 kits 根下的一个版本）。 */
    struct WindowsSdk
    {
        /** SDK 版本，如 10.0.26100.0；未知时为空 */
        std::string m_version = "" ;
        /** 发现来源：registry / env / common / user / sweep */
        std::string m_source = "" ;
        /** kits 根(含 Include、Lib 的那一层) */
        Path m_root = {} ;
        /** include 根(<root>/Include/<version>) */
        Path m_include_dir = {} ;
        /** lib 根(<root>/Lib/<version>) */
        Path m_lib_dir = {} ;
        /** rc.exe / mt.exe 所在目录(<root>/bin/<version>/x64) */
        Path m_bin_dir = {} ;
        /** 关键头文件、库与工具是否齐全 */
        bool m_is_checked = false ;
    };

    /** 扫描 Windows SDK；按 Include/Lib 布局枚举版本并验证关键文件 */
    std::vector<WindowsSdk> scan_windows_sdks(const Path& target_path , int depth = 5);

    /// MARK: Ninja

    /** 探测到的 Ninja 构建工具。 */
    struct NinjaTool
    {
        /** 是否找到能正常执行的 ninja */
        bool m_found = false ;
        /** ninja.exe 路径；没找到时为空 */
        Path m_executable = {} ;
        /** ninja --version 的输出；没找到时为空 */
        std::string m_version = {} ;
    };

    /** 探测本机 Ninja：PATH 与 VS 自带目录，ninja --version 成功才算可用 */
    NinjaTool detect_ninja();

    /// MARK: 其他

    /** 综合检查：编译器关键文件齐全且 SDK 头文件/库齐全 */
    bool synthesize_check(const Compiler& compiler , const NXSdk& sdk) ;
}
