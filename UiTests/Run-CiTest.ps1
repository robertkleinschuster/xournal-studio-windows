# Runs a UI test directly in the current interactive session, without Windows Sandbox - on a GitHub-hosted Windows runner
# (.github/workflows/ui-tests.yml), a fresh disposable machine. Lays out the folder the sandbox shares (C:\sbx: app\,
# data\, TestKit.ps1 and InkPages' *.cs, test.ps1, out\), runs the test script in Windows PowerShell 5.1 as the sandbox
# does, and copies C:\sbx\out to -Out. The same test scripts run locally in the sandbox (Run-AppTest.ps1) for debugging.
# Never run this on a developer machine: it replaces C:\sbx and C:\work and takes over mouse, keyboard and pen.
#   UiTests\Run-CiTest.ps1 -Test UiTests\TasksTest.ps1 -AppDir app -Extra uiaprobe.exe -Out results
# Exit code 0 = PASS (done.txt written, no ERROR line in the transcript), 1 = FAIL.
param(
    [Parameter(Mandatory)][string]$Test,
    [Parameter(Mandatory)][string]$AppDir,          # XournalStudio.exe and its DLLs (the CI artifact app-<sha>)
    [Parameter(Mandatory)][string]$Out,             # results: screenshots, transcript, logs, runner.txt
    [string[]]$Extra = @(),                         # more files for C:\sbx\data (uiaprobe.exe)
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$kit = Join-Path $repo 'external\InkPages\UiTests'
$sbx = 'C:\sbx'

foreach ($dir in $sbx, 'C:\work') { if (Test-Path $dir) { Remove-Item -Recurse -Force $dir } }
New-Item -ItemType Directory -Force "$sbx\app", "$sbx\data", "$sbx\out" | Out-Null
Copy-Item (Join-Path $AppDir 'XournalStudio.exe') "$sbx\app"
Get-ChildItem $AppDir -Filter *.dll | Copy-Item -Destination "$sbx\app"
. (Join-Path $PSScriptRoot 'TestData.ps1')
$data = @(Get-UiTestData $repo $Test) + $Extra   # Schaltungen.* and the test's "# Data:" files
foreach ($file in $data) { Copy-Item $file "$sbx\data" }
Copy-Item "$kit\*.cs", "$kit\TestKit.ps1" $sbx -Exclude HiddenDesktop.cs
Copy-Item $Test "$sbx\test.ps1"

# What the machine offers the test (session, screen, DPI, foreground) - in the results as runner.txt.
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class RunnerInfo {
    [DllImport("user32")] public static extern int GetSystemMetrics(int i);
    [DllImport("user32")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32")] public static extern uint GetDpiForSystem();
    [DllImport("user32")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
    [DllImport("kernel32")] public static extern bool ProcessIdToSessionId(uint pid, out uint session);
    [DllImport("kernel32")] public static extern uint WTSGetActiveConsoleSessionId();
}
'@
[void][RunnerInfo]::SetProcessDpiAwarenessContext([IntPtr]-4)   # PER_MONITOR_AWARE_V2: physical pixels
$session = 0; [void][RunnerInfo]::ProcessIdToSessionId([uint32]$PID, [ref]$session)
$os = Get-CimInstance Win32_OperatingSystem
$gpu = (Get-CimInstance Win32_VideoController | ForEach-Object { "$($_.Name) ($($_.CurrentHorizontalResolution)x$($_.CurrentVerticalResolution))" }) -join '; '
$info = @(
    "os: $($os.Caption) $($os.Version) build $([Environment]::OSVersion.Version.Build)"
    "user: $env:USERNAME, session $session, console session $([RunnerInfo]::WTSGetActiveConsoleSessionId()), interactive $([Environment]::UserInteractive)"
    "screen: $([RunnerInfo]::GetSystemMetrics(0))x$([RunnerInfo]::GetSystemMetrics(1)), system DPI $([RunnerInfo]::GetDpiForSystem())"
    "foreground window: $([RunnerInfo]::GetForegroundWindow())"
    "gpu: $gpu"
    "powershell 5.1: $((Get-Command powershell.exe).Source)"
)
$info | Set-Content "$sbx\out\runner.txt"
$info | ForEach-Object { "runner $_" }

$start = Get-Date
$proc = Start-Process powershell.exe -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File C:\sbx\test.ps1' -PassThru -NoNewWindow
$timedOut = -not $proc.WaitForExit($TimeoutSeconds * 1000)
if ($timedOut) {
    Write-Warning "the test ran longer than $TimeoutSeconds s: stopped"
    # The screen as the test left it.
    Add-Type -AssemblyName System.Drawing
    $w = [RunnerInfo]::GetSystemMetrics(0); $h = [RunnerInfo]::GetSystemMetrics(1)
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen(0, 0, 0, 0, (New-Object System.Drawing.Size $w, $h))
    $bmp.Save("$sbx\out\timeout.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    $proc.Kill(); $proc.WaitForExit()
    foreach ($app in Get-Process XournalStudio -ErrorAction SilentlyContinue) { $app.Kill(); $app.WaitForExit() }
}
$seconds = [int]((Get-Date) - $start).TotalSeconds

if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
Copy-Item -Recurse "$sbx\out" $Out
$transcript = Join-Path $Out 'transcript.txt'
$errors = @()
if (Test-Path $transcript) {
    $errors = @(Select-String -Path $transcript -Pattern '^ERROR' | ForEach-Object { $_.Line })
    Select-String -Path $transcript -Pattern '^(init|shot|exited|ERROR)' | ForEach-Object { $_.Line }
}
$passed = -not $timedOut -and (Test-Path (Join-Path $Out 'done.txt')) -and (Test-Path $transcript) -and -not $errors
$result = if ($passed) { 'PASS' } else { 'FAIL' }
"$result $([IO.Path]::GetFileNameWithoutExtension($Test)) in $seconds s$(if ($timedOut) { ' (timed out)' })" | Tee-Object (Join-Path $Out 'result.txt')
if (-not $passed) { exit 1 }
