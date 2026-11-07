#pragma once
// ============================================================================
// scan_service.h — 三域并行扫描会话。
//
// 构造即启动三个后台线程，分别扫描编译器工具链、NX SDK 与 Windows SDK；
// 每个域先校验缓存路径，缓存失效才走该域的全量回退链。主线程随时读取进度
// 快照用于渲染，需要结果时 wait() 收敛。析构会取消并等待线程结束。
// ============================================================================
#include "scan_control.h"
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace scan
{
/** 三个可独立并发的扫描域。 */
enum class ScanDomain
{
    Compiler ,
    NxSdk ,
    WindowsSdk
} ;

/** 单个域的扫描进度快照。 */
struct DomainScanState
{
    /** 本域标识。 */
    ScanDomain m_domain = ScanDomain::Compiler ;
    /** 已访问的目录数。 */
    std::size_t m_visited = 0 ;
    /** 本域已确认的候选数。 */
    std::size_t m_found = 0 ;
    /** 本域是否已结束。 */
    bool m_finished = false ;
    /** 是否因取消而提前结束。 */
    bool m_cancelled = false ;
    /** 是否直接由缓存校验完成。 */
    bool m_from_cache = false ;
    /** 失败或说明文本；正常时为空。 */
    std::string m_message ;
} ;

/** 三域进度快照。 */
struct ScanState
{
    /** 按 ScanDomain 枚举顺序存放三个域状态。 */
    std::array<DomainScanState, 3> m_domains = {} ;
    /** 三个域是否都已结束。 */
    bool all_finished() const ;
} ;

struct ScanSessionData ;

/** 扫描会话的额外输入。 */
struct ScanSessionOptions
{
    /** 额外扫描根：便携工具链/开发包提示目录；三个域都会扫这些根的子树。 */
    std::vector<Path> m_extra_roots = {} ;
} ;

/** 三域并行扫描会话；构造启动线程，析构取消并等待线程收尾。 */
class ScanSession
{
    public:
    /** 启动三个后台扫描线程，立即返回；options 提供额外扫描根。 */
    explicit ScanSession(const ScanSessionOptions& options = {}) ;
    /** 取消并等待后台线程结束。 */
    ~ScanSession() ;
    ScanSession(const ScanSession&) = delete ;
    /** 禁止复制扫描会话。 */
    ScanSession& operator=(const ScanSession&) = delete ;

    /** 取当前进度快照；线程安全。 */
    ScanState state() const ;
    /** 请求取消；后台线程会尽快结束。 */
    void cancel() ;
    /** 阻塞等待三个域全部结束。 */
    void wait() ;
    /** 取扫描结果；未结束的域只包含当前已确认的部分。 */
    ScanReport results() const ;
    /** 把结果写入扫描缓存；缓存目录不可写时返回 false。 */
    bool save() const ;

    private:
    std::unique_ptr<ScanSessionData> m_data ;
} ;

/** 手动定位进度回调；参数为当前目录与累计访问数，返回 false 表示中止。 */
using LocateObserver = std::function<bool(const Path& directory, std::size_t visited)> ;

/** 在近似目录内递归（depth 层）按关键字找第一个编译器；找不到返回 nullopt。 */
std::optional<Compiler> locate_compiler(const Path& hint_directory, int depth = 5,
    const LocateObserver& observer = {}) ;

/** 在近似目录内递归（depth 层）按关键字找第一个 NX SDK；找不到返回 nullopt。 */
std::optional<NXSdk> locate_nx_sdk(const Path& hint_directory, int depth = 5,
    const LocateObserver& observer = {}) ;

/** 在近似目录内递归（depth 层）按关键字找第一个 Windows SDK；找不到返回 nullopt。 */
std::optional<WindowsSdk> locate_windows_sdk(const Path& hint_directory, int depth = 5,
    const LocateObserver& observer = {}) ;
} // namespace scan
