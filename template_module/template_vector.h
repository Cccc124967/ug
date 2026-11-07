#pragma once
#include <string>

namespace template_math
{
// 目标工程文件 vector.hpp：内容与 backup/templates/vector.hpp.txt 逐字一致（行尾统一为 LF）。
inline std::string vector_hpp = R"(#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>
#include <NXOpen/ugmath.hxx>
#include "basic_type_defs.h"

/* <windows.h> 默认把 min / max 定义成函数式宏, 会把下面的成员名 min / max 打碎。
 * 这里先摘掉, 文件末尾原样恢复 —— 模板自身不受调用方宏状态影响, 也不改变调用方。 */
#ifdef min
    #pragma push_macro("min")
    #undef min
    #define NX_VECTOR_PUSHED_MIN 1
#endif
#ifdef max
    #pragma push_macro("max")
    #undef max
    #define NX_VECTOR_PUSHED_MAX 1
#endif

NX_NAMESPACE_BEGIN

/* 向量 / 点 / 四元数共用的一组标量工具 (固定 double, 与 matrix.hpp 同口径)。
 * 源库 (saya_math) 是 float/int 泛型的图形侧实现; NX 侧 API 全是 double,
 * 所以这里去掉模板、去掉 SIMD 与 GUI 专用分支, 只留 NX 用得上的那部分。 */

/** 近似相等的绝对容差 (与 matrix.hpp 的 MatrixEps 同口径)。 */
constexpr double MathEps = 1e-7 ;

/** 绝对值; NaN / Inf 原样传播, 调用方要先用 math_is_finite 拦一道。 */
constexpr double math_abs(double value) noexcept
{
    return value > 0.0 ? value : -value ;
}

/** 有限性判定; 不用 std::isfinite 是因为它到 C++23 才 constexpr (inf/NaN 时 v - v 是 NaN)。 */
constexpr bool math_is_finite(double value) noexcept
{
    return (value - value) == 0.0 ;
}

/** 逐分量近似相等; 精确比较用 operator==, 几何容差用这个。 */
constexpr bool math_near(double first , double second) noexcept
{
    return math_abs(first - second) < MathEps ;
}

/** 常量求值友好的开方: 常量求值时走牛顿迭代, 运行期直接用 std::sqrt。 */
constexpr double math_sqrt(double value) noexcept
{
    if (std::is_constant_evaluated())
    {
        if (value != value) return value ;                      // NaN 防护: 否则牛顿迭代死循环
        if (value < 0.0) return value ;                         // 负数也就是 NaN
        if (value == 0.0) return 0.0 ;
        double current = value ;
        double previous = 0.0 ;
        for (int guard = 0 ; current != previous && guard < 128 ; ++guard)
        {
            previous = current ;
            current = 0.5 * (current + value / current) ;
        }
        return current ;
    }
    return std::sqrt(value) ;
}

/** 二维向量; x / y 连续存放, 可直接喂 double[2] 的 NX 接口。 */
struct Vec2
{
    double x , y ;

    /** 默认构造: 全零 (不是 NaN 哨兵)。 */
    constexpr Vec2() noexcept : x(0.0) , y(0.0) {}

    /** 按分量构造。 */
    constexpr Vec2(double x_value , double y_value) noexcept : x(x_value) , y(y_value) {}

    /** 四个标量广播。 */
    static constexpr Vec2 splat(double value) noexcept
    {
        return Vec2(value , value) ;
    }

    /** 零向量 (具名工厂, 让调用点读出意图)。 */
    static constexpr Vec2 zero() noexcept
    {
        return Vec2(0.0 , 0.0) ;
    }

    /** 全 1 向量。 */
    static constexpr Vec2 one() noexcept
    {
        return Vec2(1.0 , 1.0) ;
    }

    /** X 轴单位向量。 */
    static constexpr Vec2 unit_x() noexcept
    {
        return Vec2(1.0 , 0.0) ;
    }

    /** Y 轴单位向量。 */
    static constexpr Vec2 unit_y() noexcept
    {
        return Vec2(0.0 , 1.0) ;
    }

    /** 精确相等 (不做容差)。 */
    constexpr bool operator==(const Vec2& other) const noexcept
    {
        return x == other.x && y == other.y ;
    }

    /** 字典序比较 (供排序容器用; 含 NaN 时不是严格弱序)。 */
    friend constexpr bool operator<(const Vec2& first , const Vec2& second) noexcept
    {
        if (first.x < second.x) return true ;
        if (second.x < first.x) return false ;
        return first.y < second.y ;
    }

    /** 逐分量加。 */
    constexpr Vec2& operator+=(const Vec2& other) noexcept
    {
        x += other.x ;
        y += other.y ;
        return *this ;
    }

    /** 逐分量减。 */
    constexpr Vec2& operator-=(const Vec2& other) noexcept
    {
        x -= other.x ;
        y -= other.y ;
        return *this ;
    }

    /** 标量乘。 */
    constexpr Vec2& operator*=(double scalar) noexcept
    {
        x *= scalar ;
        y *= scalar ;
        return *this ;
    }

    /** 标量除。 */
    constexpr Vec2& operator/=(double scalar) noexcept
    {
        x /= scalar ;
        y /= scalar ;
        return *this ;
    }

    /** 逐分量乘 (缩放 / 遮罩)。 */
    constexpr Vec2& operator*=(const Vec2& other) noexcept
    {
        x *= other.x ;
        y *= other.y ;
        return *this ;
    }

    /** 逐分量除。 */
    constexpr Vec2& operator/=(const Vec2& other) noexcept
    {
        x /= other.x ;
        y /= other.y ;
        return *this ;
    }

    /** 逐分量加。 */
    constexpr Vec2 operator+(const Vec2& other) const noexcept
    {
        return Vec2(*this) += other ;
    }

