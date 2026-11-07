#include "../build_plan.h"
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <array>
#include <cstddef>
#include <system_error>

namespace scan
{
namespace
{
/** 判断是否为已存在的常规文件。 */
bool file_exists(const Path& path)
{
    std::error_code error ;
    return std::filesystem::is_regular_file(path, error) ;
}
} // namespace

bool BuildPlan::needs_generated_toolchain() const noexcept
{
    return m_kind == BuildPlanKind::GeneratedToolchain ;
}

bool BuildPlan::uses_ninja() const noexcept
{
    return m_generator == "Ninja" ;
}

std::filesystem::path find_nmake(const Compiler& compiler)
{
    if (!compiler.m_cl_path.empty())
    {
        const Path beside_cl = compiler.m_cl_path.parent_path() / L"nmake.exe" ;
        if (file_exists(beside_cl))
        {
            return beside_cl ;
        }
    }
    std::array<wchar_t, 32768> buffer = {} ;
    const DWORD length = SearchPathW(nullptr, L"nmake.exe", nullptr,
        static_cast<DWORD>(buffer.size()), buffer.data(), nullptr) ;
    if (length > 0 && length < buffer.size())
    {
        return Path(std::wstring(buffer.data(), length)) ;
    }
    return {} ;
}

BuildPlan resolve_build_plan(const Compiler& compiler, bool ninja_available)
{
    BuildPlan plan ;
    if (compiler.m_cl_path.empty())
    {
        plan.m_message = "编译器条目里没有 cl.exe 路径，无法确定构建方案。" ;
        return plan ;
    }
    // 1) 现成工具链文件（便携工具链常见）：直接用，生成器交给 Ninja / NMake。
    const bool has_toolchain_file = !compiler.m_vcvars_path.empty() &&
        compiler.m_vcvars_path.extension() == L".cmake" ;
    if (has_toolchain_file)
    {
        plan.m_kind = BuildPlanKind::ToolchainFile ;
        plan.m_toolchain_file = compiler.m_vcvars_path ;
    }
    // 2) 完整 VS 安装：Visual Studio 生成器，不需要 Ninja。
    else if (!compiler.m_generator.empty())
    {
        plan.m_kind = BuildPlanKind::VisualStudio ;
        plan.m_generator = compiler.m_generator ;
        plan.m_single_config = false ;
        plan.m_ok = true ;
        return plan ;
    }
    // 3) 其余：由 cl + Windows SDK 自动生成 toolchain 文件。
    else
    {
        plan.m_kind = BuildPlanKind::GeneratedToolchain ;
    }

    if (ninja_available)
    {
        plan.m_generator = "Ninja" ;
        plan.m_single_config = true ;
        plan.m_ok = true ;
        return plan ;
    }
    const Path nmake = find_nmake(compiler) ;
    if (!nmake.empty())
    {
        plan.m_generator = "NMake Makefiles" ;
        plan.m_single_config = true ;
        plan.m_make_program = nmake ;
        plan.m_ok = true ;
        return plan ;
    }
    plan.m_kind = BuildPlanKind::Unresolved ;
    plan.m_message = "本机既没有 Ninja 也没有 nmake，无法生成可构建工程；"
        "请安装 Ninja，或改用带 nmake.exe 的 MSVC 工具集。" ;
    return plan ;
}
} // namespace scan
