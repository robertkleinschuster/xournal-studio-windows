#include "NativeTheme.h"

#include "DarkColors.h"

#include <Darkmodelib.h>

namespace xournal::app::NativeTheme
{
    void ApplyMainWindow(HWND window, bool dark)
    {
        dmlib::initDarkMode();
        auto mode = dark ? dmlib::DarkModeType::dark : dmlib::DarkModeType::classic;
        dmlib::setDarkModeConfigEx(static_cast<UINT>(mode));
        dmlib::setDefaultColors(false);
        if (dark)
        {
            dmlib::setBackgroundColor(DarkColors::Background);
            dmlib::setCtrlBackgroundColor(DarkColors::Input);
            dmlib::setDlgBackgroundColor(DarkColors::Background);
            dmlib::setHotBackgroundColor(DarkColors::Selected);
            dmlib::setTextColor(DarkColors::Text);
            dmlib::setViewBackgroundColor(DarkColors::Input);
            dmlib::setViewTextColor(DarkColors::Text);
        }
        dmlib::updateThemeBrushesAndPens();
        dmlib::updateCommonDlgsBrushes();
        dmlib::calculateTreeViewStyle();
        dmlib::setDarkWndSafe(window);
        dmlib::setPrevTreeViewStyle();
        if (dark) dmlib::setWindowMenuBarSubclass(window);
        else dmlib::removeWindowMenuBarSubclass(window);
    }

    void ApplyDialog(HWND dialog)
    {
        dmlib::setDarkWndSafe(dialog);
    }

    HRESULT ShowTaskDialog(TASKDIALOGCONFIG const& config, int* button)
    {
        if (dmlib::isEnabled())
            return dmlib::darkTaskDialogIndirect(&config, button, nullptr, nullptr);
        return TaskDialogIndirect(&config, button, nullptr, nullptr);
    }
}