    /** 逐分量减。 */
    constexpr Vec2 operator-(const Vec2& other) const noexcept
    {
        return Vec2(*this) -= other ;
    }

    /** 标量乘。 */
    constexpr Vec2 operator*(double scalar) const noexcept
    {
        return Vec2(*this) *= scalar ;
    }

    /** 标量除。 */
    constexpr Vec2 operator/(double scalar) const noexcept
    {
        return Vec2(*this) /= scalar ;
    }

    /** 逐分量乘。 */
    constexpr Vec2 operator*(const Vec2& other) const noexcept
    {
        return Vec2(*this) *= other ;
    }

    /** 逐分量除。 */
    constexpr Vec2 operator/(const Vec2& other) const noexcept
    {
        return Vec2(*this) /= other ;
    }

    /** 标量在左的乘法。 */
    friend constexpr Vec2 operator*(double scalar , const Vec2& vector) noexcept
    {
        return vector * scalar ;
    }

    /** 取负。 */
    constexpr Vec2 operator-() const noexcept
    {
        return Vec2(-x , -y) ;
    }

    /** 点积。 */
    constexpr double dot(const Vec2& other) const noexcept
    {
        return x * other.x + y * other.y ;
    }

    /** 静态点积 (写表达式里更顺手)。 */
    static constexpr double dot(const Vec2& first , const Vec2& second) noexcept
    {
        return first.dot(second) ;
    }

    /** 二维叉积 (标量): 用于判断左右侧 / 平行四边形面积。 */
    constexpr double cross(const Vec2& other) const noexcept
    {
        return x * other.y - y * other.x ;
    }

    /** 静态二维叉积。 */
    static constexpr double cross(const Vec2& first , const Vec2& second) noexcept
    {
        return first.cross(second) ;
    }

    /** |点积|。 */
    constexpr double abs_dot(const Vec2& other) const noexcept
    {
        return math_abs(dot(other)) ;
    }

    /** 长度平方 (比较远近时省一次开方)。 */
    constexpr double length_sq() const noexcept
    {
        return dot(*this) ;
    }

    /** 长度。 */
    constexpr double length() const noexcept
    {
        return math_sqrt(length_sq()) ;
    }

    /** 距离平方。 */
    constexpr double distance_sq(const Vec2& other) const noexcept
    {
        return (*this - other).length_sq() ;
    }

    /** 距离。 */
    constexpr double distance(const Vec2& other) const noexcept
    {
        return (*this - other).length() ;
    }

    /** 归一化; 零向量仍是零向量 (判据是 len > 0, 不是跟 eps 比)。 */
    constexpr Vec2 normalized() const noexcept
    {
        const double len = length() ;
        return (len > 0.0) ? Vec2(*this) / len : Vec2() ;
    }

    /** 线性插值 a*(1-t) + b*t; t=1 时精确等于 b。 */
    constexpr Vec2 lerp(const Vec2& other , double t) const noexcept
    {
        return Vec2(*this) * (1.0 - t) + other * t ;
    }

    /** 逐分量插值 (每个轴各插各的)。 */
    constexpr Vec2 lerp(const Vec2& other , const Vec2& t) const noexcept
    {
        return Vec2(*this) * (one() - t) + other * t ;
    }

    /** 逆时针 90 度 (二维法线 / 描边偏移)。 */
    constexpr Vec2 perp() const noexcept
    {
        return Vec2(-y , x) ;
    }

    /** 极角 atan2(y, x), 弧度。 */
    inline double angle() const noexcept
    {
        return std::atan2(y , x) ;
    }

    /** 由极角构造单位向量。 */
    static inline Vec2 from_angle(double radians) noexcept
    {
        return Vec2(std::cos(radians) , std::sin(radians)) ;
    }

    /** 绕原点逆时针旋转; 内部只调 cos/sin 各一次。 */
    inline Vec2 rotated(double radians) const noexcept
    {
        const double cosine = std::cos(radians) ;
        const double sine = std::sin(radians) ;
        return Vec2(x * cosine - y * sine , x * sine + y * cosine) ;
    }

    /** 与 other 的夹角余弦; 按最大分量预归一化, 规避长度平方上下溢。 */
    constexpr double cos_angle(const Vec2& other) const noexcept
    {
        const double scale_first = max_value(math_abs(x) , math_abs(y)) ;
        const double scale_second = max_value(math_abs(other.x) , math_abs(other.y)) ;
        if (scale_first == 0.0 || scale_second == 0.0) return std::numeric_limits<double>::quiet_NaN() ;
        const Vec2 scaled_first = *this / scale_first ;
        const Vec2 scaled_second = other / scale_second ;
        const double cosine = scaled_first.dot(scaled_second) / (scaled_first.length() * scaled_second.length()) ;
        return clamp_value(cosine , -1.0 , 1.0) ;
    }

    /** 夹角 (弧度); 零向量口径继承 cos_angle。 */
    inline double angle_between(const Vec2& other) const noexcept
    {
        return std::acos(cos_angle(other)) ;
    }

    /** 是否"基本是零" (|v| <= eps); eps 必填, 免得魔法容差吃掉合法小值。 */
    constexpr bool nearly_zero(double eps) const noexcept
    {
        return length_sq() <= eps * eps ;
    }

    /** 是否是单位向量 (|len - 1| <= eps)。 */
    constexpr bool is_unit(double eps) const noexcept
    {
        return math_abs(length() - 1.0) <= eps ;
    }

    /** 逐分量近似相等。 */
    constexpr bool nearly_equal(const Vec2& other) const noexcept
    {
        return math_near(x , other.x) && math_near(y , other.y) ;
    }

    /** 是否精确等于零向量。 */
    constexpr bool is_zero() const noexcept
    {
        return x == 0.0 && y == 0.0 ;
    }

