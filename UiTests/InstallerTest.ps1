# The Inno Setup installer in Windows Sandbox: a silent setup of an older version, then the new one over it (one
# Apps & Features entry left, the new version), files, Start menu shortcut, Apps & Features entry, the app started from the
# shortcut, a silent uninstall that leaves nothing of the installer (the user's data stays), then a setup without silent
# flags (one page, a click on Installieren, then progress) and its uninstall.
#   installer\Build-Installer.ps1 -Version 0.1.99 -Out <old.exe>; installer\Build-Installer.ps1 -Version 0.2.0 -Out <new.exe>
#   UiTests\Run-AppTest.ps1 -Test UiTests\InstallerTest.ps1 -Extra <old.exe>,<new.exe> -TimeoutSeconds 1200
# Every check prints "shot check: ..." (the host shows those lines of the transcript).
. C:\sbx\TestKit.ps1

$appId = '{B3C4C7F5-6B0A-4D2E-9A53-5E0F7C6D1A42}_is1'
$entry = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$appId"
$folder = Join-Path $env:LOCALAPPDATA 'Programs\Xournal Studio (Win32)'
$programs = Join-Path $env:LOCALAPPDATA 'Programs'
$link = Join-Path ([Environment]::GetFolderPath('Programs')) 'Xournal Studio (Win32).lnk'
$userData = Join-Path $env:LOCALAPPDATA 'XournalStudioForWindows'

