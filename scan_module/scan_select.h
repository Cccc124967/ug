#pragma once
// ============================================================================
// scan_select.h — 工具链选型规则与选型记忆。
//
// 自动选型给快速部署用；排序与光标定位给普通路径的手动菜单用；
// 选型记忆与扫描缓存同目录存放（AppData → Temp → 不存）。
// ============================================================================
#include "build_plan.h"
#include "scan_control.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace scan
{
/** 自动选型结果。 */
struct ToolchainSelection
{
    /** 是否选齐必需项（编译器、NX SDK、generator）。 */
    bool m_ok = false ;
    /** 选中的编译器。 */
    Compiler m_compiler ;
    /** 选中的 NX SDK。 */
    NXSdk m_nx_sdk ;
    /** 选中的 Windows SDK；版本为空表示不锁定。 */
    WindowsSdk m_windows_sdk ;
    /** CMake 生成器名，如 "Visual Studio <主版本> <年份>"；推导失败时为空。 */
    std::string m_generator ;
    /** 构建方案：生成器、工具链文件与 make 程序。 */
    BuildPlan m_build_plan ;
    /** 非致命提示，例如选中 V145 的警告。 */
    std::string m_warning ;
    /** 失败原因。 */
    std::string m_message ;
} ;

/** 自动选编译器：V142 最优、V143 次之、V145 仅兜底并写警告；无可用返回 nullopt。 */
std::optional<Compiler> pick_compiler(const std::vector<Compiler>& compilers, std::string& warning) ;

/** 自动选 NX SDK：运行中的优先，否则版本号最高；无可用返回 nullopt。 */
std::optional<NXSdk> pick_nx_sdk(const std::vector<NXSdk>& sdks) ;

/** 自动选 Windows SDK：版本号最高；无可用返回 nullopt（调用方按不锁版本处理）。 */
std::optional<WindowsSdk> pick_windows_sdk(const std::vector<WindowsSdk>& sdks) ;

/** 组合自动选型；ninja_available 决定生成器（Ninja → NMake 静默回退）。 */
ToolchainSelection pick_toolchain(const ScanReport& report, bool ninja_available = true) ;

/** 从编译器信息推导 CMake 生成器名；无法推导返回空串。 */
std::string cmake_generator_name(const Compiler& compiler) ;

/** 手动菜单排序：编译器按自动选型优先级，NX SDK 运行中优先再按版本降序，Windows SDK 按版本降序。 */
void sort_compilers(std::vector<Compiler>& compilers) ;
void sort_nx_sdks(std::vector<NXSdk>& sdks) ;
void sort_windows_sdks(std::vector<WindowsSdk>& sdks) ;

/** 上次手动选型记忆，用于菜单光标定位。 */
struct SelectionMemory
{
    /** 上次编译器的 cl 路径文本。 */
    std::string m_compiler_key ;
    /** 上次 NX SDK 的安装根文本。 */
    std::string m_nx_sdk_key ;
    /** 上次 Windows SDK 的版本。 */
    std::string m_windows_sdk_key ;
} ;

/** 读取选型记忆；没有或损坏返回空。 */
SelectionMemory load_selection_memory() ;

/** 保存选型记忆；目录不可写返回 false。 */
bool save_selection_memory(const SelectionMemory& memory) ;

/** 在编译器菜单里定位记忆项的下标；找不到返回 0。 */
std::size_t find_compiler_index(const std::vector<Compiler>& compilers, const SelectionMemory& memory) ;

/** 在 NX SDK 菜单里定位记忆项的下标；找不到返回 0。 */
std::size_t find_nx_sdk_index(const std::vector<NXSdk>& sdks, const SelectionMemory& memory) ;

/** 在 Windows SDK 菜单里定位记忆项的下标；找不到返回 0。 */
std::size_t find_windows_sdk_index(const std::vector<WindowsSdk>& sdks, const SelectionMemory& memory) ;
} // namespace scan