    /** 两个分量是否都有限。 */
    constexpr bool is_finite() const noexcept
    {
        return math_is_finite(x) && math_is_finite(y) ;
    }

    /** 是否含 NaN。 */
    constexpr bool is_nan() const noexcept
    {
        return x != x || y != y ;
    }

    /** 逐分量较小值 (标量参数)。 */
    constexpr Vec2 component_min(double scalar) const noexcept
    {
        return component_min(splat(scalar)) ;
    }

    /** 逐分量较大值 (标量参数)。 */
    constexpr Vec2 component_max(double scalar) const noexcept
    {
        return component_max(splat(scalar)) ;
    }

    /** 逐分量夹取 (标量参数)。 */
    constexpr Vec2 clamp(double lower , double upper) const noexcept
    {
        return clamp(splat(lower) , splat(upper)) ;
    }

    /** 夹到 [0, 1]。 */
    constexpr Vec2 clamp01() const noexcept
    {
        return clamp(0.0 , 1.0) ;
    }

    /** 逐分量较小值。 */
    constexpr Vec2 component_min(const Vec2& other) const noexcept
    {
        return Vec2(min_value(x , other.x) , min_value(y , other.y)) ;
    }

    /** 逐分量较大值。 */
    constexpr Vec2 component_max(const Vec2& other) const noexcept
    {
        return Vec2(max_value(x , other.x) , max_value(y , other.y)) ;
    }

    /** 逐分量夹取。 */
    constexpr Vec2 clamp(const Vec2& lower , const Vec2& upper) const noexcept
    {
        return Vec2(clamp_value(x , lower.x , upper.x) , clamp_value(y , lower.y , upper.y)) ;
    }

    /** 逐分量绝对值。 */
    constexpr Vec2 abs() const noexcept
    {
        return Vec2(math_abs(x) , math_abs(y)) ;
    }

    /** 最小分量。 */
    constexpr double min_component() const noexcept
    {
        return min_value(x , y) ;
    }

    /** 最大分量。 */
    constexpr double max_component() const noexcept
    {
        return max_value(x , y) ;
    }

    /** 分量和。 */
    constexpr double sum() const noexcept
    {
        return x + y ;
    }

    /** 分量平均。 */
    constexpr double average() const noexcept
    {
        return (x + y) / 2.0 ;
    }

    /** 泛型下标访问 (依赖成员连续无 padding)。 */
    constexpr double& operator[](std::size_t index) noexcept
    {
        return (&x)[index] ;
    }

    /** 泛型下标访问 (只读)。 */
    constexpr const double& operator[](std::size_t index) const noexcept
    {
        return (&x)[index] ;
    }

    /** 原始数组, 可交给 double[2] 的 NX 接口或 IO。 */
    constexpr double* data() noexcept
    {
        return &x ;
    }

    /** 原始数组 (只读)。 */
    constexpr const double* data() const noexcept
    {
        return &x ;
    }

    /** 分量个数。 */
    static constexpr std::size_t size() noexcept
    {
        return 2 ;
    }

    /** range-for 起点。 */
    constexpr double* begin() noexcept
    {
        return data() ;
    }

    /** range-for 终点。 */
    constexpr double* end() noexcept
    {
        return data() + size() ;
    }

    /** 从裸数组构造。 */
    static constexpr Vec2 from_array(const double* values) noexcept
    {
        return Vec2(values[0] , values[1]) ;
    }

    /** 转成 std::array。 */
    constexpr std::array<double , 2> to_array() const noexcept
    {
        return { x , y } ;
    }

    /** 接管 NX 的二维点。 */
    static Vec2 from_nx(const NXOpen::Point2d& source) noexcept
    {
        return Vec2(source.X , source.Y) ;
    }

    /** 转成 NX 的二维点。 */
    NXOpen::Point2d to_nx() const noexcept
    {
        return NXOpen::Point2d(x , y) ;
    }

    /** 和另一向量交换分量。 */
    constexpr void swap(Vec2& other) noexcept
    {
        const double first = x ;
        x = other.x ;
        other.x = first ;
        const double second = y ;
        y = other.y ;
        other.y = second ;
    }

    private:
    /** 逐分量较小值; 不用 std::min 以免引入 <algorithm>。 */
    static constexpr double min_value(double first , double second) noexcept
    {
        return (second < first) ? second : first ;
    }

    /** 逐分量较大值。 */
    static constexpr double max_value(double first , double second) noexcept
    {
        return (first < second) ? second : first ;
    }

    /** 夹取到 [lower, upper]。 */
    static constexpr double clamp_value(double value , double lower , double upper) noexcept
    {
        return min_value(max_value(value , lower) , upper) ;
    }
} ;

/** 交换两个向量 (ADL 版本, 供 std::swap 泛化使用)。 */
constexpr void swap(Vec2& first , Vec2& second) noexcept
{
    first.swap(second) ;
}

static_assert(sizeof(Vec2) == 2 * sizeof(double) , "Vec2 不能有 padding") ;
static_assert(alignof(Vec2) == alignof(double) , "Vec2 要与 double 同对齐") ;

/** 三维向量; 与 NXOpen::Vector3d / Point3d 的内存布局一致。 */
struct Vec3
{
    double x , y , z ;

    /** 默认构造: 全零。 */
    constexpr Vec3() noexcept : x(0.0) , y(0.0) , z(0.0) {}

    /** 按分量构造。 */
    constexpr Vec3(double x_value , double y_value , double z_value) noexcept
        : x(x_value) , y(y_value) , z(z_value) {}

    /** 由二维向量补 z 构造。 */
    constexpr Vec3(const Vec2& planar , double z_value) noexcept : x(planar.x) , y(planar.y) , z(z_value) {}

    /** 三个标量广播。 */
    static constexpr Vec3 splat(double value) noexcept
    {
        return Vec3(value , value , value) ;
    }

