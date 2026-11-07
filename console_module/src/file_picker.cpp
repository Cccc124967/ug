#include "../console_control.h"
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <memory>
#include <sstream>

namespace
{
/** STA 生命周期；初始化失败时不借用不兼容的 COM 单元。 */
class ComApartment
{
    public:
    ComApartment() : m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))
    {
    }
    ~ComApartment()
    {
        if (SUCCEEDED(m_result))
        {
            CoUninitialize() ;
        }
    }
    ComApartment(const ComApartment&) = delete ;
    /** 禁止复制 COM 生命周期。 */
    ComApartment& operator=(const ComApartment&) = delete ;
    /** 取得 COM 初始化状态。 */
    HRESULT result() const noexcept
    {
        return m_result ;
    }

    private:
    HRESULT m_result ;
} ;

/** 保留系统错误码与失败阶段，避免把系统故障误报为用户取消。 */
console::FilePickerResult picker_error(HRESULT result, const char* stage)
{
    std::ostringstream message ;
    message << "文件选择器失败（" << stage << "，0x" << std::hex
            << static_cast<unsigned long>(result) << "）" ;
    return { console::FilePickerStatus::Failed, {}, message.str() } ;
}

/** 选择原生对话框的所有者窗口；隐藏的伪控制台窗口不能当所有者。 */
HWND picker_owner()
{
    const HWND console_window = GetConsoleWindow() ;
    if (console_window != nullptr && IsWindow(console_window) != FALSE &&
        IsWindowVisible(console_window) != FALSE)
    {
        return console_window ;
    }
    return nullptr ;
}

/** 打开对话框；控制台窗口不可用或首次失败时退化为无所有者重试。 */
HRESULT show_dialog(IFileOpenDialog* dialog, HWND owner)
{
    HRESULT status = dialog->Show(owner) ;
    if (FAILED(status) && status != HRESULT_FROM_WIN32(ERROR_CANCELLED) && owner != nullptr)
    {
        status = dialog->Show(nullptr) ;
    }
    return status ;
}
} // namespace

namespace console
{
FilePickerResult pick_file(const FilePickerOptions& options)
{
    try
    {
        ComApartment apartment ;
        if (FAILED(apartment.result()))
        {
            return picker_error(apartment.result(), "COM 初始化") ;
        }
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog ;
        HRESULT status = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(dialog.GetAddressOf())) ;
        if (FAILED(status))
        {
            return picker_error(status, "创建对话框") ;
        }
        DWORD flags = 0 ;
        status = dialog->GetOptions(&flags) ;
        if (FAILED(status))
        {
            return picker_error(status, "读取选项") ;
        }
        flags |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR | FOS_DONTADDTORECENT ;
        flags |= options.m_mode == FilePickerMode::Directory ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST ;
        status = dialog->SetOptions(flags) ;
        if (FAILED(status))
        {
            return picker_error(status, "设置选项") ;
        }
        const auto title = core::utf8_to_wide(options.m_title) ;
        if (!title.empty() && FAILED(status = dialog->SetTitle(title.c_str())))
        {
            return picker_error(status, "设置标题") ;
        }
        if (!options.m_initial_directory.empty())
        {
            Microsoft::WRL::ComPtr<IShellItem> directory ;
            if (SUCCEEDED(SHCreateItemFromParsingName(options.m_initial_directory.c_str(), nullptr,
                    IID_PPV_ARGS(directory.GetAddressOf()))))
            {
                // 无效起始目录不影响正常浏览，系统会使用默认位置。
                dialog->SetFolder(directory.Get()) ;
            }
        }
        std::vector<std::wstring> names ;
        std::vector<std::wstring> patterns ;
        std::vector<COMDLG_FILTERSPEC> filters ;
        if (options.m_mode == FilePickerMode::File && !options.m_filters.empty())
        {
            names.reserve(options.m_filters.size()) ;
            patterns.reserve(options.m_filters.size()) ;
            for (const auto& filter : options.m_filters)
            {
                if (filter.m_name.empty() || filter.m_pattern.empty())
                {
                    return picker_error(E_INVALIDARG, "过滤条件") ;
                }
                names.push_back(core::utf8_to_wide(filter.m_name)) ;
                patterns.push_back(core::utf8_to_wide(filter.m_pattern)) ;
            }
            for (std::size_t index = 0 ; index < names.size() ; ++index)
            {
                filters.push_back({ names[index].c_str(), patterns[index].c_str() }) ;
            }
            status = dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data()) ;
            if (FAILED(status))
            {
                return picker_error(status, "设置文件类型") ;
            }
        }
        status = show_dialog(dialog.Get(), picker_owner()) ;
        if (status == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        {
            return {} ;
        }
        if (FAILED(status))
        {
            return picker_error(status, "打开对话框") ;
        }
        Microsoft::WRL::ComPtr<IShellItem> item ;
        status = dialog->GetResult(item.GetAddressOf()) ;
        if (FAILED(status))
        {
            return picker_error(status, "获取选择结果") ;
        }
        wchar_t* raw_path = nullptr ;
        status = item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path) ;
        const auto release = [](wchar_t* text)
        {
            CoTaskMemFree(text) ;
        } ;
        std::unique_ptr<wchar_t, decltype(release)> path(raw_path, release) ;
        if (FAILED(status) || !path)
        {
            return picker_error(FAILED(status) ? status : E_FAIL, "读取路径") ;
        }
        return { FilePickerStatus::Selected, std::filesystem::path(path.get()), {} } ;
    }
    catch (const std::exception& error)
    {
        return { FilePickerStatus::Failed, {}, error.what() } ;
    }
}
} // namespace console
