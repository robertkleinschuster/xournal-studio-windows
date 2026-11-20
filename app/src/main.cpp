// Xournal Studio for Windows (native): the entry point. Sets up the log, COM (the UI thread is an STA; the process keeps
// an MTA for work on the thread pool), the common controls and the settings, then runs the main window.
// Command line: a document to open (otherwise the last one).

#include "AppFolder.h"
#include "MainWindow.h"

#include "inkpages/UiDispatch.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/settings/Settings.h"

#include <windows.h>
#include <combaseapi.h>
#include <commctrl.h>
#include <shellapi.h>

#include <winrt/base.h>

#include <format>

namespace
{
    std::optional<std::filesystem::path> StartFile()
    {
        int count = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments)
        {
            xournal::LogError("CommandLineToArgvW failed");
            return std::nullopt;
        }
        std::optional<std::filesystem::path> file;
        if (count > 1)
        {
            file = std::filesystem::path(arguments[1]);
        }
        LocalFree(arguments);
        return file;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    try
    {
        xournal::app::AppFolder::SetUpLogging();
        xournal::Log("app: start");
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        inkpages::UiDispatcherScope dispatcher;
        CO_MTA_USAGE_COOKIE mta{};
        winrt::check_hresult(CoIncrementMTAUsage(&mta)); // thread pool work (PDF sizes, loading) runs in the implicit MTA

        INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES };
        if (!InitCommonControlsEx(&controls))
        {
            xournal::LogError("InitCommonControlsEx failed");
        }

        auto settings = xournal::settings::Settings::Load(xournal::settings::Settings::FileIn(xournal::app::AppFolder::Path()));
        xournal::app::MainWindow window(instance, std::move(settings), StartFile());
        int code = window.Run();
        xournal::Log("app: end");
        return code;
    }
    catch (...)
    {
        xournal::LogException("app");
        MessageBoxW(nullptr, L"Xournal Studio konnte nicht gestartet werden. Einzelheiten stehen in errors.log.",
            L"Xournal Studio", MB_ICONERROR | MB_OK);
        return 1;
    }
}
