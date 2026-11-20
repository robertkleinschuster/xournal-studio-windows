// ProjectFiles::Transfer: copy and move with the shell (IFileOperation), as File Explorer's paste does.

#include "xournal/project/ProjectFiles.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"

#include <shellapi.h> // FOF_* (WIN32_LEAN_AND_MEAN leaves it out)
#include <sherrors.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <format>
#include <optional>

using Microsoft::WRL::ComPtr;

namespace xournal::project::ProjectFiles
{
    namespace
    {
        std::string Name(path const& p) { return ToUtf8(p.native()); }

        // Collects where the shell put each item it copied or moved (IFileOperationProgressSink, the documented way to
        // learn the names it chose on a conflict). The other notifications need nothing.
        class TransferSink : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
            IFileOperationProgressSink>
        {
        public:
            /// Only the items asked for are recorded (the shell reports every file inside a copied folder too).
            explicit TransferSink(std::vector<std::wstring> requested) : items(std::move(requested)) {}
            std::vector<Transferred> Done() const { return done; }

            STDMETHODIMP PostMoveItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT result, IShellItem* created) override
            {
                return Record(item, result, created);
            }
            STDMETHODIMP PostCopyItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT result, IShellItem* created) override
            {
                return Record(item, result, created);
            }
            STDMETHODIMP StartOperations() override { return S_OK; }
            STDMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
            STDMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
            STDMETHODIMP PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
            STDMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
            STDMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
            STDMETHODIMP PreDeleteItem(DWORD, IShellItem*) override { return S_OK; }
            STDMETHODIMP PostDeleteItem(DWORD, IShellItem*, HRESULT, IShellItem*) override { return S_OK; }
            STDMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
            STDMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override { return S_OK; }
            STDMETHODIMP UpdateProgress(UINT, UINT) override { return S_OK; }
            STDMETHODIMP ResetTimer() override { return S_OK; }
            STDMETHODIMP PauseTimer() override { return S_OK; }
            STDMETHODIMP ResumeTimer() override { return S_OK; }

        private:
            static std::optional<path> PathOf(IShellItem* item)
            {
                if (!item) return std::nullopt;
                PWSTR name = nullptr;
                HRESULT hr = item->GetDisplayName(SIGDN_FILESYSPATH, &name);
                if (FAILED(hr))
                {
                    LogError(std::format("transfer: path of an item: {}", DescribeHr(hr)));
                    return std::nullopt;
                }
                path p = name;
                CoTaskMemFree(name);
                return p;
            }

            HRESULT Record(IShellItem* item, HRESULT result, IShellItem* created)
            {
                if (FAILED(result))
                {
                    LogError(std::format("transfer: an item was not copied or moved: {}", DescribeHr(result)));
                    return S_OK; // the shell reports it (and goes on with the others)
                }
                auto from = PathOf(item);
                auto to = PathOf(created);
                bool asked = from && std::ranges::any_of(items, [&](std::wstring const& i) { return Paths::Same(i, *from); });
                if (asked && to)
                {
                    done.push_back({ *from, *to });
                }
                return S_OK;
            }

            std::vector<std::wstring> items;
            std::vector<Transferred> done;
        };
    }

    std::vector<Transferred> Transfer(std::vector<path> const& items, path const& folder, bool move, HWND owner)
    {
        std::string step = std::format("{} {} item(s) to {}", move ? "move" : "copy", items.size(), Name(folder));
        std::wstring target = Paths::WithoutTrailingSeparator(folder).native();
        ComPtr<IShellItem> destination;
        Check(step + " (SHCreateItemFromParsingName)",
            SHCreateItemFromParsingName(target.c_str(), nullptr, IID_PPV_ARGS(&destination)));
        // Copies beside their original get File Explorer's new name ("Name - Kopie", FOF_RENAMEONCOLLISION); copies and
        // moves elsewhere keep the shell's question on a name conflict - two operations when both occur.
        std::vector<std::wstring> beside, elsewhere;
        for (path const& item : items)
        {
            std::wstring source = Paths::WithoutTrailingSeparator(item).native();
            bool sameFolder = Paths::Same(path(source).parent_path(), target);
            if (move && sameFolder)
            {
                continue; // already there
            }
            (sameFolder ? beside : elsewhere).push_back(std::move(source));
        }
        std::vector<std::wstring> requested = beside;
        requested.insert(requested.end(), elsewhere.begin(), elsewhere.end());
        auto sink = Microsoft::WRL::Make<TransferSink>(std::move(requested));
        if (!sink)
        {
            ThrowHr(step + " (progress sink)", E_OUTOFMEMORY);
        }
        // True when the user cancelled: the rest of the paste does not run either.
        auto perform = [&](std::vector<std::wstring> const& sources, DWORD extraFlags)
        {
            if (sources.empty())
            {
                return false;
            }
            ComPtr<IFileOperation> operation;
            Check(step + " (CoCreateInstance FileOperation)",
                CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation)));
            Check(step + " (SetOperationFlags)", operation->SetOperationFlags(FOF_ALLOWUNDO | FOFX_ADDUNDORECORD |
                FOF_NOCONFIRMMKDIR | extraFlags));
            if (owner)
            {
                Check(step + " (SetOwnerWindow)", operation->SetOwnerWindow(owner));
            }
            DWORD cookie = 0;
            Check(step + " (Advise)", operation->Advise(sink.Get(), &cookie));
            for (std::wstring const& source : sources)
            {
                ComPtr<IShellItem> shellItem;
                Check(std::format("{} (SHCreateItemFromParsingName {})", step, ToUtf8(source)),
                    SHCreateItemFromParsingName(source.c_str(), nullptr, IID_PPV_ARGS(&shellItem)));
                Check(step + (move ? " (MoveItem)" : " (CopyItem)"), move
                    ? operation->MoveItem(shellItem.Get(), destination.Get(), nullptr, nullptr)
                    : operation->CopyItem(shellItem.Get(), destination.Get(), nullptr, nullptr));
            }
            HRESULT hr = operation->PerformOperations();
            if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || hr == COPYENGINE_E_USER_CANCELLED)
            {
                Log(std::format("{}: cancelled by the user", step));
                return true;
            }
            Check(step, hr);
            BOOL aborted = FALSE;
            Check(step + " (GetAnyOperationsAborted)", operation->GetAnyOperationsAborted(&aborted));
            return aborted != FALSE;
        };
        if (!perform(beside, FOF_RENAMEONCOLLISION))
        {
            perform(elsewhere, 0);
        }
        return sink->Done();
    }
}
