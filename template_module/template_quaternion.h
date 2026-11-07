#pragma once
#include <string>

namespace template_math
{
// 目标工程文件 quaternion.hpp：内容与 backup/templates/quaternion.hpp.txt 逐字一致（行尾统一为 LF）。
inline std::string quaternion_hpp = R"(#pragma once
#include <cmath>
#include "matrix.hpp"
#include "vector.hpp"

NX_NAMESPACE_BEGIN

/* 四元数 (单位四元数 = 旋转)。存储 (w, x, y, z): w 为标量部, (x,y,z) 为向量部。
 * 约定与源库一致: rotate(v) 是主动旋转 (等价于 v × 旋转矩阵), to_matrix3() 给
 * 行 = 轴 的 NX 矩阵 —— 与 NX 的 UGII 矩阵布局对得上。 */

/** 四元数; 默认构造 = 单位四元数 (恒等旋转)。 */
struct Quaternion
{
    double w , x , y , z ;

    /** 默认构造: 单位四元数 (恒等旋转)。 */
    constexpr Quaternion() noexcept : w(1.0) , x(0.0) , y(0.0) , z(0.0) {}

    /** 按分量构造 (w = 标量部, (x,y,z) = 向量部)。 */
    constexpr Quaternion(double w_value , double x_value , double y_value , double z_value) noexcept
        : w(w_value) , x(x_value) , y(y_value) , z(z_value) {}

    /** 单位四元数 (具名工厂)。 */
    static constexpr Quaternion identity() noexcept
    {
        return Quaternion() ;
    }

    /** 精确相等 (四元数累积误差后几乎必然为假, 几何判等用 nearly_equal)。 */
    constexpr bool operator==(const Quaternion& other) const noexcept
    {
        return w == other.w && x == other.x && y == other.y && z == other.z ;
    }

    /** 逐分量加。 */
    constexpr Quaternion& operator+=(const Quaternion& other) noexcept
    {
        w += other.w ;
        x += other.x ;
        y += other.y ;
        z += other.z ;
        return *this ;
    }

    /** 逐分量减。 */
    constexpr Quaternion& operator-=(const Quaternion& other) noexcept
    {
        w -= other.w ;
        x -= other.x ;
        y -= other.y ;
        z -= other.z ;
        return *this ;
    }

    /** 逐分量加。 */
    constexpr Quaternion operator+(const Quaternion& other) const noexcept
    {
        return Quaternion(*this) += other ;
    }

    /** 逐分量减。 */
    constexpr Quaternion operator-(const Quaternion& other) const noexcept
    {
        return Quaternion(*this) -= other ;
    }

    /** 标量乘。 */
    constexpr Quaternion& operator*=(double scalar) noexcept
    {
        w *= scalar ;
        x *= scalar ;
        y *= scalar ;
        z *= scalar ;
        return *this ;
    }

    /** 标量除。 */
    constexpr Quaternion& operator/=(double scalar) noexcept
    {
        w /= scalar ;
        x /= scalar ;
        y /= scalar ;
        z /= scalar ;
        return *this ;
    }

    /** 标量乘。 */
    constexpr Quaternion operator*(double scalar) const noexcept
    {
        return Quaternion(*this) *= scalar ;
    }

    /** 标量除。 */
    constexpr Quaternion operator/(double scalar) const noexcept
    {
        return Quaternion(*this) /= scalar ;
    }

    /** 标量在左的乘法。 */
    friend constexpr Quaternion operator*(double scalar , const Quaternion& rotation) noexcept
    {
        return rotation * scalar ;
    }

    /** 逐分量取负; 注意 -q 与 q 表示同一个旋转, 这不是求逆。 */
    constexpr Quaternion operator-() const noexcept
    {
        return Quaternion(-w , -x , -y , -z) ;
    }

    /** Hamilton 积: 先作用 other 的旋转, 再作用自己的旋转。 */
    constexpr Quaternion operator*(const Quaternion& other) const noexcept
    {
        return Quaternion(w * other.w - x * other.x - y * other.y - z * other.z ,
                          w * other.x + x * other.w + y * other.z - z * other.y ,
                          w * other.y - x * other.z + y * other.w + z * other.x ,
                          w * other.z + x * other.y - y * other.x + z * other.w) ;
    }

    /** 就地乘上 other (旋转复合)。 */
    constexpr Quaternion& operator*=(const Quaternion& other) noexcept
    {
        *this = *this * other ;
        return *this ;
    }

    /** 共轭; 单位四元数下即逆旋转。 */
    constexpr Quaternion conjugated() const noexcept
    {
        return Quaternion(w , -x , -y , -z) ;
    }

    /** 逆 = 共轭 / 范数平方 (非单位四元数也正确; 零四元数返回单位四元数)。 */
    constexpr Quaternion inverse() const noexcept
    {
        const double norm = length_sq() ;
        if (norm == 0.0) return Quaternion() ;
        const Quaternion conjugate = conjugated() ;
        return Quaternion(conjugate.w / norm , conjugate.x / norm , conjugate.y / norm , conjugate.z / norm) ;
    }

