#pragma once

#include <windows.h>
#include <commctrl.h>

namespace xournal::app::NativeTheme
{
    void ApplyMainWindow(HWND window, bool dark);
    void ApplyDialog(HWND dialog);
    HRESULT ShowTaskDialog(TASKDIALOGCONFIG const& config, int* button);
}
