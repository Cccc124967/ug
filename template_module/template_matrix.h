#pragma once
#include <string>

namespace template_math
{
// 目标工程文件 matrix.hpp：内容与 backup/templates/matrix.hpp.txt 逐字一致（行尾统一为 LF）。
inline std::string matrix_hpp = R"(#pragma once
#include <type_traits>
#include <NXOpen/ugmath.hxx>
#include "basic_type_defs.h"
#include "vector.hpp"

NX_NAMESPACE_BEGIN

/** 矩阵共用的奇异判据 (与 NX 常用精度一致)。 */
constexpr double MatrixEps = 1e-7 ;

/** 求绝对值; NaN / Inf 原样传播, 调用方要先用 matrix_is_finite 拦一道。 */
constexpr double matrix_abs(double value) noexcept
{
    return value > 0.0 ? value : -value ;
}

/** 有限性判定; 不用 std::isfinite 是因为它到 C++23 才 constexpr (inf/NaN 时 v - v 是 NaN)。 */
constexpr bool matrix_is_finite(double value) noexcept
{
    return (value - value) == 0.0 ;
}

/* 存储与方向约定 (3x3 / 4x4 相同):
 *   · 行主序 m_data[row * N + col], 与 NXOpen::Matrix3x3 / Matrix4x4 的内存布局一致;
 *   · 3x3 的行 = X/Y/Z 方向向量; 4x4 前三行 = (旋转行, 平移分量), 末行 = (0, 0, 0, 1/scale);
 *   · 数学约定: 列向量 v' = M * v; NX 的 UF_MTX*_vec_multiply 走的是 v x M, 方向相反。 */

/** 3x3 行主序矩阵; 行 = X/Y/Z 方向向量, 内存布局与 NXOpen::Matrix3x3 一致。 */
struct Matrix3x3
{
    double m_data[9] ;   // 行主序: m_data[row * 3 + col]

    /** 单位阵。 */
    constexpr Matrix3x3() noexcept
        : m_data{ 1.0, 0.0, 0.0,
                  0.0, 1.0, 0.0,
                  0.0, 0.0, 1.0 } {}

    /** 按行主序接管 9 个分量。 */
    constexpr explicit Matrix3x3(const double (&values)[9]) noexcept
        : m_data{ values[0], values[1], values[2],
                  values[3], values[4], values[5],
                  values[6], values[7], values[8] } {}

    constexpr Matrix3x3(const Matrix3x3&) = default ;
    constexpr Matrix3x3& operator=(const Matrix3x3&) = default ;
    constexpr Matrix3x3(Matrix3x3&&) noexcept = default ;
    constexpr Matrix3x3& operator=(Matrix3x3&&) noexcept = default ;
    ~Matrix3x3() = default ;

    /** 逐分量相等, 不做容差。 */
    constexpr bool operator==(const Matrix3x3& other) const noexcept
    {
        for (int i = 0 ; i < 9 ; ++i) if (m_data[i] != other.m_data[i]) return false ;
        return true ;
    }

    /** 逐分量不等。 */
    constexpr bool operator!=(const Matrix3x3& other) const noexcept
    {
        return !(*this == other) ;
    }

    /** 单位阵 (具名工厂, 让调用点能读出意图)。 */
    static constexpr Matrix3x3 identity() noexcept
    {
        return Matrix3x3() ;
    }

    /** 按 (行, 列) 取元素引用。 */
    constexpr double& operator()(int row , int col) noexcept
    {
        return m_data[row * 3 + col] ;
    }

    /** 按 (行, 列) 取元素 (只读)。 */
    constexpr const double& operator()(int row , int col) const noexcept
    {
        return m_data[row * 3 + col] ;
    }

    /** 原始数组, 可直接喂 UF_MTX3_* (布局与 NX 一致)。 */
    constexpr double* data() noexcept
    {
        return m_data ;
    }

    /** 原始数组 (只读), 可直接喂 UF_MTX3_*。 */
    constexpr const double* data() const noexcept
    {
        return m_data ;
    }