    /** 四维点积。 */
    constexpr double dot(const Quaternion& other) const noexcept
    {
        return w * other.w + x * other.x + y * other.y + z * other.z ;
    }

    /** 模长平方。 */
    constexpr double length_sq() const noexcept
    {
        return dot(*this) ;
    }

    /** 模长。 */
    constexpr double length() const noexcept
    {
        return math_sqrt(length_sq()) ;
    }

    /** 归一化; 零四元数仍是零 (调用方须保证非零)。 */
    constexpr Quaternion normalized() const noexcept
    {
        const double norm = length() ;
        if (norm == 0.0) return Quaternion(0.0 , 0.0 , 0.0 , 0.0) ;
        return Quaternion(w / norm , x / norm , y / norm , z / norm) ;
    }

    /** 逐分量近似相等。 */
    constexpr bool nearly_equal(const Quaternion& other) const noexcept
    {
        return math_near(w , other.w) && math_near(x , other.x) &&
               math_near(y , other.y) && math_near(z , other.z) ;
    }

    /** 四个分量是否全为 0 (零四元数不是合法旋转)。 */
    constexpr bool is_zero() const noexcept
    {
        return w == 0.0 && x == 0.0 && y == 0.0 && z == 0.0 ;
    }

    /** 是否含 NaN。 */
    constexpr bool is_nan() const noexcept
    {
        return w != w || x != x || y != y || z != z ;
    }

    /** 四个分量是否都有限。 */
    constexpr bool is_finite() const noexcept
    {
        return math_is_finite(w) && math_is_finite(x) &&
               math_is_finite(y) && math_is_finite(z) ;
    }

    /** 旋转一个向量 (单位四元数): v' = q v q^-1 的展开式。 */
    constexpr Vec3 rotate(const Vec3& vector) const noexcept
    {
        const Vec3 axis(x , y , z) ;
        const Vec3 cross_term = axis.cross(vector) * 2.0 ;
        return vector + cross_term * w + axis.cross(cross_term) ;
    }

    /** 转旋转矩阵; 返回行 = X/Y/Z 轴的 NX 布局矩阵 (NXOpen::Matrix3x3 可直接取用)。 */
    constexpr Matrix3x3 to_matrix3() const noexcept
    {
        const double xx = x * x ;
        const double yy = y * y ;
        const double zz = z * z ;
        const double xy = x * y ;
        const double xz = x * z ;
        const double yz = y * z ;
        const double wx = w * x ;
        const double wy = w * y ;
        const double wz = w * z ;
        /* 每三行一组 = 一条轴: 第 0 组是 X 轴, 第 1 组 Y 轴, 第 2 组 Z 轴。 */
        const double values[9] = { 1.0 - 2.0 * (yy + zz) , 2.0 * (xy + wz) , 2.0 * (xz - wy) ,
                                   2.0 * (xy - wz) , 1.0 - 2.0 * (xx + zz) , 2.0 * (yz + wx) ,
                                   2.0 * (xz + wy) , 2.0 * (yz - wx) , 1.0 - 2.0 * (xx + yy) } ;
        return Matrix3x3(values) ;
    }

    /** 转平移为 0 的 4x4 (旋转 + 零平移; 末行 (0,0,0,1))。 */
    constexpr Matrix4x4 to_matrix4() const noexcept
    {
        const Matrix3x3 rotation = to_matrix3() ;
        const double values[16] = { rotation.m_data[0] , rotation.m_data[1] , rotation.m_data[2] , 0.0 ,
                                    rotation.m_data[3] , rotation.m_data[4] , rotation.m_data[5] , 0.0 ,
                                    rotation.m_data[6] , rotation.m_data[7] , rotation.m_data[8] , 0.0 ,
                                    0.0 , 0.0 , 0.0 , 1.0 } ;
        return Matrix4x4(values) ;
    }

    /** 由轴角构造; axis 必须已归一化, radians 为弧度, 右手系。 */
    static Quaternion from_axis_angle(const Vec3& axis , double radians) noexcept
    {
        const double half = radians * 0.5 ;
        const double sine = std::sin(half) ;
        return Quaternion(std::cos(half) , axis.x * sine , axis.y * sine , axis.z * sine) ;
    }

    /** 由欧拉角构造 (yaw-pitch-roll 顺序, 即 Y * X * Z; 弧度)。 */
    static Quaternion from_euler(double pitch , double yaw , double roll) noexcept
    {
        const double half_pitch = pitch * 0.5 ;
        const double half_yaw = yaw * 0.5 ;
        const double half_roll = roll * 0.5 ;
        const double cosine_pitch = std::cos(half_pitch) ;
        const double sine_pitch = std::sin(half_pitch) ;
        const double cosine_yaw = std::cos(half_yaw) ;
        const double sine_yaw = std::sin(half_yaw) ;
        const double cosine_roll = std::cos(half_roll) ;
        const double sine_roll = std::sin(half_roll) ;
        return Quaternion(cosine_yaw * cosine_pitch * cosine_roll + sine_yaw * sine_pitch * sine_roll ,
                          cosine_yaw * sine_pitch * cosine_roll + sine_yaw * cosine_pitch * sine_roll ,
                          sine_yaw * cosine_pitch * cosine_roll - cosine_yaw * sine_pitch * sine_roll ,
                          cosine_yaw * cosine_pitch * sine_roll - sine_yaw * sine_pitch * cosine_roll) ;
    }