    /** 零向量。 */
    static constexpr Vec3 zero() noexcept
    {
        return Vec3(0.0 , 0.0 , 0.0) ;
    }

    /** 全 1 向量。 */
    static constexpr Vec3 one() noexcept
    {
        return Vec3(1.0 , 1.0 , 1.0) ;
    }

    /** X 轴单位向量。 */
    static constexpr Vec3 unit_x() noexcept
    {
        return Vec3(1.0 , 0.0 , 0.0) ;
    }

    /** Y 轴单位向量。 */
    static constexpr Vec3 unit_y() noexcept
    {
        return Vec3(0.0 , 1.0 , 0.0) ;
    }

    /** Z 轴单位向量。 */
    static constexpr Vec3 unit_z() noexcept
    {
        return Vec3(0.0 , 0.0 , 1.0) ;
    }

    /** 精确相等 (不做容差)。 */
    constexpr bool operator==(const Vec3& other) const noexcept
    {
        return x == other.x && y == other.y && z == other.z ;
    }

    /** 逐分量加。 */
    constexpr Vec3& operator+=(const Vec3& other) noexcept
    {
        x += other.x ;
        y += other.y ;
        z += other.z ;
        return *this ;
    }

    /** 逐分量减。 */
    constexpr Vec3& operator-=(const Vec3& other) noexcept
    {
        x -= other.x ;
        y -= other.y ;
        z -= other.z ;
        return *this ;
    }

    /** 标量乘。 */
    constexpr Vec3& operator*=(double scalar) noexcept
    {
        x *= scalar ;
        y *= scalar ;
        z *= scalar ;
        return *this ;
    }

    /** 标量除。 */
    constexpr Vec3& operator/=(double scalar) noexcept
    {
        x /= scalar ;
        y /= scalar ;
        z /= scalar ;
        return *this ;
    }

    /** 逐分量乘。 */
    constexpr Vec3& operator*=(const Vec3& other) noexcept
    {
        x *= other.x ;
        y *= other.y ;
        z *= other.z ;
        return *this ;
    }

    /** 逐分量除。 */
    constexpr Vec3& operator/=(const Vec3& other) noexcept
    {
        x /= other.x ;
        y /= other.y ;
        z /= other.z ;
        return *this ;
    }

    /** 逐分量加。 */
    constexpr Vec3 operator+(const Vec3& other) const noexcept
    {
        return Vec3(*this) += other ;
    }

    /** 逐分量减。 */
    constexpr Vec3 operator-(const Vec3& other) const noexcept
    {
        return Vec3(*this) -= other ;
    }

    /** 标量乘。 */
    constexpr Vec3 operator*(double scalar) const noexcept
    {
        return Vec3(*this) *= scalar ;
    }

    /** 标量除。 */
    constexpr Vec3 operator/(double scalar) const noexcept
    {
        return Vec3(*this) /= scalar ;
    }

    /** 逐分量乘。 */
    constexpr Vec3 operator*(const Vec3& other) const noexcept
    {
        return Vec3(*this) *= other ;
    }

    /** 逐分量除。 */
    constexpr Vec3 operator/(const Vec3& other) const noexcept
    {
        return Vec3(*this) /= other ;
    }

    /** 标量在左的乘法。 */
    friend constexpr Vec3 operator*(double scalar , const Vec3& vector) noexcept
    {
        return vector * scalar ;
    }

    /** 取负。 */
    constexpr Vec3 operator-() const noexcept
    {
        return Vec3(-x , -y , -z) ;
    }

    /** 点积。 */
    constexpr double dot(const Vec3& other) const noexcept
    {
        return x * other.x + y * other.y + z * other.z ;
    }

    /** 静态点积。 */
    static constexpr double dot(const Vec3& first , const Vec3& second) noexcept
    {
        return first.dot(second) ;
    }

    /** 叉积 (右手系)。 */
    constexpr Vec3 cross(const Vec3& other) const noexcept
    {
        return Vec3(y * other.z - z * other.y ,
                    z * other.x - x * other.z ,
                    x * other.y - y * other.x) ;
    }

    /** 静态叉积。 */
    static constexpr Vec3 cross(const Vec3& first , const Vec3& second) noexcept
    {
        return first.cross(second) ;
    }

    /** |点积|。 */
    constexpr double abs_dot(const Vec3& other) const noexcept
    {
        return math_abs(dot(other)) ;
    }

    /** 长度平方。 */
    constexpr double length_sq() const noexcept
    {
        return dot(*this) ;
    }

    /** 长度。 */
    constexpr double length() const noexcept
    {
        return math_sqrt(length_sq()) ;
    }

    /** 距离平方。 */
    constexpr double distance_sq(const Vec3& other) const noexcept
    {
        return (*this - other).length_sq() ;
    }

    /** 距离。 */
    constexpr double distance(const Vec3& other) const noexcept
    {
        return (*this - other).length() ;
    }

    /** 归一化; 零向量仍是零向量。 */
    constexpr Vec3 normalized() const noexcept
    {
        const double len = length() ;
        return (len > 0.0) ? Vec3(*this) / len : Vec3() ;
    }

    /** 线性插值。 */
    constexpr Vec3 lerp(const Vec3& other , double t) const noexcept
    {
        return Vec3(*this) * (1.0 - t) + other * t ;
    }

    /** 逐分量插值。 */
    constexpr Vec3 lerp(const Vec3& other , const Vec3& t) const noexcept
    {
        return Vec3(*this) * (one() - t) + other * t ;
    }