    /** 矩阵 * 向量 = 标准列向量乘 (M v); 对 NX 矩阵 (行 = 轴) 是全局 -> 局部。 */
    constexpr Vec3 operator*(const Vec3& vector) const noexcept
    {
        return Vec3(m_data[0] * vector.x + m_data[1] * vector.y + m_data[2] * vector.z ,
                    m_data[3] * vector.x + m_data[4] * vector.y + m_data[5] * vector.z ,
                    m_data[6] * vector.x + m_data[7] * vector.y + m_data[8] * vector.z) ;
    }

    /** 向量 * 矩阵 = 行向量乘 (v × M = M^T v); 等价 NX 的 UF_MTX3_vec_multiply, 对 NX 矩阵是局部 -> 全局。 */
    friend constexpr Vec3 operator*(const Vec3& vector , const Matrix3x3& matrix) noexcept
    {
        return Vec3(vector.x * matrix.m_data[0] + vector.y * matrix.m_data[3] + vector.z * matrix.m_data[6] ,
                    vector.x * matrix.m_data[1] + vector.y * matrix.m_data[4] + vector.z * matrix.m_data[7] ,
                    vector.x * matrix.m_data[2] + vector.y * matrix.m_data[5] + vector.z * matrix.m_data[8]) ;
    }

    /** 全局向量 -> 本矩阵局部坐标 (同矩阵 * 向量); 等价 NX 的 UF_MTX3_vec_multiply_t。 */
    constexpr Vec3 transform_to_local(const Vec3& vector) const noexcept
    {
        return *this * vector ;
    }

    /** 本矩阵局部坐标 -> 全局向量 (同向量 * 矩阵); 等价 NX 的 UF_MTX3_vec_multiply。 */
    constexpr Vec3 transform_to_global(const Vec3& vector) const noexcept
    {
        return vector * *this ;
    }

    /** 矩阵乘法: this × other。 */
    constexpr Matrix3x3 operator*(const Matrix3x3& other) const noexcept
    {
        Matrix3x3 result ;
        for (int row = 0 ; row < 3 ; ++row)
        {
            for (int col = 0 ; col < 3 ; ++col)
            {
                double sum = 0.0 ;
                for (int k = 0 ; k < 3 ; ++k) sum += m_data[row * 3 + k] * other.m_data[k * 3 + col] ;
                result.m_data[row * 3 + col] = sum ;
            }
        }
        return result ;
    }

    /** 就地乘上 other。 */
    constexpr Matrix3x3& operator*=(const Matrix3x3& other) noexcept
    {
        *this = *this * other ;
        return *this ;
    }

    /** 转置 (按值返回)。 */
    constexpr Matrix3x3 transposed() const noexcept
    {
        Matrix3x3 result ;
        for (int row = 0 ; row < 3 ; ++row)
        {
            for (int col = 0 ; col < 3 ; ++col) result.m_data[row * 3 + col] = m_data[col * 3 + row] ;
        }
        return result ;
    }

    /** 行列式。 */
    constexpr double determinant() const noexcept
    {
        return m_data[0] * (m_data[4] * m_data[8] - m_data[5] * m_data[7])
             - m_data[1] * (m_data[3] * m_data[8] - m_data[5] * m_data[6])
             + m_data[2] * (m_data[3] * m_data[7] - m_data[4] * m_data[6]) ;
    }

