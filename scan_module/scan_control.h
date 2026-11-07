#pragma once
#include "tool_chain_scan.h"
#include <cstddef>
#include <functional>
#include <vector>

namespace scan
{
/** 分级扫描的阶段；由近及远，前三级通常在一两秒内完成。 */
enum class ScanTier
{
    ActiveProcess ,
    Environment ,
    CommonRoot ,
    UserDirectory ,
    DiskSweep
};

/** 一次进度的快照；界面据此显示进度并决定是否取消。 */
struct ScanProgress
{
    /** 当前阶段。 */
    ScanTier m_tier = ScanTier::ActiveProcess ;
    /** 正在访问的目录。 */
    Path m_directory = {} ;
    /** 本阶段已访问的目录数。 */
    std::size_t m_visited = 0 ;
    /** 目前已确认的编译器与 SDK 总数。 */
    std::size_t m_found = 0 ;
    /** 本阶段是否已结束；结束事件的目录为空。 */
    bool m_stage_finished = false ;
};

/** 进度回调；返回 false 表示立即停止扫描。 */
using ScanObserver = std::function<bool(const ScanProgress&)> ;

/** 扫描输入；每个字段都有可直接使用的默认值。 */
struct ScanRequest
{
    /** 用户指定的目录；为空则跳过用户目录阶段。 */
    Path m_user_directory = {} ;
    /** 是否扫描本机的常见安装根；自动化测试可关闭。 */
    bool m_system_roots = true ;
    /** 用户目录与全盘阶段的递归深度。 */
    int m_depth = 5 ;
    /** 是否执行全盘兜底；默认关闭。 */
    bool m_disk_sweep = false ;
    /** 进度回调；为空表示不汇报进度也无法取消。 */
    ScanObserver m_observer = {} ;
};

/** 扫描产出；用于填充界面选项或写入缓存。 */
struct ScanReport
{
    /** 找到的编译器，按路径去重。 */
    std::vector<Compiler> m_compilers ;
    /** 找到的 NX SDK，按路径去重。 */
    std::vector<NXSdk> m_sdks ;
    /** 找到的 Windows SDK（同一个根下每个版本一条），按版本去重。 */
    std::vector<WindowsSdk> m_windows_sdks ;
    /** 实际走到的最后一个阶段。 */
    ScanTier m_reached = ScanTier::ActiveProcess ;
    /** 是否被观察者或中断要求提前停止。 */
    bool m_cancelled = false ;
};

/** 转成 UTF-8 路径文本；非法码位用替换字符，不抛异常。 */
std::string path_text(const Path& path) ;

/** 从 UTF-8 文本还原路径；非法字节按替换字符处理，不抛异常。 */
Path path_from_text(const std::string& text) ;

/** 读取环境变量；不存在或读取失败返回空串。 */
std::string environment_value(const std::string& name) ;

/** 调 NXBIN\env_print -v 取权威版本；没有该程序时返回空串。 */
std::string nx_runtime_version(const Path& nx_root) ;

/** 按五级阶段扫描编译器、NX SDK 与 Windows SDK：进程/注册表 → 环境变量 → 常规目录 → 手动目录 → 全盘；同一路径只检查一次。 */
ScanReport scan_environment(const ScanRequest& request) ;

/** 读取上次扫描缓存；文件缺失或损坏返回空报告。 */
ScanReport load_cache() ;

/** 写入扫描缓存；目录不可写时返回 false。 */
bool save_cache(const ScanReport& report) ;

/** 返回缓存文件路径，用于界面提示。 */
Path cache_path() ;

/** 缓存目录（NX_DEV_CACHE_DIR → LOCALAPPDATA/nx_dev → TEMP/nx_dev）；都不可用返回空。 */
Path cache_directory() ;
} // namespace scan