function Check($what, [bool]$ok) { if (-not $ok) { Shot ("failed-" + ($what -replace '[^A-Za-z0-9]', '_')) | Out-Null; throw "check failed: $what" }; "shot check: $what" }
function Run([string]$exe, [string]$arguments) {
    $p = Start-Process $exe -ArgumentList $arguments -PassThru
    if (-not $p.WaitForExit(360000)) { throw "$exe $arguments did not finish" }
    "shot $([IO.Path]::GetFileName($exe)) $arguments -> $($p.ExitCode)"
    if ($p.ExitCode -ne 0) { throw "$exe $arguments failed ($($p.ExitCode))" }
}
function Setup([string]$exe, [string]$log, [string]$flags = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART') { Run $exe "$flags /LOG=`"$out\$log.log`"" }
function Uninstall([string]$log) {
    $u = (Get-ItemProperty $entry).UninstallString.Trim('"')
    Run $u "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=`"$out\$log.log`""
    # (the uninstaller continues in a copy of itself: wait until the entry and the folder are gone)
    for ($i = 0; $i -lt 60 -and ((Test-Path $entry) -or (Test-Path $folder)); $i++) { Start-Sleep -Milliseconds 500 }
}
function Snapshot { if (Test-Path $folder) { @(Get-ChildItem -Recurse -Force $folder | Where-Object Name -notlike 'unins000.*' | ForEach-Object { $_.FullName.Substring($folder.Length) }) } else { @() } }

Invoke-Test {
    $old = (Get-ChildItem C:\sbx\data\*.exe | Where-Object Name -like '*0.1.99*').FullName
    $new = (Get-ChildItem C:\sbx\data\*.exe | Where-Object { $_.Name -like '*setup.exe' -and $_.Name -notlike '*0.1.99*' }).FullName
    $programsExisted = Test-Path $programs
    "shot check: Programs folder existed before: $programsExisted"

    # 1. Silent setup of the old version, then the new one over it.
    Setup $old 'install-old'
    Check 'old version registered' ((Get-ItemProperty $entry).DisplayVersion -eq '0.1.99')
    Setup $new 'install-new'
    $info = Get-ItemProperty $entry
    Check 'version 0.2.0 after the upgrade' ($info.DisplayVersion -eq '0.2.0')
    Check "one Apps & Features entry" (@(Get-ChildItem 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' | Where-Object { (Get-ItemProperty $_.PSPath).DisplayName -like 'Xournal Studio*' }).Count -eq 1)
    Check "publisher ($($info.Publisher))" ($info.Publisher -eq 'Robert Kleinschuster')
    Check "Apps & Features icon ($($info.DisplayIcon))" ($info.DisplayIcon -like '*XournalStudio.exe*')
    Check 'install folder' (Test-Path (Join-Path $folder 'XournalStudio.exe'))
    $files = Snapshot
    $files | ForEach-Object { "file $_" }
    Check 'pdfium.dll and licenses' ((Test-Path (Join-Path $folder 'pdfium.dll')) -and (Test-Path (Join-Path $folder 'licenses\THIRD-PARTY.md')) -and (Test-Path (Join-Path $folder 'licenses\pdfium')))
    Check 'Start menu shortcut' (Test-Path $link)
    $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($link)
    "shot check: shortcut target $($shortcut.TargetPath), icon $($shortcut.IconLocation)"
    Check 'shortcut target' ($shortcut.TargetPath -eq (Join-Path $folder 'XournalStudio.exe'))

    # 2. The app from the shortcut: its window, closed with exit code 0.
    Start-Process $link
    $script:p = $null
    for ($i = 0; $i -lt 40 -and -not $script:p; $i++) {
        Start-Sleep -Milliseconds 500
        $script:p = Get-Process XournalStudio -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$folder\*" } | Select-Object -First 1
    }
    Check 'app process from the shortcut' ([bool]$script:p)
    $null = $script:p.Handle   # (a process found by Get-Process keeps its exit code only with a handle opened before it exits)
    for ($i = 0; $i -lt 40 -and $script:p.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 500; $script:p.Refresh() }
    Check 'app window' ($script:p.MainWindowHandle -ne 0)
    Start-Sleep 3
    Shot '01-started-from-shortcut'
    $script:p.CloseMainWindow() | Out-Null
    Check 'app closed' ($script:p.WaitForExit(10000))
    Check "exit code $($script:p.ExitCode)" ($script:p.ExitCode -eq 0)
    $script:p = $null   # (Shot would report the closed app)
    Check 'user data folder written by the app' (Test-Path $userData)
    Set-Content (Join-Path $userData 'marker.txt') 'user data'

    # 3. Silent uninstall: nothing of the installer left, the user's data kept.
    Uninstall 'uninstall'
    Check 'no Apps & Features entry' (-not (Test-Path $entry))
    Check 'install folder removed' (-not (Test-Path $folder))
    Check 'shortcut removed' (-not (Test-Path $link))
    "shot check: Programs folder exists after uninstall: $(Test-Path $programs)"
    if (-not $programsExisted) { Check 'Programs folder removed (the installer created it)' (-not (Test-Path $programs)) }
    Check 'user data kept' (Test-Path (Join-Path $userData 'marker.txt'))

    # 4. A setup without silent flags (a double click): one page (Bereit zur Installation), a click on Installieren, then progress.
    $p = Start-Process $new -ArgumentList "/LOG=`"$out\install-ui.log`"" -PassThru
    Start-Sleep -Milliseconds 1500
    [Inj]::Shot("$out\02-install-ui.png"); 'shot 02-install-ui'
    # The wizard's button "Installieren" (a Delphi TNewButton; at 150 % the wizard is larger than the sandbox's screen, so
    # the button lies below the visible area): a click message to the button itself.
    $wizard = Get-Process | Where-Object { $_.MainWindowTitle -like 'Setup - Xournal Studio*' } | Select-Object -First 1
    Check 'setup wizard window' ([bool]$wizard)
    Add-Type -Namespace Native -Name Buttons -MemberDefinition @'
public delegate bool EnumProc(System.IntPtr h, System.IntPtr l);
[System.Runtime.InteropServices.DllImport("user32")] public static extern bool EnumChildWindows(System.IntPtr parent, EnumProc cb, System.IntPtr l);
[System.Runtime.InteropServices.DllImport("user32")] public static extern bool EnumWindows(EnumProc cb, System.IntPtr l);
[System.Runtime.InteropServices.DllImport("user32", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] public static extern int GetClassName(System.IntPtr h, System.Text.StringBuilder s, int n);
[System.Runtime.InteropServices.DllImport("user32", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] public static extern int GetWindowText(System.IntPtr h, System.Text.StringBuilder s, int n);
[System.Runtime.InteropServices.DllImport("user32")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[System.Runtime.InteropServices.DllImport("user32")] public static extern bool PostMessage(System.IntPtr h, uint msg, System.IntPtr w, System.IntPtr l);
'@
    # Every window of the wizard's process (top level and children): class, text, handle.
    $all = New-Object System.Collections.ArrayList
    $collect = [Native.Buttons+EnumProc] {
        param($h, $l)
        $c = New-Object System.Text.StringBuilder 64; $t = New-Object System.Text.StringBuilder 128
        [Native.Buttons]::GetClassName($h, $c, 64) | Out-Null; [Native.Buttons]::GetWindowText($h, $t, 128) | Out-Null
        $all.Add([pscustomobject]@{ H = $h; Class = "$c"; Text = "$t" }) | Out-Null
        $true
    }
    $tops = New-Object System.Collections.ArrayList
    $topProc = [Native.Buttons+EnumProc] {
        param($h, $l)
        $pid2 = [uint32]0; [Native.Buttons]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
        if ($pid2 -eq $wizard.Id) { $tops.Add($h) | Out-Null }
        $true
    }
    [Native.Buttons]::EnumWindows($topProc, [IntPtr]::Zero) | Out-Null
    foreach ($top in $tops) {
        $c = New-Object System.Text.StringBuilder 64; $t = New-Object System.Text.StringBuilder 128
        [Native.Buttons]::GetClassName($top, $c, 64) | Out-Null; [Native.Buttons]::GetWindowText($top, $t, 128) | Out-Null
        $all.Add([pscustomobject]@{ H = $top; Class = "$c"; Text = "$t" }) | Out-Null
        [Native.Buttons]::EnumChildWindows($top, $collect, [IntPtr]::Zero) | Out-Null
    }
    $all | ForEach-Object { "shot window $($_.Class) '$($_.Text)'" }
    $found = @($all | Where-Object { $_.Class -match 'Button' -and $_.Text -like '*Installieren*' } | Select-Object -First 1)
    $button = if ($found.Count) { $found[0].H } else { [IntPtr]::Zero }
    Check 'button Installieren' ($button -ne [IntPtr]::Zero)
    [Native.Buttons]::PostMessage($button, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # BM_CLICK
    Check 'setup finished after one click' ($p.WaitForExit(360000))
    Check "setup exit code $($p.ExitCode)" ($p.ExitCode -eq 0)
    Check 'installed' ((Test-Path $entry) -and (Test-Path (Join-Path $folder 'XournalStudio.exe')) -and (Test-Path $link))
    Check 'same files as the silent setup' ((Compare-Object $files (Snapshot)) -eq $null)
    Uninstall 'uninstall-2'
    Check 'removed again' (-not (Test-Path $entry) -and -not (Test-Path $folder) -and -not (Test-Path $link))
    'shot check: all passed'
}