    /** 伴随矩阵法求逆; 奇异或行列式非有限时返回 false 且 out 保持原样。 */
    constexpr bool invert(Matrix3x3& out) const noexcept
    {
        const double det = determinant() ;
        if (!matrix_is_finite(det) || matrix_abs(det) < MatrixEps) return false ;
        const double inv = 1.0 / det ;
        out.m_data[0] = (m_data[4] * m_data[8] - m_data[5] * m_data[7]) * inv ;
        out.m_data[1] = (m_data[2] * m_data[7] - m_data[1] * m_data[8]) * inv ;
        out.m_data[2] = (m_data[1] * m_data[5] - m_data[2] * m_data[4]) * inv ;
        out.m_data[3] = (m_data[5] * m_data[6] - m_data[3] * m_data[8]) * inv ;
        out.m_data[4] = (m_data[0] * m_data[8] - m_data[2] * m_data[6]) * inv ;
        out.m_data[5] = (m_data[2] * m_data[3] - m_data[0] * m_data[5]) * inv ;
        out.m_data[6] = (m_data[3] * m_data[7] - m_data[4] * m_data[6]) * inv ;
        out.m_data[7] = (m_data[1] * m_data[6] - m_data[0] * m_data[7]) * inv ;
        out.m_data[8] = (m_data[0] * m_data[4] - m_data[1] * m_data[3]) * inv ;
        return true ;
    }

    /** 接管 NX 的 3x3 矩阵; 两边内存布局一致, 逐分量拷贝。 */
    static Matrix3x3 from_nx(const NXOpen::Matrix3x3& source) noexcept
    {
        Matrix3x3 result ;
        result.m_data[0] = source.Xx ;
        result.m_data[1] = source.Xy ;
        result.m_data[2] = source.Xz ;
        result.m_data[3] = source.Yx ;
        result.m_data[4] = source.Yy ;
        result.m_data[5] = source.Yz ;
        result.m_data[6] = source.Zx ;
        result.m_data[7] = source.Zy ;
        result.m_data[8] = source.Zz ;
        return result ;
    }

    /** 转成 NX 的 3x3 矩阵; 两边内存布局一致, 逐分量拷贝。 */
    NXOpen::Matrix3x3 to_nx() const noexcept
    {
        NXOpen::Matrix3x3 result ;
        result.Xx = m_data[0] ;
        result.Xy = m_data[1] ;
        result.Xz = m_data[2] ;
        result.Yx = m_data[3] ;
        result.Yy = m_data[4] ;
        result.Yz = m_data[5] ;
        result.Zx = m_data[6] ;
        result.Zy = m_data[7] ;
        result.Zz = m_data[8] ;
        return result ;
    }

    /** 和另一个矩阵交换内容。 */
    constexpr void swap(Matrix3x3& other) noexcept
    {
        for (int i = 0 ; i < 9 ; ++i)
        {
            const double temp = m_data[i] ;
            m_data[i] = other.m_data[i] ;
            other.m_data[i] = temp ;
        }
    }
} ;

/** 交换两个矩阵 (ADL 版本, 供 std::swap 泛化使用)。 */
constexpr void swap(Matrix3x3& first , Matrix3x3& second) noexcept
{
    first.swap(second) ;
}

static_assert(std::is_trivially_copyable_v<Matrix3x3> , "Matrix3x3 必须是平凡可拷贝类型") ;
static_assert(sizeof(Matrix3x3) == 9 * sizeof(double) , "Matrix3x3 不能有 padding") ;
static_assert(sizeof(Matrix3x3) == sizeof(NXOpen::Matrix3x3) , "Matrix3x3 要与 NXOpen::Matrix3x3 同尺寸") ;
static_assert(alignof(Matrix3x3) == alignof(NXOpen::Matrix3x3) , "Matrix3x3 要与 NXOpen::Matrix3x3 同对齐") ;

/** 4x4 行主序矩阵; 前三行 = (旋转行, 平移分量), 末行 = (0, 0, 0, 1/scale)。 */
struct Matrix4x4
{
    double m_data[16] ;   // 行主序: m_data[row * 4 + col]

    /** 单位阵。 */
    constexpr Matrix4x4() noexcept
        : m_data{ 1.0, 0.0, 0.0, 0.0,
                  0.0, 1.0, 0.0, 0.0,
                  0.0, 0.0, 1.0, 0.0,
                  0.0, 0.0, 0.0, 1.0 } {}

