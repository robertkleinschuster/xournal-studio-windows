# The MSI in Windows Sandbox: a silent install of an older version, then the new one over it (MajorUpgrade: one product
# left), files, Start menu shortcut and Apps & Features entry, install.ps1's MSI check, the app started from the
# shortcut, a silent uninstall that leaves nothing of the package (the user's data stays), then an install with
# Windows Installer's UI (no dialogs: progress only) and its uninstall.
#   installer\Build-Msi.ps1 -Version 0.1.99 -Out <old.msi>; installer\Build-Msi.ps1 -Version 0.2.0 -Out <new.msi>
#   UiTests\Run-AppTest.ps1 -Test UiTests\MsiTest.ps1 -Extra <old.msi>,<new.msi>
# Every check prints "shot check: ..." (the host shows those lines of the transcript).
. C:\sbx\TestKit.ps1

$upgradeCode = '{2ED20F25-015E-4F3B-9B2A-7572D4BBBB6A}'
$folder = Join-Path $env:LOCALAPPDATA 'Programs\Xournal Studio (Win32)'
$programs = Join-Path $env:LOCALAPPDATA 'Programs'
$link = Join-Path ([Environment]::GetFolderPath('Programs')) 'Xournal Studio (Win32).lnk'
$userData = Join-Path $env:LOCALAPPDATA 'XournalStudioForWindows'
$installer = New-Object -ComObject WindowsInstaller.Installer

function Check($what, [bool]$ok) { if (-not $ok) { Shot ("failed-" + ($what -replace '[^A-Za-z0-9]', '_')) | Out-Null; throw "check failed: $what" }; "shot check: $what" }
function Products { @($installer.GetType().InvokeMember('RelatedProducts', 'GetProperty', $null, $installer, @($upgradeCode))) }
function Info($product, $name) { $installer.GetType().InvokeMember('ProductInfo', 'GetProperty', $null, $installer, @($product, $name)) }
function Msi([string]$arguments, [string]$log) {
    $p = Start-Process msiexec.exe -ArgumentList "$arguments /l*v `"$out\$log.log`"" -PassThru
    $p.WaitForExit()
    "shot msiexec $arguments -> $($p.ExitCode)"
    if ($p.ExitCode -ne 0) { throw "msiexec $arguments failed ($($p.ExitCode)), see $log.log" }
}
function Snapshot { if (Test-Path $folder) { @(Get-ChildItem -Recurse -Force $folder | ForEach-Object { $_.FullName.Substring($folder.Length) }) } else { @() } }

Invoke-Test {
    $old = (Get-ChildItem C:\sbx\data\*.msi | Where-Object Name -like '*0.1.99*').FullName
    $new = (Get-ChildItem C:\sbx\data\*.msi | Where-Object Name -notlike '*0.1.99*').FullName
    $programsExisted = Test-Path $programs
    "shot check: Programs folder existed before: $programsExisted"

    # 1. Silent install of the old version, then the new one over it.
    Msi "/i `"$old`" /qn" 'install-old'
    Check 'old version installed' ((Products).Count -eq 1 -and (Info (Products)[0] 'VersionString') -eq '0.1.99')
    $oldProduct = (Products)[0]
    Msi "/i `"$new`" /qn" 'install-new'
    $products = Products
    Check "one product after the upgrade ($($products -join ', '))" ($products.Count -eq 1 -and $products[0] -ne $oldProduct)
    $product = $products[0]
    Check 'version 0.2.0' ((Info $product 'VersionString') -eq '0.2.0')
    Check "publisher ($(Info $product 'Publisher'))" ((Info $product 'Publisher') -eq 'Robert Kleinschuster')
    Check "Apps & Features icon ($(Info $product 'ProductIcon'))" ([bool](Info $product 'ProductIcon'))
    Check "per-user install ($(Info $product 'AssignmentType'))" ((Info $product 'AssignmentType') -eq '0')
    Check "install folder ($(Info $product 'InstallLocation'))" (Test-Path (Join-Path $folder 'XournalStudio.exe'))
    $files = Snapshot
    $files | ForEach-Object { "file $_" }
    Check 'pdfium.dll and licenses' ((Test-Path (Join-Path $folder 'pdfium.dll')) -and (Test-Path (Join-Path $folder 'licenses\THIRD-PARTY.md')))
    Check 'Start menu shortcut' (Test-Path $link)
    $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($link)
    "shot check: shortcut target $($shortcut.TargetPath), icon $($shortcut.IconLocation)"
    Check 'shortcut target' ($shortcut.TargetPath -eq (Join-Path $folder 'XournalStudio.exe'))
    Check 'registry key path' (Test-Path 'HKCU:\Software\Xournal Studio (Win32)\Installer')

    # 2. install.ps1's check (the same call) sees the MSI.
    Check 'install.ps1 would refuse' ((Products).Count -gt 0)

    # 3. The app from the shortcut: its window, closed with exit code 0.
    Start-Process $link
    $script:p = $null
    for ($i = 0; $i -lt 40 -and -not $script:p; $i++) {
        Start-Sleep -Milliseconds 500
        $script:p = Get-Process XournalStudio -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$folder\*" } | Select-Object -First 1
    }
    Check 'app process from the shortcut' ([bool]$script:p)
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

    # 4. Silent uninstall: nothing of the package left, the user's data kept.
    Msi "/x $product /qn" 'uninstall'
    Check 'no product' ((Products).Count -eq 0)
    Check 'install folder removed' (-not (Test-Path $folder))
    Check 'shortcut removed' (-not (Test-Path $link))
    Check 'registry key removed' (-not (Test-Path 'HKCU:\Software\Xournal Studio (Win32)'))
    "shot check: Programs folder exists after uninstall: $(Test-Path $programs)"
    if (-not $programsExisted) { Check 'Programs folder removed (the package created it)' (-not (Test-Path $programs)) }
    Check 'user data kept' (Test-Path (Join-Path $userData 'marker.txt'))

    # 5. With Windows Installer's UI (a double click): progress only, no dialog to answer.
    $p = Start-Process msiexec.exe -ArgumentList "/i `"$new`" /l*v `"$out\install-ui.log`"" -PassThru
    Start-Sleep -Milliseconds 1500
    [Inj]::Shot("$out\02-install-ui.png"); 'shot 02-install-ui'
    Check 'install with UI finished without input' ($p.WaitForExit(120000))
    Check "install with UI exit code $($p.ExitCode)" ($p.ExitCode -eq 0)
    Check 'installed with UI' ((Products).Count -eq 1 -and (Test-Path (Join-Path $folder 'XournalStudio.exe')) -and (Test-Path $link))
    Check 'same files as the silent install' ((Compare-Object $files (Snapshot)) -eq $null)
    Msi "/x $((Products)[0]) /qn" 'uninstall-2'
    Check 'removed again' ((Products).Count -eq 0 -and -not (Test-Path $folder) -and -not (Test-Path $link))
    'shot check: all passed'
}
