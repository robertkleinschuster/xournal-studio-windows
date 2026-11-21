# Verifies native menu items and tree-view colors across a dark/light round trip.
. C:\sbx\TestKit.ps1
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ThemeProbe {
    [DllImport("user32")] public static extern IntPtr SendMessage(IntPtr w, uint m, IntPtr a, IntPtr b);
    [DllImport("user32")] public static extern bool PostMessage(IntPtr w, uint m, IntPtr a, IntPtr b);
    [DllImport("user32",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32")] public static extern IntPtr GetMenu(IntPtr w);
    [DllImport("user32")] public static extern bool IsWindowVisible(IntPtr w);
    [DllImport("user32")] public static extern IntPtr GetSubMenu(IntPtr m, int item);
    [DllImport("user32")] public static extern uint GetMenuState(IntPtr m, uint item, uint flags);
}
"@
Invoke-Test {
    New-Item -ItemType Directory -Force "$out\config","$out\project" | Out-Null
    Copy-Item 'C:\sbx\data\Schaltungen.*' "$out\project"
    $env:XOURNAL_STUDIO_CONFIG_DIR = "$out\config"
    $env:XOURNAL_STUDIO_LOG = "$out\app.log"
    @{
        paneOpen = $true; paneTab = 0; darkMode = $true; projectFolder = "$out\project"
        mouseHintShown = $true
    } | ConvertTo-Json -Compress | Set-Content "$out\config\settings-native.json" -Encoding ascii
    Start-TestApp 'XournalStudio.exe' ('"' + "$out\project\Schaltungen.xopp" + '"')
    $mainMenu = [ThemeProbe]::GetMenu($script:h)
    $view = [ThemeProbe]::GetSubMenu($mainMenu,3)
    $dark = [ThemeProbe]::GetMenuState($view,409,0)
    if ($dark -eq [UInt32]::MaxValue -or ($dark -band 8) -eq 0) { throw "Dark mode is not checked: $dark" }
    if (($dark -band 0x100) -ne 0) { throw 'Dark command should keep native menu drawing.' }
    $pane = [ThemeProbe]::FindWindowEx($script:h,[IntPtr]::Zero,'XournalStudioSidePane',$null)
    if ($pane -eq [IntPtr]::Zero) { throw 'Side pane not found.' }
    $tabs = [ThemeProbe]::FindWindowEx($pane,[IntPtr]::Zero,'SysTabControl32',$null)
    if (![ThemeProbe]::IsWindowVisible($tabs)) { throw 'Native side pane tabs are hidden in dark mode.' }
    $tree = [ThemeProbe]::FindWindowEx($pane,[IntPtr]::Zero,'SysTreeView32',$null)
    if ($tree -eq [IntPtr]::Zero) { throw 'File tree not found.' }
    function TreeColors {
        foreach ($message in 0x111f, 0x1120) { # TVM_GETBKCOLOR and TVM_GETTEXTCOLOR
            [ThemeProbe]::SendMessage($tree,$message,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -band 0xffffffffL
        }
    }
    $darkColors = @(TreeColors)
    if ($darkColors[0] -ne 0x242120 -or $darkColors[1] -ne 0xf0eeee) { throw "Wrong dark tree colors: $darkColors" }
    Shot '01-dark'
    $status = [ThemeProbe]::FindWindowEx($script:h,[IntPtr]::Zero,'msctls_statusbar32',$null)
    $part = [ThemeProbe]::SendMessage($status,0x40c,[IntPtr]1,[IntPtr]::Zero).ToInt64() # SB_GETTEXTLENGTHW
    if (($part -band 0xffff) -eq 0 -or ($part -band 0x10000000) -ne 0) {
        throw 'Zoom status must expose its text through the native status bar in dark mode.'
    }
    ClickAnywhere 'Aufgaben'
    Shot '02-dark-tasks'
    if ([ThemeProbe]::SendMessage($tabs,0x130b,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 1) { # TCM_GETCURSEL
        throw 'Clicking Aufgaben did not select the native tab.'
    }
    ClickAnywhere 'Dateien'
    Shot '03-dark-files'
    if (![ThemeProbe]::IsWindowVisible($tree)) { throw 'Clicking Dateien did not reopen the tree.' }
    [ThemeProbe]::SendMessage($script:h,0x111,[IntPtr]409,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 300
    Shot '04-after-click'
    $lightColors = @(TreeColors)
    if ($lightColors[0] -ne 0xffffffffL -or $lightColors[1] -ne 0xffffffffL) {
        throw "Expected the tree-view system-color sentinel for both colors, got $lightColors"
    }
    $stored = (Get-Content "$out\config\settings-native.json" -Raw | ConvertFrom-Json).darkMode
    if ($stored) { throw 'Dark mode setting stayed enabled.' }
    if (![ThemeProbe]::IsWindowVisible($tabs)) { throw 'Native side pane tabs are hidden in light mode.' }
    $light = [ThemeProbe]::GetMenuState($view,409,0)
    if ($light -eq [UInt32]::MaxValue -or ($light -band 8) -ne 0) { throw "Dark mode is still checked: $light" }
    if (($light -band 0x100) -ne 0) { throw 'Light command did not restore native drawing.' }
    [ThemeProbe]::PostMessage($script:h,0x111,[IntPtr]409,[IntPtr]::Zero) | Out-Null
    Shot '05-back-to-dark'
    if (![ThemeProbe]::IsWindowVisible($tabs)) { throw 'Native side pane tabs did not survive the toggle.' }
    if ((@(TreeColors) -join ',') -ne ($darkColors -join ',')) { throw 'Dark tree colors did not restore.' }
    if (!(Get-Content "$out\config\settings-native.json" -Raw | ConvertFrom-Json).darkMode) { throw 'Dark appearance did not restore.' }
}
