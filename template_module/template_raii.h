#pragma once
#include <string>

namespace template_raii
{
inline std::string concept_template = R"(
/** 能被 UFGuard 接管的句柄: UF 分配、要用 UF_free 释放的 tag 数组或字符串。 */
template <typename T>
concept GuardType = std::same_as<T , tag_t*> || std::same_as<T , char*> || std::same_as<T , const char*> ;

/** 能被 UFStringArrayGuard 接管的句柄:UF 分配、要用 UF_free_string_array 释放的字符串数组。 */
template <typename T>
concept StringArrayGuardType = std::same_as<T , char**> || std::same_as<T , const char**> ;

/** 能被 UFListGuard 接管的句柄:UF 分配、要用 UF_MODL_delete_list 释放的链表。 */
template <typename T>
concept ListGuardType = std::same_as<T , uf_list_p_t> ;
)";

inline std::string uf_guard = R"(
template <GuardType T>
class UFGuard
{
    public:
    /** 接管一个句柄；传空表示不持有资源。 */
    explicit UFGuard(T handle = nullptr) noexcept : m_handle(handle) {}

    /** 原地起 guard:把句柄地址交给 caller:由 caller 调 UF 函数把输出写进来。 */
    template <typename Caller>
        requires std::invocable<Caller, T*>
    explicit UFGuard(Caller&& caller) : m_handle(nullptr)
    {
        std::forward<Caller>(caller)(&m_handle) ;
    }

    /** 原地起 guard:调用 function(args..., &m_handle)，要求输出句柄是函数最后一个参数。 */
    template <typename Function, typename... Args>
        requires(sizeof...(Args) > 0) && std::invocable<Function, Args..., T*>
    explicit UFGuard(Function function, Args&&... args) : m_handle(nullptr)
    {
        function(std::forward<Args>(args)..., &m_handle) ;
    }

    /** 禁止拷贝：一块 UF 内存只能有一个主人。 */
    UFGuard(const UFGuard&) = delete ;

    /** 禁止拷贝赋值：一块 UF 内存只能有一个主人。 */
    UFGuard& operator=(const UFGuard&) = delete ;

    /** 转移所有权：接管 other 的句柄,other 置空。 */
    UFGuard(UFGuard&& other) noexcept : m_handle(other.release()) {}

    /** 转移所有权：先释放自己的旧句柄，再接管 other 的。 */
    UFGuard& operator=(UFGuard&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release()) ;
        }
        return *this ;
    }

    ~UFGuard()
    {
        reset() ;
    }

    /** 返回当前句柄，不转移所有权；可能为空。 */
    T get() const noexcept
    {
        return m_handle ;
    }

    /** 交出所有权：返回句柄并把自身置空。 */
    T release() noexcept
    {
        T handle = m_handle ;
        m_handle = nullptr ;
        return handle ;
    }

    /** 释放旧句柄并接管新句柄；传空等于只释放。 */
    void reset(T handle = nullptr) noexcept
    {
        free_handle(m_handle) ;
        m_handle = handle ;
    }

    /** 是否持有资源。 */
    explicit operator bool() const noexcept
    {
        return m_handle != nullptr ;
    }

    /** 和另一个 guard 交换句柄，两边都不释放。 */
    void swap(UFGuard& other) noexcept
    {
        T handle = m_handle ;
        m_handle = other.m_handle ;
        other.m_handle = handle ;
    }

    private:
    /** 用 UF_free 释放句柄: const 句柄先去掉 const。 */
    static void free_handle(T handle) noexcept
    {
        if (handle != nullptr)
        {
            UF_free(const_cast<void*>(static_cast<const void*>(handle))) ;
        }
    }

    T m_handle = nullptr ;  // 由 UF_free 负责释放的原始句柄
} ;
)";

inline std::string nxopen_guard = R"(
/** RAII 句柄：接管 UF 分配的字符串数组，析构或 reset 时用 UF_free_string_array 释放。 */
template <StringArrayGuardType T>
class UFStringArrayGuard
{
    public:
    /** 接管一个字符串数组和元素个数；传空表示不持有资源。 */
    explicit UFStringArrayGuard(T array = nullptr , int count = 0) noexcept : m_array(array) , m_count(count) {}

    /** 原地起 guard:把个数和数组地址交给 filler,由 filler 调 UF 函数把输出写进来。 */
    template <typename Filler>
        requires std::invocable<Filler , int* , T*>
    explicit UFStringArrayGuard(Filler&& filler) : m_array(nullptr) , m_count(0)
    {
        std::forward<Filler>(filler)(&m_count , &m_array) ;
    }

    /** 原地起 guard:调用 function(args..., &count, &array)，要求个数和数组是最后两个参数。 */
    template <typename Function, typename... Args>
        requires(sizeof...(Args) > 0) && std::invocable<Function , Args... , int* , T*>
    explicit UFStringArrayGuard(Function function , Args&&... args) : m_array(nullptr) , m_count(0)
    {
        function(std::forward<Args>(args)... , &m_count , &m_array) ;
    }

    /** 禁止拷贝：一块 UF 内存只能有一个主人。 */
    UFStringArrayGuard(const UFStringArrayGuard&) = delete ;

    /** 禁止拷贝赋值：一块 UF 内存只能有一个主人。 */
    UFStringArrayGuard& operator=(const UFStringArrayGuard&) = delete ;

    /** 转移所有权：连个数一起接管 other 的数组,other 清空。 */
    UFStringArrayGuard(UFStringArrayGuard&& other) noexcept : m_array(other.m_array) , m_count(other.m_count)
    {
        other.m_array = nullptr ;
        other.m_count = 0 ;
    }