    /** 与 other 的夹角余弦; 按最大分量预归一化。 */
    constexpr double cos_angle(const Vec3& other) const noexcept
    {
        const double scale_first = max_component_of(*this) ;
        const double scale_second = max_component_of(other) ;
        if (scale_first == 0.0 || scale_second == 0.0) return std::numeric_limits<double>::quiet_NaN() ;
        const Vec3 scaled_first = *this / scale_first ;
        const Vec3 scaled_second = other / scale_second ;
        const double cosine = scaled_first.dot(scaled_second) / (scaled_first.length() * scaled_second.length()) ;
        return clamp_value(cosine , -1.0 , 1.0) ;
    }

    /** 夹角 (弧度)。 */
    inline double angle_between(const Vec3& other) const noexcept
    {
        return std::acos(cos_angle(other)) ;
    }

    /** 关于单位法线 normal 的反射 (normal 必须已归一化)。 */
    constexpr Vec3 reflect(const Vec3& normal) const noexcept
    {
        return Vec3(*this) - normal * (2.0 * dot(normal)) ;
    }

    /** 折射; 全内反射时返回零向量 (入射向量与法线都必须已归一化)。 */
    constexpr Vec3 refract(const Vec3& normal , double eta) const noexcept
    {
        const double cosine = dot(normal) ;
        const double k = 1.0 - eta * eta * (1.0 - cosine * cosine) ;
        if (k < 0.0) return Vec3() ;
        return Vec3(*this) * eta - normal * (eta * cosine + math_sqrt(k)) ;
    }

    /** 把法线摆到与入射方向相对的一侧 (双面材质用)。 */
    constexpr Vec3 faceforward(const Vec3& incident) const noexcept
    {
        return (dot(incident) < 0.0) ? Vec3(*this) : -Vec3(*this) ;
    }

    /** 投影到 normal 上 (normal 不必归一化, 除的是 |normal|^2)。 */
    constexpr Vec3 project(const Vec3& normal) const noexcept
    {
        const double denominator = normal.length_sq() ;
        return (denominator > 0.0) ? normal * (dot(normal) / denominator) : Vec3() ;
    }

    /** 垂直分量 = 自身 - 投影。 */
    constexpr Vec3 reject(const Vec3& normal) const noexcept
    {
        return Vec3(*this) - project(normal) ;
    }

    /** 任取一个与自身正交的单位向量 (挑最不平行的轴去叉乘, 避免极点退化)。 */
    constexpr Vec3 any_orthogonal() const noexcept
    {
        const double magnitude_x = math_abs(x) ;
        const double magnitude_y = math_abs(y) ;
        const double magnitude_z = math_abs(z) ;
        const Vec3 axis = (magnitude_x <= magnitude_y && magnitude_x <= magnitude_z) ? Vec3::unit_x()
                        : (magnitude_y <= magnitude_z)                                ? Vec3::unit_y()
                                                                                      : Vec3::unit_z() ;
        return cross(axis).normalized() ;
    }

    /** 由单位法线造一组正交基 (t, b); Duff 2017 写法, |z|→1 也不退化。 */
    constexpr std::pair<Vec3 , Vec3> orthonormal_basis() const noexcept
    {
        const double sign = (z >= 0.0) ? 1.0 : -1.0 ;
        const double reciprocal = -1.0 / (sign + z) ;
        const double mixed = x * y * reciprocal ;
        const Vec3 tangent(1.0 + sign * x * x * reciprocal , sign * mixed , -sign * x) ;
        const Vec3 bitangent(mixed , sign + y * y * reciprocal , -y) ;
        return { tangent , bitangent } ;
    }

    /** 绕单位轴旋转 (Rodrigues); axis 必须已归一化。 */
    inline Vec3 rotate_axis(const Vec3& axis , double radians) const noexcept
    {
        const double cosine = std::cos(radians) ;
        const double sine = std::sin(radians) ;
        return Vec3(*this) * cosine + axis.cross(*this) * sine + axis * (axis.dot(*this) * (1.0 - cosine)) ;
    }

    /** 是否"基本是零" (|v| <= eps)。 */
    constexpr bool nearly_zero(double eps) const noexcept
    {
        return length_sq() <= eps * eps ;
    }

    /** 是否是单位向量 (|len - 1| <= eps)。 */
    constexpr bool is_unit(double eps) const noexcept
    {
        return math_abs(length() - 1.0) <= eps ;
    }

    /** 逐分量近似相等。 */
    constexpr bool nearly_equal(const Vec3& other) const noexcept
    {
        return math_near(x , other.x) && math_near(y , other.y) && math_near(z , other.z) ;
    }

    /** 是否精确等于零向量。 */
    constexpr bool is_zero() const noexcept
    {
        return x == 0.0 && y == 0.0 && z == 0.0 ;
    }

    /** 三个分量是否都有限。 */
    constexpr bool is_finite() const noexcept
    {
        return math_is_finite(x) && math_is_finite(y) && math_is_finite(z) ;
    }

    /** 是否含 NaN。 */
    constexpr bool is_nan() const noexcept
    {
        return x != x || y != y || z != z ;
    }

    /** 逐分量较小值 (标量参数)。 */
    constexpr Vec3 component_min(double scalar) const noexcept
    {
        return component_min(splat(scalar)) ;
    }

    /** 逐分量较大值 (标量参数)。 */
    constexpr Vec3 component_max(double scalar) const noexcept
    {
        return component_max(splat(scalar)) ;
    }

    /** 逐分量较小值。 */
    constexpr Vec3 component_min(const Vec3& other) const noexcept
    {
        return Vec3(min_value(x , other.x) , min_value(y , other.y) , min_value(z , other.z)) ;
    }

    /** 逐分量较大值。 */
    constexpr Vec3 component_max(const Vec3& other) const noexcept
    {
        return Vec3(max_value(x , other.x) , max_value(y , other.y) , max_value(z , other.z)) ;
    }

    /** 逐分量夹取 (标量参数)。 */
    constexpr Vec3 clamp(double lower , double upper) const noexcept
    {
        return clamp(splat(lower) , splat(upper)) ;
    }

