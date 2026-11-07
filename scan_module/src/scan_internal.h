#pragma once
// ============================================================================
// scan_internal.h — scan_module 内部接口。
//
// 这里放出按域扫描、缓存校验、VS 安装证据与缓存目录解析等仅供模块内部和
// agent_build 测试使用的东西；不对 app 或其它模块暴露。
// ============================================================================
#include "../scan_control.h"
#include "../scan_service.h"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace scan::engine
{
/** 单域扫描的回调集合：进度上报与取消查询由调用方提供。 */
struct DomainScanHooks
{
    /** 每访问一个目录回调一次；返回 false 中止扫描。 */
    std::function<bool(std::size_t visited, std::size_t found)> m_progress = {} ;
    /** 查询是否已请求取消；为空视为不取消。 */
    std::function<bool()> m_cancelled = {} ;
} ;

/** 一个 Visual Studio 安装实例；除安装根外字段都可能为空。 */
struct VisualStudioInstall
{
    /** 安装根。 */
    Path m_root = {} ;
    /** 安装显示名，如 Visual Studio Community 2026。 */
    std::string m_display_name = {} ;
    /** 安装版本，如 18.6.11819.183。 */
    std::string m_version = {} ;
    /** 产品主版本，如 18；未知时为空。 */
    std::string m_major = {} ;
    /** 显示名里的年份，如 2026；未知时为空。 */
    std::string m_year = {} ;
} ;

/** 只扫给定额外根的编译器；缓存命中时用来补扫便携工具链目录。 */
std::vector<Compiler> scan_compiler_roots(const std::vector<Path>& roots, int depth,
    const DomainScanHooks& hooks) ;

/** 只扫给定额外根的 NX SDK；缓存命中时用来补扫便携工具链目录。 */
std::vector<NXSdk> scan_nx_sdk_roots(const std::vector<Path>& roots, int depth,
    const DomainScanHooks& hooks) ;

/** 只扫给定额外根的 Windows SDK；缓存命中时用来补扫便携工具链目录。 */
std::vector<WindowsSdk> scan_windows_sdk_roots(const std::vector<Path>& roots, int depth,
    const DomainScanHooks& hooks) ;

/** 按回退链扫描编译器域；额外扫描根先扫，不含用户目录与全盘。 */
std::vector<Compiler> scan_compiler_domain(int depth, const DomainScanHooks& hooks,
    const std::vector<Path>& extra_roots = {}) ;

/** 按回退链扫描 NX SDK 域；额外扫描根先扫，不含用户目录与全盘。 */
std::vector<NXSdk> scan_nx_sdk_domain(int depth, const DomainScanHooks& hooks,
    const std::vector<Path>& extra_roots = {}) ;

/** 按回退链扫描 Windows SDK 域；额外扫描根先扫，不含用户目录与全盘。 */
std::vector<WindowsSdk> scan_windows_sdk_domain(int depth, const DomainScanHooks& hooks,
    const std::vector<Path>& extra_roots = {}) ;

/** 校验缓存里的编译器条目：关键文件仍在才算有效。 */
bool cached_compiler_ok(const Compiler& compiler) ;

/** 校验缓存里的 NX SDK 条目：头文件与库仍在才算有效。 */
bool cached_nx_sdk_ok(const NXSdk& sdk) ;

/** 校验缓存里的 Windows SDK 条目：头文件、库与 rc.exe 仍在才算有效。 */
bool cached_windows_sdk_ok(const WindowsSdk& sdk) ;

/** 只凭安装根构造 VS 实例信息；显示名与版本留空。 */
VisualStudioInstall visual_studio_from_path(const Path& root) ;

/** 由安装实例拼 CMake 生成器名，如 "Visual Studio <主版本> <年份>"；证据不足返回空串。 */
std::string generator_from_install(const VisualStudioInstall& install) ;

/** 在给定安装实例里按安装根匹配并拼生成器名；纯函数，匹配不到返回空串。 */
std::string generator_for_install(const Path& compiler_root,
    const std::vector<VisualStudioInstall>& installs) ;

/** 枚举一个 VS 安装实例下的编译器候选：每个真实可用的平台工具集一条。 */
std::vector<Compiler> compiler_candidates(const VisualStudioInstall& install) ;

/** 给一个编译器补平台工具集名；缓存里没有时按本机 VS 安装与工具集版本重新推导，推不出返回空。 */
std::string platform_toolset_for_compiler(const Compiler& compiler) ;

/** 给一个编译器补生成器名；缓存里没有生成器时用本机 VS 安装证据推导，推不出返回空。 */
std::string generator_for_compiler(const Compiler& compiler) ;

/** 探测 cl 的 _MSC_VER / _MSC_FULL_VER；成功时回填版本字段并返回 true。 */
bool probe_compiler(Compiler& compiler) ;

/** 把 _MSC_VER（如 1929）映射到工具链大类版本；未知返回 Undefined。 */
ToolChainVersion version_from_msvc_ver(int msvc_ver) ;

/** 由 cl.exe 路径反推工具集根（含 include/ 与 lib/ 的那层）；推不出返回空。 */
Path portable_toolset_root(const Path& cl_path) ;

/** 解析当前可用的缓存目录：LOCALAPPDATA → TEMP；都不可用返回空。 */
Path cache_directory() ;

/** 按数字段比较版本文本；left 大返回正数，小返回负数，相等返回 0。 */
int compare_version_text(const std::string& left, const std::string& right) ;
} // namespace scan::engine
