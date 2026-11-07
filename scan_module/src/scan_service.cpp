#include "../scan_service.h"
#include "scan_internal.h"
#include <atomic>
#include <cstddef>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace scan
{
namespace
{
/** 三个可并发扫描的域数量。 */
constexpr std::size_t domain_count = 3 ;

/** 会话扫描的递归深度；不含用户目录与全盘。 */
constexpr int session_depth = 5 ;

/** 去重键：路径文本转小写。 */
std::string identity_key(const Path& path)
{
    std::string text = path_text(path) ;
    for (char& letter : text)
    {
        if (letter >= 'A' && letter <= 'Z')
        {
            letter = static_cast<char>(letter - 'A' + 'a') ;
        }
    }
    return text ;
}

/** 合并编译器；按 cl 路径（或环境脚本）去重。 */
void merge_compilers(std::vector<Compiler>& target, std::vector<Compiler> extra)
{
    std::set<std::string> keys ;
    for (const Compiler& compiler : target)
    {
        keys.insert(identity_key(compiler.m_cl_path.empty() ? compiler.m_vcvars_path : compiler.m_cl_path)) ;
    }
    for (Compiler& compiler : extra)
    {
        const std::string key = identity_key(compiler.m_cl_path.empty() ? compiler.m_vcvars_path : compiler.m_cl_path) ;
        if (keys.insert(key).second)
        {
            target.push_back(std::move(compiler)) ;
        }
    }
}

/** 合并 NX SDK；按安装根（或头文件根）去重。 */
void merge_nx_sdks(std::vector<NXSdk>& target, std::vector<NXSdk> extra)
{
    std::set<std::string> keys ;
    for (const NXSdk& sdk : target)
    {
        const Path identity = sdk.m_root.empty() ?
            (sdk.m_uf_header_dir.empty() ? sdk.m_nxopen_header_dir : sdk.m_uf_header_dir) : sdk.m_root ;
        keys.insert(identity_key(identity)) ;
    }
    for (NXSdk& sdk : extra)
    {
        const Path identity = sdk.m_root.empty() ?
            (sdk.m_uf_header_dir.empty() ? sdk.m_nxopen_header_dir : sdk.m_uf_header_dir) : sdk.m_root ;
        if (keys.insert(identity_key(identity)).second)
        {
            target.push_back(std::move(sdk)) ;
        }
    }
}

/** 合并 Windows SDK；按 kits 根 + 版本去重。 */
void merge_windows_sdks(std::vector<WindowsSdk>& target, std::vector<WindowsSdk> extra)
{
    std::set<std::string> keys ;
    for (const WindowsSdk& sdk : target)
    {
        keys.insert(identity_key(sdk.m_root) + "\t" + sdk.m_version) ;
    }
    for (WindowsSdk& sdk : extra)
    {
        if (keys.insert(identity_key(sdk.m_root) + "\t" + sdk.m_version).second)
        {
            target.push_back(std::move(sdk)) ;
        }
    }
}
} // namespace

/** 扫描会话的共享数据；除原子量外的字段都由 m_mutex 保护。 */
struct ScanSessionData
{
    /** 初始化三个域的原子量与共享报告。 */
    ScanSessionData()
    {
        for (std::size_t index = 0 ; index < domain_count ; ++index)
        {
            m_visited[index].store(0) ;
            m_found[index].store(0) ;
            m_finished[index].store(false) ;
            m_cancelled[index].store(false) ;
            m_from_cache[index].store(false) ;
        }
        m_report.m_reached = ScanTier::CommonRoot ;
    }

    /** 取当前进度快照；线程安全。 */
    ScanState state() const
    {
        ScanState result ;
        std::lock_guard<std::mutex> guard(m_mutex) ;
        for (std::size_t index = 0 ; index < domain_count ; ++index)
        {
            DomainScanState& domain = result.m_domains[index] ;
            domain.m_domain = static_cast<ScanDomain>(index) ;
            domain.m_visited = m_visited[index].load() ;
            domain.m_found = m_found[index].load() ;
            domain.m_finished = m_finished[index].load() ;
            domain.m_cancelled = m_cancelled[index].load() ;
            domain.m_from_cache = m_from_cache[index].load() ;
            domain.m_message = m_messages[index] ;
        }
        return result ;
    }

    /** 取共享报告副本；线程安全。 */
    ScanReport report() const
    {
        std::lock_guard<std::mutex> guard(m_mutex) ;
        return m_report ;
    }

    /** 取某个域的进度与取消钩子。 */
    engine::DomainScanHooks hooks(std::size_t index)
    {
        engine::DomainScanHooks result ;
        result.m_progress = [this, index](std::size_t visited, std::size_t found)
        {
            m_visited[index].store(visited) ;
            m_found[index].store(found) ;
            return !m_cancel.load() ;
        } ;
        result.m_cancelled = [this]()
        {
            return m_cancel.load() ;
        } ;
        return result ;
    }

    /** 把编译器域结果发布到共享报告并标记该域结束。 */
    void publish_compilers(std::vector<Compiler> found_compilers, bool from_cache, bool cancelled,
        std::string message)
    {
        const std::size_t index = static_cast<std::size_t>(ScanDomain::Compiler) ;
        const std::size_t count = found_compilers.size() ;
        {
            std::lock_guard<std::mutex> guard(m_mutex) ;
            m_report.m_compilers = std::move(found_compilers) ;
            m_report.m_cancelled = m_report.m_cancelled || cancelled ;
            m_messages[index] = std::move(message) ;
        }
        m_found[index].store(count) ;
        m_from_cache[index].store(from_cache) ;
        m_cancelled[index].store(cancelled) ;
        m_finished[index].store(true) ;
    }

    /** 把 NX SDK 域结果发布到共享报告并标记该域结束。 */
    void publish_sdks(std::vector<NXSdk> sdks, bool from_cache, bool cancelled, std::string message)
    {
        const std::size_t index = static_cast<std::size_t>(ScanDomain::NxSdk) ;
        const std::size_t count = sdks.size() ;
        {
            std::lock_guard<std::mutex> guard(m_mutex) ;
            m_report.m_sdks = std::move(sdks) ;
            m_report.m_cancelled = m_report.m_cancelled || cancelled ;
            m_messages[index] = std::move(message) ;
        }
        m_found[index].store(count) ;
        m_from_cache[index].store(from_cache) ;
        m_cancelled[index].store(cancelled) ;
        m_finished[index].store(true) ;
    }

    /** 把 Windows SDK 域结果发布到共享报告并标记该域结束。 */
    void publish_windows_sdks(std::vector<WindowsSdk> sdks, bool from_cache, bool cancelled,
        std::string message)
    {
        const std::size_t index = static_cast<std::size_t>(ScanDomain::WindowsSdk) ;
        const std::size_t count = sdks.size() ;
        {
            std::lock_guard<std::mutex> guard(m_mutex) ;
            m_report.m_windows_sdks = std::move(sdks) ;
            m_report.m_cancelled = m_report.m_cancelled || cancelled ;
            m_messages[index] = std::move(message) ;
        }
        m_found[index].store(count) ;
        m_from_cache[index].store(from_cache) ;
        m_cancelled[index].store(cancelled) ;
        m_finished[index].store(true) ;
    }

    /** 等待并回收三个后台线程；重复调用无副作用。 */
    void join()
    {
        if (m_joined.exchange(true))
        {
            return ;
        }
        for (std::thread& thread : m_threads)
        {
            if (thread.joinable())
            {
                thread.join() ;
            }
        }
    }

    std::atomic_bool m_cancel = false ;
    std::atomic_bool m_joined = false ;
    std::array<std::atomic<std::size_t>, domain_count> m_visited = {} ;
    std::array<std::atomic<std::size_t>, domain_count> m_found = {} ;
    std::array<std::atomic_bool, domain_count> m_finished = {} ;
    std::array<std::atomic_bool, domain_count> m_cancelled = {} ;
    std::array<std::atomic_bool, domain_count> m_from_cache = {} ;
    mutable std::mutex m_mutex ;
    std::array<std::string, domain_count> m_messages = {} ;
    ScanReport m_report = {} ;
    /** 构造时读到的缓存；线程启动后只读。 */
    ScanReport m_cache = {} ;
    /** 额外扫描根；线程启动后只读。 */
    std::vector<Path> m_extra_roots = {} ;
    std::array<std::thread, domain_count> m_threads = {} ;
} ;

namespace
{
/** 编译器域线程入口：缓存优先，缓存无效才走全量回退链。 */
void run_compiler_domain(ScanSessionData& data)
{
    std::vector<Compiler> cached ;
    for (const Compiler& compiler : data.m_cache.m_compilers)
    {
        if (!engine::cached_compiler_ok(compiler))
        {
            continue ;
        }
        // 旧缓存没有生成器/来源/真实版本字段；命中缓存时用本机证据与 cl 自己补一次。
        Compiler enriched = compiler ;
        enriched.m_generator = engine::generator_for_compiler(enriched) ;
        enriched.m_platform_toolset = engine::platform_toolset_for_compiler(enriched) ;
        if (enriched.m_kind == ToolChainKind::Unknown)
        {
            if (!enriched.m_generator.empty())
            {
                enriched.m_kind = ToolChainKind::VisualStudio ;
            }
            else if (enriched.m_vcvars_path.extension() == L".cmake")
            {
                enriched.m_kind = ToolChainKind::Portable ;
            }
        }
        if (enriched.m_msvc_ver == 0)
        {
            (void) engine::probe_compiler(enriched) ;
        }
        const bool portable_entry = enriched.m_kind == ToolChainKind::Portable ||
            enriched.m_vcvars_path.extension() == L".cmake" ;
        if (!portable_entry && enriched.m_platform_toolset.empty())
        {
            // 对不上本机真实平台工具集的老 VS 条目不可用于快速部署，丢弃后走全量扫描。
            continue ;
        }
        cached.push_back(std::move(enriched)) ;
    }
    if (!cached.empty())
    {
        if (!data.m_extra_roots.empty())
        {
            merge_compilers(cached, engine::scan_compiler_roots(data.m_extra_roots, session_depth,
                data.hooks(static_cast<std::size_t>(ScanDomain::Compiler)))) ;
        }
        data.publish_compilers(std::move(cached), true, false, {}) ;
        return ;
    }
    if (data.m_cancel.load())
    {
        data.publish_compilers({}, false, true, "已取消") ;
        return ;
    }
    std::vector<Compiler> found_compilers = engine::scan_compiler_domain(session_depth,
        data.hooks(static_cast<std::size_t>(ScanDomain::Compiler)), data.m_extra_roots) ;
    const bool cancelled = data.m_cancel.load() ;
    std::string message ;
    if (cancelled)
    {
        message = "已取消" ;
    }
    else if (found_compilers.empty())
    {
        message = "没有找到可用的编译器" ;
    }
    data.publish_compilers(std::move(found_compilers), false, cancelled, std::move(message)) ;
}

/** NX SDK 域线程入口：缓存优先，缓存无效才走全量回退链。 */
void run_nx_sdk_domain(ScanSessionData& data)
{
    std::vector<NXSdk> cached ;
    for (const NXSdk& sdk : data.m_cache.m_sdks)
    {
        if (engine::cached_nx_sdk_ok(sdk))
        {
            cached.push_back(sdk) ;
        }
    }
    if (!cached.empty())
    {
        if (!data.m_extra_roots.empty())
        {
            merge_nx_sdks(cached, engine::scan_nx_sdk_roots(data.m_extra_roots, session_depth,
                data.hooks(static_cast<std::size_t>(ScanDomain::NxSdk)))) ;
        }
        data.publish_sdks(std::move(cached), true, false, {}) ;
        return ;
    }
    if (data.m_cancel.load())
    {
        data.publish_sdks({}, false, true, "已取消") ;
        return ;
    }
    std::vector<NXSdk> sdks = engine::scan_nx_sdk_domain(session_depth,
        data.hooks(static_cast<std::size_t>(ScanDomain::NxSdk)), data.m_extra_roots) ;
    const bool cancelled = data.m_cancel.load() ;
    std::string message ;
    if (cancelled)
    {
        message = "已取消" ;
    }
    else if (sdks.empty())
    {
        message = "没有找到可用的 NX SDK" ;
    }
    data.publish_sdks(std::move(sdks), false, cancelled, std::move(message)) ;
}

/** Windows SDK 域线程入口：缓存优先，缓存无效才走全量回退链。 */
void run_windows_sdk_domain(ScanSessionData& data)
{
    std::vector<WindowsSdk> cached ;
    for (const WindowsSdk& sdk : data.m_cache.m_windows_sdks)
    {
        if (engine::cached_windows_sdk_ok(sdk))
        {
            cached.push_back(sdk) ;
        }
    }
    if (!cached.empty())
    {
        if (!data.m_extra_roots.empty())
        {
            merge_windows_sdks(cached, engine::scan_windows_sdk_roots(data.m_extra_roots, session_depth,
                data.hooks(static_cast<std::size_t>(ScanDomain::WindowsSdk)))) ;
        }
        data.publish_windows_sdks(std::move(cached), true, false, {}) ;
        return ;
    }
    if (data.m_cancel.load())
    {
        data.publish_windows_sdks({}, false, true, "已取消") ;
        return ;
    }
    std::vector<WindowsSdk> sdks = engine::scan_windows_sdk_domain(session_depth,
        data.hooks(static_cast<std::size_t>(ScanDomain::WindowsSdk)), data.m_extra_roots) ;
    const bool cancelled = data.m_cancel.load() ;
    std::string message ;
    if (cancelled)
    {
        message = "已取消" ;
    }
    else if (sdks.empty())
    {
        message = "没有找到可用的 Windows SDK" ;
    }
    data.publish_windows_sdks(std::move(sdks), false, cancelled, std::move(message)) ;
}
} // namespace

bool ScanState::all_finished() const
{
    for (const DomainScanState& domain : m_domains)
    {
        if (!domain.m_finished)
        {
            return false ;
        }
    }
    return true ;
}

ScanSession::ScanSession(const ScanSessionOptions& options) : m_data(std::make_unique<ScanSessionData>())
{
    m_data->m_cache = load_cache() ;
    m_data->m_extra_roots = options.m_extra_roots ;
    try
    {
        m_data->m_threads[0] = std::thread([data = m_data.get()]()
        {
            try
            {
                run_compiler_domain(*data) ;
            }
            catch (...)
            {
                data->publish_compilers({}, false, false, "编译器扫描线程异常") ;
            }
        }) ;
        m_data->m_threads[1] = std::thread([data = m_data.get()]()
        {
            try
            {
                run_nx_sdk_domain(*data) ;
            }
            catch (...)
            {
                data->publish_sdks({}, false, false, "NX SDK 扫描线程异常") ;
            }
        }) ;
        m_data->m_threads[2] = std::thread([data = m_data.get()]()
        {
            try
            {
                run_windows_sdk_domain(*data) ;
            }
            catch (...)
            {
                data->publish_windows_sdks({}, false, false, "Windows SDK 扫描线程异常") ;
            }
        }) ;
    }
    catch (...)
    {
        m_data->m_cancel.store(true) ;
        m_data->join() ;
        throw ;
    }
}

ScanSession::~ScanSession()
{
    cancel() ;
    wait() ;
}

ScanState ScanSession::state() const
{
    return m_data->state() ;
}

void ScanSession::cancel()
{
    if (m_data != nullptr)
    {
        m_data->m_cancel.store(true) ;
    }
}

void ScanSession::wait()
{
    if (m_data != nullptr)
    {
        m_data->join() ;
    }
}

ScanReport ScanSession::results() const
{
    return m_data->report() ;
}

bool ScanSession::save() const
{
    return save_cache(results()) ;
}
} // namespace scan
