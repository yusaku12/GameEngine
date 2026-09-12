#include "Pch.h"
#include "Core\System\Dialog.h"

#include <shobjidl.h>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")

namespace Engine
{
    namespace
    {
        class ScopedComInitialization final
        {
        public:
            ScopedComInitialization() noexcept
                : m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))
            {
            }

            ~ScopedComInitialization()
            {
                if (SUCCEEDED(m_result))
                    CoUninitialize();
            }

            GE_DISABLE_COPY_AND_MOVE(ScopedComInitialization);

            bool succeeded() const noexcept { return SUCCEEDED(m_result); }

        private:
            HRESULT m_result;
        };

        DialogResult toDialogResult(const HRESULT result) noexcept
        {
            return result == HRESULT_FROM_WIN32(ERROR_CANCELLED) ? DialogResult::Cancel : DialogResult::Error;
        }

        HRESULT readShellItemPath(IShellItem& item, std::filesystem::path& outPath)
        {
            PWSTR rawPath = nullptr;
            const HRESULT result = item.GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
            if (FAILED(result))
                return result;

            const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> ownedPath(rawPath, &CoTaskMemFree);
            outPath = ownedPath.get();
            return S_OK;
        }
    }

    HRESULT Dialog::configure(
        IFileDialog& dialog,
        const std::wstring_view title,
        const std::filesystem::path& initialPath,
        const std::span<const FileDialogFilter> filters)
    {
        if (!title.empty())
        {
            const std::wstring titleString(title);
            if (const HRESULT result = dialog.SetTitle(titleString.c_str()); FAILED(result))
                return result;
        }

        std::vector<COMDLG_FILTERSPEC> filterSpecs;
        filterSpecs.reserve(filters.size());
        for (const FileDialogFilter& filter : filters)
        {
            if (filter.name != nullptr && filter.pattern != nullptr)
                filterSpecs.push_back({ filter.name, filter.pattern });
        }
        if (!filterSpecs.empty())
        {
            if (const HRESULT result = dialog.SetFileTypes(
                static_cast<UINT>(filterSpecs.size()), filterSpecs.data()); FAILED(result))
                return result;
        }

        if (initialPath.empty())
            return S_OK;

        std::error_code error;
        const bool isDirectory = std::filesystem::is_directory(initialPath, error);
        const std::filesystem::path folderPath = isDirectory ? initialPath : initialPath.parent_path();
        if (!isDirectory && !initialPath.filename().empty())
        {
            if (const HRESULT result = dialog.SetFileName(initialPath.filename().c_str()); FAILED(result))
                return result;
        }
        if (folderPath.empty())
            return S_OK;

        std::filesystem::path absoluteFolder = std::filesystem::absolute(folderPath, error);
        if (error)
            return S_OK;

        while (!std::filesystem::is_directory(absoluteFolder, error))
        {
            error.clear();
            const std::filesystem::path parent = absoluteFolder.parent_path();
            if (parent.empty() || parent == absoluteFolder)
                return S_OK;
            absoluteFolder = parent;
        }

        Microsoft::WRL::ComPtr<IShellItem> folder;
        const HRESULT result = SHCreateItemFromParsingName(
            absoluteFolder.c_str(), nullptr, IID_PPV_ARGS(folder.GetAddressOf()));
        return SUCCEEDED(result) ? dialog.SetFolder(folder.Get()) : S_OK;
    }

    DialogResult Dialog::openFile(
        std::vector<std::filesystem::path>& outPaths,
        const std::wstring_view title,
        const std::filesystem::path& initialPath,
        const std::span<const FileDialogFilter> filters,
        const bool multiSelect,
        const HWND ownerWindow)
    {
        outPaths.clear();
        const ScopedComInitialization com;
        if (!com.succeeded())
            return DialogResult::Error;

        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        HRESULT result = CoCreateInstance(
            CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()));
        if (FAILED(result))
            return DialogResult::Error;

        DWORD options = 0;
        result = dialog->GetOptions(&options);
        if (FAILED(result))
            return DialogResult::Error;
        options |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
        if (multiSelect)
            options |= FOS_ALLOWMULTISELECT;
        if (FAILED(dialog->SetOptions(options)) || FAILED(configure(*dialog.Get(), title, initialPath, filters)))
            return DialogResult::Error;

        result = dialog->Show(ownerWindow);
        if (FAILED(result))
            return toDialogResult(result);

        Microsoft::WRL::ComPtr<IShellItemArray> items;
        result = dialog->GetResults(items.GetAddressOf());
        if (FAILED(result))
            return DialogResult::Error;

        DWORD count = 0;
        if (FAILED(items->GetCount(&count)))
            return DialogResult::Error;
        outPaths.reserve(count);
        for (DWORD index = 0; index < count; ++index)
        {
            Microsoft::WRL::ComPtr<IShellItem> item;
            std::filesystem::path path;
            if (FAILED(items->GetItemAt(index, item.GetAddressOf())) || FAILED(readShellItemPath(*item.Get(), path)))
            {
                outPaths.clear();
                return DialogResult::Error;
            }
            outPaths.push_back(std::move(path));
        }
        return DialogResult::Ok;
    }

    DialogResult Dialog::saveFile(
        std::filesystem::path& outPath,
        const std::wstring_view title,
        const std::filesystem::path& initialPath,
        const std::wstring_view defaultExtension,
        const std::span<const FileDialogFilter> filters,
        const HWND ownerWindow)
    {
        outPath.clear();
        const ScopedComInitialization com;
        if (!com.succeeded())
            return DialogResult::Error;

        Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;
        HRESULT result = CoCreateInstance(
            CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()));
        if (FAILED(result))
            return DialogResult::Error;

        DWORD options = 0;
        result = dialog->GetOptions(&options);
        if (FAILED(result))
            return DialogResult::Error;
        options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT;
        if (FAILED(dialog->SetOptions(options)) || FAILED(configure(*dialog.Get(), title, initialPath, filters)))
            return DialogResult::Error;

        if (!defaultExtension.empty())
        {
            const std::wstring extension(defaultExtension.front() == L'.'
                ? defaultExtension.substr(1) : defaultExtension);
            if (FAILED(dialog->SetDefaultExtension(extension.c_str())))
                return DialogResult::Error;
        }

        result = dialog->Show(ownerWindow);
        if (FAILED(result))
            return toDialogResult(result);

        Microsoft::WRL::ComPtr<IShellItem> item;
        if (FAILED(dialog->GetResult(item.GetAddressOf())) || FAILED(readShellItemPath(*item.Get(), outPath)))
            return DialogResult::Error;
        return DialogResult::Ok;
    }
} // namespace Engine