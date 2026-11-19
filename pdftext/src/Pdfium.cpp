#include "Pdfium.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <windows.h>
#include <delayimp.h>
#include <fpdfview.h>

#include <filesystem>
#include <format>

namespace xournal::pdftext::detail
{
    namespace
    {
        std::once_flag once;
        bool ready = false;
        std::string error;

        // The delay-load helper reports a DLL or export it cannot find as an SEH exception (Microsoft Learn, "Error
        // handling and notification" of delay-loaded DLLs): taken here, everything else goes on.
        LONG DelayLoadFilter(DWORD code)
        {
            return code == VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND) ||
                           code == VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND)
                       ? EXCEPTION_EXECUTE_HANDLER
                       : EXCEPTION_CONTINUE_SEARCH;
        }

        // Binds all of pdfium.dll's imports now (documented: __HrLoadAllImportsForDll), so no later call can fail on
        // a missing export, and initialises the library. No C++ objects here (SEH).
        HRESULT BindAndInitialize(DWORD* exceptionCode)
        {
            __try
            {
                HRESULT hr = __HrLoadAllImportsForDll("pdfium.dll");
                if (FAILED(hr)) return hr;
                FPDF_LIBRARY_CONFIG config{};
                config.version = 2;
                FPDF_InitLibraryWithConfig(&config);
                return S_OK;
            }
            __except (DelayLoadFilter(*exceptionCode = GetExceptionCode()))
            {
                return HRESULT_FROM_WIN32(*exceptionCode & 0xFFFF);
            }
        }

        void Load()
        {
            wchar_t exe[MAX_PATH] = L"";
            DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
            if (length == 0 || length == MAX_PATH)
            {
                error = std::format("PDF text: the exe's folder is unknown ({})", DescribeHr(HRESULT_FROM_WIN32(GetLastError())));
                return;
            }
            auto dll = std::filesystem::path(exe).parent_path() / L"pdfium.dll";
            // Only from the exe's folder (and system DLLs for its own imports) - never from the search path.
            if (!LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32))
            {
                error = std::format("PDF text: pdfium.dll cannot be loaded from the app's folder ({}); search is off",
                                    DescribeHr(HRESULT_FROM_WIN32(GetLastError())));
                return;
            }
            DWORD code = 0;
            HRESULT hr = BindAndInitialize(&code);
            if (FAILED(hr))
            {
                error = std::format("PDF text: pdfium.dll does not provide what the app needs ({}); search is off", DescribeHr(hr));
                return;
            }
            ready = true;
        }
    }

    bool EnsurePdfium()
    {
        std::call_once(once, [] {
            Load();
            if (ready) Log("PDF text: PDFium loaded");
            else LogError(error);
        });
        return ready;
    }

    std::string const& PdfiumError() { return error; }

    std::mutex& PdfiumLock()
    {
        static std::mutex lock;
        return lock;
    }

    std::string LastPdfiumError()
    {
        switch (FPDF_GetLastError())
        {
        case FPDF_ERR_SUCCESS: return "no error";
        case FPDF_ERR_FILE: return "file not found or not readable";
        case FPDF_ERR_FORMAT: return "not a PDF or damaged";
        case FPDF_ERR_PASSWORD: return "password required";
        case FPDF_ERR_SECURITY: return "unsupported security scheme";
        case FPDF_ERR_PAGE: return "page not found or damaged";
        default: return std::format("PDFium error {}", FPDF_GetLastError());
        }
    }
}