    /** 按行主序接管 16 个分量。 */
    constexpr explicit Matrix4x4(const double (&values)[16]) noexcept
        : m_data{ values[0],  values[1],  values[2],  values[3],
                  values[4],  values[5],  values[6],  values[7],
                  values[8],  values[9],  values[10], values[11],
                  values[12], values[13], values[14], values[15] } {}

    constexpr Matrix4x4(const Matrix4x4&) = default ;
    constexpr Matrix4x4& operator=(const Matrix4x4&) = default ;
    constexpr Matrix4x4(Matrix4x4&&) noexcept = default ;
    constexpr Matrix4x4& operator=(Matrix4x4&&) noexcept = default ;
    ~Matrix4x4() = default ;

    /** 逐分量相等, 不做容差。 */
    constexpr bool operator==(const Matrix4x4& other) const noexcept
    {
        for (int i = 0 ; i < 16 ; ++i) if (m_data[i] != other.m_data[i]) return false ;
        return true ;
    }

    /** 逐分量不等。 */
    constexpr bool operator!=(const Matrix4x4& other) const noexcept
    {
        return !(*this == other) ;
    }

    /** 单位阵 (具名工厂, 让调用点能读出意图)。 */
    static constexpr Matrix4x4 identity() noexcept
    {
        return Matrix4x4() ;
    }

    /** 按 (行, 列) 取元素引用。 */
    constexpr double& operator()(int row , int col) noexcept
    {
        return m_data[row * 4 + col] ;
    }

    /** 按 (行, 列) 取元素 (只读)。 */
    constexpr const double& operator()(int row , int col) const noexcept
    {
        return m_data[row * 4 + col] ;
    }

    /** 原始数组, 可直接喂 UF_MTX4_* (布局与 NX 一致)。 */
    constexpr double* data() noexcept
    {
        return m_data ;
    }

    /** 原始数组 (只读), 可直接喂 UF_MTX4_*。 */
    constexpr const double* data() const noexcept
    {
        return m_data ;
    }

    /** 左上 3x3 旋转块。 */
    constexpr Matrix3x3 rotation() const noexcept
    {
        const double block[9] = { m_data[0] , m_data[1] , m_data[2] ,
                                  m_data[4] , m_data[5] , m_data[6] ,
                                  m_data[8] , m_data[9] , m_data[10] } ;
        return Matrix3x3(block) ;
    }

    /** 平移分量; NX 把它放在每行第 4 个元素 (Xt/Yt/Zt)。 */
    NXOpen::Vector3d translation() const noexcept
    {
        return NXOpen::Vector3d(m_data[3] , m_data[7] , m_data[11]) ;
    }

    /** 替换平移分量, 旋转与 scale 不动。 */
    void set_translation(const NXOpen::Vector3d& value) noexcept
    {
        m_data[3] = value.X ;
        m_data[7] = value.Y ;
        m_data[11] = value.Z ;
    }

    /** 平移分量 (Vec3 版本, 数学运算用)。 */
    constexpr Vec3 translation_vector() const noexcept
    {
        return Vec3(m_data[3] , m_data[7] , m_data[11]) ;
    }

    /** 替换平移分量 (Vec3 版本)。 */
    constexpr void set_translation(const Vec3& value) noexcept
    {
        m_data[3] = value.x ;
        m_data[7] = value.y ;
        m_data[11] = value.z ;
    }

    /** 局部方向 -> 全局方向 (只旋转, 不含平移): v * R。 */
    constexpr Vec3 transform_vector(const Vec3& vector) const noexcept
    {
        return vector * rotation() ;
    }

    /** 局部点 -> 全局点: p * R + T。 */
    constexpr Vec3 transform_point(const Vec3& point) const noexcept
    {
        return point * rotation() + translation_vector() ;
    }

    /** 全局方向 -> 局部方向: R * v。 */
    constexpr Vec3 inverse_transform_vector(const Vec3& vector) const noexcept
    {
        return rotation() * vector ;
    }

