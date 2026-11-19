# Runs a UI test of the native app in Windows Sandbox with InkPages' runner and TestKit (external/InkPages/UiTests):
# the app from the build, examples\Schaltungen.xopp (+ its PDF) as data. The sandbox is shared with other agents: the
# test only starts when no sandbox runs (`wsb list` empty), and holds .local-build\sandbox.lock (repository root) while
# it runs. The sandbox window opens on the desktop.
#   native\UiTests\Run-AppTest.ps1 -Test native\UiTests\InkRoundTripTest.ps1
#   -Build: the build folder below native\.local-build (default cmake); -Extra: more files for C:\sbx\data.
param(
    [Parameter(Mandatory)][string]$Test,
    [string]$Config = 'Release',
    [string]$Build = 'cmake',
    [string[]]$Extra = @(),
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
$native = Split-Path $PSScriptRoot
$repo = Split-Path $native
$lock = Join-Path $repo '.local-build\sandbox.lock'

$running = (wsb list) | Where-Object { $_ -and $_.Trim() }
if ($running) { throw "a sandbox is running already (wsb list): $running" }
if (Test-Path $lock) { throw "the sandbox is in use: $lock ($(Get-Content $lock -Raw))" }
New-Item -ItemType Directory -Force (Split-Path $lock) | Out-Null
Set-Content $lock "native app test $Test, pid $PID, $(Get-Date -Format s)"
try {
    $app = Join-Path $native '.local-build\sandbox-app'
    if (Test-Path $app) { Remove-Item -Recurse -Force $app }
    New-Item -ItemType Directory -Force $app | Out-Null
    Copy-Item (Join-Path $native ".local-build\$Build\app\$Config\XournalStudio.exe") $app
    # One results folder per test: agents running different tests one after another keep each other's results.
    $work = Join-Path $env:TEMP ('xournal-native-sandbox-' + [IO.Path]::GetFileNameWithoutExtension($Test))
    $data = @((Join-Path $repo 'examples\Schaltungen.xopp'), (Join-Path $repo 'examples\Schaltungen.pdf')) + $Extra
    & (Join-Path $native 'external\InkPages\UiTests\Run-SandboxTest.ps1') -App $app -Test $Test -Data $data `
        -Work $work -TimeoutSeconds $TimeoutSeconds
}
finally {
    Remove-Item $lock -Force
}