    /** 夹到 [0, 1]。 */
    constexpr Vec3 clamp01() const noexcept
    {
        return clamp(0.0 , 1.0) ;
    }

    /** 逐分量夹取。 */
    constexpr Vec3 clamp(const Vec3& lower , const Vec3& upper) const noexcept
    {
        return Vec3(clamp_value(x , lower.x , upper.x) ,
                    clamp_value(y , lower.y , upper.y) ,
                    clamp_value(z , lower.z , upper.z)) ;
    }

    /** 逐分量绝对值。 */
    constexpr Vec3 abs() const noexcept
    {
        return Vec3(math_abs(x) , math_abs(y) , math_abs(z)) ;
    }

    /** 最小分量。 */
    constexpr double min_component() const noexcept
    {
        return min_value(min_value(x , y) , z) ;
    }

    /** 最大分量。 */
    constexpr double max_component() const noexcept
    {
        return max_value(max_value(x , y) , z) ;
    }

    /** 分量和。 */
    constexpr double sum() const noexcept
    {
        return x + y + z ;
    }

    /** 分量平均。 */
    constexpr double average() const noexcept
    {
        return (x + y + z) / 3.0 ;
    }

    /** 降维到二维 (丢掉 z)。 */
    constexpr Vec2 xy() const noexcept
    {
        return Vec2(x , y) ;
    }

    /** 泛型下标访问 (依赖成员连续无 padding)。 */
    constexpr double& operator[](std::size_t index) noexcept
    {
        return (&x)[index] ;
    }

    /** 泛型下标访问 (只读)。 */
    constexpr const double& operator[](std::size_t index) const noexcept
    {
        return (&x)[index] ;
    }

    /** 原始数组; 只能逐分量读写, 不要当 128-bit 载入用 (只有 24 字节)。 */
    constexpr double* data() noexcept
    {
        return &x ;
    }

    /** 原始数组 (只读)。 */
    constexpr const double* data() const noexcept
    {
        return &x ;
    }

    /** 分量个数。 */
    static constexpr std::size_t size() noexcept
    {
        return 3 ;
    }

    /** range-for 起点。 */
    constexpr double* begin() noexcept
    {
        return data() ;
    }

    /** range-for 终点。 */
    constexpr double* end() noexcept
    {
        return data() + size() ;
    }

    /** 从裸数组构造。 */
    static constexpr Vec3 from_array(const double* values) noexcept
    {
        return Vec3(values[0] , values[1] , values[2]) ;
    }

    /** 转成 std::array。 */
    constexpr std::array<double , 3> to_array() const noexcept
    {
        return { x , y , z } ;
    }

    /** 接管 NX 的 Vector3d。 */
    static Vec3 from_nx(const NXOpen::Vector3d& source) noexcept
    {
        return Vec3(source.X , source.Y , source.Z) ;
    }

    /** 接管 NX 的 Point3d (当向量用)。 */
    static Vec3 from_nx(const NXOpen::Point3d& source) noexcept
    {
        return Vec3(source.X , source.Y , source.Z) ;
    }

    /** 转成 NX 的 Vector3d。 */
    NXOpen::Vector3d to_nx_vector() const noexcept
    {
        return NXOpen::Vector3d(x , y , z) ;
    }

    /** 转成 NX 的 Point3d。 */
    NXOpen::Point3d to_nx_point() const noexcept
    {
        return NXOpen::Point3d(x , y , z) ;
    }

    /** 和另一向量交换分量。 */
    constexpr void swap(Vec3& other) noexcept
    {
        Vec3 temporary = *this ;
        *this = other ;
        other = temporary ;
    }

    private:
    /** 取绝对值最大的分量 (cos_angle 的预归一化尺度)。 */
    static constexpr double max_component_of(const Vec3& vector) noexcept
    {
        return max_value(max_value(math_abs(vector.x) , math_abs(vector.y)) , math_abs(vector.z)) ;
    }

    /** 逐分量较小值。 */
    static constexpr double min_value(double first , double second) noexcept
    {
        return (second < first) ? second : first ;
    }

    /** 逐分量较大值。 */
    static constexpr double max_value(double first , double second) noexcept
    {
        return (first < second) ? second : first ;
    }

    /** 夹取到 [lower, upper]。 */
    static constexpr double clamp_value(double value , double lower , double upper) noexcept
    {
        return min_value(max_value(value , lower) , upper) ;
    }
} ;

/** 交换两个向量 (ADL 版本)。 */
constexpr void swap(Vec3& first , Vec3& second) noexcept
{
    first.swap(second) ;
}

static_assert(sizeof(Vec3) == 3 * sizeof(double) , "Vec3 不能有 padding") ;
static_assert(alignof(Vec3) == alignof(double) , "Vec3 要与 double 同对齐") ;
static_assert(sizeof(Vec3) == sizeof(NXOpen::Vector3d) , "Vec3 要与 NXOpen::Vector3d 同尺寸") ;

/** 四维齐次向量; 点用 w=1, 方向用 w=0 (见 point / direction 工厂)。 */
struct Vec4
{
    double x , y , z , w ;

    /** 默认构造: 全零。 */
    constexpr Vec4() noexcept : x(0.0) , y(0.0) , z(0.0) , w(0.0) {}

    /** 按分量构造。 */
    constexpr Vec4(double x_value , double y_value , double z_value , double w_value) noexcept
        : x(x_value) , y(y_value) , z(z_value) , w(w_value) {}

    /** 由三维向量构造; 默认把 w 当 1 (点)。方向请用 direction()。 */
    constexpr explicit Vec4(const Vec3& vector , double w_value = 1.0) noexcept
        : x(vector.x) , y(vector.y) , z(vector.z) , w(w_value) {}

    /** 四个标量广播。 */
    static constexpr Vec4 splat(double value) noexcept
    {
        return Vec4(value , value , value , value) ;
    }