    /** 全局点 -> 局部点: R * (g - T)。 */
    constexpr Vec3 inverse_transform_point(const Vec3& point) const noexcept
    {
        return rotation() * (point - translation_vector()) ;
    }

    /** 由 3x3 旋转 + 3D 平移拼 4x4; 末行为 (0, 0, 0, 1), 即 scale = 1 的刚体变换。 */
    static Matrix4x4 from_rotation_translation(const Matrix3x3& rotation , const NXOpen::Vector3d& translation) noexcept
    {
        Matrix4x4 result ;
        for (int row = 0 ; row < 3 ; ++row)
        {
            for (int col = 0 ; col < 3 ; ++col) result.m_data[row * 4 + col] = rotation.m_data[row * 3 + col] ;
        }
        result.m_data[3] = translation.X ;
        result.m_data[7] = translation.Y ;
        result.m_data[11] = translation.Z ;
        return result ;
    }

    /** 由 3x3 旋转 + Vec3 平移拼 4x4; 末行为 (0, 0, 0, 1)。 */
    static constexpr Matrix4x4 from_rotation_translation(const Matrix3x3& rotation , const Vec3& translation) noexcept
    {
        Matrix4x4 result ;
        for (int row = 0 ; row < 3 ; ++row)
        {
            for (int col = 0 ; col < 3 ; ++col) result.m_data[row * 4 + col] = rotation.m_data[row * 3 + col] ;
        }
        result.m_data[3] = translation.x ;
        result.m_data[7] = translation.y ;
        result.m_data[11] = translation.z ;
        return result ;
    }

    /** 矩阵乘法: this × other。 */
    constexpr Matrix4x4 operator*(const Matrix4x4& other) const noexcept
    {
        Matrix4x4 result ;
        for (int row = 0 ; row < 4 ; ++row)
        {
            for (int col = 0 ; col < 4 ; ++col)
            {
                double sum = 0.0 ;
                for (int k = 0 ; k < 4 ; ++k) sum += m_data[row * 4 + k] * other.m_data[k * 4 + col] ;
                result.m_data[row * 4 + col] = sum ;
            }
        }
        return result ;
    }

    /** 就地乘上 other。 */
    constexpr Matrix4x4& operator*=(const Matrix4x4& other) noexcept
    {
        *this = *this * other ;
        return *this ;
    }

    /** 转置 (按值返回)。 */
    constexpr Matrix4x4 transposed() const noexcept
    {
        Matrix4x4 result ;
        for (int row = 0 ; row < 4 ; ++row)
        {
            for (int col = 0 ; col < 4 ; ++col) result.m_data[row * 4 + col] = m_data[col * 4 + row] ;
        }
        return result ;
    }

    /** (row, col) 处的带符号余子式; sub 按行主序 m[row * 4 + col] 解释。 */
    static constexpr double cofactor(const double* values , int row , int col) noexcept
    {
        double sub[9] ;
        int index = 0 ;
        for (int r = 0 ; r < 4 ; ++r)
        {
            if (r == row) continue ;
            for (int c = 0 ; c < 4 ; ++c)
            {
                if (c == col) continue ;
                sub[index++] = values[r * 4 + c] ;
            }
        }
        const double det = sub[0] * (sub[4] * sub[8] - sub[5] * sub[7])
                         - sub[1] * (sub[3] * sub[8] - sub[5] * sub[6])
                         + sub[2] * (sub[3] * sub[7] - sub[4] * sub[6]) ;
        return ((row + col) & 1) ? -det : det ;
    }

    /** 行列式 (沿第 0 行展开)。 */
    constexpr double determinant() const noexcept
    {
        double det = 0.0 ;
        for (int col = 0 ; col < 4 ; ++col) det += m_data[col] * cofactor(m_data , 0 , col) ;
        return det ;
    }