    /** 转移所有权：先释放自己的旧数组，再连个数一起接管 other 的。 */
    UFStringArrayGuard& operator=(UFStringArrayGuard&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.m_array , other.m_count) ;
            other.m_array = nullptr ;
            other.m_count = 0 ;
        }
        return *this ;
    }

    ~UFStringArrayGuard()
    {
        reset() ;
    }

    /** 返回当前数组，不转移所有权；可能为空。 */
    T get() const noexcept
    {
        return m_array ;
    }

    /** 返回数组元素个数；没有资源时为 0。 */
    int count() const noexcept
    {
        return m_count ;
    }

    /** 交出所有权：返回 (数组, 个数) 并把自身清空。 */
    std::pair<T , int> release() noexcept
    {
        std::pair<T , int> taken { m_array , m_count } ;
        m_array = nullptr ;
        m_count = 0 ;
        return taken ;
    }

    /** 释放旧数组并接管新数组；传空等于只释放。 */
    void reset(T array = nullptr , int count = 0) noexcept
    {
        free_handle(m_array , m_count) ;
        m_array = array ;
        m_count = count ;
    }

    /** 是否持有资源。 */
    explicit operator bool() const noexcept
    {
        return m_array != nullptr ;
    }

    /** 和另一个 guard 交换数组与个数，两边都不释放。 */
    void swap(UFStringArrayGuard& other) noexcept
    {
        T array = m_array ;
        m_array = other.m_array ;
        other.m_array = array ;
        int count = m_count ;
        m_count = other.m_count ;
        other.m_count = count ;
    }

    private:
    /** 用 UF_free_string_array 释放整个数组；个数必须回传，否则只放出数组块。 */
    static void free_handle(T array , int count) noexcept
    {
        if (array != nullptr)
        {
            UF_free_string_array(count , mutable_string_array(array)) ;
        }
    }

    /** 把只读的字符串数组转成 UF_free_string_array 要求的 char**。 */
    static char** mutable_string_array(T array) noexcept
    {
        if constexpr (std::same_as<T , char**>)
        {
            return array ;
        }else
        {
            return reinterpret_cast<char**>(const_cast<void*>(static_cast<const void*>(array))) ;
        }
    }

    T m_array = nullptr ;  // 由 UF_free_string_array 负责释放的字符串数组
    int m_count = 0 ;      // 数组元素个数，释放时必须回传
} ;
)";

inline std::string list_guard = R"(
/** RAII 句柄:接管 UF 分配的链表,析构或 reset 时用 UF_MODL_delete_list 释放。 */
template <ListGuardType T>
class UFListGuard
{
    public:
    /** 接管一个链表;传空表示不持有资源。 */
    explicit UFListGuard(T handle = nullptr) noexcept : m_handle(handle) {}

    /** 原地起 guard:把句柄地址交给 caller,由 caller 调 UF 函数把输出写进来。 */
    template <typename Caller>
        requires std::invocable<Caller , T*>
    explicit UFListGuard(Caller&& caller) : m_handle(nullptr)
    {
        std::forward<Caller>(caller)(&m_handle) ;
    }

    /** 原地起 guard:调用 function(args..., &m_handle),要求输出句柄是函数最后一个参数。 */
    template <typename Function, typename... Args>
        requires(sizeof...(Args) > 0) && std::invocable<Function , Args... , T*>
    explicit UFListGuard(Function function , Args&&... args) : m_handle(nullptr)
    {
        function(std::forward<Args>(args)... , &m_handle) ;
    }

    /** 禁止拷贝:一条 UF 链表只能有一个主人。 */
    UFListGuard(const UFListGuard&) = delete ;

    /** 禁止拷贝赋值:一条 UF 链表只能有一个主人。 */
    UFListGuard& operator=(const UFListGuard&) = delete ;

    /** 转移所有权:接管 other 的链表,other 置空。 */
    UFListGuard(UFListGuard&& other) noexcept : m_handle(other.release()) {}

    /** 转移所有权:先释放自己的旧链表,再接管 other 的。 */
    UFListGuard& operator=(UFListGuard&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release()) ;
        }
        return *this ;
    }

    ~UFListGuard()
    {
        reset() ;
    }

    /** 返回当前链表,不转移所有权;可能为空。 */
    T get() const noexcept
    {
        return m_handle ;
    }

    /** 交出所有权:返回链表并把自身置空。 */
    T release() noexcept
    {
        T handle = m_handle ;
        m_handle = nullptr ;
        return handle ;
    }

    /** 释放旧链表并接管新链表;传空等于只释放。 */
    void reset(T handle = nullptr) noexcept
    {
        free_handle() ;
        m_handle = handle ;
    }

    /** 是否持有资源。 */
    explicit operator bool() const noexcept
    {
        return m_handle != nullptr ;
    }

    /** 和另一个 guard 交换链表,两边都不释放。 */
    void swap(UFListGuard& other) noexcept
    {
        T handle = m_handle ;
        m_handle = other.m_handle ;
        other.m_handle = handle ;
    }

    private:
    /** 用 UF_MODL_delete_list 释放链表;它接收句柄地址,成功时顺手把成员置空。 */
    void free_handle() noexcept
    {
        if (m_handle != nullptr)
        {
            UF_MODL_delete_list(&m_handle) ;
        }
    }

    T m_handle = nullptr ;  // 由 UF_MODL_delete_list 负责释放的链表句柄
} ;
)";
} // namespace template_raii