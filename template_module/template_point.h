#pragma once
#include <string>

namespace template_math
{
// 目标工程文件 point.hpp：内容与 backup/templates/point.hpp.txt 逐字一致（行尾统一为 LF）。
inline std::string point_hpp = R"(#pragma once
#include <limits>
#include "vector.hpp"

NX_NAMESPACE_BEGIN

/* 二维点 (UI / 平面坐标用): 默认构造是"无效值"哨兵, 不是零。
 * 源库用整型/浮点模板区分像素坐标与数学坐标; NX 侧统一 double,
 * 所以去掉了整型溢出守卫分支 —— 浮点 NaN 会自己传播。 */

/** 二维点; 默认构造为 (NaN, NaN), 表示"没有坐标"。 */
struct Point
{
    double x , y ;

    /** 默认构造: 无效值哨兵 (NaN, NaN)。 */
    constexpr Point() noexcept
    {
        const double sentinel = std::numeric_limits<double>::quiet_NaN() ;
        x = sentinel ;
        y = sentinel ;
    }

    /** 按分量构造 (负坐标是合法的, 只有 NaN 才算无效)。 */
    constexpr Point(double x_value , double y_value) noexcept : x(x_value) , y(y_value) {}

    /** 由二维向量构造; 显式, 免得把"未初始化点"当合法向量算进布局。 */
    explicit constexpr Point(const Vec2& vector) noexcept : x(vector.x) , y(vector.y) {}

    /** 无效值哨兵 (与默认构造等价, 写出来读得出意图)。 */
    static constexpr Point invalid() noexcept
    {
        return Point() ;
    }

    /** 转成向量 (进点积 / 长度 / 归一化那套数学)。 */
    constexpr Vec2 to_vec2() const noexcept
    {
        return Vec2(x , y) ;
    }

    /** 精确相等 (无效点之间不等于自身, 这是 NaN 的语义)。 */
    constexpr bool operator==(const Point& other) const noexcept
    {
        return x == other.x && y == other.y ;
    }

    /** 两个分量是否都是有限值 (NaN / ±Inf 都算无效)。 */
    constexpr bool is_valid() const noexcept
    {
        return math_is_finite(x) && math_is_finite(y) ;
    }

    /** 是否无效 (默认构造即无效)。 */
    constexpr bool is_invalid() const noexcept
    {
        return !is_valid() ;
    }

    /** 平移。 */
    constexpr Point& operator+=(const Point& other) noexcept
    {
        x += other.x ;
        y += other.y ;
        return *this ;
    }

    /** 反向平移。 */
    constexpr Point& operator-=(const Point& other) noexcept
    {
        x -= other.x ;
        y -= other.y ;
        return *this ;
    }

    /** 缩放 (含无效值吸收: NaN 分量会自己传播)。 */
    constexpr Point& operator*=(double scalar) noexcept
    {
        x *= scalar ;
        y *= scalar ;
        return *this ;
    }

    /** 缩放 (除数为 0 时得到 Inf/NaN, 即无效点)。 */
    constexpr Point& operator/=(double scalar) noexcept
    {
        x /= scalar ;
        y /= scalar ;
        return *this ;
    }

    /** 平移。 */
    constexpr Point operator+(const Point& other) const noexcept
    {
        return Point(*this) += other ;
    }

    /** 反向平移。 */
    constexpr Point operator-(const Point& other) const noexcept
    {
        return Point(*this) -= other ;
    }

    /** 缩放。 */
    constexpr Point operator*(double scalar) const noexcept
    {
        return Point(*this) *= scalar ;
    }

    /** 缩放。 */
    constexpr Point operator/(double scalar) const noexcept
    {
        return Point(*this) /= scalar ;
    }

    /** 标量在左的乘法。 */
    friend constexpr Point operator*(double scalar , const Point& point) noexcept
    {
        return point * scalar ;
    }

    /** 相对原点取负。 */
    constexpr Point operator-() const noexcept
    {
        return Point(-x , -y) ;
    }

    /** 点积 (把点当二维向量用)。 */
    constexpr double dot(const Point& other) const noexcept
    {
        return x * other.x + y * other.y ;
    }

    /** 二维叉积 (左手/右手侧判定)。 */
    constexpr double cross(const Point& other) const noexcept
    {
        return x * other.y - y * other.x ;
    }

    /** 到原点的距离平方。 */
    constexpr double length_sq() const noexcept
    {
        return dot(*this) ;
    }

    /** 到原点的距离。 */
    constexpr double length() const noexcept
    {
        return math_sqrt(length_sq()) ;
    }

    /** 两点距离。 */
    constexpr double distance(const Point& other) const noexcept
    {
        return (*this - other).length() ;
    }

    /** 接管 NX 的二维点。 */
    static Point from_nx(const NXOpen::Point2d& source) noexcept
    {
        return Point(source.X , source.Y) ;
    }

    /** 转成 NX 的二维点。 */
    NXOpen::Point2d to_nx() const noexcept
    {
        return NXOpen::Point2d(x , y) ;
    }

    /** 和另一个点交换坐标。 */
    constexpr void swap(Point& other) noexcept
    {
        const double first = x ;
        x = other.x ;
        other.x = first ;
        const double second = y ;
        y = other.y ;
        other.y = second ;
    }
} ;

/** 交换两个点 (ADL 版本)。 */
constexpr void swap(Point& first , Point& second) noexcept
{
    first.swap(second) ;
}

static_assert(sizeof(Point) == 2 * sizeof(double) , "Point 不能有 padding") ;
static_assert(sizeof(Point) == sizeof(NXOpen::Point2d) , "Point 要与 NXOpen::Point2d 同尺寸") ;

NX_NAMESPACE_END
)";
} // namespace template_math