    /** 伴随矩阵法求逆; 奇异或行列式非有限时返回 false 且 out 保持原样。 */
    constexpr bool invert(Matrix4x4& out) const noexcept
    {
        double cofactors[16] ;
        for (int row = 0 ; row < 4 ; ++row)
        {
            for (int col = 0 ; col < 4 ; ++col) cofactors[row * 4 + col] = cofactor(m_data , row , col) ;
        }
        double det = 0.0 ;
        for (int col = 0 ; col < 4 ; ++col) det += m_data[col] * cofactors[col] ;
        if (!matrix_is_finite(det) || matrix_abs(det) < MatrixEps) return false ;
        const double inv = 1.0 / det ;
        for (int row = 0 ; row < 4 ; ++row)
        {
            for (int col = 0 ; col < 4 ; ++col) out.m_data[row * 4 + col] = cofactors[col * 4 + row] * inv ;
        }
        return true ;
    }

    /** 接管 NX 的 4x4 矩阵; 两边内存布局一致, 逐分量拷贝。 */
    static Matrix4x4 from_nx(const NXOpen::Matrix4x4& source) noexcept
    {
        Matrix4x4 result ;
        result.m_data[0] = source.Rxx ;
        result.m_data[1] = source.Rxy ;
        result.m_data[2] = source.Rxz ;
        result.m_data[3] = source.Xt ;
        result.m_data[4] = source.Ryx ;
        result.m_data[5] = source.Ryy ;
        result.m_data[6] = source.Ryz ;
        result.m_data[7] = source.Yt ;
        result.m_data[8] = source.Rzx ;
        result.m_data[9] = source.Rzy ;
        result.m_data[10] = source.Rzz ;
        result.m_data[11] = source.Zt ;
        result.m_data[12] = source.Sx ;
        result.m_data[13] = source.Sy ;
        result.m_data[14] = source.Sz ;
        result.m_data[15] = source.Ss ;
        return result ;
    }

    /** 转成 NX 的 4x4 矩阵; 两边内存布局一致, 逐分量拷贝。 */
    NXOpen::Matrix4x4 to_nx() const noexcept
    {
        NXOpen::Matrix4x4 result ;
        result.Rxx = m_data[0] ;
        result.Rxy = m_data[1] ;
        result.Rxz = m_data[2] ;
        result.Xt = m_data[3] ;
        result.Ryx = m_data[4] ;
        result.Ryy = m_data[5] ;
        result.Ryz = m_data[6] ;
        result.Yt = m_data[7] ;
        result.Rzx = m_data[8] ;
        result.Rzy = m_data[9] ;
        result.Rzz = m_data[10] ;
        result.Zt = m_data[11] ;
        result.Sx = m_data[12] ;
        result.Sy = m_data[13] ;
        result.Sz = m_data[14] ;
        result.Ss = m_data[15] ;
        return result ;
    }

    /** 和另一个矩阵交换内容。 */
    constexpr void swap(Matrix4x4& other) noexcept
    {
        for (int i = 0 ; i < 16 ; ++i)
        {
            const double temp = m_data[i] ;
            m_data[i] = other.m_data[i] ;
            other.m_data[i] = temp ;
        }
    }
} ;

/** 交换两个矩阵 (ADL 版本, 供 std::swap 泛化使用)。 */
constexpr void swap(Matrix4x4& first , Matrix4x4& second) noexcept
{
    first.swap(second) ;
}

static_assert(std::is_trivially_copyable_v<Matrix4x4> , "Matrix4x4 必须是平凡可拷贝类型") ;
static_assert(sizeof(Matrix4x4) == 16 * sizeof(double) , "Matrix4x4 不能有 padding") ;
static_assert(sizeof(Matrix4x4) == sizeof(NXOpen::Matrix4x4) , "Matrix4x4 要与 NXOpen::Matrix4x4 同尺寸") ;
static_assert(alignof(Matrix4x4) == alignof(NXOpen::Matrix4x4) , "Matrix4x4 要与 NXOpen::Matrix4x4 同对齐") ;

NX_NAMESPACE_END
)";
} // namespace template_math
