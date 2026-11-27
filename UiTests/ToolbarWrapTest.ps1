# The native toolbar wraps on resize and the writing surface follows its height.
. C:\sbx\TestKit.ps1
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class WrapProbe {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int left, top, right, bottom; }
    [DllImport("user32")] static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32", SetLastError=true)] static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int w, int h, uint flags);
    [DllImport("user32", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string name);
    [DllImport("user32", SetLastError=true)] static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32", CharSet=CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr hwnd, int message, IntPtr wp, IntPtr lp);
    public static void Resize(IntPtr hwnd, int width) {
        ShowWindow(hwnd, 9);
        if (!SetWindowPos(hwnd, IntPtr.Zero, 20, 20, width, 650, 0x0014)) throw new System.ComponentModel.Win32Exception();
    }
    public static int Rows(IntPtr toolbar) { return SendMessage(toolbar, 1024 + 40, IntPtr.Zero, IntPtr.Zero).ToInt32(); } // TB_GETROWS
    public static void ToggleLargeIcons(IntPtr window) { SendMessage(window, 0x111, (IntPtr)410, IntPtr.Zero); } // WM_COMMAND, CmdLargeToolbarIcons
    public static Rect Bounds(IntPtr hwnd) {
        Rect rect;
        if (!GetWindowRect(hwnd, out rect)) throw new System.ComponentModel.Win32Exception();
        return rect;
    }
}
'@
Invoke-Test {
    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    New-Item -ItemType Directory -Force "$out\config" | Out-Null
    Set-Content "$out\config\settings-native.json" '{"largeToolbarIcons":false,"paneOpen":false}'
    Start-TestApp 'XournalStudio.exe'
    $container = [WrapProbe]::FindWindowEx($script:h, [IntPtr]::Zero, 'InkPagesToolbar', $null)
    $bar = [WrapProbe]::FindWindowEx($container, [IntPtr]::Zero, 'ToolbarWindow32', $null)
    $surface = [WrapProbe]::FindWindowEx($script:h, [IntPtr]::Zero, 'InkPagesWritingSurface', $null)
    # The tab strip lies between the toolbar and the writing surface (a direct child: the side pane's tabs are not).
    $tabs = [WrapProbe]::FindWindowEx($script:h, [IntPtr]::Zero, 'SysTabControl32', $null)
    if (!$container -or !$bar -or !$surface -or !$tabs) { throw 'Could not find toolbar, tab strip or writing surface' }
    foreach ($width in 1400, 700, 490, 1400) {
        [WrapProbe]::Resize($script:h, $width)
        Start-Sleep -Milliseconds 350
        $rows = [WrapProbe]::Rows($bar)
        $barBounds = [WrapProbe]::Bounds($container)
        $surfaceBounds = [WrapProbe]::Bounds($surface)
        "width=$width, rows=$rows, toolbar height=$($barBounds.bottom - $barBounds.top), surface top=$($surfaceBounds.top)"
        if (($width -eq 1400 -and $rows -ne 1) -or ($width -ne 1400 -and $rows -lt 2)) { throw "Unexpected number of rows: $rows" }
        $tabBounds = [WrapProbe]::Bounds($tabs)
        if ($barBounds.bottom -ne $tabBounds.top -or $tabBounds.bottom -ne $surfaceBounds.top) { throw 'Toolbar, tab strip and writing surface do not meet' }
        $height = $barBounds.bottom - $barBounds.top
        if (!$singleHeight) { $singleHeight = $height }
        if ($width -eq 1400 -and $height -ne $singleHeight) { throw 'The toolbar did not return to its single-row height' }
        if ($width -ne 1400 -and $height -le $singleHeight) { throw 'Wrapped rows are clipped by the toolbar container' }
        if ($width -eq 490) { Shot 'wrapped' }
    }
    [WrapProbe]::ToggleLargeIcons($script:h)
    [WrapProbe]::Resize($script:h, 700)
    $largeRows = [WrapProbe]::Rows($bar)
    "large icons, width=700, rows=$largeRows"
    if ($largeRows -lt 2) { throw 'Large toolbar icons did not wrap' }
    if ([WrapProbe]::Bounds($container).bottom -ne [WrapProbe]::Bounds($tabs).top) { throw 'Large toolbar overlaps the tab strip' }
    Shot 'large-icons'
}
