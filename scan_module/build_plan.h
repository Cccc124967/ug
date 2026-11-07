#pragma once
// ============================================================================
// build_plan.h — 由编译器与选件推导"这个工程该怎么构建"。
//
// 与具体工具链布局无关：VS 安装用 Visual Studio 生成器；便携工具集优先用
// 现成的 *.cmake toolchain 文件；两者都没有时用扫描到的 cl + Windows SDK 自动
// 生成一份 toolchain 文件。Ninja 不可用时静默回退 NMake Makefiles——cl 可用
// 才是硬条件，make 程序只是执行器。
// ============================================================================
#include "tool_chain_scan.h"
#include <filesystem>
#include <string>

namespace scan
{
/** 构建方案类型。 */
enum class BuildPlanKind
{
    /** 完整 VS 安装：用 Visual Studio 生成器，不需要工具链文件 */
    VisualStudio ,
    /** Ninja / NMake + 现成的 toolchain 文件 */
    ToolchainFile ,
    /** Ninja / NMake + 由扫描结果自动生成的 toolchain 文件 */
    GeneratedToolchain ,
    /** 推不出可构建方案 */
    Unresolved
} ;

/** 一次构建方案；生成器、工具链文件与 make 程序都是推导结果。 */
struct BuildPlan
{
    /** 方案类型 */
    BuildPlanKind m_kind = BuildPlanKind::Unresolved ;
    /** CMake 生成器名；Unresolved 时为空 */
    std::string m_generator = {} ;
    /** 是否单配置生成器（Ninja / NMake Makefiles） */
    bool m_single_config = false ;
    /** 现成的工具链文件；需要自动生成时为空 */
    std::filesystem::path m_toolchain_file = {} ;
    /** NMake 方案要显式指定的 nmake 路径；其余方案为空 */
    std::filesystem::path m_make_program = {} ;
    /** 失败原因；成功时为空 */
    std::string m_message = {} ;
    /** 是否可构建 */
    bool m_ok = false ;

    /** 是否需要在工程里自动生成 toolchain 文件。 */
    bool needs_generated_toolchain() const noexcept ;

    /** 是否使用 Ninja 生成器。 */
    bool uses_ninja() const noexcept ;
} ;

/** 从编译器推导构建方案；ninja_available 来自 Ninja 探测结果。 */
BuildPlan resolve_build_plan(const Compiler& compiler, bool ninja_available) ;

/** 找 nmake.exe：优先 cl 同目录，其次 PATH；找不到返回空。 */
std::filesystem::path find_nmake(const Compiler& compiler) ;
} // namespace scan