    /** 齐次点 (w = 1)。 */
    static constexpr Vec4 point(const Vec3& vector) noexcept
    {
        return Vec4(vector.x , vector.y , vector.z , 1.0) ;
    }

    /** 齐次方向 (w = 0); 方向绝对不能当点去做带平移的变换。 */
    static constexpr Vec4 direction(const Vec3& vector) noexcept
    {
        return Vec4(vector.x , vector.y , vector.z , 0.0) ;
    }

    /** 零向量。 */
    static constexpr Vec4 zero() noexcept
    {
        return Vec4(0.0 , 0.0 , 0.0 , 0.0) ;
    }

    /** 全 1 向量。 */
    static constexpr Vec4 one() noexcept
    {
        return Vec4(1.0 , 1.0 , 1.0 , 1.0) ;
    }

    /** W 轴单位向量 (0, 0, 0, 1)。 */
    static constexpr Vec4 unit_w() noexcept
    {
        return Vec4(0.0 , 0.0 , 0.0 , 1.0) ;
    }

    /** 精确相等。 */
    constexpr bool operator==(const Vec4& other) const noexcept
    {
        return x == other.x && y == other.y && z == other.z && w == other.w ;
    }

    /** 逐分量加。 */
    constexpr Vec4& operator+=(const Vec4& other) noexcept
    {
        x += other.x ;
        y += other.y ;
        z += other.z ;
        w += other.w ;
        return *this ;
    }

    /** 逐分量减。 */
    constexpr Vec4& operator-=(const Vec4& other) noexcept
    {
        x -= other.x ;
        y -= other.y ;
        z -= other.z ;
        w -= other.w ;
        return *this ;
    }

    /** 标量乘。 */
    constexpr Vec4& operator*=(double scalar) noexcept
    {
        x *= scalar ;
        y *= scalar ;
        z *= scalar ;
        w *= scalar ;
        return *this ;
    }

    /** 标量除。 */
    constexpr Vec4& operator/=(double scalar) noexcept
    {
        x /= scalar ;
        y /= scalar ;
        z /= scalar ;
        w /= scalar ;
        return *this ;
    }

    /** 逐分量加。 */
    constexpr Vec4 operator+(const Vec4& other) const noexcept
    {
        return Vec4(*this) += other ;
    }

    /** 逐分量减。 */
    constexpr Vec4 operator-(const Vec4& other) const noexcept
    {
        return Vec4(*this) -= other ;
    }

    /** 标量乘。 */
    constexpr Vec4 operator*(double scalar) const noexcept
    {
        return Vec4(*this) *= scalar ;
    }

    /** 标量除。 */
    constexpr Vec4 operator/(double scalar) const noexcept
    {
        return Vec4(*this) /= scalar ;
    }

    /** 标量在左的乘法。 */
    friend constexpr Vec4 operator*(double scalar , const Vec4& vector) noexcept
    {
        return vector * scalar ;
    }

    /** 取负。 */
    constexpr Vec4 operator-() const noexcept
    {
        return Vec4(-x , -y , -z , -w) ;
    }

    /** 点积。 */
    constexpr double dot(const Vec4& other) const noexcept
    {
        return x * other.x + y * other.y + z * other.z + w * other.w ;
    }

    /** 静态点积。 */
    static constexpr double dot(const Vec4& first , const Vec4& second) noexcept
    {
        return first.dot(second) ;
    }

    /** 长度平方。 */
    constexpr double length_sq() const noexcept
    {
        return dot(*this) ;
    }

    /** 长度。 */
    constexpr double length() const noexcept
    {
        return math_sqrt(length_sq()) ;
    }

    /** 距离平方。 */
    constexpr double distance_sq(const Vec4& other) const noexcept
    {
        return (*this - other).length_sq() ;
    }

    /** 距离。 */
    constexpr double distance(const Vec4& other) const noexcept
    {
        return (*this - other).length() ;
    }

    /** 归一化 (按标量大小); 零向量仍是零向量。 */
    constexpr Vec4 normalized() const noexcept
    {
        const double magnitude = length() ;
        return (magnitude > 0.0) ? Vec4(*this) / magnitude : Vec4() ;
    }

    /** 线性插值。 */
    constexpr Vec4 lerp(const Vec4& other , double t) const noexcept
    {
        return Vec4(*this) * (1.0 - t) + other * t ;
    }

    /** 透视除法 (齐次 -> 欧氏); w 为 0 时原样返回 xyz。 */
    constexpr Vec3 perspective_divide() const noexcept
    {
        return (w != 0.0) ? Vec3(x / w , y / w , z / w) : Vec3(x , y , z) ;
    }

    /** 取前三维。 */
    constexpr Vec3 xyz() const noexcept
    {
        return Vec3(x , y , z) ;
    }

    /** 取前两维。 */
    constexpr Vec2 xy() const noexcept
    {
        return Vec2(x , y) ;
    }

    /** 逐分量夹取 (标量参数)。 */
    constexpr Vec4 clamp(double lower , double upper) const noexcept
    {
        return clamp(splat(lower) , splat(upper)) ;
    }

    /** 夹到 [0, 1]。 */
    constexpr Vec4 clamp01() const noexcept
    {
        return clamp(0.0 , 1.0) ;
    }

    /** 逐分量夹取。 */
    constexpr Vec4 clamp(const Vec4& lower , const Vec4& upper) const noexcept
    {
        return Vec4(clamp_value(x , lower.x , upper.x) ,
                    clamp_value(y , lower.y , upper.y) ,
                    clamp_value(z , lower.z , upper.z) ,
                    clamp_value(w , lower.w , upper.w)) ;
    }

    /** 逐分量绝对值。 */
    constexpr Vec4 abs() const noexcept
    {
        return Vec4(math_abs(x) , math_abs(y) , math_abs(z) , math_abs(w)) ;
    }