    /** 由旋转矩阵提取 (Shoemake 对角最大分支; 矩阵按行 = 轴 的 NX 布局解释)。 */
    static Quaternion from_matrix(const Matrix3x3& matrix) noexcept
    {
        const double trace = matrix.m_data[0] + matrix.m_data[4] + matrix.m_data[8] ;
        if (trace > 0.0)
        {
            const double scale = std::sqrt(trace + 1.0) * 2.0 ;
            return Quaternion(0.25 * scale ,
                              (matrix.m_data[5] - matrix.m_data[7]) / scale ,
                              (matrix.m_data[6] - matrix.m_data[2]) / scale ,
                              (matrix.m_data[1] - matrix.m_data[3]) / scale) ;
        }
        if (matrix.m_data[0] > matrix.m_data[4] && matrix.m_data[0] > matrix.m_data[8])
        {
            const double scale = std::sqrt(1.0 + matrix.m_data[0] - matrix.m_data[4] - matrix.m_data[8]) * 2.0 ;
            return Quaternion((matrix.m_data[5] - matrix.m_data[7]) / scale , 0.25 * scale ,
                              (matrix.m_data[1] + matrix.m_data[3]) / scale ,
                              (matrix.m_data[2] + matrix.m_data[6]) / scale) ;
        }
        if (matrix.m_data[4] > matrix.m_data[8])
        {
            const double scale = std::sqrt(1.0 + matrix.m_data[4] - matrix.m_data[0] - matrix.m_data[8]) * 2.0 ;
            return Quaternion((matrix.m_data[6] - matrix.m_data[2]) / scale ,
                              (matrix.m_data[1] + matrix.m_data[3]) / scale , 0.25 * scale ,
                              (matrix.m_data[5] + matrix.m_data[7]) / scale) ;
        }
        const double scale = std::sqrt(1.0 + matrix.m_data[8] - matrix.m_data[0] - matrix.m_data[4]) * 2.0 ;
        return Quaternion((matrix.m_data[1] - matrix.m_data[3]) / scale ,
                          (matrix.m_data[2] + matrix.m_data[6]) / scale ,
                          (matrix.m_data[5] + matrix.m_data[7]) / scale , 0.25 * scale) ;
    }

    /** 球面线性插值 (走最短弧; 两端须为单位四元数, t ∈ [0,1])。 */
    static Quaternion slerp(const Quaternion& first , const Quaternion& second , double t) noexcept
    {
        double cosine = first.dot(second) ;
        Quaternion target = second ;
        if (cosine < 0.0)
        {
            cosine = -cosine ;
            target = Quaternion(-second.w , -second.x , -second.y , -second.z) ;
        }
        if (cosine > 0.9995) return nlerp(first , target , t) ;   // 近重合: 线性近似防除零抖动
        const double omega = std::acos(cosine) ;
        const double sine = std::sin(omega) ;
        const double weight_first = std::sin((1.0 - t) * omega) / sine ;
        const double weight_second = std::sin(t * omega) / sine ;
        return Quaternion(first.w * weight_first + target.w * weight_second ,
                          first.x * weight_first + target.x * weight_second ,
                          first.y * weight_first + target.y * weight_second ,
                          first.z * weight_first + target.z * weight_second) ;
    }

    /** 线性插值 + 归一化 (slerp 的快速近似)。 */
    static Quaternion nlerp(const Quaternion& first , const Quaternion& second , double t) noexcept
    {
        const double sign = (first.dot(second) < 0.0) ? -1.0 : 1.0 ;
        return Quaternion(first.w + (second.w * sign - first.w) * t ,
                          first.x + (second.x * sign - first.x) * t ,
                          first.y + (second.y * sign - first.y) * t ,
                          first.z + (second.z * sign - first.z) * t).normalized() ;
    }

    /** 和另一个四元数交换分量。 */
    constexpr void swap(Quaternion& other) noexcept
    {
        Quaternion temporary = *this ;
        *this = other ;
        other = temporary ;
    }
} ;

/** 交换两个四元数 (ADL 版本)。 */
constexpr void swap(Quaternion& first , Quaternion& second) noexcept
{
    first.swap(second) ;
}

static_assert(sizeof(Quaternion) == 4 * sizeof(double) , "Quaternion 不能有 padding") ;
static_assert(alignof(Quaternion) == alignof(double) , "Quaternion 要与 double 同对齐") ;

NX_NAMESPACE_END
)";
} // namespace template_math
