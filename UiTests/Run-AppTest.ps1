# Runs a UI test of the native app in Windows Sandbox with InkPages' runner and TestKit (external/InkPages/UiTests):
# the app from the build, examples\Schaltungen.xopp (+ its PDF) as data. The sandbox is shared with other agents: the
# test holds the machine-wide mutex Global\XournalStudioSandbox (one test at a time across all worktrees) and starts
# only when no sandbox runs (`wsb list` empty). The sandbox window opens on a desktop of its own (InkPages' runner),
# never on the user's; -Show opens it on the user's desktop to watch or debug a test (tell the user first).
#   UiTests\Run-AppTest.ps1 -Test UiTests\InkRoundTripTest.ps1
#   -Build: the build folder below .local-build (default cmake); -Extra: more files for C:\sbx\data.
#   -AppDir <folder>: the app from that folder instead (XournalStudio.exe and its DLLs, e.g. an installed or staged app).
#   -FromCi [-Commit <rev>]: the app CI staged (the newest green run on main, or that commit's; tools\CiArtifact.ps1)
#   - no local build; the same exe install.ps1 -FromCi installs.
param(
    [Parameter(Mandatory)][string]$Test,
    [string]$Config = 'Release',
    [string]$Build = 'cmake',
    [string[]]$Extra = @(),
    [int]$TimeoutSeconds = 300,
    [switch]$Show,
    [string]$AppDir = '',
    [switch]$FromCi,
    [string]$Commit = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
# One sandbox test at a time on this machine, across all worktrees (named mutex Global\XournalStudioSandbox,
# tools\BuildLock.ps1): a second test waits and names the holder. A sandbox started some other way still refuses.
. (Join-Path $repo 'tools\BuildLock.ps1')
$lock = Enter-XournalSandboxLock "Run-AppTest $Test in $repo"
try {
    $running = (wsb list) | Where-Object { $_ -and $_.Trim() }
    if ($running) { throw "a sandbox is running already (wsb list): $running" }
    $app = Join-Path $repo '.local-build\sandbox-app'
    if (Test-Path $app) { Remove-Item -Recurse -Force $app }
    New-Item -ItemType Directory -Force $app | Out-Null
    $from = Join-Path $repo ".local-build\$Build\app\$Config"
    if ($AppDir) { $from = $AppDir }
    if ($FromCi) {
        . (Join-Path $repo 'tools\CiArtifact.ps1')
        $from = Join-Path $repo '.local-build\ci-app'
        Get-XournalCiApp -Repo $repo -Commit $Commit -Destination $from | Out-Null
    }
    Copy-Item (Join-Path $from 'XournalStudio.exe') $app
    # The DLLs next to it (pdfium.dll: the PDF's text for search).
    Get-ChildItem $from -Filter *.dll | Copy-Item -Destination $app
    # One results folder per test: agents running different tests one after another keep each other's results.
    $work = Join-Path $env:TEMP ('xournal-native-sandbox-' + [IO.Path]::GetFileNameWithoutExtension($Test))
    . (Join-Path $PSScriptRoot 'TestData.ps1')
    $data = @(Get-UiTestData $repo $Test) + $Extra   # Schaltungen.* and the test's "# Data:" files
    & (Join-Path $repo 'external\InkPages\UiTests\Run-SandboxTest.ps1') -App $app -Test $Test -Data $data `
        -Work $work -TimeoutSeconds $TimeoutSeconds -Show:$Show
}
finally {
    Exit-XournalSandboxLock $lock
}