    /** 最大分量。 */
    constexpr double max_component() const noexcept
    {
        return max_value(max_value(x , y) , max_value(z , w)) ;
    }

    /** 分量和。 */
    constexpr double sum() const noexcept
    {
        return x + y + z + w ;
    }

    /** 分量平均。 */
    constexpr double average() const noexcept
    {
        return (x + y + z + w) / 4.0 ;
    }

    /** 是否"基本是零" (|v| <= eps)。 */
    constexpr bool nearly_zero(double eps) const noexcept
    {
        return length_sq() <= eps * eps ;
    }

    /** 逐分量近似相等。 */
    constexpr bool nearly_equal(const Vec4& other) const noexcept
    {
        return math_near(x , other.x) && math_near(y , other.y) &&
               math_near(z , other.z) && math_near(w , other.w) ;
    }

    /** 是否精确等于零向量。 */
    constexpr bool is_zero() const noexcept
    {
        return x == 0.0 && y == 0.0 && z == 0.0 && w == 0.0 ;
    }

    /** 四个分量是否都有限。 */
    constexpr bool is_finite() const noexcept
    {
        return math_is_finite(x) && math_is_finite(y) && math_is_finite(z) && math_is_finite(w) ;
    }

    /** 是否含 NaN。 */
    constexpr bool is_nan() const noexcept
    {
        return x != x || y != y || z != z || w != w ;
    }

    /** 逐分量较小值 (标量参数)。 */
    constexpr Vec4 component_min(double scalar) const noexcept
    {
        return component_min(splat(scalar)) ;
    }

    /** 逐分量较大值 (标量参数)。 */
    constexpr Vec4 component_max(double scalar) const noexcept
    {
        return component_max(splat(scalar)) ;
    }

    /** 逐分量较小值。 */
    constexpr Vec4 component_min(const Vec4& other) const noexcept
    {
        return Vec4(min_value(x , other.x) , min_value(y , other.y) ,
                    min_value(z , other.z) , min_value(w , other.w)) ;
    }

    /** 逐分量较大值。 */
    constexpr Vec4 component_max(const Vec4& other) const noexcept
    {
        return Vec4(max_value(x , other.x) , max_value(y , other.y) ,
                    max_value(z , other.z) , max_value(w , other.w)) ;
    }

    /** 最小分量。 */
    constexpr double min_component() const noexcept
    {
        return min_value(min_value(x , y) , min_value(z , w)) ;
    }

    /** 泛型下标访问 (依赖成员连续无 padding)。 */
    constexpr double& operator[](std::size_t index) noexcept
    {
        return (&x)[index] ;
    }

    /** 泛型下标访问 (只读)。 */
    constexpr const double& operator[](std::size_t index) const noexcept
    {
        return (&x)[index] ;
    }

    /** 原始数组, 可交给 double[4] 的 NX 接口。 */
    constexpr double* data() noexcept
    {
        return &x ;
    }

    /** 原始数组 (只读)。 */
    constexpr const double* data() const noexcept
    {
        return &x ;
    }

    /** 分量个数。 */
    static constexpr std::size_t size() noexcept
    {
        return 4 ;
    }

    /** range-for 起点。 */
    constexpr double* begin() noexcept
    {
        return data() ;
    }

    /** range-for 终点。 */
    constexpr double* end() noexcept
    {
        return data() + size() ;
    }

    /** 从裸数组构造。 */
    static constexpr Vec4 from_array(const double* values) noexcept
    {
        return Vec4(values[0] , values[1] , values[2] , values[3]) ;
    }

    /** 转成 std::array。 */
    constexpr std::array<double , 4> to_array() const noexcept
    {
        return { x , y , z , w } ;
    }

    /** 接管 NX 的 Point4d (齐次点)。 */
    static Vec4 from_nx(const NXOpen::Point4d& source) noexcept
    {
        return Vec4(source.X , source.Y , source.Z , source.W) ;
    }

    /** 转成 NX 的 Point4d。 */
    NXOpen::Point4d to_nx() const noexcept
    {
        return NXOpen::Point4d(x , y , z , w) ;
    }

    /** 和另一向量交换分量。 */
    constexpr void swap(Vec4& other) noexcept
    {
        Vec4 temporary = *this ;
        *this = other ;
        other = temporary ;
    }

    private:
    /** 逐分量较小值。 */
    static constexpr double min_value(double first , double second) noexcept
    {
        return (second < first) ? second : first ;
    }

    /** 逐分量较大值。 */
    static constexpr double max_value(double first , double second) noexcept
    {
        return (first < second) ? second : first ;
    }

    /** 夹取到 [lower, upper]。 */
    static constexpr double clamp_value(double value , double lower , double upper) noexcept
    {
        return min_value(max_value(value , lower) , upper) ;
    }
} ;

/** 交换两个向量 (ADL 版本)。 */
constexpr void swap(Vec4& first , Vec4& second) noexcept
{
    first.swap(second) ;
}

static_assert(sizeof(Vec4) == 4 * sizeof(double) , "Vec4 不能有 padding") ;
static_assert(alignof(Vec4) == alignof(double) , "Vec4 要与 double 同对齐") ;
static_assert(sizeof(Vec4) == sizeof(NXOpen::Point4d) , "Vec4 要与 NXOpen::Point4d 同尺寸") ;

NX_NAMESPACE_END

/* 恢复调用方的 min / max 宏状态。 */
#ifdef NX_VECTOR_PUSHED_MIN
    #pragma pop_macro("min")
    #undef NX_VECTOR_PUSHED_MIN
#endif
#ifdef NX_VECTOR_PUSHED_MAX
    #pragma pop_macro("max")
    #undef NX_VECTOR_PUSHED_MAX
#endif
)";
} // namespace template_math
